cmake_minimum_required(VERSION 3.16)
project(CadetPrep VERSION 0.5 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# --- Libraries, downloaded automatically the first time you configure ---
FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

FetchContent_Declare(glfw                     # creates the window
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(imgui                    # draws buttons, text, tables
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.5)

FetchContent_Declare(miniaudio               # plays the sound effects
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG 0.11.21
    GIT_SHALLOW TRUE)

FetchContent_Declare(stb                     # loads the rank insignia pictures (stb_image.h)
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG 4e9b368bfc071e97206ea5c6adba68dd0cbf8897)

FetchContent_MakeAvailable(json glfw imgui miniaudio stb)
find_package(OpenGL REQUIRED)

# Dear ImGui has no CMake file of its own, so we list its source files here.
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC glfw OpenGL::GL)

# --- Shared quiz logic, used by both the console and GUI apps ---
add_library(afoqt_core STATIC
    src/QuestionBank.cpp
    src/SubtestRegistry.cpp
    src/TestSession.cpp
    src/ProgressStore.cpp
    src/QuizEngine.cpp
    src/FlashcardDeck.cpp
    src/RepetitionDeck.cpp
    src/QuestionGenerator.cpp
    src/Composites.cpp)
target_include_directories(afoqt_core PUBLIC src)
target_link_libraries(afoqt_core PUBLIC nlohmann_json::nlohmann_json)
target_compile_definitions(afoqt_core PUBLIC AFOQT_DATA_DIR="${CMAKE_SOURCE_DIR}/data")

# --- Console app (unchanged) ---
add_executable(afoqt src/main.cpp src/ConsoleUI.cpp)
target_link_libraries(afoqt PRIVATE afoqt_core)

# --- GUI app (on macOS, a double-clickable "Cadet Prep.app") ---
# The app icon (the F-22 logo), copied into the app's Resources folder on macOS
set(APP_ICON "${CMAKE_SOURCE_DIR}/assets/AppIcon.icns")
set_source_files_properties(${APP_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")

add_executable(afoqt_gui MACOSX_BUNDLE src/gui_main.cpp src/GuiApp.cpp src/Widgets.cpp src/Visuals.cpp src/Sound.cpp
               ${APP_ICON})
target_link_libraries(afoqt_gui PRIVATE afoqt_core imgui)
target_include_directories(afoqt_gui PRIVATE ${miniaudio_SOURCE_DIR})
target_include_directories(afoqt_gui SYSTEM PRIVATE ${stb_SOURCE_DIR})
if(APPLE)
    target_link_libraries(afoqt_gui PRIVATE "-framework CoreFoundation" "-framework CoreAudio" "-framework AudioToolbox")
elseif(UNIX)
    target_link_libraries(afoqt_gui PRIVATE dl pthread m)
endif()

if(APPLE)
    set_target_properties(afoqt_gui PROPERTIES
        OUTPUT_NAME "Cadet Prep"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME "Cadet Prep"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.renzo.cadetprep"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_ICON_FILE "AppIcon.icns")

    # Copy the question files into the app, so it keeps working after you move
    # it to Applications. Runs on every build, so edited questions are picked up.
    add_custom_target(afoqt_gui_data ALL
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/data"
                "$<TARGET_BUNDLE_CONTENT_DIR:afoqt_gui>/Resources/data"
        VERBATIM)
    add_dependencies(afoqt_gui_data afoqt_gui)
endif()

foreach(t afoqt_core afoqt afoqt_gui)
    if(MSVC)
        target_compile_options(${t} PRIVATE /W4)
    else()
        target_compile_options(${t} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()
