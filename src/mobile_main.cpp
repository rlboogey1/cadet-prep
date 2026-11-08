// iPhone and iPad version (SDL3 + Dear ImGui, drawn with SDL's renderer, which
// uses Metal on Apple devices). The same GuiApp as the Mac window version; the
// layout switches to a phone layout on narrow screens.
//
// It also builds on the Mac as "Cadet Prep Mobile" for trying the phone layout:
//   ./build/cadet_mobile                 iPhone-sized window (390 x 844)
//   ./build/cadet_mobile --ipad          iPad-sized window (820 x 1180)
//   ./build/cadet_mobile --size 844x390  any size (points)
// Add --touch to use the mouse like a finger (drag to scroll).
//
// For checking layouts without clicking around: --script file.txt runs one command
// per line (taps act like a finger), then quits:
//   wait 30                 frames to wait
//   tap 200 400             tap at x, y (points)
//   drag 200 600 200 200 20 drag from one point to another over 20 frames
//   shot out.bmp            save a screenshot
#include "AppPaths.h"
#include "GuiApp.h"
#include "Visuals.h"
#include "Widgets.h"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <algorithm>
#include <cfloat>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
constexpr bool kPhoneOS = true;
#else
constexpr bool kPhoneOS = false;
#endif

namespace {

SDL_Renderer* gRenderer = nullptr;

// Uploads a picture (the rank insignia) as an SDL texture.
ImTextureID makeSdlTexture(const unsigned char* rgba, int w, int h) {
    SDL_Texture* t = SDL_CreateTexture(gRenderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
    if (!t) return 0;
    SDL_UpdateTexture(t, nullptr, rgba, w * 4);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
    return static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(t));
}

// The question folder: inside the app (iOS: next to the program; macOS app:
// Contents/Resources), else the usual places (see AppPaths.h).
std::string dataFolder(int argc, char* argv[]) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (const char* base = SDL_GetBasePath()) {
        const fs::path bundled = fs::path(base) / "data";
        if (fs::exists(bundled / "subtests.json", ec)) return bundled.string();
    }
    // Only pass a real folder argument on (not the --flags below)
    char* args[2] = {argc > 0 ? argv[0] : nullptr, nullptr};
    int n = argc > 0 ? 1 : 0;
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] == '-') {
            if (!std::strcmp(argv[i], "--size") || !std::strcmp(argv[i], "--script")) ++i;  // skip its value
            continue;
        }
        args[n++] = argv[i];
        break;
    }
    return apppaths::findDataDir(n, args);
}

// --script: plays taps and drags and takes screenshots (see the top of the file).
// Every frame it sets where the finger is and whether it touches the screen, after
// the SDL backend has read the real mouse, so the real mouse is ignored.
struct Script {
    std::vector<std::string> lines;
    size_t next = 0;
    int waitFrames = 0;
    int dragFrame = -1, dragFrames = 0;  // a drag in progress
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    float fx = -FLT_MAX, fy = -FLT_MAX;  // the finger (off screen when lifted)
    bool fingerDown = false, liftNext = false, hideNext = false;
    std::string shot;  // save a screenshot after this frame is drawn

    bool load(const char* path) {
        std::ifstream in(path);
        for (std::string l; std::getline(in, l);)
            if (!l.empty() && l[0] != '#') lines.push_back(l);
        return in.eof();
    }
    void lift() {  // like a real finger: it lifts where it is, then has no position
        fingerDown = false;
        hideNext = true;
    }
    // Moves the script on by one frame. Returns false when it's over.
    bool step() {
        if (hideNext) {
            hideNext = false;
            fx = fy = -FLT_MAX;
        }
        if (liftNext) {  // a tap lifts the frame after it touched
            liftNext = false;
            lift();
        } else if (dragFrame >= 0) {
            const float t = static_cast<float>(++dragFrame) / static_cast<float>(dragFrames);
            fx = x0 + (x1 - x0) * t;
            fy = y0 + (y1 - y0) * t;
            if (dragFrame >= dragFrames) {
                dragFrame = -1;
                liftNext = true;
            }
        } else if (waitFrames > 0) {
            --waitFrames;
        } else {
            if (next >= lines.size()) return false;
            std::istringstream cmd(lines[next++]);
            std::string op;
            cmd >> op;
            if (op == "wait") {
                cmd >> waitFrames;
            } else if (op == "tap") {
                cmd >> fx >> fy;
                fingerDown = liftNext = true;
            } else if (op == "drag") {
                cmd >> x0 >> y0 >> x1 >> y1 >> dragFrames;
                dragFrames = std::max(dragFrames, 1);
                fx = x0;
                fy = y0;
                fingerDown = true;
                dragFrame = 0;
            } else if (op == "shot") {
                cmd >> shot;
            }
        }
        return true;
    }
    void feed(ImGuiIO& io) const {
        io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
        io.AddMousePosEvent(fx, fy);
        io.AddMouseButtonEvent(0, fingerDown);
    }
};

}  // namespace

int main(int argc, char* argv[]) {
    // Window size for trying the layout on the Mac, and touch emulation
    int winW = 390, winH = 844;
    bool touch = kPhoneOS;
    Script script;
    bool scripted = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--ipad")) winW = 820, winH = 1180;
        else if (!std::strcmp(argv[i], "--touch")) touch = true;
        else if (!std::strcmp(argv[i], "--size") && i + 1 < argc) std::sscanf(argv[++i], "%dx%d", &winW, &winH);
        else if (!std::strcmp(argv[i], "--script") && i + 1 < argc) {
            if (!script.load(argv[++i])) {
                std::cerr << "Can't read the script " << argv[i] << "\n";
                return 1;
            }
            scripted = touch = true;
        }
    }

    const std::string dataDir = dataFolder(argc, argv);
    apppaths::useUserDataDir();

    GuiApp app;
    std::string error;
    if (!app.load(dataDir, error)) {
        std::cerr << "Startup error: " << error << "\n";
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Cadet Prep", error.c_str(), nullptr);
        return 1;
    }
    app.setTouch(touch);

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
        return 1;
    }
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (kPhoneOS) flags |= SDL_WINDOW_FULLSCREEN | SDL_WINDOW_BORDERLESS;
    SDL_Window* window = SDL_CreateWindow("Cadet Prep", winW, winH, flags);
    if (!window) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n";
        return 1;
    }
    gRenderer = SDL_CreateRenderer(window, nullptr);
    if (!gRenderer) {
        std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << "\n";
        return 1;
    }
    SDL_SetRenderVSync(gRenderer, 1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    float scale = SDL_GetWindowDisplayScale(window);
    if (scale <= 0.0f) scale = 1.0f;
    ui::loadFonts(io, scale);
    ui::applyTheme();
    ImGui_ImplSDL3_InitForSDLRenderer(window, gRenderer);
    ImGui_ImplSDLRenderer3_Init(gRenderer);
    // Scripted input replaces the real mouse each frame: take the last event as is
    if (scripted) io.ConfigInputTrickleEventQueue = false;
    ui::setTextureMaker(makeSdlTexture);

    bool done = false, background = false;
    bool fingerDown = false;  // a finger is on the screen
    Uint64 lastInput = SDL_GetTicks();
    while (!done) {
        // Sleep while the app is in the background; when nothing is happening,
        // redraw a few times a second (enough for the timer) to save battery.
        SDL_Event e;
        bool touchEvents = false;
        const bool idle = !scripted && SDL_GetTicks() - lastInput > 1500;
        bool got = background ? SDL_WaitEvent(&e) : idle ? SDL_WaitEventTimeout(&e, 250) : SDL_PollEvent(&e);
        while (got) {
            ImGui_ImplSDL3_ProcessEvent(&e);
            // Mouse events SDL makes from touches
            if ((e.type == SDL_EVENT_MOUSE_MOTION && e.motion.which == SDL_TOUCH_MOUSEID) ||
                ((e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP) &&
                 e.button.which == SDL_TOUCH_MOUSEID)) {
                touchEvents = true;
                if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) fingerDown = true;
                if (e.type == SDL_EVENT_MOUSE_BUTTON_UP) fingerDown = false;
            }
            switch (e.type) {
                case SDL_EVENT_QUIT:
                case SDL_EVENT_TERMINATING: done = true; break;
                case SDL_EVENT_WILL_ENTER_BACKGROUND:
                case SDL_EVENT_DID_ENTER_BACKGROUND: background = true; break;
                case SDL_EVENT_WILL_ENTER_FOREGROUND:
                case SDL_EVENT_DID_ENTER_FOREGROUND: background = false; break;
                case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                    if (e.window.windowID == SDL_GetWindowID(window)) done = true;
                    break;
                default: break;
            }
            if (e.type != SDL_EVENT_WINDOW_EXPOSED) lastInput = SDL_GetTicks();
            got = SDL_PollEvent(&e);
        }
        // A finger has no hover: once it's off the screen (SDL may send one more
        // movement after it lifts), nothing stays highlighted. ImGui handles events
        // in order, so a tap in this batch still lands first.
        if (touch && touchEvents && !fingerDown) io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        if (done) break;
        if (background) continue;
        if (app.animating()) lastInput = SDL_GetTicks();

        // Keep content out from under the notch, rounded corners, and home bar
        SDL_Rect safe{};
        int w = 0, h = 0;
        SDL_GetWindowSize(window, &w, &h);
        if (SDL_GetWindowSafeArea(window, &safe))
            app.setSafeArea(static_cast<float>(safe.y), static_cast<float>(w - safe.x - safe.w),
                            static_cast<float>(h - safe.y - safe.h), static_cast<float>(safe.x));

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        if (scripted) {
            if (!script.step()) break;
            script.feed(io);
        }
        ImGui::NewFrame();
        app.draw();
        ImGui::Render();

        SDL_SetRenderScale(gRenderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
        const ImVec4 bg = app.backgroundColor();
        SDL_SetRenderDrawColorFloat(gRenderer, bg.x, bg.y, bg.z, 1.0f);
        SDL_RenderClear(gRenderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), gRenderer);
        if (!script.shot.empty()) {
            if (SDL_Surface* shot = SDL_RenderReadPixels(gRenderer, nullptr)) {
                SDL_SaveBMP(shot, script.shot.c_str());
                SDL_DestroySurface(shot);
            }
            script.shot.clear();
        }
        SDL_RenderPresent(gRenderer);
    }

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(gRenderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
