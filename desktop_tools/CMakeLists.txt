cmake_minimum_required(VERSION 3.16)

project(univibe_desktop_tools CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(univibe_cli
    src/main.cpp
    src/processor.cpp
    src/wav_io.cpp
)

target_include_directories(univibe_cli PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/src"
    "${CMAKE_CURRENT_LIST_DIR}/../src"
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
)

target_include_directories(dsp_validate PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/src"
    "${CMAKE_CURRENT_LIST_DIR}/../src"
)

if(MSVC)
    target_compile_options(dsp_validate PRIVATE /W4)
else()
    target_compile_options(dsp_validate PRIVATE -Wall -Wextra -Wpedantic)
endif()
