#define GL_SILENCE_DEPRECATION  // macOS marks OpenGL deprecated; it still works
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
        0x0020, 0x00FF,  // Latin, plus degree, squared, times, divide
        0x0370, 0x03FF,  // Greek (pi)
        0x2070, 0x209F,  // superscripts
        0x2200, 0x22FF,  // math symbols (square root)
        0,
    };
    const char* path = "/System/Library/Fonts/Supplemental/Arial Unicode.ttf";
    // Render the font at screen resolution so it stays sharp on Retina displays.
    if (std::ifstream(path).good() &&
        io.Fonts->AddFontFromFileTTF(path, 20.0f * scale, nullptr, ranges)) {
        io.FontGlobalScale = 1.0f / scale;
        return;
    }
    io.FontGlobalScale = 1.4f;  // fallback: built-in font, basic characters only
}

int main(int argc, char* argv[]) {
    // Load questions first so a bad data file is reported clearly.
    GuiApp app;
    std::string error;
    std::string dataDir = argc > 1 ? argv[1] : AFOQT_DATA_DIR;
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
    ImGui::StyleColorsDark();
    ImGui::GetStyle().FramePadding = ImVec2(10, 8);
    ImGui::GetStyle().ItemSpacing = ImVec2(10, 8);
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
        glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
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
