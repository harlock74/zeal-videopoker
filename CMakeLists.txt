cmake_minimum_required(VERSION 3.16)

set(target_name "zpoker")

set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/bin)

# Ensure the bin directory exists
file(MAKE_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

set(ZOS_TOOLCHAIN sdcc)
include($ENV{ZOS_PATH}/cmake/zos_init.cmake)

zos_use(zvb)
find_package(ZVB_SDK REQUIRED)

zos_use(coreutils)
find_package(COREUTILS REQUIRED)

zos_use(zgdk)
find_package(ZGDK REQUIRED)

project(${target_name} C ASM)


add_executable(${target_name}
    src/main.c
    src/audio.c
    src/splash.c
    src/gameplay.c
    src/render.c
    src/assets.c
)
# prevent linking against stdlib
target_compile_options(${target_name} PRIVATE
    $<$<COMPILE_LANGUAGE:C>:--nostdlib>
)

target_link_libraries(${target_name} PUBLIC core)
target_link_libraries(${target_name} PUBLIC zvb_gfx zvb_sound)
target_link_libraries(${target_name} PUBLIC zgdk)


gif2zeal(${target_name} FILES
    "assets/cards.gif"
    BIT 4
    COLORS 16
    # COMPRESSED
)

tiled2zeal(${target_name} FILES
    "assets/cards.tmx"
    LAYER 0
)

add_asset(${target_name} FILES
    "assets/splash.zmt"
    "assets/music.zmt"
)

copy_assets(${target_name} FILES
    "assets/cards.zts"
    "assets/cards.ztp"
)

zgdk_add_outputs(${target_name})
