
cmake_minimum_required(VERSION 3.15)
project(rts_sdl2 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
add_library(rts_core core/src/sim.cpp core/src/pathfinding.cpp core/src/data.cpp)
target_include_directories(rts_core PUBLIC core/include)
target_compile_definitions(rts_core PUBLIC RTS_FIXED_TICK=20)
find_package(SDL2 CONFIG REQUIRED)
find_package(SDL2_mixer CONFIG REQUIRED)
find_package(SDL2_ttf CONFIG REQUIRED)
add_executable(rts_sdl platform/sdl/main.cpp platform/sdl/renderer.cpp platform/sdl/input.cpp)
target_link_libraries(rts_sdl PRIVATE rts_core SDL2::SDL2main SDL2::SDL2 SDL2_mixer::SDL2_mixer SDL2_ttf::SDL2_ttf)
add_custom_command(TARGET rts_sdl POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:rts_sdl>/assets)
