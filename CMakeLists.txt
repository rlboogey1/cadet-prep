cmake_minimum_required(VERSION 3.16)
project(CadetPrep VERSION 0.5 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# iPhone/iPad: configure with  cmake -S . -B build-ios -G Xcode -DCMAKE_SYSTEM_NAME=iOS
# (see IOS.md). That builds only the touch version. On the Mac, -DCADET_MOBILE=ON also
# builds it as "cadet_mobile", a phone-sized window for trying the layout.
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(CADET_IOS ON)
endif()
option(CADET_MOBILE "Also build the SDL3 touch version (cadet_mobile)" OFF)
if(CADET_IOS)
    set(CADET_MOBILE ON)
endif()
if(CADET_MOBILE AND APPLE)
    enable_language(OBJC)  # SDL3's Apple code is Objective-C
endif()

# --- Libraries, downloaded automatically the first time you configure ---
FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

if(NOT CADET_IOS)
FetchContent_Declare(glfw                     # creates the window (Mac version)
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
endif()

if(CADET_MOBILE)
FetchContent_Declare(SDL3                     # window, touch input, and drawing on iPhone/iPad
    URL https://github.com/libsdl-org/SDL/releases/download/release-3.2.10/SDL3-3.2.10.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
endif()

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

FetchContent_MakeAvailable(json imgui miniaudio stb)
if(NOT CADET_IOS)
    FetchContent_MakeAvailable(glfw)
    find_package(OpenGL REQUIRED)
endif()
if(CADET_MOBILE)
    FetchContent_MakeAvailable(SDL3)
endif()

# Dear ImGui has no CMake file of its own, so we list its source files here.
# imgui_base is the library itself; each app adds the backends for its window.
add_library(imgui_base STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp)
target_include_directories(imgui_base PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)

if(NOT CADET_IOS)
    add_library(imgui STATIC                  # Mac version: GLFW + OpenGL
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
    target_link_libraries(imgui PUBLIC imgui_base glfw OpenGL::GL)
endif()
if(CADET_MOBILE)
    add_library(imgui_sdl STATIC              # touch version: SDL3 + SDL's renderer
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp)
    target_link_libraries(imgui_sdl PUBLIC imgui_base SDL3::SDL3-static)
endif()

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

if(NOT CADET_IOS)
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
endif()

# --- Touch version: iPhone and iPad (cadet_mobile) ---
if(CADET_MOBILE)
    set(MOBILE_SOURCES src/mobile_main.cpp src/GuiApp.cpp src/Widgets.cpp src/Visuals.cpp src/Sound.cpp)
    if(CADET_IOS)
        add_executable(cadet_mobile MACOSX_BUNDLE ${MOBILE_SOURCES} ios/Assets.xcassets)
        set_source_files_properties(ios/Assets.xcassets PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
        set(CADET_TEAM_ID "" CACHE STRING "Apple team ID for signing (or pick your team in Xcode)")
        set_target_properties(cadet_mobile PROPERTIES
            OUTPUT_NAME "CadetPrep"
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/ios/Info.plist.in"
            MACOSX_BUNDLE_BUNDLE_NAME "Cadet Prep"
            MACOSX_BUNDLE_GUI_IDENTIFIER "com.renzo.cadetprep"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
            MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
            XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.renzo.cadetprep"
            XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"          # iPhone and iPad
            XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "15.0"
            XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
            XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${CADET_TEAM_ID}"
            XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon"
            XCODE_GENERATE_SCHEME ON)
        target_link_libraries(cadet_mobile PRIVATE "-framework CoreText" "-framework CoreFoundation"
                              "-framework AudioToolbox" "-framework CoreAudio")
        # miniaudio's iOS audio-session setup is Objective-C; skip it (the default session works).
        set_source_files_properties(src/Sound.cpp PROPERTIES COMPILE_DEFINITIONS MA_NO_AVFOUNDATION)
        # Question files and pictures go inside the app, at CadetPrep.app/data
        add_custom_command(TARGET cadet_mobile POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/data"
                    "$<TARGET_BUNDLE_CONTENT_DIR:cadet_mobile>/data"
            VERBATIM)
    else()
        add_executable(cadet_mobile ${MOBILE_SOURCES})  # Mac preview, reads the project's data/
        if(APPLE)
            target_link_libraries(cadet_mobile PRIVATE "-framework CoreFoundation" "-framework CoreAudio"
                                  "-framework AudioToolbox")
        endif()
    endif()
    target_link_libraries(cadet_mobile PRIVATE afoqt_core imgui_sdl)
    target_include_directories(cadet_mobile PRIVATE ${miniaudio_SOURCE_DIR})
    target_include_directories(cadet_mobile SYSTEM PRIVATE ${stb_SOURCE_DIR})
endif()

set(WARN_TARGETS afoqt_core)
if(NOT CADET_IOS)
    list(APPEND WARN_TARGETS afoqt afoqt_gui)
endif()
if(CADET_MOBILE)
    list(APPEND WARN_TARGETS cadet_mobile)
endif()
foreach(t ${WARN_TARGETS})
    if(MSVC)
        target_compile_options(${t} PRIVATE /W4)
    else()
        target_compile_options(${t} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()
