cmake_minimum_required(VERSION 3.16)

option(PICO2VIBE_BUILD_FIRMWARE "Build RP2350 firmware" ON)
option(PICO2VIBE_BUILD_JUCE_PLUGIN "Build the JUCE VST3/plugin wrapper" OFF)
set(PICO2VIBE_JUCE_DIR "" CACHE PATH "Optional path to a local JUCE checkout")

if(PICO2VIBE_BUILD_FIRMWARE)
    # Defaults voltados ao RP2350 Zero (compatível com perfil pico2)
    if(NOT DEFINED PICO_PLATFORM)
        set(PICO_PLATFORM rp2350 CACHE STRING "Pico platform")
    endif()

    if(NOT DEFINED PICO_BOARD)
        set(PICO_BOARD pico2 CACHE STRING "Pico board")
    endif()

    if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/pico_sdk_import.cmake")
        include(${CMAKE_CURRENT_LIST_DIR}/pico_sdk_import.cmake)
    elseif(DEFINED ENV{PICO_SDK_PATH} AND EXISTS "$ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake")
        include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
    else()
        message(FATAL_ERROR "Pico SDK import não encontrado. Defina PICO_SDK_PATH ou adicione pico_sdk_import.cmake no projeto.")
    endif()

    project(univibe_rp2350_dma C CXX ASM)
else()
    project(pico2vibe VERSION 0.1.0 LANGUAGES CXX)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(USER_INTERFACE "Enable rotary encoder + LED user interface" ON)

if(PICO2VIBE_BUILD_FIRMWARE)
    pico_sdk_init()

    add_executable(univibe_rp2350_dma
        univibe_rp2350_dma.cpp
    )

    pico_generate_pio_header(
        univibe_rp2350_dma
        ${CMAKE_CURRENT_LIST_DIR}/i2s_bidir_dma.pio
    )

    target_link_libraries(univibe_rp2350_dma
        pico_stdlib
        hardware_clocks
        hardware_gpio
        hardware_pio
        hardware_dma
        hardware_irq
        hardware_sync
    )

    target_compile_options(univibe_rp2350_dma PRIVATE
        -Wall
        -Wextra
        -Wdouble-promotion
    )

    target_compile_definitions(univibe_rp2350_dma PRIVATE
        USER_INTERFACE=$<BOOL:${USER_INTERFACE}>
    )

    pico_enable_stdio_usb(univibe_rp2350_dma 1)
    pico_enable_stdio_uart(univibe_rp2350_dma 0)

    pico_add_extra_outputs(univibe_rp2350_dma)
endif()

if(PICO2VIBE_BUILD_JUCE_PLUGIN)
    enable_language(CXX)

    if(PICO2VIBE_JUCE_DIR)
        add_subdirectory(${PICO2VIBE_JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE)
    else()
        find_package(JUCE CONFIG REQUIRED)
    endif()

    add_subdirectory(plugin/juce)
endif()
