#include "PluginEditor.h"

namespace {
constexpr int kMargin = 22;
constexpr int kHeaderHeight = 78;
constexpr int kSelectorHeight = 30;
constexpr int kMeterHeight = 12;
constexpr size_t kRateSliderIndex = 1;
constexpr size_t kTempoBpmSliderIndex = 9;
constexpr size_t kTempoDivisionSliderIndex = 10;

struct SliderSpec {
    const char* id;
    const char* label;
    const char* suffix;
};

constexpr SliderSpec kSliderSpecs[] = {
    { "depth", "DEPTH", "" },
    { "lfo_rate_hz", "RATE", " Hz" },
    { "mix", "MIX", "" },
    { "feedback", "FEEDBACK", "" },
    { "input_drive", "DRIVE", "x" },
    { "stereo_width", "WIDTH", "" },
    { "tone_tilt", "TONE", "" },
    { "noise_amount", "NOISE", "" },
    { "output_gain", "OUTPUT", "x" },
    { "tempo_bpm", "BPM FALLBACK", " BPM" },
    { "tempo_division_beats", "BEATS / CYCLE", " beats" }
};

juce::Colour backgroundColour() { return juce::Colour::fromRGB(12, 12, 10); }
juce::Colour panelColour() { return juce::Colour::fromRGB(22, 22, 18); }
juce::Colour edgeColour() { return juce::Colour::fromRGB(59, 54, 39); }
juce::Colour textColour() { return juce::Colour::fromRGB(219, 213, 190); }
juce::Colour mutedColour() { return juce::Colour::fromRGB(142, 134, 105); }
juce::Colour accentColour() { return juce::Colour::fromRGB(86, 167, 132); }
}  // namespace

Pico2VibeAudioProcessorEditor::Pico2VibeAudioProcessorEditor(Pico2VibeAudioProcessor& owner)
    : AudioProcessorEditor(&owner), audioProcessor(owner) {
    setSize(840, 510);

    titleLabel.setText("pico2vibe", juce::dontSendNotification);
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    titleLabel.setColour(juce::Label::textColourId, accentColour());
    titleLabel.setFont(juce::Font(25.0f, juce::Font::bold));
    addAndMakeVisible(titleLabel);

    subtitleLabel.setText("optical phase chorus / vibrato", juce::dontSendNotification);
    subtitleLabel.setJustificationType(juce::Justification::centredLeft);
    subtitleLabel.setColour(juce::Label::textColourId, mutedColour());
    subtitleLabel.setFont(juce::Font(13.0f));
    addAndMakeVisible(subtitleLabel);

    presetBox.addItemList(Pico2VibeAudioProcessor::factoryPresetNames(), 1);
    presetBox.setSelectedItemIndex(audioProcessor.getCurrentProgram(), juce::dontSendNotification);
    presetBox.onChange = [this] {
        const int selected = presetBox.getSelectedItemIndex();
        if (selected >= 0) audioProcessor.setCurrentProgram(selected);
    };
    addAndMakeVisible(presetBox);

    voicingBox.addItemList(Pico2VibeAudioProcessor::voicingChoices(), 1);
    addAndMakeVisible(voicingBox);
    voicingAttachment = std::make_unique<ComboAttachment>(audioProcessor.parameters, "voicing", voicingBox);

    qualityBox.addItemList(Pico2VibeAudioProcessor::qualityChoices(), 1);
    addAndMakeVisible(qualityBox);
    qualityAttachment = std::make_unique<ComboAttachment>(audioProcessor.parameters, "quality", qualityBox);

    tempoSyncButton.setButtonText("SYNC");
    tempoSyncButton.setClickingTogglesState(true);
    tempoSyncButton.setTooltip("Use the DAW tempo when available");
    tempoSyncButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(32, 31, 26));
    tempoSyncButton.setColour(juce::TextButton::buttonOnColourId, accentColour().darker(0.35f));
    tempoSyncButton.setColour(juce::TextButton::textColourOffId, mutedColour());
    tempoSyncButton.setColour(juce::TextButton::textColourOnId, textColour());
    addAndMakeVisible(tempoSyncButton);
    tempoSyncAttachment = std::make_unique<ButtonAttachment>(audioProcessor.parameters, "tempo_sync", tempoSyncButton);

    phaseLockButton.setButtonText("PHASE LOCK");
    phaseLockButton.setTooltip("Align the LFO cycle to the DAW timeline");
    phaseLockButton.setColour(juce::ToggleButton::textColourId, mutedColour());
    phaseLockButton.setColour(juce::ToggleButton::tickColourId, accentColour());
    phaseLockButton.setColour(juce::ToggleButton::tickDisabledColourId, edgeColour());
    addAndMakeVisible(phaseLockButton);
    phaseLockAttachment = std::make_unique<ButtonAttachment>(audioProcessor.parameters, "phase_lock", phaseLockButton);

    bypassButton.setButtonText("BYPASS");
    bypassButton.setClickingTogglesState(true);
    bypassButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(32, 31, 26));
    bypassButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour::fromRGB(64, 31, 28));
    bypassButton.setColour(juce::TextButton::textColourOffId, mutedColour());
    bypassButton.setColour(juce::TextButton::textColourOnId, juce::Colour::fromRGB(236, 112, 93));
    addAndMakeVisible(bypassButton);
    bypassAttachment = std::make_unique<ButtonAttachment>(audioProcessor.parameters, "bypass", bypassButton);

    for (size_t i = 0; i < sliders.size(); ++i) {
        configureSlider(sliders[i], kSliderSpecs[i].suffix);
        configureLabel(sliderLabels[i], kSliderSpecs[i].label);
        addAndMakeVisible(sliders[i]);
        addAndMakeVisible(sliderLabels[i]);
        sliderAttachments[i] = std::make_unique<SliderAttachment>(audioProcessor.parameters, kSliderSpecs[i].id, sliders[i]);
    }

    startTimerHz(30);
}

void Pico2VibeAudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(backgroundColour());
    auto panel = getLocalBounds().reduced(10).toFloat();
    g.setColour(panelColour());
    g.fillRoundedRectangle(panel, 8.0f);
    g.setColour(edgeColour());
    g.drawRoundedRectangle(panel, 8.0f, 1.0f);

    g.setColour(juce::Colour::fromRGB(38, 36, 29));
    g.fillRect(kMargin, kHeaderHeight + 26, getWidth() - 2 * kMargin, 1);

    auto meterArea = getLocalBounds().reduced(kMargin).removeFromBottom(30);
    drawMeter(g, meterArea.removeFromTop(kMeterHeight), meterLeft, "L");
    meterArea.removeFromTop(5);
    drawMeter(g, meterArea.removeFromTop(kMeterHeight), meterRight, "R");
}

void Pico2VibeAudioProcessorEditor::resized() {
    auto area = getLocalBounds().reduced(kMargin);
    auto header = area.removeFromTop(kHeaderHeight);
    auto titleArea = header.removeFromLeft(270);
    titleLabel.setBounds(titleArea.removeFromTop(34));
    subtitleLabel.setBounds(titleArea.removeFromTop(24));

    auto selectorArea = header.removeFromRight(520);
    presetBox.setBounds(selectorArea.removeFromTop(kSelectorHeight));
    selectorArea.removeFromTop(8);
    const int selectorWidth = selectorArea.getWidth() / 4;
    voicingBox.setBounds(selectorArea.removeFromLeft(selectorWidth));
    selectorArea.removeFromLeft(8);
    qualityBox.setBounds(selectorArea.removeFromLeft(selectorWidth - 8));
    selectorArea.removeFromLeft(8);
    tempoSyncButton.setBounds(selectorArea.removeFromLeft(selectorWidth - 8).withHeight(kSelectorHeight));
    selectorArea.removeFromLeft(8);
    bypassButton.setBounds(selectorArea.withHeight(kSelectorHeight));

    area.removeFromTop(34);
    auto grid = area;
    grid.removeFromBottom(44);
    const int columns = 4;
    const int rows = 3;
    const int cellW = grid.getWidth() / columns;
    const int cellH = grid.getHeight() / rows;
    for (int i = 0; i < (int)sliders.size(); ++i) {
        const int col = i % columns;
        const int row = i / columns;
        auto cell = juce::Rectangle<int>(grid.getX() + col * cellW, grid.getY() + row * cellH, cellW, cellH).reduced(8, 4);
        sliderLabels[(size_t)i].setBounds(cell.removeFromTop(18));
        sliders[(size_t)i].setBounds(cell);
    }
    const auto phaseCell = juce::Rectangle<int>(grid.getX() + 3 * cellW, grid.getY() + 2 * cellH, cellW, cellH).reduced(18, 12);
    phaseLockButton.setBounds(phaseCell.withSizeKeepingCentre(140, 28));
}

void Pico2VibeAudioProcessorEditor::configureSlider(juce::Slider& slider, const juce::String& suffix) {
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 78, 18);
    slider.setTextValueSuffix(suffix);
    slider.setColour(juce::Slider::rotarySliderFillColourId, accentColour());
    slider.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour::fromRGB(54, 50, 38));
    slider.setColour(juce::Slider::thumbColourId, accentColour());
    slider.setColour(juce::Slider::textBoxTextColourId, textColour());
    slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
}

void Pico2VibeAudioProcessorEditor::configureLabel(juce::Label& label, const juce::String& text) {
    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, mutedColour());
    label.setFont(juce::Font(11.0f, juce::Font::bold));
}

void Pico2VibeAudioProcessorEditor::drawMeter(juce::Graphics& g, juce::Rectangle<int> bounds, float value, const juce::String& label) {
    const auto labelArea = bounds.removeFromLeft(18);
    g.setColour(mutedColour());
    g.drawText(label, labelArea, juce::Justification::centredLeft);

    auto meterBounds = bounds.reduced(0, 2);
    g.setColour(juce::Colour::fromRGB(28, 27, 23));
    g.fillRect(meterBounds);
    const int fillWidth = juce::roundToInt((float)meterBounds.getWidth() * juce::jlimit(0.0f, 1.0f, value));
    g.setColour(accentColour());
    g.fillRect(meterBounds.withWidth(fillWidth));
    g.setColour(edgeColour());
    g.drawRect(meterBounds);
}

void Pico2VibeAudioProcessorEditor::timerCallback() {
    const float decay = 0.82f;
    meterLeft = juce::jmax(audioProcessor.getOutputMeterLeft(), meterLeft * decay);
    meterRight = juce::jmax(audioProcessor.getOutputMeterRight(), meterRight * decay);

    const auto* syncValue = audioProcessor.parameters.getRawParameterValue("tempo_sync");
    const bool syncEnabled = syncValue != nullptr && syncValue->load() >= 0.5f;
    const float hostBpm = audioProcessor.getHostTempoBpm();
    sliders[kRateSliderIndex].setEnabled(!syncEnabled);
    sliders[kTempoBpmSliderIndex].setEnabled(syncEnabled && hostBpm <= 0.0f);
    sliders[kTempoDivisionSliderIndex].setEnabled(syncEnabled);
    phaseLockButton.setEnabled(syncEnabled);
    tempoSyncButton.setButtonText(syncEnabled && hostBpm > 0.0f
                                      ? juce::String(hostBpm, 1) + " BPM"
                                      : "SYNC");
    repaint();
}
