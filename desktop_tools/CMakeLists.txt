cmake_minimum_required(VERSION 3.16)

project(univibe_desktop_tools CXX)

enable_testing()

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

function(add_silence_test target)
    add_executable(${target}
        src/silence_test.cpp
        src/processor.cpp
        src/wav_io.cpp
    )
    target_include_directories(${target} PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src"
        "${CMAKE_CURRENT_LIST_DIR}/../src"
    )
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

add_silence_test(silence_invariant_active)
add_silence_test(silence_invariant_feedback_off)
target_compile_definitions(silence_invariant_feedback_off PRIVATE VIBE_DIAG_DISABLE_FEEDBACK=1)
add_silence_test(silence_invariant_modulation_frozen)
target_compile_definitions(silence_invariant_modulation_frozen PRIVATE VIBE_DIAG_FREEZE_MODULATION=1)

add_test(NAME silence_invariant_active COMMAND silence_invariant_active)
add_test(NAME silence_invariant_feedback_off COMMAND silence_invariant_feedback_off)
add_test(NAME silence_invariant_modulation_frozen COMMAND silence_invariant_modulation_frozen)
