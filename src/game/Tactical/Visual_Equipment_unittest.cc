#include "gtest/gtest.h"

#include "Animation_Control.h"
#include "Animation_Data.h"
#include "DefaultContentManager.h"
#include "GameInstance.h"
#include "HImage.h"
#include "Json.h"
#include "SGPFile.h"
#include "Soldier_Control.h"
#include "VObject.h"
#include "VObject_Blitters.h"
#include "Visual_Equipment.h"
#include "Shading.h"
#include "RenderWorld.h"
#include "UILayout.h"
#include "VSurface.h"
#include "WorldDef.h"
#include <SDL.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <map>
#include <memory>

TEST(VisualEquipment, ClipsSkipsTransparencyAndDoesNotDoubleShadow)
{
	VisualEquipmentFrame frame{3, 2, -1, -1, {1, 254, 256, 2, 3, 4}};
	std::array<UINT16, 16> pixels; pixels.fill(900);
	std::array<UINT16, 256> palette{};
	for (int i = 0; i < 256; ++i) palette[i] = 100 + i;
	SGPRect clip{0, 0, 4, 4};
	DrawVisualEquipment(frame, pixels.data(), 8, nullptr, 5, 0, 1, clip, palette.data(),
		false, false, false, false, false);
	EXPECT_EQ(pixels[0], 900); // Shadow marker at x=0 is ignored.
	EXPECT_EQ(pixels[1], 900); // Transparent pixel.
	EXPECT_EQ(pixels[4], 103); // Negative offset clips the first column.
	EXPECT_EQ(pixels[5], 104);
	EXPECT_EQ(std::count(pixels.begin(), pixels.end(), 900), 14);
}

TEST(VisualEquipment, CoversItsOwnDepthButRespectsForegroundAndNoWritePasses)
{
	VisualEquipmentFrame frame{3, 1, 0, 0, {1, 1, 1}};
	std::array<UINT16, 3> pixels{0, 0, 0}, depth{9, 10, 11};
	std::array<UINT16, 256> palette{}; palette[1] = 123;
	SGPRect clip{0, 0, 3, 1};
	DrawVisualEquipment(frame, pixels.data(), 6, depth.data(), 10, 0, 0, clip, palette.data(),
		true, true, false, false, false);
	EXPECT_EQ(pixels, (std::array<UINT16, 3>{123, 123, 0}));
	EXPECT_EQ(depth, (std::array<UINT16, 3>{10, 10, 11}));
	depth[0] = 9;
	DrawVisualEquipment(frame, pixels.data(), 6, depth.data(), 10, 0, 0, clip, palette.data(),
		true, false, false, false, false);
	EXPECT_EQ(depth[0], 9);
}

TEST(VisualEquipment, ObscuredPixelsAreDitheredAndDoNotOverwriteOccluderDepth)
{
	VisualEquipmentFrame frame{4, 1, 0, 0, {1, 1, 1, 1}};
	std::array<UINT16, 4> pixels{}, depth{20, 20, 20, 20};
	std::array<UINT16, 256> palette{}; palette[1] = 123;
	SGPRect clip{0, 0, 4, 1};
	DrawVisualEquipment(frame, pixels.data(), 8, depth.data(), 10, 0, 0, clip, palette.data(),
		true, true, true, false, false);
	EXPECT_EQ(pixels, (std::array<UINT16, 4>{123, 0, 123, 0}));
	EXPECT_EQ(depth, (std::array<UINT16, 4>{20, 20, 20, 20}));
}

TEST(VisualEquipment, CompositesOverlappingLayersBeforeApplyingTranslucency)
{
	auto back = std::make_shared<VisualEquipmentFrame>(VisualEquipmentFrame{2,1,-1,0,{1,1}});
	auto front = std::make_shared<VisualEquipmentFrame>(VisualEquipmentFrame{1,1,0,0,{2}});
	VisualEquipmentAppearance appearance{{{back,"body","","",false}, {front,"vest","","",false}},true};
	SOLDIERTYPE soldier{};
	std::array<UINT16,256> colors{}; colors[1] = 0xf800; colors[2] = 0x001f;
	auto const composite = ComposeVisualEquipment(appearance, soldier, colors.data());
	ASSERT_EQ(composite.frame.width, 2);
	EXPECT_EQ(composite.frame.offsetX, -1);
	EXPECT_EQ(composite.colors[composite.frame.pixels[0]], 0xf800);
	EXPECT_EQ(composite.colors[composite.frame.pixels[1]], 0x001f);
	UINT16 const oldMask = guiTranslucentMask; guiTranslucentMask = 0x7bef;
	std::array<UINT16,2> pixels{0x07e0,0x07e0};
	SGPRect clip{0,0,2,1};
	DrawVisualEquipment(composite.frame,pixels.data(),4,nullptr,0,1,0,clip,composite.colors.data(),false,false,false,true,true);
	guiTranslucentMask = oldMask;
	EXPECT_EQ(pixels[1], ((0x001f >> 1) & 0x7bef) + ((0x07e0 >> 1) & 0x7bef));
}

TEST(VisualEquipment, EquipmentShadowsDoNotAccumulateAcrossLayers)
{
	auto shadow = std::make_shared<VisualEquipmentFrame>(VisualEquipmentFrame{1,1,0,0,{254}});
	VisualEquipmentAppearance appearance{{{shadow,"shadow","","",true}, {shadow,"gun","","",true}},true};
	SOLDIERTYPE soldier{}; std::array<UINT16,256> colors{};
	auto const composite = ComposeVisualEquipment(appearance, soldier, colors.data());
	UINT16 pixel = 100;
	UINT16 const old = ShadeTable[100]; ShadeTable[100] = 50;
	SGPRect clip{0,0,1,1};
	DrawVisualEquipment(composite.frame,&pixel,2,nullptr,0,0,0,clip,composite.colors.data(),false,false,false,false,false,true);
	ShadeTable[100] = old;
	EXPECT_EQ(pixel,50);
}

TEST(VisualEquipment, ShadowsRespectEqualDepthAndForegroundObjects)
{
	VisualEquipmentFrame frame{3,1,0,0,{254,254,254}};
	std::array<UINT16,3> pixels{100,100,100}, depth{9,10,11};
	std::array<UINT16,256> colors{};
	UINT16 const old = ShadeTable[100]; ShadeTable[100] = 50;
	SGPRect clip{0,0,3,1};
	DrawVisualEquipment(frame,pixels.data(),6,depth.data(),10,0,0,clip,colors.data(),true,false,true,false,false,true);
	ShadeTable[100] = old;
	EXPECT_EQ(pixels,(std::array<UINT16,3>{50,100,100}));
	EXPECT_EQ(depth,(std::array<UINT16,3>{9,10,11}));
}

class VanillaGearAssets : public testing::Test
{
protected:
	static VisualEquipmentLayer const* FindLayer(VisualEquipmentAppearance const& appearance, char const* name)
	{
		for (auto const& layer : appearance.layers) if (layer.name == name) return &layer;
		return nullptr;
	}
	void SetUp() override
	{
		previous = GCM;
		auto const* gameDir = std::getenv("JA2_VISUAL_TEST_GAMEDIR");
		if (!gameDir) GTEST_SKIP() << "Set JA2_VISUAL_TEST_GAMEDIR to validate against original animations.";
		RustPointer<EngineOptions> options(EngineOptions_default());
		EngineOptions_setVanillaGameDir(options.get(), gameDir);
		content = std::make_unique<DefaultContentManager>(std::move(options));
		ASSERT_TRUE(content->loadGameData());
		GCM = content.get();
		// The normal video manager establishes RGB565 before gameplay.
		oldMasks = {gusRedMask, gusGreenMask, gusBlueMask};
		oldShifts = {gusRedShift, gusGreenShift, gusBlueShift};
		gusRedMask = 0xf800; gusGreenMask = 0x07e0; gusBlueMask = 0x001f;
		gusRedShift = 8; gusGreenShift = 3; gusBlueShift = -3;
		ResetVisualEquipment();
	}
	void TearDown() override
	{
		ResetVisualEquipment();
		for (auto const& entry : oldSurfaces) gAnimSurfaceDatabase[entry.first].hVideoObject = entry.second;
		if (content)
		{
			gusRedMask = oldMasks[0]; gusGreenMask = oldMasks[1]; gusBlueMask = oldMasks[2];
			gusRedShift = oldShifts[0]; gusGreenShift = oldShifts[1]; gusBlueShift = oldShifts[2];
		}
		GCM = previous;
	}
	SGPVObject* LoadBase(UINT16 surface)
	{
		auto found = baseObjects.find(surface);
		if (found == baseObjects.end())
		{
			oldSurfaces[surface] = gAnimSurfaceDatabase[surface].hVideoObject;
			found = baseObjects.emplace(surface, AutoSGPVObject(AddVideoObjectFromFile(gAnimSurfaceDatabase[surface].Filename))).first;
			gAnimSurfaceDatabase[surface].hVideoObject = found->second.get();
		}
		return found->second.get();
	}
	ContentManager* previous{};
	std::unique_ptr<DefaultContentManager> content;
	std::map<UINT16, SGPVObject*> oldSurfaces;
	std::map<UINT16, AutoSGPVObject> baseObjects;
	std::array<UINT16, 3> oldMasks{};
	std::array<INT16, 3> oldShifts{};
};

TEST_F(VanillaGearAssets, CoreAnimationsCoverEveryDirectionForAllMercBodyTypes)
{
	struct Case { UINT8 body; UINT16 surface; };
	Case const cases[] = {
		{REGMALE, RGMSTANDING}, {REGMALE, RGMBASICWALKING}, {REGMALE, RGMRUNNING},
		{REGMALE, RGMCROUCHING}, {REGMALE, RGMSNEAKING}, {REGMALE, RGMPRONE},
		{REGMALE, RGMSTANDAIM}, {REGMALE, RGMCROUCHAIM},
		{STOCKYMALE, RGMSTANDING}, {BIGMALE, BGMSTANDING}, {REGFEMALE, RGFSTANDING},
		{BIGMALE, BGMWALKING}, {BIGMALE, BGMRUNNING}, {BIGMALE, BGMCROUCHING},
		{BIGMALE, BGMSNEAKING}, {BIGMALE, BGMPRONE}, {BIGMALE, BGMSTANDAIM}, {BIGMALE, BGMCROUCHAIM},
		{REGFEMALE, RGFWALKING}, {REGFEMALE, RGFRUNNING}, {REGFEMALE, RGFCROUCHING},
		{REGFEMALE, RGFSNEAKING}, {REGFEMALE, RGFPRONE}, {REGFEMALE, RGFSTANDAIM}, {REGFEMALE, RGFCROUCHAIM},
	};
	for (auto const& entry : cases)
	{
		SOLDIERTYPE soldier{};
		soldier.ubBodyType = entry.body;
		soldier.inv[HELMETPOS].usItem = STEEL_HELMET;
		soldier.inv[HANDPOS].usItem = G11;
		soldier.usAnimSurface = entry.surface;
		auto* base = LoadBase(entry.surface);
		for (UINT16 i = 0; i < base->SubregionCount(); ++i)
		{
			auto const appearance = GetVisualEquipmentAppearance(soldier, i);
			auto const* layer = FindLayer(appearance, "helmet");
			auto const* frame = layer ? layer->frame.get() : nullptr;
			ASSERT_NE(frame, nullptr) << "Body " << int(entry.body) << ", surface " << entry.surface << ", frame " << i;
			EXPECT_EQ(frame->pixels.size(), frame->width * frame->height);
		}
	}
}

TEST_F(VanillaGearAssets, EveryWornItemUsesItsVanillaSlotAndTreatmentFamily)
{
	struct Body { UINT8 type; UINT16 surface; };
	Body const bodies[] = {{REGMALE, RGMSTANDING}, {BIGMALE, BGMSTANDING}, {REGFEMALE, RGFSTANDING}};
	struct Gear { int slot; std::vector<UINT16> items; char const* layer; char const* name; };
	Gear const cases[] = {
		{HELMETPOS, {176}, "helmet", "SteelHelmet"},
		{HELMETPOS, {177,178,179}, "helmet", "KevlarHelmet"},
		{HELMETPOS, {180,181,182}, "helmet", "SpectraHelmet"},
		{VESTPOS, {161,162,163}, "vest", "FlakVest"},
		{VESTPOS, {164,165,166}, "vest", "KevlarVest"},
		{VESTPOS, {167,168,169}, "vest", "SpectraVest"},
		{VESTPOS, {196,197,198}, "vest", "GuardianVest"},
		{VESTPOS, {188,189,190,191}, "hands", "Longsleeves"},
		{LEGPOS, {170,171,172}, "legarmor", "KevlarLeggings"},
		{LEGPOS, {173,174,175}, "legarmor", "SpectraLeggings"},
		{HEAD1POS, {210}, "ears", "HeadSet"}, {HEAD2POS, {210}, "ears", "HeadSet"},
		{HEAD1POS, {211,246}, "facegear", "NVG"}, {HEAD2POS, {211,246}, "facegear", "NVG"},
		{HEAD1POS, {212}, "facegear", "Goggles"}, {HEAD2POS, {212}, "facegear", "Goggles"},
		{HEAD1POS, {213}, "gasmask", "Gasmask"}, {HEAD2POS, {213}, "gasmask", "Gasmask"},
	};
	for (auto const& body : bodies)
	for (auto const& entry : cases)
	for (auto item : entry.items)
	{
		SCOPED_TRACE(testing::Message() << "Body " << int(body.type) << ", item " << item << ", slot " << entry.slot);
		SOLDIERTYPE soldier{};
		soldier.ubBodyType = body.type; soldier.usAnimSurface = body.surface;
		soldier.inv[HANDPOS].usItem = G11;
		soldier.inv[entry.slot].usItem = item;
		auto* base = LoadBase(body.surface);
		for (UINT16 direction = 0; direction < 8; ++direction)
		{
			auto const appearance = GetVisualEquipmentAppearance(soldier, direction * (base->SubregionCount() / 8));
			auto const* layer = FindLayer(appearance, entry.layer);
			ASSERT_NE(layer, nullptr);
			EXPECT_EQ(layer->gear, entry.name);
			EXPECT_TRUE(appearance.replaceBase);
		}
		// Carrying an item in a pocket must not wear it.
		soldier.inv[entry.slot].usItem = 0;
		soldier.inv[BIGPOCK1POS].usItem = item;
		auto const appearance = GetVisualEquipmentAppearance(soldier, 0);
		for (auto const& layer : appearance.layers) EXPECT_NE(layer.gear, entry.name);
	}
}

TEST_F(VanillaGearAssets, MixedEquipmentAndWeaponSwapsAreImmediateAndDoNotMutateSoldier)
{
	SOLDIERTYPE soldier{};
	soldier.ubBodyType = REGMALE; soldier.usAnimSurface = RGMSTANDING;
	soldier.bActionPoints = 17; soldier.bLife = 73;
	soldier.inv[HANDPOS].usItem = G11;
	soldier.inv[HELMETPOS].usItem = SPECTRA_HELMET;
	soldier.inv[VESTPOS].usItem = KEVLAR_VEST;
	soldier.inv[LEGPOS].usItem = KEVLAR_LEGGINGS;
	soldier.inv[HEAD1POS].usItem = NIGHTGOGGLES;
	soldier.inv[HEAD2POS].usItem = EXTENDEDEAR;
	LoadBase(RGMSTANDING);
	auto const before = GetVisualEquipmentAppearance(soldier, 0);
	ASSERT_TRUE(before.replaceBase);
	for (auto const* name : {"helmet", "vest", "legarmor", "facegear", "ears", "gun"})
		ASSERT_NE(FindLayer(before, name), nullptr) << name;
	auto const* gun = FindLayer(before, "gun")->frame.get();
	soldier.inv[HANDPOS].usItem = 26; // AKM
	soldier.inv[HELMETPOS].usItem = 0;
	auto const after = GetVisualEquipmentAppearance(soldier, 0);
	ASSERT_NE(FindLayer(after, "gun"), nullptr);
	EXPECT_NE(gun, FindLayer(after, "gun")->frame.get());
	EXPECT_EQ(FindLayer(after, "helmet"), nullptr);
	EXPECT_EQ(soldier.bActionPoints, 17); EXPECT_EQ(soldier.bLife, 73);
	EXPECT_EQ(soldier.inv[HANDPOS].usItem, 26);
	EXPECT_EQ(soldier.inv[VESTPOS].usItem, KEVLAR_VEST);
	soldier.ubBodyType = HATKIDCIV;
	EXPECT_TRUE(GetVisualEquipmentAppearance(soldier, 0).layers.empty());
}

TEST_F(VanillaGearAssets, VanillaGunsAndDualHandgunsHaveReplacementBodyLayers)
{
	struct Body { UINT8 type; UINT16 rifle, pistol, dual; };
	Body const bodies[] = {
		{REGMALE, RGMSTANDING, RGMPISTOLBREATH, RGMDBLBREATH},
		{BIGMALE, BGMSTANDING, BGMPISTOLBREATH, BGMDBLBREATH},
		{REGFEMALE, RGFSTANDING, RGFPISTOLBREATH, RGFDBLBREATH},
	};
	for (auto const& body : bodies)
	for (UINT16 item = 1; item <= 36; ++item)
	{
		SCOPED_TRACE(testing::Message() << "Body " << int(body.type) << ", gun " << item);
		SOLDIERTYPE soldier{};
		soldier.ubBodyType = body.type;
		bool const oneHanded = item <= 10 || item == 15;
		soldier.usAnimSurface = oneHanded ? body.pistol : body.rifle;
		soldier.inv[HANDPOS].usItem = item;
		auto* base = LoadBase(soldier.usAnimSurface);
		for (UINT16 direction = 0; direction < 8; ++direction)
		{
			auto const appearance = GetVisualEquipmentAppearance(soldier, direction * (base->SubregionCount() / 8));
			EXPECT_TRUE(appearance.replaceBase);
			ASSERT_NE(FindLayer(appearance, "gun"), nullptr);
		}
		if (oneHanded)
		{
			soldier.usAnimSurface = body.dual;
			soldier.inv[SECONDHANDPOS].usItem = 3;
			LoadBase(body.dual);
			auto const appearance = GetVisualEquipmentAppearance(soldier, 0);
			EXPECT_TRUE(appearance.replaceBase);
			EXPECT_NE(FindLayer(appearance, "gunleft"), nullptr);
		}
	}
}

TEST_F(VanillaGearAssets, CompleteArmourAndMixedDualGunsCoverMovementAndStanceFrames)
{
	struct Body { UINT8 type; std::array<UINT16,8> rifle, pistol, dual; };
	Body const bodies[] = {
		{REGMALE,
		 {RGMSTANDING,RGMCROUCHAIM,RGMPRONE,RGMBASICWALKING,RGMRUNNING,RGMSNEAKING,RGMCROUCHING,RGMSTANDAIM},
		 {RGMPISTOLBREATH,RGMHANDGUN_C_SHOT,RGMHANDGUN_PRONE,RGMNOTHING_WALK,RGMNOTHING_RUN,RGMNOTHING_SWAT,RGMNOTHING_CROUCH,RGMHANDGUN_S_SHOT},
		 {RGMDBLBREATH,RGMCDBLSHOT,RGMDWPRONE,RGMNOTHING_WALK,RGMNOTHING_RUN,RGMNOTHING_SWAT,RGMNOTHING_CROUCH,RGMSTANDDWALAIM}},
		{BIGMALE,
		 {BGMSTANDING,BGMCROUCHAIM,BGMPRONE,BGMWALKING,BGMRUNNING,BGMSNEAKING,BGMCROUCHING,BGMSTANDAIM},
		 {BGMPISTOLBREATH,BGMHANDGUN_C_SHOT,BGMHANDGUN_PRONE,BGMNOTHING_WALK,BGMNOTHING_RUN,BGMNOTHING_SWAT,BGMNOTHING_CROUCH,BGMHANDGUN_S_SHOT},
		 {BGMDBLBREATH,BGMCDBLSHOT,BGMDWPRONE,BGMNOTHING_WALK,BGMNOTHING_RUN,BGMNOTHING_SWAT,BGMNOTHING_CROUCH,BGMSTANDDWALAIM}},
		{REGFEMALE,
		 {RGFSTANDING,RGFCROUCHAIM,RGFPRONE,RGFWALKING,RGFRUNNING,RGFSNEAKING,RGFCROUCHING,RGFSTANDAIM},
		 {RGFPISTOLBREATH,RGFHANDGUN_C_SHOT,RGFHANDGUN_PRONE,RGFNOTHING_WALK,RGFNOTHING_RUN,RGFNOTHING_SWAT,RGFNOTHING_CROUCH,RGFHANDGUN_S_SHOT},
		 {RGFDBLBREATH,RGFCDBLSHOT,RGFDWPRONE,RGFNOTHING_WALK,RGFNOTHING_RUN,RGFNOTHING_SWAT,RGFNOTHING_CROUCH,RGFSTANDDWALAIM}},
	};
	for (auto const& body : bodies)
	for (int weapon = 0; weapon < 3; ++weapon)
	for (auto surface : weapon == 0 ? body.rifle : weapon == 1 ? body.pistol : body.dual)
	{
		SCOPED_TRACE(testing::Message() << "Body " << int(body.type) << ", weapon configuration " << weapon << ", surface " << surface);
		SOLDIERTYPE soldier{};
		soldier.ubBodyType = body.type; soldier.usAnimSurface = surface;
		soldier.inv[HANDPOS].usItem = weapon == 0 ? G11 : 7; // Desert Eagle
		soldier.inv[SECONDHANDPOS].usItem = weapon == 2 ? 3 : 0; // Mixed Beretta/DE
		soldier.inv[HELMETPOS].usItem = STEEL_HELMET;
		soldier.inv[VESTPOS].usItem = KEVLAR_VEST;
		soldier.inv[LEGPOS].usItem = KEVLAR_LEGGINGS;
		soldier.inv[HEAD1POS].usItem = NIGHTGOGGLES;
		soldier.inv[HEAD2POS].usItem = EXTENDEDEAR;
		auto* base = LoadBase(surface);
		for (UINT16 frame = 0; frame < base->SubregionCount(); ++frame)
		{
			auto const appearance = GetVisualEquipmentAppearance(soldier, frame);
			ASSERT_TRUE(appearance.replaceBase) << "Frame " << frame;
			for (auto const* name : {"helmet","vest","legarmor","facegear","ears","gun"})
				ASSERT_NE(FindLayer(appearance,name),nullptr) << name << ", frame " << frame;
			if (weapon == 2) ASSERT_NE(FindLayer(appearance,"gunleft"),nullptr) << "Frame " << frame;
		}
	}
}

TEST_F(VanillaGearAssets, M14CopyDoesNotUseTheUnrelated113Rifle)
{
	SOLDIERTYPE soldier{};
	soldier.ubBodyType = REGMALE; soldier.usAnimSurface = RGMSTANDING;
	LoadBase(RGMSTANDING);
	soldier.inv[HANDPOS].usItem = 27;
	auto const normal = GetVisualEquipmentAppearance(soldier, 0);
	soldier.inv[HANDPOS].usItem = 331;
	auto const copy = GetVisualEquipmentAppearance(soldier, 0);
	ASSERT_NE(FindLayer(normal, "gun"), nullptr);
	ASSERT_NE(FindLayer(copy, "gun"), nullptr);
	EXPECT_EQ(FindLayer(normal, "gun")->frame, FindLayer(copy, "gun")->frame);
}

TEST_F(VanillaGearAssets, IncompatibleOrOutOfRangeAnimationFramesKeepTheOriginalBody)
{
	SOLDIERTYPE soldier{};
	soldier.ubBodyType = REGMALE; soldier.usAnimSurface = RGMSTANDING;
	soldier.inv[HANDPOS].usItem = G11; soldier.inv[HELMETPOS].usItem = STEEL_HELMET;
	auto* standing = LoadBase(RGMSTANDING);
	auto const invalid = GetVisualEquipmentAppearance(soldier, standing->SubregionCount());
	EXPECT_FALSE(invalid.replaceBase); EXPECT_TRUE(invalid.layers.empty());
	auto* walking = LoadBase(RGMBASICWALKING);
	ASSERT_NE(walking->SubregionCount(),standing->SubregionCount());
	gAnimSurfaceDatabase[RGMSTANDING].hVideoObject = walking;
	auto const incompatible = GetVisualEquipmentAppearance(soldier, 0);
	EXPECT_FALSE(incompatible.replaceBase);
	EXPECT_EQ(FindLayer(incompatible,"gun"),nullptr);
	gAnimSurfaceDatabase[RGMSTANDING].hVideoObject = standing;
}

TEST_F(VanillaGearAssets, ArmouredDualGunsRespectWallsAndClippingAt1080p)
{
	constexpr int width = 1920, height = 1080, stride = width + 8, guard = 32;
	constexpr UINT16 guardValue = 0xdead, ground = 0x4208, wall = 0x632c;
	struct Body { UINT8 type; std::array<UINT16,3> poses; };
	Body const bodies[] = {
		{REGMALE,{RGMSTANDDWALAIM,RGMCDBLSHOT,RGMDWPRONE}},
		{BIGMALE,{BGMSTANDDWALAIM,BGMCDBLSHOT,BGMDWPRONE}},
		{REGFEMALE,{RGFSTANDDWALAIM,RGFCDBLSHOT,RGFDWPRONE}},
	};
	std::vector<UINT16> pixels(stride * height + guard * 2), depths(pixels.size());
	SGPRect clip{0,0,width,height};
	for (auto const& body : bodies)
	for (auto surface : body.poses)
	{
		SOLDIERTYPE soldier{};
		soldier.ubBodyType = body.type; soldier.usAnimSurface = surface;
		soldier.inv[HANDPOS].usItem = 7; soldier.inv[SECONDHANDPOS].usItem = 3;
		soldier.inv[HELMETPOS].usItem = STEEL_HELMET; soldier.inv[VESTPOS].usItem = KEVLAR_VEST;
		soldier.inv[LEGPOS].usItem = KEVLAR_LEGGINGS;
		auto* base = LoadBase(surface);
		std::array<UINT16,256> colors{};
		for (int i = 0; i < 256; ++i) colors[i] = 0x1000 + i;
		for (int direction = 0; direction < 8; ++direction)
		{
			SCOPED_TRACE(testing::Message() << "Body " << int(body.type) << ", surface " << surface << ", direction " << direction);
			UINT16 const index = (direction + 1) * (base->SubregionCount() / 8) - 1;
			auto const appearance = GetVisualEquipmentAppearance(soldier,index);
			ASSERT_TRUE(appearance.replaceBase);
			auto const composite = ComposeVisualEquipment(appearance,soldier,colors.data());
			auto const& image = composite.frame;
			int const x = width / 2 - image.offsetX - image.width / 2;
			int const y = height / 2 - image.offsetY - image.height / 2;
			for (bool obscured : {false,true})
			{
				std::fill(pixels.begin(),pixels.end(),guardValue); std::fill(depths.begin(),depths.end(),guardValue);
				auto* buffer = pixels.data() + guard; auto* zbuffer = depths.data() + guard;
				for (int py = 0; py < height; ++py)
				for (int px = 0; px < width; ++px)
				{
					buffer[py * stride + px] = px >= width / 2 ? wall : ground;
					zbuffer[py * stride + px] = px >= width / 2 ? 200 : 0;
				}
				DrawVisualEquipment(image,buffer,stride * 2,zbuffer,100,x,y,clip,composite.colors.data(),true,true,obscured,false,false,true);
				int ghostPixels = 0;
				for (int py = y + image.offsetY; py < y + image.offsetY + image.height; ++py)
				for (int px = width / 2; px < x + image.offsetX + image.width; ++px)
				{
					EXPECT_EQ(zbuffer[py * stride + px],200);
					UINT16 const index = image.pixels[(py - y - image.offsetY) * image.width + px - x - image.offsetX];
					bool const ghost = obscured && (px + py) % 2 == 0 && index != 254 && index != 256;
					EXPECT_EQ(buffer[py * stride + px],ghost ? composite.colors[index] : wall);
					ghostPixels += ghost;
				}
				if (obscured) EXPECT_GT(ghostPixels,0);
			}
			// Exercise all four screen edges using real composite extents and
			// padded scanlines. Guard regions must remain untouched.
			for (auto origin : {std::pair<int,int>{-image.offsetX - image.width / 2,540},
				{width - image.offsetX - image.width / 2,540},
				{960,-image.offsetY - image.height / 2},{960,height - image.offsetY - image.height / 2}})
			{
				std::fill(pixels.begin(),pixels.end(),guardValue); std::fill(depths.begin(),depths.end(),guardValue);
				for (int py = 0; py < height; ++py)
				{
					std::fill_n(pixels.data() + guard + py * stride,width,ground);
					std::fill_n(depths.data() + guard + py * stride,width,0);
				}
				DrawVisualEquipment(image,pixels.data() + guard,stride * 2,depths.data() + guard,100,origin.first,origin.second,clip,composite.colors.data(),true,true,false,false,false,true);
				for (int py = 0; py < height; ++py)
				for (int px = width; px < stride; ++px)
				{
					EXPECT_EQ(pixels[guard + py * stride + px],guardValue);
					EXPECT_EQ(depths[guard + py * stride + px],guardValue);
				}
				for (int i = 0; i < guard; ++i)
				{
					EXPECT_EQ(pixels[i],guardValue); EXPECT_EQ(pixels[pixels.size() - 1 - i],guardValue);
					EXPECT_EQ(depths[i],guardValue); EXPECT_EQ(depths[depths.size() - 1 - i],guardValue);
				}
			}
		}
	}
}

TEST_F(VanillaGearAssets, ProneWorldRendererDrawsOnlyTheReplacementBody)
{
	constexpr int width = 320, height = 240, tileX = 80, tileY = 80;
	constexpr int tile = tileY * WORLD_COLS + tileX;
	constexpr UINT16 background = 0x4208;
	auto surface = std::make_unique<SGPVSurface>(width,height,16);
	std::vector<UINT16> depth(width * height), expected(width * height);
	struct Restore
	{
		UILayout layout = g_ui;
		SGPVSurface* surface = FRAME_BUFFER;
		UINT16* depth = gpZBuffer;
		MAP_ELEMENT map = gpWorldLevelData[tile];
		INT16 offsetX = gsRenderWorldOffsetX, offsetY = gsRenderWorldOffsetY, renderHeight = gsRenderHeight;
		~Restore()
		{
			g_ui = layout; FRAME_BUFFER = surface; gpZBuffer = depth; gpWorldLevelData[tile] = map;
			gsRenderWorldOffsetX = offsetX; gsRenderWorldOffsetY = offsetY; gsRenderHeight = renderHeight;
		}
	} restore;
	FRAME_BUFFER = surface.get(); gpZBuffer = depth.data();
	g_ui.m_tacticalMapCenterX = 160; g_ui.m_tacticalMapCenterY = 160;
	g_ui.m_worldClippingRect = {0,0,width,height};
	gsRenderWorldOffsetX = gsRenderWorldOffsetY = gsRenderHeight = 0;
	LEVELNODE node{}; node.ubShadeLevel = 6; // Vanilla shade adjustment selects pShades[4].
	gpWorldLevelData[tile] = {}; gpWorldLevelData[tile].pMercHead = &node;
	std::array<UINT16,256> colors{}; colors.fill(0x001f);
	struct Body { UINT8 type; std::array<UINT16,3> poses; };
	Body const bodies[] = {
		{REGMALE,{RGMPRONE,RGMHANDGUN_PRONE,RGMDWPRONE}},
		{BIGMALE,{BGMPRONE,BGMHANDGUN_PRONE,BGMDWPRONE}},
		{REGFEMALE,{RGFPRONE,RGFHANDGUN_PRONE,RGFDWPRONE}},
	};
	for (auto const& body : bodies)
	for (int weapon = 0; weapon < 3; ++weapon)
	{
		SOLDIERTYPE soldier{};
		soldier.ubBodyType = body.type; soldier.usAnimSurface = body.poses[weapon];
		soldier.bActive = TRUE; soldier.bLastRenderVisibleValue = 1; soldier.bSide = OUR_TEAM;
		soldier.uiStatusFlags = SOLDIER_MULTITILE; soldier.sZLevelOverride = 1000;
		soldier.dXPos = gsRenderCenterX; soldier.dYPos = gsRenderCenterY;
		soldier.pShades[4] = colors.data();
		soldier.inv[HANDPOS].usItem = weapon == 0 ? G11 : 7;
		soldier.inv[SECONDHANDPOS].usItem = weapon == 2 ? 3 : 0;
		soldier.inv[HELMETPOS].usItem = STEEL_HELMET; soldier.inv[VESTPOS].usItem = KEVLAR_VEST;
		node.pSoldier = &soldier;
		auto* base = LoadBase(soldier.usAnimSurface);
		// Loaded game animations receive these from their structure data. Supply
		// a neutral profile here so the legacy prone path is exercised too.
		base->ppZStripInfo = std::make_unique<std::unique_ptr<ZStripInfo>[]>(8);
		for (int i = 0; i < 8; ++i)
		{
			base->ppZStripInfo[i] = std::make_unique<ZStripInfo>();
			base->ppZStripInfo[i]->ubFirstZStripWidth = 20;
		}
		for (UINT16 frame = 0; frame < base->SubregionCount(); ++frame)
		{
			soldier.usAniFrame = frame;
			soldier.bDirection = frame / (base->SubregionCount() / 8);
			SCOPED_TRACE(testing::Message() << "Body " << int(body.type) << ", weapon " << weapon << ", frame " << frame);
			auto const appearance = GetVisualEquipmentAppearance(soldier,frame);
			ASSERT_TRUE(appearance.replaceBase);
			auto const composite = ComposeVisualEquipment(appearance,soldier,colors.data());
			std::fill(expected.begin(),expected.end(),background);
			DrawVisualEquipment(composite.frame,expected.data(),width * 2,nullptr,1000,160,160,
				g_ui.m_worldClippingRect,composite.colors.data(),false,false,false,false,false,true);
			surface->Fill(background); std::fill(depth.begin(),depth.end(),0);
			RenderMultiTileMercForTesting(tileX,tileY);
			SGPVSurface::Lock actual(surface.get());
			int mismatches = 0;
			for (int y = 0; y < height; ++y)
			{
				auto const* row = reinterpret_cast<UINT16 const*>(reinterpret_cast<UINT8 const*>(actual.Buffer<UINT16>()) + y * actual.Pitch());
				for (int x = 0; x < width; ++x) mismatches += row[x] != expected[y * width + x];
			}
			ASSERT_EQ(mismatches,0) << "The world renderer drew pixels beyond the replacement sprite.";
		}
	}
}

TEST_F(VanillaGearAssets, OptionalRenderContactSheet)
{
	auto const* output = std::getenv("JA2_VISUAL_TEST_OUTPUT");
	if (!output) GTEST_SKIP() << "Set JA2_VISUAL_TEST_OUTPUT to save a render contact sheet.";
	constexpr int width = 960, height = 1440, cellWidth = 60, rowHeight = 80;
	std::vector<UINT16> pixels(width * height, 0x4208);
	SGPRect clip{0, 0, width, height};
	LoadPaletteData();
	BuildShadeTable();
	struct Body { UINT8 type; std::array<UINT16,6> poses; };
	Body const bodies[] = {
		{REGMALE, {RGMHANDGUN_S_SHOT,RGMHANDGUN_C_SHOT,RGMHANDGUN_PRONE,RGMSTANDDWALAIM,RGMCDBLSHOT,RGMDWPRONE}},
		{BIGMALE, {BGMHANDGUN_S_SHOT,BGMHANDGUN_C_SHOT,BGMHANDGUN_PRONE,BGMSTANDDWALAIM,BGMCDBLSHOT,BGMDWPRONE}},
		{REGFEMALE, {RGFHANDGUN_S_SHOT,RGFHANDGUN_C_SHOT,RGFHANDGUN_PRONE,RGFSTANDDWALAIM,RGFCDBLSHOT,RGFDWPRONE}},
	};
	for (int row = 0; row < 18; ++row)
	{
		auto const& body = bodies[row / 6]; int pose = row % 6;
		SOLDIERTYPE soldier{};
		soldier.ubBodyType = body.type;
		soldier.usAnimSurface = body.poses[pose];
		soldier.inv[HANDPOS].usItem = 7; // Desert Eagle
		soldier.inv[SECONDHANDPOS].usItem = pose >= 3 ? 3 : 0;
		auto* base = LoadBase(soldier.usAnimSurface);
		std::array<SGPPaletteEntry, 256> palette;
		std::copy_n(base->Palette(), 256, palette.begin());
		SetPaletteReplacement(palette.data(), "BROWNHEAD");
		SetPaletteReplacement(palette.data(), "GREENVEST");
		SetPaletteReplacement(palette.data(), "BEIGEPANTS");
		SetPaletteReplacement(palette.data(), "PINKSKIN");
		CreateSoldierShadeTables(soldier.pShades, soldier.effect_shade, soldier.pGlowShades, palette.data());
		for (int direction = 0; direction < 8; ++direction)
		for (int equipped = 0; equipped < 2; ++equipped)
		{
			soldier.inv[HELMETPOS].usItem = equipped ? SPECTRA_HELMET : 0;
			soldier.inv[VESTPOS].usItem = equipped ? KEVLAR2_VEST : 0;
			soldier.inv[LEGPOS].usItem = equipped ? KEVLAR_LEGGINGS : 0;
			soldier.inv[HEAD1POS].usItem = equipped ? NIGHTGOGGLES : 0;
			soldier.inv[HEAD2POS].usItem = equipped ? EXTENDEDEAR : 0;
			UINT16 const frame = direction * (base->SubregionCount() / 8) + (base->SubregionCount() / 8 - 1);
			auto const appearance = GetVisualEquipmentAppearance(soldier, frame);
			int const x = (direction * 2 + equipped) * cellWidth + 30, y = row * rowHeight + 58;
			if (!appearance.replaceBase) Blt8BPPDataTo16BPPBufferTransShadowClip(pixels.data(), width * 2, base, x, y, frame, &clip, soldier.pShades[4]);
			auto const composite = ComposeVisualEquipment(appearance, soldier, soldier.pShades[4]);
			DrawVisualEquipment(composite.frame, pixels.data(), width * 2, nullptr, 0, x, y, clip,
				composite.colors.data(), false, false, false, false, false, true);
		}
		for (auto* shade : soldier.pShades) delete[] shade;
		for (auto* shade : soldier.pGlowShades) delete[] shade;
		delete[] soldier.effect_shade;
	}
	DeletePaletteData();
	auto* surface = SDL_CreateRGBSurfaceFrom(pixels.data(), width, height, 16, width * 2, 0xf800, 0x07e0, 0x001f, 0);
	ASSERT_NE(surface, nullptr);
	int const result = SDL_SaveBMP(surface, output); SDL_FreeSurface(surface);
	EXPECT_EQ(result, 0) << SDL_GetError();
}
