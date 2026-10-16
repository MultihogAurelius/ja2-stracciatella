#pragma once

#include "ScreenIDs.h"
#include <SDL.h>

struct InterfacePresentation
{
	SDL_Rect source{}, destination{};
	int scale = 1;
};

InterfacePresentation MakeInterfacePresentation(int width, int height, ScreenID screen, int requestedScale);
SDL_Point InterfaceToDisplay(InterfacePresentation const&, SDL_Point);
SDL_Point DisplayToInterface(InterfacePresentation const&, SDL_Point);
bool SameInterfacePresentation(InterfacePresentation const&, InterfacePresentation const&);
void RenderScaledInterface(SDL_Renderer*, SDL_Texture*, InterfacePresentation const&);

// The last displayed transform is also the transform used by incoming events.
InterfacePresentation const& GetInterfacePresentation();
void SetInterfacePresentation(InterfacePresentation const&);
