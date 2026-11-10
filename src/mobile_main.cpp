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
//   type some text          type into the text box that has focus
//   key enter               press Return
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
#include <functional>
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

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
constexpr bool kWeb = true;  // the website: the browser calls us once per frame
#else
constexpr bool kWeb = false;
#endif

namespace {

#if defined(__EMSCRIPTEN__)
std::function<bool()> gWebFrame;
// True on a phone or tablet browser (the main pointer is a finger)
EM_JS(int, coarsePointer, (), { return window.matchMedia('(pointer: coarse)').matches ? 1 : 0; });
EM_JS(int, pageWidth, (), { return window.innerWidth; });
EM_JS(int, pageHeight, (), { return window.innerHeight; });
#endif

SDL_Renderer* gRenderer = nullptr;
SDL_Window* gWindow = nullptr;

// Shows the on-screen keyboard while a text box has focus. Replaces the ImGui
// backend's version so the keyboard fits the field (email, password) and iOS
// doesn't capitalize or autocorrect what's typed.
void setImeData(ImGuiContext*, ImGuiViewport* viewport, ImGuiPlatformImeData* data) {
    static SDL_Window* typing = nullptr;  // the window with the keyboard up
    static ui::TextKind typingKind = ui::TextKind::None;
    SDL_Window* window = SDL_GetWindowFromID(static_cast<SDL_WindowID>(reinterpret_cast<intptr_t>(viewport->PlatformHandle)));
    const ui::TextKind kind = ui::activeTextKind();
    if (typing && (!data->WantVisible || typing != window || kind != typingKind)) {
        SDL_StopTextInput(typing);
        typing = nullptr;
    }
    if (!data->WantVisible || !window) return;
    SDL_Rect r{static_cast<int>(data->InputPos.x), static_cast<int>(data->InputPos.y), 1,
               static_cast<int>(data->InputLineHeight)};
    SDL_SetTextInputArea(window, &r, 0);
    if (typing) return;
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetNumberProperty(props, SDL_PROP_TEXTINPUT_TYPE_NUMBER,
                          kind == ui::TextKind::Email      ? SDL_TEXTINPUT_TYPE_TEXT_EMAIL
                          : kind == ui::TextKind::Password ? SDL_TEXTINPUT_TYPE_TEXT_PASSWORD_HIDDEN
                                                           : SDL_TEXTINPUT_TYPE_TEXT);
    if (kind == ui::TextKind::Email || kind == ui::TextKind::Password) {
        SDL_SetNumberProperty(props, SDL_PROP_TEXTINPUT_CAPITALIZATION_NUMBER, SDL_CAPITALIZE_NONE);
        SDL_SetBooleanProperty(props, SDL_PROP_TEXTINPUT_AUTOCORRECT_BOOLEAN, false);
    }
    SDL_StartTextInputWithProperties(window, props);
    SDL_DestroyProperties(props);
    typing = window;
    typingKind = kind;
}

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
    std::string typed;  // text to type this frame
    bool enter = false, enterUp = false;  // press Return this frame; let it go the next

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
            } else if (op == "type") {
                std::getline(cmd >> std::ws, typed);
            } else if (op == "key") {
                enter = true;  // the only key so far: Return
            }
        }
        return true;
    }
    void feed(ImGuiIO& io) {
        io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
        io.AddMousePosEvent(fx, fy);
        io.AddMouseButtonEvent(0, fingerDown);
        if (!typed.empty()) io.AddInputCharactersUTF8(typed.c_str());
        typed.clear();
        if (enterUp) io.AddKeyEvent(ImGuiKey_Enter, false);
        if (enter) io.AddKeyEvent(ImGuiKey_Enter, true);
        enterUp = enter;
        enter = false;
    }
};

}  // namespace

#if defined(__EMSCRIPTEN__)
// Called by the page (study_shell.html) whenever the canvas changes size: the
// browser window resized, a phone rotated, or the zoom changed.
extern "C" EMSCRIPTEN_KEEPALIVE void cadet_resize(int w, int h) {
    if (gWindow && w > 0 && h > 0) SDL_SetWindowSize(gWindow, w, h);
}
#endif

int main(int argc, char* argv[]) {
    // Window size for trying the layout on the Mac, and touch emulation
    // Everything the frame function uses is static: in the browser main() returns
    // once the loop is set up, and the frames keep running after it.
    int winW = 390, winH = 844;
    static bool touch = kPhoneOS;
#if defined(__EMSCRIPTEN__)
    // A phone or tablet browser: finger scrolling and "tap" wording
    touch = coarsePointer() != 0;
    // The canvas fills the tab (study_shell.html); SDL follows its size from there on
    winW = pageWidth();
    winH = pageHeight();
#endif
    static Script script;
    static bool scripted = false;
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

    static GuiApp app;
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
    static SDL_Window* window = nullptr;
    gWindow = window = SDL_CreateWindow("Cadet Prep", winW, winH, flags);
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
    static ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    float scale = SDL_GetWindowDisplayScale(window);
    if (scale <= 0.0f) scale = 1.0f;
    ui::loadFonts(io, scale, dataDir + "/fonts");
    ui::applyTheme();
    ImGui_ImplSDL3_InitForSDLRenderer(window, gRenderer);
    ImGui_ImplSDLRenderer3_Init(gRenderer);
    ImGui::GetPlatformIO().Platform_SetImeDataFn = setImeData;
    // Scripted input replaces the real mouse each frame: take the last event as is
    if (scripted) io.ConfigInputTrickleEventQueue = false;
    ui::setTextureMaker(makeSdlTexture);

    static bool done = false, background = false;
    static bool fingerDown = false;  // a finger is on the screen
    static Uint64 lastInput = 0;
    lastInput = SDL_GetTicks();
    // One frame: read input, draw. Returns false when it's time to quit.
    auto frame = []() -> bool {
        // Sleep while the app is in the background; when nothing is happening,
        // redraw a few times a second (enough for the timer) to save battery.
        SDL_Event e;
        bool touchEvents = false;
        const bool idle = !scripted && SDL_GetTicks() - lastInput > 1500;
        // (A web page can't wait inside a frame: the browser paces it.)
        bool got = kWeb         ? SDL_PollEvent(&e)
                   : background ? SDL_WaitEvent(&e)
                   : idle       ? SDL_WaitEventTimeout(&e, 250)
                                : SDL_PollEvent(&e);
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
                case SDL_EVENT_DID_ENTER_FOREGROUND:
                    background = false;
                    app.onForeground();  // pull progress saved on the other devices
                    break;
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
        if (done) return false;
        if (background) return true;
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
            if (!script.step()) return false;
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
        return true;
    };
#if defined(__EMSCRIPTEN__)
    // The browser runs the frames from here on
    gWebFrame = frame;
    emscripten_set_main_loop([] { if (!gWebFrame()) emscripten_cancel_main_loop(); }, 0, false);
    return 0;
#else
    while (frame()) {}
#endif

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(gRenderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
