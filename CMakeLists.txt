cmake_minimum_required(VERSION 3.16)

set(ZOS_TOOLCHAIN sdcc)
include($ENV{ZOS_PATH}/cmake/zos_init.cmake)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

project(poker C ASM)

add_executable(poker
    src/videopoker.c
    src/assets.c
)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_custom_target(generate_layout_map
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_layout_map.py
        --tmx ${CMAKE_CURRENT_SOURCE_DIR}/assets/cards.tmx
        --out ${CMAKE_CURRENT_SOURCE_DIR}/src/layout_map.h
    DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/cards.tmx
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_layout_map.py
    COMMENT "Generating src/layout_map.h from assets/cards.tmx"
    VERBATIM
)

add_dependencies(poker generate_layout_map)

zos_use(zvb)
find_package(ZVB_SDK REQUIRED)
target_link_libraries(poker PUBLIC zvb_gfx zvb_sound)

zos_use(zgdk)
find_package(ZGDK REQUIRED)
target_link_libraries(poker PUBLIC zgdk)

gif2zeal(poker FILES
    "assets/cards.gif")

zos_add_outputs(poker)
