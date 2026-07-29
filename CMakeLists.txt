cmake_minimum_required(VERSION 3.12.4)
project(lvshell C)
set(CMAKE_C_STANDARD 11)

# Debug logging: writes discovery/decoder diagnostics to /tmp/lvshell.dbg and
# routes LVGL's own log there too. Applied before add_subdirectory(lvgl) so it
# also reaches lv_conf.h. Off by default; enable with -DLVSHELL_DEBUG=ON.
option(LVSHELL_DEBUG "Enable lvshell debug logging to /tmp/lvshell.dbg" OFF)
if(LVSHELL_DEBUG)
    add_compile_definitions(LVSHELL_DEBUG)
endif()

find_package(PkgConfig REQUIRED)
# Not IMPORTED_TARGET: SDL2's Cflags pull in directfb's (via the DirectFB video
# driver) whose .pc can carry a non-relocatable include path that trips CMake's
# imported-target validation. Use the plain variables instead.
pkg_check_modules(SDL2 REQUIRED sdl2)

# So main.c can include "lvgl/..." and lvgl's SDL driver can find <SDL2/SDL.h>.
include_directories(${PROJECT_SOURCE_DIR} ${SDL2_INCLUDE_DIRS})

# lvshell doesn't use the demos/examples/thorvg; keep the build lean.
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory(lvgl)

add_executable(lvshell main.c apps.c util.c)
target_link_libraries(lvshell PRIVATE lvgl ${SDL2_LIBRARIES})
install(TARGETS lvshell RUNTIME DESTINATION bin)

# Small standalone helper (no lvgl/SDL) that creates the data partition.
add_executable(datapart datapart.c)
install(TARGETS datapart RUNTIME DESTINATION bin)
