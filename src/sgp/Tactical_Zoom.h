#pragma once

#include <SDL.h>

struct TacticalPresentation
{
	SDL_Rect viewport{}, source{};
	bool Zoomed() const { return source.w > 0 && (source.w != viewport.w || source.h != viewport.h); }
};

TacticalPresentation MakeTacticalPresentation(SDL_Rect viewport, int zoomStep);
SDL_Point TacticalToDisplay(TacticalPresentation const&, SDL_Point);
SDL_Point DisplayToTactical(TacticalPresentation const&, SDL_Point);
bool SameTacticalPresentation(TacticalPresentation const&, TacticalPresentation const&);
void RenderTacticalPresentation(SDL_Renderer*, SDL_Texture*, TacticalPresentation const&);
TacticalPresentation const& GetTacticalPresentation();
void SetTacticalPresentation(TacticalPresentation const&);
int GetTacticalZoomStep();
void SetTacticalZoomStep(int);
