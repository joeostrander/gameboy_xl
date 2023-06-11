cmake_minimum_required(VERSION 3.12)

# Pull in PICO SDK (must be before project).
include(../pico-sdk/external/pico_sdk_import.cmake)

# We also need PICO EXTRAS.
include(../pico-extras/external/pico_extras_import.cmake)

project(pico_playground C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the Pico SDK.
pico_sdk_init()

if (TARGET pico_scanvideo_dpi)
    add_executable(gameboy_xl
            gameboy_xl.c
            osd.c
            colors.c
            )

    target_sources(gameboy_xl PRIVATE gameboy_xl.c)

    target_compile_definitions(gameboy_xl PRIVATE
        -DPICO_SCANVIDEO_MAX_SCANLINE_BUFFER_WORDS=500
        )

    # RGB222
    add_compile_definitions(PICO_SCANVIDEO_COLOR_PIN_COUNT=6)    # scanvideo_base.h
    add_compile_definitions(PICO_SCANVIDEO_DPI_PIXEL_RSHIFT=4)   # scanvideo.h
    add_compile_definitions(PICO_SCANVIDEO_DPI_PIXEL_GSHIFT=2)   # scanvideo.h
    add_compile_definitions(PICO_SCANVIDEO_DPI_PIXEL_BSHIFT=0)   # scanvideo.h
    add_compile_definitions(PICO_SCANVIDEO_DPI_PIXEL_RCOUNT=2)   # scanvideo.h
    add_compile_definitions(PICO_SCANVIDEO_DPI_PIXEL_GCOUNT=2)   # scanvideo.h
    add_compile_definitions(PICO_SCANVIDEO_DPI_PIXEL_BCOUNT=2)   # scanvideo.h

    add_compile_definitions(PICO_SCANVIDEO_ENABLE_CLOCK_PIN=1)
    add_compile_definitions(PICO_SCANVIDEO_ENABLE_DEN_PIN=1)
    add_compile_definitions(PICO_SCANVIDEO_CLOCK_PIN=9)
    add_compile_definitions(PICO_SCANVIDEO_SYNC_PIN_BASE=6)
    add_compile_definitions(PICO_SCANVIDEO_HSYNC_PIN_OFFSET=0 PICO_SCANVIDEO_VSYNC_PIN_OFFSET=1 PICO_SCANVIDEO_DEN_PIN_OFFSET=2)

    target_link_libraries(gameboy_xl PRIVATE
            pico_multicore
            pico_stdlib
            pico_scanvideo_dpi
            hardware_pwm
            )

    # pico_enable_stdio_usb(gameboy_xl 1)
    # pico_enable_stdio_uart(gameboy_xl 0)

    pico_add_extra_outputs(gameboy_xl)
endif ()
