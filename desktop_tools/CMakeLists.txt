cmake_minimum_required(VERSION 3.16)

project(univibe_desktop_tools CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(PICO_SOURCE "${CMAKE_CURRENT_LIST_DIR}/../univibe_rp2350_dma.cpp")
set(GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(GENERATED_DSP_HEADER "${GENERATED_DIR}/vibe_core.generated.hpp")

add_custom_command(
    OUTPUT "${GENERATED_DSP_HEADER}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GENERATED_DIR}"
    COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_CURRENT_LIST_DIR}/scripts/extract_dsp.py"
            --input "${PICO_SOURCE}"
            --output "${GENERATED_DSP_HEADER}"
    DEPENDS "${PICO_SOURCE}" "${CMAKE_CURRENT_LIST_DIR}/scripts/extract_dsp.py"
    VERBATIM
)

add_custom_target(generate_vibe_dsp ALL DEPENDS "${GENERATED_DSP_HEADER}")

add_executable(univibe_cli
    src/main.cpp
    src/processor.cpp
    src/wav_io.cpp
    "${GENERATED_DSP_HEADER}"
)

add_dependencies(univibe_cli generate_vibe_dsp)

target_include_directories(univibe_cli PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/src"
    "${GENERATED_DIR}"
)

if(MSVC)
    target_compile_options(univibe_cli PRIVATE /W4)
else()
    target_compile_options(univibe_cli PRIVATE -Wall -Wextra -Wpedantic)
endif()


add_executable(dsp_validate
    src/analysis_main.cpp
    src/processor.cpp
    src/wav_io.cpp
    "${GENERATED_DSP_HEADER}"
)

add_dependencies(dsp_validate generate_vibe_dsp)

target_include_directories(dsp_validate PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/src"
    "${GENERATED_DIR}"
)

if(MSVC)
    target_compile_options(dsp_validate PRIVATE /W4)
else()
    target_compile_options(dsp_validate PRIVATE -Wall -Wextra -Wpedantic)
endif()
