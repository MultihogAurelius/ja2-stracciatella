#include "Tactical_Zoom.h"
#include <algorithm>

namespace
{
TacticalPresentation currentTactical;
int tacticalZoomStep;
bool Contains(SDL_Rect const& rect, SDL_Point point) { return SDL_PointInRect(&point,&rect); }
int TacticalFloorDivide(int value, int divisor)
{
	return value >= 0 ? value / divisor : -((-value + divisor - 1) / divisor);
}
}

TacticalPresentation MakeTacticalPresentation(SDL_Rect viewport, int step)
{
	TacticalPresentation result{viewport,viewport};
	if (viewport.w <= 0 || viewport.h <= 0) return result;
	int const quarters = 4 + std::clamp(step,0,8);
	result.source.w = std::max(1,viewport.w * 4 / quarters);
	result.source.h = std::max(1,viewport.h * 4 / quarters);
	result.source.x += (viewport.w - result.source.w) / 2;
	result.source.y += (viewport.h - result.source.h) / 2;
	return result;
}

SDL_Point DisplayToTactical(TacticalPresentation const& p, SDL_Point point)
{
	if (!p.Zoomed() || !Contains(p.viewport,point)) return point;
	return {p.source.x + (2 * (point.x - p.viewport.x) + 1) * p.source.w / (2 * p.viewport.w),
		p.source.y + (2 * (point.y - p.viewport.y) + 1) * p.source.h / (2 * p.viewport.h)};
}

SDL_Point TacticalToDisplay(TacticalPresentation const& p, SDL_Point point)
{
	if (!p.Zoomed() || !Contains(p.viewport,point)) return point;
	// Choose the centre of the source pixel's displayed footprint. This keeps
	// warps and the cursor stable at fractional zoom factors.
	return {p.viewport.x + TacticalFloorDivide((2 * (point.x - p.source.x) + 1) * p.viewport.w,2 * p.source.w),
		p.viewport.y + TacticalFloorDivide((2 * (point.y - p.source.y) + 1) * p.viewport.h,2 * p.source.h)};
}

bool SameTacticalPresentation(TacticalPresentation const& a, TacticalPresentation const& b)
{
	return SDL_RectEquals(&a.viewport,&b.viewport) && SDL_RectEquals(&a.source,&b.source);
}

void RenderTacticalPresentation(SDL_Renderer* renderer, SDL_Texture* texture, TacticalPresentation const& p)
{
	SDL_SetTextureScaleMode(texture,SDL_ScaleModeNearest);
	SDL_RenderCopy(renderer,texture,nullptr,nullptr);
	SDL_RenderCopy(renderer,texture,&p.source,&p.viewport);
}

TacticalPresentation const& GetTacticalPresentation() { return currentTactical; }
void SetTacticalPresentation(TacticalPresentation const& p) { currentTactical = p; }
int GetTacticalZoomStep() { return tacticalZoomStep; }
void SetTacticalZoomStep(int step) { tacticalZoomStep = std::clamp(step,0,8); }
