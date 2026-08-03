cmake_minimum_required(VERSION 3.16)

project(univibe_desktop_tools CXX)

enable_testing()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

function(configure_desktop_tool_target target)
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

add_executable(univibe_cli
    src/main.cpp
    src/processor.cpp
    src/wav_io.cpp
)
configure_desktop_tool_target(univibe_cli)

add_executable(dsp_validate
    src/analysis_main.cpp
    src/processor.cpp
    src/wav_io.cpp
)

configure_desktop_tool_target(dsp_validate)
add_executable(vst_param_manifest
    src/vst_param_manifest.cpp
)
configure_desktop_tool_target(vst_param_manifest)

function(add_silence_test target)
    add_executable(${target}
        src/silence_test.cpp
        src/processor.cpp
        src/wav_io.cpp
    )
    configure_desktop_tool_target(${target})
endfunction()

add_silence_test(silence_invariant_active)
add_silence_test(silence_invariant_feedback_off)
target_compile_definitions(silence_invariant_feedback_off PRIVATE VIBE_DIAG_DISABLE_FEEDBACK=1)
add_silence_test(silence_invariant_modulation_frozen)
target_compile_definitions(silence_invariant_modulation_frozen PRIVATE VIBE_DIAG_FREEZE_MODULATION=1)

add_test(NAME silence_invariant_active COMMAND silence_invariant_active)
add_test(NAME silence_invariant_feedback_off COMMAND silence_invariant_feedback_off)
add_test(NAME silence_invariant_modulation_frozen COMMAND silence_invariant_modulation_frozen)

add_executable(phase_notch_test
    src/phase_notch_test.cpp
    src/processor.cpp
    src/wav_io.cpp
)
configure_desktop_tool_target(phase_notch_test)
target_compile_definitions(phase_notch_test PRIVATE VIBE_DIAG_FREEZE_MODULATION=1 VIBE_DIAG_DISABLE_FEEDBACK=1)
add_test(NAME phase_notch_test COMMAND phase_notch_test)

add_executable(block_size_invariance_test
    src/block_size_invariance_test.cpp
)
configure_desktop_tool_target(block_size_invariance_test)
add_test(NAME block_size_invariance_test COMMAND block_size_invariance_test)
