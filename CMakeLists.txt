cmake_minimum_required(VERSION 3.10)
project(lvshell)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(sdl2 REQUIRED IMPORTED_TARGET sdl2)

INCLUDE_DIRECTORIES(${PROJECT_SOURCE_DIR})

file(GLOB_RECURSE INCLUDES "./*.h" )
add_subdirectory(lvgl)
add_subdirectory(lv_drivers)

add_executable(lvshell main.c util.c ${SOURCES} ${INCLUDES})
add_compile_definitions(LV_CONF_INCLUDE_SIMPLE)
target_link_libraries(lvshell PRIVATE PkgConfig::sdl2 lvgl lvgl::examples lvgl::demos lvgl::drivers)
install(TARGETS lvshell)
