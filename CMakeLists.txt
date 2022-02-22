cmake_minimum_required(VERSION 3.10)
project(lvshell)
set(CMAKE_C_STANDARD 11)#C11
set(CMAKE_CXX_STANDARD 17)#C17
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FindPkgConfig)
pkg_check_modules(sdl2 REQUIRED IMPORTED_TARGET sdl2)

INCLUDE_DIRECTORIES(${PROJECT_SOURCE_DIR})

file(GLOB_RECURSE INCLUDES "./*.h" )

add_subdirectory(lvgl)
add_subdirectory(lv_drivers)

SET(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
SET(CMAKE_CXX_FLAGS "-O3")

add_executable(lvshell main.c mouse_cursor_icon.c ${SOURCES} ${INCLUDES})

add_compile_definitions(LV_CONF_INCLUDE_SIMPLE)
target_link_libraries(lvshell PRIVATE PkgConfig::sdl2 lvgl lvgl::examples lvgl::demos lvgl::drivers)
