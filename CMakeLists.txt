cmake_minimum_required(VERSION 3.12.4)
project(lvshell C)
set(CMAKE_C_STANDARD 11)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)

# So main.c can include "lvgl/..." and lvgl's SDL driver can find <SDL2/SDL.h>.
include_directories(${PROJECT_SOURCE_DIR} ${SDL2_INCLUDE_DIRS})

# lvshell doesn't use the demos/examples/thorvg; keep the build lean.
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory(lvgl)

add_executable(lvshell main.c util.c)
target_link_libraries(lvshell PRIVATE lvgl PkgConfig::SDL2)
install(TARGETS lvshell RUNTIME DESTINATION bin)
