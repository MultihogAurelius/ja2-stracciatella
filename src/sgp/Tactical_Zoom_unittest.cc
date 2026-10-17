#include "gtest/gtest.h"
#include "Tactical_Zoom.h"
#include "Interface_Scaling.h"
#include "Input.h"
#include "Video.h"
#include "JAScreens.h"
#include "Map_Information.h"
#include "UILayout.h"
#include "RenderWorld.h"
#include "Isometric_Utils.h"
#include "Interface.h"
#include "Sys_Globals.h"
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

TEST(TacticalZoom, CameraReachesAllVisibleMapEdgesAndClampsWhenZoomingOut)
{
	struct Restore
	{
		UILayout layout = g_ui; BOOLEAN loaded = gfWorldLoaded; bool editor = gfEditMode; int step = GetTacticalZoomStep();
		INT16 left = gsLeftX, right = gsRightX, top = gsTopY, bottom = gsBottomY;
		INT16 x = gsRenderCenterX, y = gsRenderCenterY;
		INT16 tlx = gsTopLeftWorldX, tly = gsTopLeftWorldY, brx = gsBottomRightWorldX, bry = gsBottomRightWorldY;
		BOOLEAN atLeft = gfScrolledToLeft, atRight = gfScrolledToRight, atTop = gfScrolledToTop, atBottom = gfScrolledToBottom;
		~Restore()
		{
			g_ui = layout; gfWorldLoaded = loaded; gfEditMode = editor; SetTacticalZoomStep(step);
			gsLeftX = left; gsRightX = right; gsTopY = top; gsBottomY = bottom; gsRenderCenterX = x; gsRenderCenterY = y;
			gsTopLeftWorldX = tlx; gsTopLeftWorldY = tly; gsBottomRightWorldX = brx; gsBottomRightWorldY = bry;
			gfScrolledToLeft = atLeft; gfScrolledToRight = atRight; gfScrolledToTop = atTop; gfScrolledToBottom = atBottom;
		}
	} restore;
	g_ui.setScreenSize(1920,1080); g_ui.m_tacticalMapCenterX = 960; g_ui.m_tacticalMapCenterY = 480;
	gsVIEWPORT_START_X = 0; gsVIEWPORT_START_Y = 0; gsVIEWPORT_END_X = 1920; gsVIEWPORT_END_Y = 960;
	gsVIEWPORT_WINDOW_START_Y = 20; gsVIEWPORT_WINDOW_END_Y = 940;
	gsLeftX = -1600; gsRightX = 1600; gsTopY = 600; gsBottomY = 2600;
	gfWorldLoaded = TRUE; gfEditMode = false;
	int const left = gsLeftX + SCROLL_LEFT_PADDING, right = gsRightX + SCROLL_RIGHT_PADDING;
	int const top = gsTopY + SCROLL_TOP_PADDING, bottom = gsBottomY + SCROLL_BOTTOM_PADDING;
	auto check = [](int sx, int sy, bool force = false) {
		INT16 x, y; FromScreenToCellCoordinates(sx,sy + 10,&x,&y);
		return ApplyCameraScrollForTesting(x,y,force);
	};
	for (int step = 1; step <= 8; ++step)
	{
		auto const crop = MakeTacticalPresentation({0,20,1920,920},step).source;
		SDL_Point const nearEdges[] = {{left + 960 - crop.x + 40,1600},
			{right + 960 - crop.x - crop.w - 40,1600},
			{0,top + 480 - crop.y + 40},{0,bottom + 480 - crop.y - crop.h - 40}};
		for (auto const& point : nearEdges)
		{
			SCOPED_TRACE(testing::Message() << "zoom step " << step << " near " << point.x << "," << point.y);
			SetTacticalZoomStep(0); EXPECT_FALSE(check(point.x,point.y)); // Original premature wall.
			SetTacticalZoomStep(step); EXPECT_TRUE(check(point.x,point.y));
		}
		// Requests beyond the visible edge are still rejected at every zoom.
		EXPECT_FALSE(check(left + 960 - crop.x - 80,1600));
		EXPECT_FALSE(check(right + 960 - crop.x - crop.w + 80,1600));
		EXPECT_FALSE(check(0,top + 480 - crop.y - 80));
		EXPECT_FALSE(check(0,bottom + 480 - crop.y - crop.h + 80));
		for (auto const& point : nearEdges)
		{
			SetTacticalZoomStep(step); ASSERT_TRUE(check(point.x,point.y,true));
			// Zoom-out must clamp using the requested step, even though the last
			// presented zoom may still be the old value.
			SetTacticalZoomStep(0); ASSERT_TRUE(ApplyCameraScrollForTesting(gsRenderCenterX,gsRenderCenterY,true));
			EXPECT_GE(gsTopLeftWorldX + gsLeftX,left - 40);
			EXPECT_LE(gsBottomRightWorldX + gsLeftX,right + 40);
			EXPECT_GE(gsTopLeftWorldY + gsTopY,top - 40);
			EXPECT_LE(gsBottomRightWorldY + gsTopY,bottom + 40);
		}
	}
}
