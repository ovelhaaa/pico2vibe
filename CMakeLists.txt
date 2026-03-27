cmake_minimum_required(VERSION 3.13)

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

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
option(USER_INTERFACE "Enable rotary encoder + LED user interface" ON)

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
