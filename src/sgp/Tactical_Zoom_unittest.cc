#include "gtest/gtest.h"
#include "Tactical_Zoom.h"
#include "Interface_Scaling.h"
#include "Input.h"
#include "Video.h"
#include "JAScreens.h"
#include "Map_Information.h"
#include "UILayout.h"
#include <memory>
#include <vector>

TEST(TacticalZoom, CropIsCentredAndDoesNotIncludeTheInterface)
{
	SDL_Rect const viewport{0,20,1920,920};
	for (int step = 0; step <= 8; ++step)
	{
		auto const p = MakeTacticalPresentation(viewport,step);
		EXPECT_EQ(p.Zoomed(),step > 0);
		EXPECT_EQ(p.source.w,1920 * 4 / (4 + step));
		EXPECT_EQ(p.source.h,920 * 4 / (4 + step));
		for (SDL_Point const outside : {SDL_Point{100,0},SDL_Point{100,940},SDL_Point{1919,1079}})
		{
			auto const mapped = DisplayToTactical(p,outside);
			EXPECT_EQ(mapped.x,outside.x); EXPECT_EQ(mapped.y,outside.y);
		}
		for (int y = p.source.y; y < p.source.y + p.source.h; y += 3)
		for (int x = p.source.x; x < p.source.x + p.source.w; x += 3)
		{
			auto const point = DisplayToTactical(p,TacticalToDisplay(p,{x,y}));
			ASSERT_EQ(point.x,x); ASSERT_EQ(point.y,y);
		}
	}
	EXPECT_FALSE(MakeTacticalPresentation({},8).Zoomed());
}

TEST(TacticalZoom, RenderedPixelsAgreeWithClicksAtEveryZoomLevel)
{
	constexpr int width = 320, height = 240;
	std::unique_ptr<SDL_Surface,decltype(&SDL_FreeSurface)> surface(SDL_CreateRGBSurfaceWithFormat(0,width,height,32,SDL_PIXELFORMAT_ARGB8888),SDL_FreeSurface);
	ASSERT_NE(surface,nullptr);
	std::unique_ptr<SDL_Renderer,decltype(&SDL_DestroyRenderer)> renderer(SDL_CreateSoftwareRenderer(surface.get()),SDL_DestroyRenderer);
	ASSERT_NE(renderer,nullptr);
	std::unique_ptr<SDL_Texture,decltype(&SDL_DestroyTexture)> texture(SDL_CreateTexture(renderer.get(),SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STATIC,width,height),SDL_DestroyTexture);
	ASSERT_NE(texture,nullptr);
	std::vector<UINT32> source(width * height), output(source.size());
	for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) source[y * width + x] = 0xff000000 | x | (y << 10);
	ASSERT_EQ(SDL_UpdateTexture(texture.get(),nullptr,source.data(),width * 4),0);
	for (int step = 0; step <= 8; ++step)
	{
		auto const p = MakeTacticalPresentation({0,20,width,180},step);
		RenderTacticalPresentation(renderer.get(),texture.get(),p);
		ASSERT_EQ(SDL_RenderReadPixels(renderer.get(),nullptr,SDL_PIXELFORMAT_ARGB8888,output.data(),width * 4),0);
		for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x)
		{
			auto const logical = DisplayToTactical(p,{x,y});
			// SDL's software backend accumulates fixed-point rounding error and
			// can choose the adjacent texel near a sampling boundary. Allow only
			// that one source pixel; the unscaled HUD must still match exactly.
			SDL_Point const display{x,y};
			bool const inside = p.Zoomed() && SDL_PointInRect(&display,&p.viewport);
			int const sampledX = output[y * width + x] & 1023;
			int const sampledY = (output[y * width + x] >> 10) & 1023;
			ASSERT_LE(std::abs(sampledX - logical.x),int(inside)) << "step " << step << " at " << x << "," << y;
			ASSERT_LE(std::abs(sampledY - logical.y),int(inside)) << "step " << step << " at " << x << "," << y;
		}
	}
}

TEST(TacticalZoom, WheelOnlyZoomsLoadedBattlefieldAndHonoursLimitsAndDirection)
{
	struct Restore
	{
		UILayout layout = g_ui; ScreenID screen = guiCurrentScreen; BOOLEAN loaded = gfWorldLoaded;
		int step = GetTacticalZoomStep(); TacticalPresentation presentation = GetTacticalPresentation();
		InterfacePresentation interface = GetInterfacePresentation(); SDL_Point mouse = GetMouseDisplayPos();
		~Restore() { g_ui = layout; guiCurrentScreen = screen; gfWorldLoaded = loaded; SetTacticalZoomStep(step); SetTacticalPresentation(presentation); SetInterfacePresentation(interface); SetSafeMousePosition(mouse.x,mouse.y); DequeueAllInputEvents(); }
	} restore;
	g_ui.setScreenSize(1920,1080); gsVIEWPORT_WINDOW_START_Y = 20; gsVIEWPORT_WINDOW_END_Y = 940;
	guiCurrentScreen = GAME_SCREEN; gfWorldLoaded = TRUE; SetInterfacePresentation({});
	SetTacticalZoomStep(0); SetTacticalPresentation({}); SetSafeMousePosition(200,200);
	SDL_MouseWheelEvent wheel{}; wheel.y = 1;
	MouseWheelScroll(&wheel); EXPECT_EQ(GetTacticalZoomStep(),1);
	InputAtom event{}; EXPECT_FALSE(DequeueEvent(&event));
	wheel.y = 20; EXPECT_TRUE(VideoHandleTacticalZoomWheel(wheel)); EXPECT_EQ(GetTacticalZoomStep(),8);
	wheel.y = 1; EXPECT_TRUE(VideoHandleTacticalZoomWheel(wheel)); EXPECT_EQ(GetTacticalZoomStep(),8);
	wheel.direction = SDL_MOUSEWHEEL_FLIPPED; EXPECT_TRUE(VideoHandleTacticalZoomWheel(wheel)); EXPECT_EQ(GetTacticalZoomStep(),7);
	SetTacticalPresentation(MakeTacticalPresentation({0,20,1920,920},4));
	SetSafeMousePosition(0,200); EXPECT_EQ(GetMouseDisplayPos().x,0); EXPECT_GT(GetMousePos().iX,0);
	SetSafeMousePosition(200,1000); EXPECT_FALSE(VideoHandleTacticalZoomWheel(wheel)); EXPECT_EQ(GetMousePos().iY,1000);
	SetSafeMousePosition(200,200); guiCurrentScreen = MAP_SCREEN; EXPECT_FALSE(VideoHandleTacticalZoomWheel(wheel));
	guiCurrentScreen = GAME_SCREEN; gfWorldLoaded = FALSE; EXPECT_FALSE(VideoHandleTacticalZoomWheel(wheel));
	wheel.y = 0; EXPECT_FALSE(VideoHandleTacticalZoomWheel(wheel));
}
