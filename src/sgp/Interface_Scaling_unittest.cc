#include "gtest/gtest.h"
#include "Interface_Scaling.h"
#include "Input.h"
#include "Video.h"
#include "JAScreens.h"
#include "MessageBoxScreen.h"
#include "UILayout.h"
#include "FPS.h"
#include "VSurface.h"
#include <memory>
#include <vector>
#include <string>

namespace
{
std::vector<UINT16>* capture;
void CapturePresentedFrame(SDL_Renderer* renderer)
{
	SDL_RenderReadPixels(renderer,nullptr,SDL_PIXELFORMAT_RGB565,capture->data(),SCREEN_WIDTH * 2);
}
}

TEST(InterfaceScaling, UsesLargestRequestedIntegerScaleThatFits)
{
	auto p = MakeInterfacePresentation(1920,1080,MAP_SCREEN,3);
	EXPECT_EQ(p.scale,2);
	EXPECT_EQ(p.source.x,640); EXPECT_EQ(p.source.y,300);
	EXPECT_EQ(p.destination.x,320); EXPECT_EQ(p.destination.y,60);
	EXPECT_EQ(p.destination.w,1280); EXPECT_EQ(p.destination.h,960);
	p = MakeInterfacePresentation(1920,1440,LAPTOP_SCREEN,3);
	EXPECT_EQ(p.scale,3); EXPECT_EQ(p.destination.w,1920); EXPECT_EQ(p.destination.h,1440);
	p = MakeInterfacePresentation(1920,1440,LAPTOP_SCREEN,2);
	EXPECT_EQ(p.scale,2);
	for (auto screen : {MAP_SCREEN,LAPTOP_SCREEN})
	{
		EXPECT_EQ(MakeInterfacePresentation(640,480,screen,3).scale,1);
		EXPECT_EQ(MakeInterfacePresentation(1920,1080,screen,1).scale,1);
	}
}

TEST(InterfaceScaling, TacticalAndOtherScreensRetainTheFullNativeCanvas)
{
	for (auto screen : {GAME_SCREEN,MAINMENU_SCREEN,OPTIONS_SCREEN,SAVE_LOAD_SCREEN,SHOPKEEPER_SCREEN})
	{
		auto const p = MakeInterfacePresentation(1920,1080,screen,3);
		EXPECT_EQ(p.scale,1); EXPECT_EQ(p.source.x,0); EXPECT_EQ(p.source.y,0);
		EXPECT_EQ(p.source.w,1920); EXPECT_EQ(p.source.h,1080);
		auto const point = DisplayToInterface(p,{1245,812});
		EXPECT_EQ(point.x,1245); EXPECT_EQ(point.y,812);
	}
}

TEST(InterfaceScaling, PointerHitsEverySourcePixelAndSubpixelAtTwoAndThreeTimes)
{
	for (int scale : {2,3})
	{
		auto const p = MakeInterfacePresentation(2560,1600,MAP_SCREEN,scale);
		ASSERT_EQ(p.scale,scale);
		for (int y = 0; y < 480; ++y)
		for (int x = 0; x < 640; ++x)
		{
			auto const display = InterfaceToDisplay(p,{p.source.x + x,p.source.y + y});
			for (int dy = 0; dy < scale; ++dy)
			for (int dx = 0; dx < scale; ++dx)
			{
				auto const source = DisplayToInterface(p,{display.x + dx,display.y + dy});
				ASSERT_EQ(source.x,p.source.x + x); ASSERT_EQ(source.y,p.source.y + y);
			}
		}
		// Black margins map outside the UI, so they cannot activate edge buttons.
		auto const outside = DisplayToInterface(p,{p.destination.x - 1,p.destination.y - 1});
		EXPECT_EQ(outside.x,p.source.x - 1); EXPECT_EQ(outside.y,p.source.y - 1);
	}
}

TEST(InterfaceScaling, MouseEventsAndScreenTransitionsUseTheDisplayedTransform)
{
	auto const previous = GetInterfacePresentation(); auto const mouse = GetMousePos();
	auto const layout = g_ui; g_ui.setScreenSize(1920,1080);
	auto const scaled = MakeInterfacePresentation(1920,1080,MAP_SCREEN,2);
	SetInterfacePresentation(scaled);
	SDL_MouseMotionEvent motion{}; motion.x = 1120; motion.y = 660;
	MouseMove(&motion);
	auto const logical = GetMousePos();
	EXPECT_EQ(logical.iX,1040); EXPECT_EQ(logical.iY,600);
	SDL_MouseButtonEvent button{}; button.button = SDL_BUTTON_LEFT; button.x = 1121; button.y = 661;
	MouseButtonDown(&button);
	EXPECT_EQ(GetMousePos().iX,1040); EXPECT_EQ(GetMousePos().iY,600);
	MouseButtonUp(&button);
	auto const physical = InterfaceToDisplay(scaled,{logical.iX,logical.iY});
	auto const tactical = MakeInterfacePresentation(1920,1080,GAME_SCREEN,3);
	SetInterfacePresentation(tactical);
	SetSafeMousePosition(physical.x,physical.y);
	EXPECT_EQ(GetMousePos().iX,1120); EXPECT_EQ(GetMousePos().iY,660);
	// Restore the original logical position without applying a transform twice.
	SetInterfacePresentation({}); SetSafeMousePosition(mouse.iX,mouse.iY); SetInterfacePresentation(previous);
	g_ui = layout;
	DequeueAllInputEvents();
}

TEST(InterfaceScaling, SDLPresentationKeepsPixelsSharpAndMatchesButtonHitCoordinates)
{
	for (int scale : {2,3})
	{
		int const width = 1920, height = scale == 3 ? 1440 : 1080;
		auto const p = MakeInterfacePresentation(width,height,LAPTOP_SCREEN,scale);
		std::unique_ptr<SDL_Surface,decltype(&SDL_FreeSurface)> output(
			SDL_CreateRGBSurfaceWithFormat(0,width,height,32,SDL_PIXELFORMAT_ARGB8888),SDL_FreeSurface);
		ASSERT_NE(output,nullptr);
		std::unique_ptr<SDL_Renderer,decltype(&SDL_DestroyRenderer)> renderer(SDL_CreateSoftwareRenderer(output.get()),SDL_DestroyRenderer);
		ASSERT_NE(renderer,nullptr);
		std::unique_ptr<SDL_Texture,decltype(&SDL_DestroyTexture)> texture(
			SDL_CreateTexture(renderer.get(),SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STATIC,width,height),SDL_DestroyTexture);
		ASSERT_NE(texture,nullptr);
		std::vector<UINT32> source(width * height,0xff000000), actual(source.size());
		for (int y = 0; y < 480; ++y)
		for (int x = 0; x < 640; ++x)
			source[(p.source.y + y) * width + p.source.x + x] = (x + y) % 2 ? 0xffe01020 : 0xff20e010;
		ASSERT_EQ(SDL_UpdateTexture(texture.get(),nullptr,source.data(),width * 4),0);
		SDL_SetRenderDrawColor(renderer.get(),0,0,0,255); SDL_RenderClear(renderer.get());
		RenderScaledInterface(renderer.get(),texture.get(),p);
		ASSERT_EQ(SDL_RenderReadPixels(renderer.get(),nullptr,SDL_PIXELFORMAT_ARGB8888,actual.data(),width * 4),0);
		for (int y = 0; y < height; ++y)
		for (int x = 0; x < width; ++x)
		{
			bool const inside = x >= p.destination.x && x < p.destination.x + p.destination.w &&
				y >= p.destination.y && y < p.destination.y + p.destination.h;
			auto const point = DisplayToInterface(p,{x,y});
			UINT32 const expected = inside ? source[point.y * width + point.x] : 0xff000000;
			ASSERT_EQ(actual[y * width + x],expected) << x << "," << y;
		}
	}
}

TEST(InterfaceScaling, ScaleShortcutsAreConsumedOnlyOnSupportedScreens)
{
	auto const previous = guiCurrentScreen; auto const parent = gMsgBox.uiExitScreen;
	guiCurrentScreen = GAME_SCREEN;
	EXPECT_FALSE(VideoHandleInterfaceScaleKey(SDLK_2,static_cast<SDL_Keymod>(KMOD_CTRL | KMOD_ALT)));
	guiCurrentScreen = MAP_SCREEN;
	EXPECT_FALSE(VideoHandleInterfaceScaleKey(SDLK_2,KMOD_NONE));
	EXPECT_FALSE(VideoHandleInterfaceScaleKey(SDLK_2,KMOD_CTRL));
	EXPECT_TRUE(VideoHandleInterfaceScaleKey(SDLK_2,static_cast<SDL_Keymod>(KMOD_CTRL | KMOD_ALT)));
	guiCurrentScreen = MSG_BOX_SCREEN; gMsgBox.uiExitScreen = LAPTOP_SCREEN;
	EXPECT_TRUE(VideoHandleInterfaceScaleKey(SDLK_3,static_cast<SDL_Keymod>(KMOD_CTRL | KMOD_ALT)));
	guiCurrentScreen = previous; gMsgBox.uiExitScreen = parent;
}

TEST(InterfaceScaling, VideoRefreshScalesTheInterfaceAndCursorAndRestoresTacticalView)
{
	ASSERT_EQ(SDL_WasInit(SDL_INIT_VIDEO),0);
	ASSERT_EQ(FRAME_BUFFER,nullptr);
	struct Restore
	{
		UILayout layout = g_ui;
		ScreenID screen = guiCurrentScreen, parent = gMsgBox.uiExitScreen;
		InterfacePresentation presentation = GetInterfacePresentation();
		SGPPoint mouse = GetMousePos();
		FPS::RenderPresent_t present = FPS::RenderPresentPtr;
		std::string driver = SDL_getenv("SDL_VIDEODRIVER") ? SDL_getenv("SDL_VIDEODRIVER") : "";
		std::string renderer = SDL_GetHint(SDL_HINT_RENDER_DRIVER) ? SDL_GetHint(SDL_HINT_RENDER_DRIVER) : "";
		bool video = false, surfaces = false;
		~Restore()
		{
			if (surfaces) ShutdownVideoSurfaceManager();
			if (video) ShutdownVideoManager();
			SDL_QuitSubSystem(SDL_INIT_VIDEO);
			SDL_setenv("SDL_VIDEODRIVER",driver.c_str(),1);
			SDL_SetHintWithPriority(SDL_HINT_RENDER_DRIVER,renderer.empty() ? nullptr : renderer.c_str(),SDL_HINT_OVERRIDE);
			g_ui = layout; guiCurrentScreen = screen; gMsgBox.uiExitScreen = parent; FPS::RenderPresentPtr = present;
			SetInterfacePresentation({}); SetSafeMousePosition(mouse.iX,mouse.iY); SetInterfacePresentation(presentation);
			capture = nullptr;
		}
	} restore;
	// Default to headless; opt in to the real renderer to catch backend-specific
	// failures such as SDL 2.0.20's OpenGL RGB565 scale-mode crash.
	bool const openGL = SDL_getenv("JA2_VIDEO_TEST_OPENGL") != nullptr;
	if (!openGL) SDL_setenv("SDL_VIDEODRIVER","dummy",1);
	SDL_SetHintWithPriority(SDL_HINT_RENDER_DRIVER,openGL ? "opengl" : "software",SDL_HINT_OVERRIDE);
	ASSERT_EQ(SDL_InitSubSystem(SDL_INIT_VIDEO),0) << SDL_GetError();
	g_ui.setScreenSize(1920,1080);
	InitializeVideoManager(VideoScaleQuality::LINEAR,60); restore.video = true;
	ASSERT_NE(GAME_WINDOW,nullptr);
	InitializeVideoSurfaceManager(); restore.surfaces = true;
	std::vector<UINT16> pixels(1920 * 1080);
	capture = &pixels; FPS::RenderPresentPtr = CapturePresentedFrame;
	FRAME_BUFFER->Fill(0x07e0); MOUSE_BUFFER->Fill(0);
	{
		SGPVSurface::Lock cursor(MOUSE_BUFFER); cursor.Buffer<UINT16>()[0] = 0xf800;
	}
	SetMouseCursorProperties(0,0,2,2);
	SetSafeMousePosition(100,40);
	guiCurrentScreen = MAP_SCREEN;
	RefreshScreen();
	ASSERT_EQ(GetInterfacePresentation().scale,2);
	EXPECT_EQ(pixels[40 * 1920 + 100],0xf800); // Cursor remains visible in a black margin.
	EXPECT_EQ(pixels[40 * 1920 + 200],0);
	EXPECT_EQ(pixels[60 * 1920 + 320],0x07e0);
	EXPECT_EQ(pixels[1019 * 1920 + 1599],0x07e0);
	EXPECT_EQ(pixels[1020 * 1920 + 1600],0);
	EXPECT_EQ(GetMousePos().iX,530); EXPECT_EQ(GetMousePos().iY,290);
	guiCurrentScreen = MSG_BOX_SCREEN; gMsgBox.uiExitScreen = MAP_SCREEN;
	RefreshScreen(); EXPECT_EQ(GetInterfacePresentation().scale,2);
	guiCurrentScreen = GAME_SCREEN;
	RefreshScreen();
	EXPECT_EQ(GetInterfacePresentation().scale,1);
	EXPECT_EQ(GetMousePos().iX,100); EXPECT_EQ(GetMousePos().iY,40);
	EXPECT_EQ(pixels[40 * 1920 + 100],0xf800);
	EXPECT_EQ(pixels[290 * 1920 + 530],0x07e0); // No stale cursor at its old logical position.
	EXPECT_EQ(pixels[1079 * 1920 + 1919],0x07e0); // Full tactical canvas is restored.
	guiCurrentScreen = LAPTOP_SCREEN; RefreshScreen();
	EXPECT_EQ(GetInterfacePresentation().scale,2);
	EXPECT_TRUE(VideoHandleInterfaceScaleKey(SDLK_1,static_cast<SDL_Keymod>(KMOD_CTRL | KMOD_ALT)));
	RefreshScreen(); EXPECT_EQ(GetInterfacePresentation().scale,1);
	EXPECT_TRUE(VideoHandleInterfaceScaleKey(SDLK_3,static_cast<SDL_Keymod>(KMOD_CTRL | KMOD_ALT)));
	RefreshScreen(); EXPECT_EQ(GetInterfacePresentation().scale,2); // Never crop controls to force 3x.
}
