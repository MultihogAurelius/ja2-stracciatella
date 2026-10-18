#include "Visual_Equipment.h"

#include "Animation_Data.h"
#include "ContentManager.h"
#include "GameInstance.h"
#include "HImage.h"
#include "ItemModel.h"
#include "Json.h"
#include "Lighting.h"
#include "SGPFile.h"
#include "Soldier_Control.h"
#include "VObject.h"
#include "VObject_Blitters.h"
#include "Shading.h"

#include <algorithm>
#include <array>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>

namespace
{
struct Mapping
{
	ST::string layer, sprite, palette, gear, slot;
	unsigned condition{};
	std::vector<UINT16> items;
	bool shadow{};
	unsigned baseFramesPerDirection{}, spriteFramesPerDirection{}, directions{};
};
struct HandCondition
{
	std::vector<UINT16> right, left;
};
struct Sprite
{
	std::vector<VisualEquipmentFrame> frames;
	size_t bytes{};
	uint64_t lastUse{};
};
struct EquipmentPalette
{
	std::array<SGPPaletteEntry, 256> colors{};
	UINT16* shades[NUM_SOLDIER_SHADES]{};
	UINT16* effect{};
	UINT16* glows[20]{};
	SGPPaletteEntry lightColor{};
	bool ready{};
	~EquipmentPalette()
	{
		for (auto* shade : shades) delete[] shade;
		delete[] effect;
		for (auto* shade : glows) delete[] shade;
	}
};
bool loaded{}, enabled{}, heldWeapons{};
std::vector<ST::string> layerOrder;
std::vector<std::vector<HandCondition>> conditions;
// The index avoids scanning unrelated bodies and animation sheets each draw.
std::map<ST::string, std::vector<Mapping>> mappings;
std::map<ST::string, std::shared_ptr<Sprite>> sprites;
size_t spriteBytes{};
uint64_t spriteClock{};
constexpr size_t SPRITE_CACHE_LIMIT = 128 * 1024 * 1024;
std::map<ST::string, std::unique_ptr<EquipmentPalette>> palettes;

std::vector<UINT16> ReadItems(JsonValue const& value)
{
	std::vector<UINT16> result;
	for (auto const& item : value.toVec()) result.push_back(item.toUInt());
	return result;
}

void LoadDefinitions()
{
	if (loaded) return;
	loaded = true;
	constexpr auto filename = "visual-equipment/vanilla-gear.json";
	if (!GCM->doesGameResExists(filename)) return;
	try
	{
		AutoSGPFile file(GCM->openGameResForReading(filename));
		auto json = JsonValue::deserialize(file->readStringToEnd()).toObject();
		enabled = json.getOptionalBool("enabled");
		heldWeapons = json.getOptionalBool("held_weapons");
		if (!enabled) return;
		for (auto const& value : json["layer_order"].toVec()) layerOrder.push_back(value.toString());
		for (auto const& value : json["conditions"].toVec())
		{
			std::vector<HandCondition> condition;
			for (auto const& entry : value.toObject()["hands"].toVec())
			{
				auto obj = entry.toObject();
				condition.push_back({ReadItems(obj["right"]), ReadItems(obj["left"])});
			}
			conditions.push_back(std::move(condition));
		}
		for (auto const& value : json["mappings"].toVec())
		{
			auto entry = value.toObject();
			Mapping mapping;
			mapping.layer = entry.GetString("layer");
			mapping.sprite = entry.GetString("sprite");
			mapping.palette = entry.GetString("palette");
			mapping.gear = entry.GetString("gear");
			mapping.condition = entry.GetUInt("condition");
			mapping.shadow = entry.getOptionalBool("shadow");
			if (entry.has("frame_remap"))
			{
				auto remap = entry["frame_remap"].toObject();
				mapping.baseFramesPerDirection = remap.GetUInt("base_frames_per_direction");
				mapping.spriteFramesPerDirection = remap.GetUInt("sprite_frames_per_direction");
				mapping.directions = remap.GetUInt("directions");
			}
			if (!mapping.gear.empty())
			{
				mapping.slot = entry.GetString("slot");
				mapping.items = ReadItems(entry["items"]);
			}
			if (mapping.condition >= conditions.size()) throw std::runtime_error("Invalid visual hand condition");
			mappings[entry.GetString("body") + ":" + entry.GetString("base_sprite").to_lower()].push_back(std::move(mapping));
		}
	}
	catch (std::exception const& error)
	{
		SLOGW("Vanilla equipment visuals disabled: {}", error.what());
		enabled = false;
	}
}

Sprite LoadSprite(ST::string const& filename)
{
	std::unique_ptr<SGPImage> image(CreateImage(filename, IMAGE_ALLDATA));
	if (image->ubBitDepth != 8 || !(image->fFlags & IMAGE_TRLECOMPRESSED))
		throw std::runtime_error("Equipment image must be an indexed ETRLE sprite");
	Sprite result;
	for (unsigned i = 0; i < image->usNumberOfObjects; ++i)
	{
		auto const& region = image->pETRLEObject[i];
		VisualEquipmentFrame frame;
		frame.width = region.usWidth; frame.height = region.usHeight;
		frame.offsetX = region.sOffsetX; frame.offsetY = region.sOffsetY;
		frame.pixels.assign(frame.width * frame.height, 256);
		auto pos = region.uiDataOffset;
		auto const end = pos + region.uiDataLength;
		if (end > image->uiSizePixData) throw std::runtime_error("Equipment frame outside sprite data");
		for (int y = 0; y < frame.height; ++y)
		{
			int x = 0;
			while (true)
			{
				if (pos >= end) throw std::runtime_error("Truncated equipment row");
				UINT8 const run = image->pImageData[pos++];
				if (!run) break;
				int const count = run & 127;
				if (x + count > frame.width) throw std::runtime_error("Equipment row exceeds frame width");
				if (!(run & 128))
				{
					if (pos + count > end) throw std::runtime_error("Truncated equipment pixels");
					for (int j = 0; j < count; ++j)
						frame.pixels[y * frame.width + x + j] = image->pImageData[pos++];
				}
				x += count;
			}
			if (x != frame.width) throw std::runtime_error("Incomplete equipment row");
		}
		result.bytes += frame.pixels.size() * sizeof(UINT16);
		result.frames.push_back(std::move(frame));
	}
	return result;
}


bool Contains(std::vector<UINT16> const& items, UINT16 item)
{
	return std::find(items.begin(), items.end(), item) != items.end();
}
UINT16 HandIdentity(UINT16 item)
{
	// 1.13 item 331 is an L85. Vanilla's M14 copy must still look like an M14.
	if (item == 331) return 27;
	return item < 66 ? item : 0;
}
bool Matches(Mapping const& mapping, SOLDIERTYPE const& soldier)
{
	if (!mapping.gear.empty())
	{
		bool worn = false;
		if (mapping.slot == "FACE")
			worn = Contains(mapping.items, soldier.inv[HEAD1POS].usItem) || Contains(mapping.items, soldier.inv[HEAD2POS].usItem);
		else
		{
			int slot = mapping.slot == "HELMETPOS" ? HELMETPOS : mapping.slot == "VESTPOS" ? VESTPOS : LEGPOS;
			worn = Contains(mapping.items, soldier.inv[slot].usItem);
		}
		if (!worn) return false;
	}
	UINT16 const right = HandIdentity(soldier.inv[HANDPOS].usItem);
	UINT16 const left = HandIdentity(soldier.inv[SECONDHANDPOS].usItem);
	for (auto const& condition : conditions[mapping.condition])
		if (Contains(condition.right, right) && Contains(condition.left, left)) return true;
	return false;
}
bool EquipmentFrameIndex(Mapping const& mapping, unsigned index, unsigned baseCount, size_t spriteCount, unsigned& sourceIndex)
{
	if (index >= baseCount) return false;
	sourceIndex = index;
	if (spriteCount == baseCount) return true;
	// Only explicitly documented conversions may differ from vanilla's frame
	// layout. Keep facing and endpoints intact; never read another direction.
	const unsigned from = mapping.baseFramesPerDirection, to = mapping.spriteFramesPerDirection;
	if (from < 2 || to < 2 || mapping.directions == 0 ||
		baseCount != from * mapping.directions || spriteCount != to * mapping.directions) return false;
	sourceIndex = (index / from) * to + ((index % from) * (to - 1) + (from - 1) / 2) / (from - 1);
	return sourceIndex < spriteCount;
}

std::shared_ptr<const VisualEquipmentFrame> Frame(Mapping const& mapping, UINT16 index, unsigned baseCount)
{
	auto found = sprites.find(mapping.sprite);
	if (found == sprites.end())
	{
		try
		{
			auto sprite = std::make_shared<Sprite>(LoadSprite(mapping.sprite));
			spriteBytes += sprite->bytes;
			found = sprites.emplace(mapping.sprite, std::move(sprite)).first;
		}
		catch (std::exception const& error)
		{
			SLOGW("Skipping equipment sprite {}: {}", mapping.sprite, error.what());
			found = sprites.emplace(mapping.sprite, std::make_shared<Sprite>()).first;
		}
	}
	auto const& frames = found->second->frames;
	found->second->lastUse = ++spriteClock;
	unsigned sourceIndex;
	if (!EquipmentFrameIndex(mapping, index, baseCount, frames.size(), sourceIndex)) return nullptr;
	return {found->second, &frames[sourceIndex]};
}
}

VisualEquipmentAppearance GetVisualEquipmentAppearance(SOLDIERTYPE const& soldier, UINT16 frame)
{
	VisualEquipmentAppearance result;
	// Shared frame owners keep appearances valid while unused sheets are evicted.
	while (spriteBytes > SPRITE_CACHE_LIMIT)
	{
		auto oldest = sprites.end();
		for (auto it = sprites.begin(); it != sprites.end(); ++it)
			if (it->second.use_count() == 1 && (oldest == sprites.end() || it->second->lastUse < oldest->second->lastUse)) oldest = it;
		if (oldest == sprites.end()) break;
		spriteBytes -= oldest->second->bytes;
		sprites.erase(oldest);
	}
	ST::string const body = soldier.ubBodyType == REGMALE || soldier.ubBodyType == STOCKYMALE ? "RGM" :
		soldier.ubBodyType == BIGMALE ? "BGM" : soldier.ubBodyType == REGFEMALE ? "RGF" : "";
	if (body.empty() || soldier.usAnimSurface >= NUMANIMATIONSURFACETYPES) return result;
	LoadDefinitions();
	if (!enabled) return result;
	auto const& base = gAnimSurfaceDatabase[soldier.usAnimSurface];
	if (!base.hVideoObject) return result;
	auto found = mappings.find(body + ":" + ST::string(base.Filename).to_lower());
	if (found == mappings.end()) return result;
	std::set<ST::string> selected;
	bool worn = false;
	for (auto const& mapping : found->second)
	{
		// Preserve LOBOT's first matching definition per layer and animation.
		if (selected.count(mapping.layer) || !Matches(mapping, soldier)) continue;
		selected.insert(mapping.layer);
		auto image = Frame(mapping, frame, base.hVideoObject->SubregionCount());
		if (!image) continue;
		result.layers.push_back({image, mapping.layer, mapping.palette, mapping.gear, mapping.shadow});
		worn |= !mapping.gear.empty();
	}
	auto has = [&](char const* name)
	{
		return std::any_of(result.layers.begin(), result.layers.end(),
			[&](VisualEquipmentLayer const& layer) { return layer.name == name; });
	};
	auto supports = [&](char const* name)
	{
		return std::any_of(found->second.begin(), found->second.end(),
			[&](Mapping const& mapping) { return mapping.layer == name; });
	};
	auto const* right = GCM->getItem(soldier.inv[HANDPOS].usItem);
	auto const* left = GCM->getItem(soldier.inv[SECONDHANDPOS].usItem);
	bool const rightWeapon = right->isWeapon() && soldier.inv[HANDPOS].usItem != NOTHING;
	bool const leftGun = left->isGun();
	// All foundational body parts must exist before suppressing the original.
	// Water sheets intentionally omit legs. Hit/death/throw sheets can omit
	// weapons entirely, even while the inventory still contains one. Require
	// weapon art only on sheets that actually support drawing that hand.
	// Missing or incompatible art on an armed sheet must still fall back.
	bool const water = ST::string(base.Filename).to_lower().contains("water");
	result.replaceBase = heldWeapons && (worn || rightWeapon || leftGun) && has("body") && has("head") && has("hands") &&
		(has("legs") || water) && (!rightWeapon || !supports("gun") || has("gun")) &&
		(!leftGun || !supports("gunleft") || has("gunleft"));
	if (!result.replaceBase)
	{
		// Overlay worn gear safely; never overlay a new gun on the generic old gun.
		result.layers.erase(std::remove_if(result.layers.begin(), result.layers.end(),
			[](VisualEquipmentLayer const& layer) { return layer.gear.empty(); }), result.layers.end());
	}
	std::stable_sort(result.layers.begin(), result.layers.end(), [](VisualEquipmentLayer const& a, VisualEquipmentLayer const& b)
	{
		return std::find(layerOrder.begin(), layerOrder.end(), a.name) < std::find(layerOrder.begin(), layerOrder.end(), b.name);
	});
	return result;
}

const UINT16* GetVisualEquipmentShade(SOLDIERTYPE const& soldier, const UINT16* shade, ST::string const& filename)
{
	if (filename.empty()) return shade;
	auto& holder = palettes[filename];
	if (!holder)
	{
		auto newPalette = std::make_unique<EquipmentPalette>();
		AutoSGPFile file(GCM->openGameResForReading(filename));
		for (auto& color : newPalette->colors)
		{
			UINT8 rgb[3]; file->read(rgb, sizeof(rgb));
			color = {rgb[0], rgb[1], rgb[2], 255};
		}
		holder = std::move(newPalette);
	}
	auto const* color = LightGetColor();
	if (!holder->ready || color->r != holder->lightColor.r || color->g != holder->lightColor.g || color->b != holder->lightColor.b)
	{
		CreateSoldierShadeTables(holder->shades, holder->effect, holder->glows, holder->colors.data());
		holder->lightColor = *color;
		holder->ready = true;
	}
	if (shade)
	{
		for (int i = 0; i < NUM_SOLDIER_SHADES; ++i)
			if (shade == soldier.pShades[i]) return holder->shades[i];
		for (int i = 0; i < 20; ++i)
			if (shade == soldier.pGlowShades[i]) return holder->glows[i];
		if (shade == soldier.effect_shade) return holder->effect;
	}
	return holder->shades[4];
}

VisualEquipmentComposite ComposeVisualEquipment(VisualEquipmentAppearance const& appearance,
	SOLDIERTYPE const& soldier, const UINT16* shade)
{
	VisualEquipmentComposite result;
	if (appearance.layers.empty()) return result;
	auto& frame = result.frame;
	auto const& first = *appearance.layers.front().frame;
	int left = first.offsetX, top = first.offsetY;
	int right = left + first.width, bottom = top + first.height;
	for (auto const& layer : appearance.layers)
	{
		auto const& image = *layer.frame;
		left = std::min(left, image.offsetX); top = std::min(top, image.offsetY);
		right = std::max(right, image.offsetX + image.width); bottom = std::max(bottom, image.offsetY + image.height);
	}
	frame.offsetX = left; frame.offsetY = top;
	frame.width = right - left; frame.height = bottom - top;
	frame.pixels.assign(frame.width * frame.height, 256);
	// Reserve the original transparency/shadow sentinels. Each layer gets a
	// palette block so lighting and glow remain independent of its artwork.
	result.colors.resize(512 + appearance.layers.size() * 256);
	for (size_t i = 0; i < appearance.layers.size(); ++i)
	{
		auto const& layer = appearance.layers[i];
		auto const& image = *layer.frame;
		auto const* colors = GetVisualEquipmentShade(soldier, shade, layer.palette);
		if (!colors) continue;
		UINT16 const paletteOffset = static_cast<UINT16>(512 + i * 256);
		std::copy_n(colors, 256, result.colors.begin() + paletteOffset);
		for (int y = 0; y < image.height; ++y)
		for (int x = 0; x < image.width; ++x)
		{
			UINT16 const pixel = image.pixels[y * image.width + x];
			if (pixel == 256 || (pixel == 254 && !layer.shadow)) continue;
			auto& dest = frame.pixels[(y + image.offsetY - top) * frame.width + x + image.offsetX - left];
			if (pixel == 254)
			{
				if (dest == 256) dest = 254;
			}
			else dest = paletteOffset + pixel;
		}
	}
	return result;
}

void ResetVisualEquipment()
{
	sprites.clear(); mappings.clear(); palettes.clear(); conditions.clear(); layerOrder.clear();
	spriteBytes = spriteClock = 0;
	loaded = enabled = heldWeapons = false;
}

void DrawVisualEquipment(const VisualEquipmentFrame& frame, UINT16* buffer, UINT32 pitch,
	UINT16* zbuffer, UINT16 z, int x, int y, SGPRect const& clip, const UINT16* colors,
	bool testZ, bool writeZ, bool obscured, bool pixelate, bool translucent, bool shadows)
{
	x += frame.offsetX; y += frame.offsetY;
	int const left = std::max<int>(x, clip.iLeft), top = std::max<int>(y, clip.iTop);
	int const right = std::min<int>(x + frame.width, clip.iRight), bottom = std::min<int>(y + frame.height, clip.iBottom);
	for (int py = top; py < bottom; ++py)
	{
		auto* dest = reinterpret_cast<UINT16*>(reinterpret_cast<UINT8*>(buffer) + py * pitch);
		auto* depth = zbuffer ? reinterpret_cast<UINT16*>(reinterpret_cast<UINT8*>(zbuffer) + py * pitch) : nullptr;
		for (int px = left; px < right; ++px)
		{
			UINT16 const index = frame.pixels[(py - y) * frame.width + px - x];
			if (index == 256 || (index == 254 && !shadows)) continue;
			bool const behind = testZ && depth && depth[px] > z;
			if (behind && !obscured) continue;
			bool const checker = (px + py) % 2 == 0;
			if ((behind || (pixelate && !translucent)) && !checker) continue;
			if (index == 254)
			{
				// Vanilla shadows only darken pixels strictly behind the soldier.
				// Equal depth is permitted for coloured equipment, but not shadows.
				if ((!testZ || !depth || depth[px] < z) && !pixelate)
					dest[px] = ShadeTable[dest[px]];
				continue;
			}
			UINT16 const color = colors[index];
			if (pixelate && translucent)
				dest[px] = ((color >> 1) & guiTranslucentMask) + ((dest[px] >> 1) & guiTranslucentMask);
			else dest[px] = color;
			if (testZ && writeZ && depth && !behind) depth[px] = z;
		}
	}
}

#ifdef WITH_UNITTESTS
#include "gtest/gtest.h"

TEST(VisualEquipment, ExplicitFrameConversionPreservesFacingAndAnimationEndpoints)
{
	Mapping mapping;
	mapping.baseFramesPerDirection = 14;
	mapping.spriteFramesPerDirection = 18;
	mapping.directions = 8;
	unsigned source;
	for (unsigned facing = 0; facing < 8; ++facing)
	{
		unsigned previous = facing * 18;
		for (unsigned frame = 0; frame < 14; ++frame)
		{
			ASSERT_TRUE(EquipmentFrameIndex(mapping, facing * 14 + frame, 112, 144, source));
			EXPECT_EQ(source / 18, facing);
			EXPECT_GE(source, previous);
			previous = source;
			if (frame == 0) EXPECT_EQ(source, facing * 18);
			if (frame == 13) EXPECT_EQ(source, facing * 18 + 17);
		}
	}
	EXPECT_FALSE(EquipmentFrameIndex(mapping,112,112,144,source));
	EXPECT_FALSE(EquipmentFrameIndex(mapping,0,104,144,source));
	EXPECT_FALSE(EquipmentFrameIndex(mapping,0,112,136,source));
	EXPECT_FALSE(EquipmentFrameIndex(Mapping{},0,112,144,source));
}
#endif
