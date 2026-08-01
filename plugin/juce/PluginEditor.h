#pragma once

#include <array>
#include <memory>

#include <JuceHeader.h>

#include "PluginProcessor.h"

class Pico2VibeAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                            private juce::Timer {
public:
    explicit Pico2VibeAudioProcessorEditor(Pico2VibeAudioProcessor& owner);
    ~Pico2VibeAudioProcessorEditor() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    Pico2VibeAudioProcessor& audioProcessor;
    juce::Label titleLabel;
    juce::Label subtitleLabel;
    juce::ComboBox presetBox;
    juce::ComboBox voicingBox;
    juce::ComboBox qualityBox;
    juce::TextButton bypassButton;
    std::array<juce::Slider, 9> sliders;
    std::array<juce::Label, 9> sliderLabels;
    std::unique_ptr<ComboAttachment> voicingAttachment;
    std::unique_ptr<ComboAttachment> qualityAttachment;
    std::unique_ptr<ButtonAttachment> bypassAttachment;
    std::array<std::unique_ptr<SliderAttachment>, 9> sliderAttachments;
    float meterLeft = 0.0f;
    float meterRight = 0.0f;

    void configureSlider(juce::Slider& slider, const juce::String& suffix);
    void configureLabel(juce::Label& label, const juce::String& text);
    void drawMeter(juce::Graphics& g, juce::Rectangle<int> bounds, float value, const juce::String& label);
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Pico2VibeAudioProcessorEditor)
};