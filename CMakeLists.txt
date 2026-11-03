cmake_minimum_required(VERSION 3.16)
project(AFOQTStudy VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- nlohmann/json: downloaded automatically the first time you configure ---
include(FetchContent)
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(json)

add_executable(afoqt
    src/main.cpp
    src/QuestionBank.cpp
    src/SubtestRegistry.cpp
    src/TestSession.cpp
    src/ProgressStore.cpp
    src/ConsoleUI.cpp
)

target_include_directories(afoqt PRIVATE src)
target_link_libraries(afoqt PRIVATE nlohmann_json::nlohmann_json)

# Default data folder = the project's data/ directory, so editing a question
# file takes effect on the next run without rebuilding.
target_compile_definitions(afoqt PRIVATE AFOQT_DATA_DIR="${CMAKE_SOURCE_DIR}/data")

if(MSVC)
    target_compile_options(afoqt PRIVATE /W4)
else()
    target_compile_options(afoqt PRIVATE -Wall -Wextra -Wpedantic)
endif()
