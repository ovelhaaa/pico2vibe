juce_add_plugin(Pico2VibePlugin
  COMPANY_NAME "pico2vibe"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE
  PLUGIN_MANUFACTURER_CODE P2vb
  PLUGIN_CODE P2vc
  FORMATS VST3
  PRODUCT_NAME "pico2vibe")

juce_generate_juce_header(Pico2VibePlugin)

target_sources(Pico2VibePlugin
  PRIVATE
    PluginEditor.cpp
    PluginProcessor.cpp)

target_include_directories(Pico2VibePlugin
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(Pico2VibePlugin
  PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

target_compile_definitions(Pico2VibePlugin
  PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

if(BUILD_TESTING)
  add_executable(Pico2VibePluginTests
    PluginSmokeTest.cpp)

  target_include_directories(Pico2VibePluginTests
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_BINARY_DIR}/Pico2VibePlugin_artefacts/JuceLibraryCode
      ${JUCE_MODULES_DIR})

  target_link_libraries(Pico2VibePluginTests
    PRIVATE
      Pico2VibePlugin)

  add_test(NAME juce_plugin_smoke_test COMMAND Pico2VibePluginTests)
endif()
