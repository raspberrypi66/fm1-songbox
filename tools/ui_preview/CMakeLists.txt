# Host (PC) build of the real UI code for preview renders.
# Uses the LVGL sources fetched by the ESP-IDF build (managed_components/lvgl__lvgl),
# so run one `idf.py build` first. Driven by render.py.
cmake_minimum_required(VERSION 3.16)
project(ui_preview C)

set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(LVGL_DIR ${ROOT}/managed_components/lvgl__lvgl)

file(GLOB_RECURSE LVGL_SRCS ${LVGL_DIR}/src/*.c)
add_library(lvgl STATIC ${LVGL_SRCS})
target_include_directories(lvgl PUBLIC ${LVGL_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(lvgl PUBLIC LV_CONF_INCLUDE_SIMPLE LV_LVGL_H_INCLUDE_SIMPLE)
target_compile_options(lvgl PRIVATE -O2 -w)

file(GLOB ASSET_SRCS ${ROOT}/main/ui/assets/*.c)
add_executable(ui_preview preview.c ${ROOT}/main/ui/ui.c ${ASSET_SRCS})
target_include_directories(ui_preview PRIVATE ${ROOT}/main/ui)
target_compile_options(ui_preview PRIVATE -O1 -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(ui_preview lvgl)
