cmake_minimum_required(VERSION 3.16)
project(speaker_simulator C CXX)
find_package(SDL2 REQUIRED)
find_package(Freetype REQUIRED)
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(../managed_components/lvgl__lvgl lvgl)
target_link_libraries(lvgl PUBLIC SDL2::SDL2 Freetype::Freetype)
add_executable(speaker_simulator main.cpp
    ../main/boards/lckfb/szpi-esp32s3/livis_indoor_font.c
    ../main/boards/lckfb/szpi-esp32s3/livis_vehicle_font.c
    ../managed_components/78__xiaozhi-fonts/src/font_noto_sans_basic_20_4.c
    ../managed_components/78__xiaozhi-fonts/src/font_material_symbols_20_4.c)
target_include_directories(speaker_simulator PRIVATE stubs ../main/boards/lckfb/szpi-esp32s3 ../managed_components/78__xiaozhi-fonts/include)
target_compile_features(speaker_simulator PRIVATE cxx_std_17)
target_link_libraries(speaker_simulator PRIVATE lvgl::lvgl SDL2::SDL2)
