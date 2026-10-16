#pragma once

#include "Types.h"
#include <string_theory/string>
#include <memory>
#include <vector>

struct SOLDIERTYPE;

// 256 is transparent, 254 is a shadow. Composite frames use additional palette
// blocks above 511 so differently coloured equipment can share one draw pass.
struct VisualEquipmentFrame
{
	int width{}, height{}, offsetX{}, offsetY{};
	std::vector<UINT16> pixels;
};

struct VisualEquipmentLayer
{
	std::shared_ptr<const VisualEquipmentFrame> frame;
	ST::string name, palette, gear;
	bool shadow;
};

struct VisualEquipmentAppearance
{
	std::vector<VisualEquipmentLayer> layers;
	bool replaceBase{};
};

struct VisualEquipmentComposite
{
	VisualEquipmentFrame frame;
	std::vector<UINT16> colors;
};

VisualEquipmentAppearance GetVisualEquipmentAppearance(SOLDIERTYPE const&, UINT16 frame);
const UINT16* GetVisualEquipmentShade(SOLDIERTYPE const&, const UINT16* soldierShade, ST::string const& palette);
VisualEquipmentComposite ComposeVisualEquipment(VisualEquipmentAppearance const&, SOLDIERTYPE const&, const UINT16* soldierShade);
void ResetVisualEquipment();

// Uses the soldier's existing visibility and depth decisions. Equal depth is
// allowed so the equipment can cover pixels the same soldier already drew.
void DrawVisualEquipment(const VisualEquipmentFrame&, UINT16* buffer, UINT32 pitch,
	UINT16* zbuffer, UINT16 z, int x, int y, SGPRect const& clip, const UINT16* palette,
	bool testZ, bool writeZ, bool obscured, bool pixelate, bool translucent, bool shadows = false);
