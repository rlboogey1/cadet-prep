#define GL_SILENCE_DEPRECATION  // macOS marks OpenGL deprecated; it still works
#include "AppPaths.h"
#include "GuiApp.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include <fstream>
#include <iostream>

#ifndef AFOQT_DATA_DIR
#define AFOQT_DATA_DIR "data"
#endif


// The built-in ImGui font only covers basic letters, so math symbols like
// pi and square roots would show as "?". Load a Mac system font that has them.
static void loadFont(ImGuiIO& io, float scale) {
    static const ImWchar ranges[] = {
        0x0020, 0x00FF, 0x0370, 0x03FF, 0x2070, 0x209F, 0x2200, 0x22FF, 0,
    };
    const char* body = "/System/Library/Fonts/Supplemental/Arial Unicode.ttf";
    const char* bold = "/System/Library/Fonts/Supplemental/Arial Bold.ttf";
    if (!std::ifstream(body).good()) {
        io.FontGlobalScale = 1.4f;
        return;
    }
    io.Fonts->AddFontFromFileTTF(body, 20.0f * scale, nullptr, ranges);  // Fonts[0]: body
    const char* title = std::ifstream(bold).good() ? bold : body;
    io.Fonts->AddFontFromFileTTF(title, 30.0f * scale, nullptr, ranges); // Fonts[1]: headings
    io.FontGlobalScale = 1.0f / scale;
}

static void applyTheme() {
    ImGuiStyle& s = ImGui::GetStyle();
    ImGui::StyleColorsDark(&s);

    // Roomier spacing and rounded corners
    s.WindowPadding = ImVec2(28, 24);
    s.FramePadding = ImVec2(14, 10);
    s.ItemSpacing = ImVec2(12, 10);
    s.FrameRounding = 8.0f;
    s.ChildRounding = 10.0f;
    s.GrabRounding = 8.0f;
    s.ScrollbarRounding = 8.0f;
    s.ButtonTextAlign = ImVec2(0.0f, 0.5f);  // left-align text, like answer cards

    // Navy and Air Force blue
    ImVec4* c = s.Colors;
    const ImVec4 bg(0.07f, 0.09f, 0.13f, 1.00f);
    const ImVec4 panel(0.11f, 0.14f, 0.20f, 1.00f);
    const ImVec4 button(0.16f, 0.21f, 0.30f, 1.00f);
    const ImVec4 hover(0.22f, 0.31f, 0.46f, 1.00f);
    const ImVec4 accent(0.29f, 0.56f, 0.89f, 1.00f);
    c[ImGuiCol_WindowBg] = bg;
    c[ImGuiCol_ChildBg] = panel;
    c[ImGuiCol_Border] = ImVec4(0.22f, 0.27f, 0.36f, 1.00f);
    c[ImGuiCol_Separator] = c[ImGuiCol_Border];
    c[ImGuiCol_Button] = button;
    c[ImGuiCol_ButtonHovered] = hover;
    c[ImGuiCol_ButtonActive] = accent;
    c[ImGuiCol_FrameBg] = button;
    c[ImGuiCol_FrameBgHovered] = hover;
    c[ImGuiCol_CheckMark] = accent;
    c[ImGuiCol_PlotHistogram] = accent;  // progress bars
    c[ImGuiCol_Text] = ImVec4(0.92f, 0.94f, 0.97f, 1.00f);
    c[ImGuiCol_TextDisabled] = ImVec4(0.55f, 0.60f, 0.68f, 1.00f);
    c[ImGuiCol_TableHeaderBg] = panel;
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.03f);
}

int main(int argc, char* argv[]) {
    // Find the questions, then switch to the folder where progress is saved.
    const std::string dataDir = apppaths::findDataDir(argc, argv);
    apppaths::useUserDataDir();

    // Load questions first so a bad data file is reported clearly.
    GuiApp app;
    std::string error;
    if (!app.load(dataDir, error)) {
        std::cerr << "Startup error: " << error << "\n";
        return 1;
    }

    // Create the window. macOS requires OpenGL 3.2 core profile.
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    GLFWwindow* window = glfwCreateWindow(1100, 760, "AFOQT Study", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);  // vsync

    // Set up Dear ImGui.
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // don't write imgui.ini
    float xscale = 1.0f, yscale = 1.0f;
    glfwGetWindowContentScale(window, &xscale, &yscale);
    loadFont(io, xscale);
    applyTheme();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 150");

    // Main loop: draw a frame, show it, repeat.
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        app.draw();

        ImGui::Render();
        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.07f, 0.09f, 0.13f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
