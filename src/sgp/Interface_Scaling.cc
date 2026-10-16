#include "Interface_Scaling.h"
#include <algorithm>

namespace
{
InterfacePresentation current;
int FloorDivide(int value, int divisor)
{
	return value >= 0 ? value / divisor : -((-value + divisor - 1) / divisor);
}
}

InterfacePresentation MakeInterfacePresentation(int width, int height, ScreenID screen, int requestedScale)
{
	InterfacePresentation result{{0,0,width,height},{0,0,width,height},1};
	if (screen != MAP_SCREEN && screen != LAPTOP_SCREEN) return result;
	int const scale = std::min({std::clamp(requestedScale,1,3),width / 640,height / 480});
	if (scale <= 1) return result;
	result.source = {(width - 640) / 2,(height - 480) / 2,640,480};
	result.destination = {(width - 640 * scale) / 2,(height - 480 * scale) / 2,640 * scale,480 * scale};
	result.scale = scale;
	return result;
}

SDL_Point InterfaceToDisplay(InterfacePresentation const& p, SDL_Point point)
{
	return {p.destination.x + (point.x - p.source.x) * p.scale,
		p.destination.y + (point.y - p.source.y) * p.scale};
}

SDL_Point DisplayToInterface(InterfacePresentation const& p, SDL_Point point)
{
	return {p.source.x + FloorDivide(point.x - p.destination.x,p.scale),
		p.source.y + FloorDivide(point.y - p.destination.y,p.scale)};
}

bool SameInterfacePresentation(InterfacePresentation const& a, InterfacePresentation const& b)
{
	return a.scale == b.scale && SDL_RectEquals(&a.source,&b.source) && SDL_RectEquals(&a.destination,&b.destination);
}

void RenderScaledInterface(SDL_Renderer* renderer, SDL_Texture* texture, InterfacePresentation const& p)
{
	SDL_SetTextureScaleMode(texture,SDL_ScaleModeNearest);
	SDL_RenderCopy(renderer,texture,&p.source,&p.destination);
}

InterfacePresentation const& GetInterfacePresentation() { return current; }
void SetInterfacePresentation(InterfacePresentation const& presentation) { current = presentation; }
