cmake_minimum_required(VERSION 3.22.1)

project(functiondrawer C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(LVGL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lvgl)

file(GLOB_RECURSE LVGL_SOURCES ${LVGL_DIR}/src/*.c)

add_library(lvgl STATIC ${LVGL_SOURCES})
target_include_directories(lvgl PUBLIC
        ${LVGL_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(lvgl PUBLIC LV_CONF_INCLUDE_SIMPLE)
target_compile_options(lvgl PRIVATE -O2 -ffast-math -Wno-unused-parameter)

file(GLOB APP_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/*.c
        ${CMAKE_CURRENT_SOURCE_DIR}/ui/*.c)

add_library(functiondrawer SHARED ${APP_SOURCES})
target_include_directories(functiondrawer PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/engine
        ${CMAKE_CURRENT_SOURCE_DIR}/ui
        ${LVGL_DIR})
target_compile_options(functiondrawer PRIVATE -O2 -Wall -Wextra -Wno-unused-parameter)

find_library(ANDROID_LIB android)
find_library(LOG_LIB log)

target_link_libraries(functiondrawer
        lvgl
        ${ANDROID_LIB}
        ${LOG_LIB}
        m)
