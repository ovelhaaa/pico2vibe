#include "PluginEditor.h"

namespace {
constexpr int kDesignWidth = 840;
constexpr int kDesignHeight = 510;
constexpr float kDisabledAlpha = 0.38f;
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
juce::Colour fieldColour() { return juce::Colour(0xff1c1b17); }
}  // namespace

// Only the rendering is custom: JUCE retains editing, gestures, keyboard and
// accessibility semantics for sliders, combo boxes and buttons.
class Pico2VibeAudioProcessorEditor::InterfaceLookAndFeel final : public juce::LookAndFeel_V4 {
public:
    InterfaceLookAndFeel() {
        setDefaultSansSerifTypefaceName("Segoe UI");
        setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff201f1a));
        setColour(juce::ComboBox::textColourId, textColour());
        setColour(juce::ComboBox::outlineColourId, edgeColour());
        setColour(juce::PopupMenu::backgroundColourId, panelColour());
        setColour(juce::PopupMenu::textColourId, textColour());
        setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xff31604c));
        setColour(juce::PopupMenu::highlightedTextColourId, textColour());
        setColour(juce::TextEditor::backgroundColourId, fieldColour());
        setColour(juce::TextEditor::textColourId, textColour());
        setColour(juce::TextEditor::highlightColourId, juce::Colour(0xff31604c));
        setColour(juce::TextEditor::focusedOutlineColourId, accentColour());
        setColour(juce::TooltipWindow::backgroundColourId, fieldColour());
        setColour(juce::TooltipWindow::textColourId, textColour());
        setColour(juce::TooltipWindow::outlineColourId, edgeColour());
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float position, float start, float end, juce::Slider& slider) override {
        const auto centre = juce::Rectangle<float>((float)x, (float)y, (float)width, (float)height).getCentre();
        constexpr float radius = 25.0f;
        g.setColour(panelColour());
        g.fillEllipse(centre.x - 27.0f, centre.y - 27.0f, 54.0f, 54.0f);
        g.setColour(juce::Colour(0xff363226));
        g.drawEllipse(centre.x - 27.0f, centre.y - 27.0f, 54.0f, 54.0f, 4.0f);
        const float angle = start + position * (end - start);
        juce::Path arc;
        arc.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, start, angle, true);
        g.setColour(accentColour());
        g.strokePath(arc, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        juce::Path needle;
        needle.startNewSubPath(centre);
        needle.lineTo(centre.getPointOnCircumference(20.0f, angle));
        g.strokePath(needle, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        if (slider.isEnabled() && slider.hasKeyboardFocus(true)) {
            g.setColour(textColour());
            g.drawEllipse(centre.x - 31.0f, centre.y - 31.0f, 62.0f, 62.0f, 1.0f);
        }
    }

    juce::Slider::SliderLayout getSliderLayout(juce::Slider& slider) override {
        juce::Slider::SliderLayout layout;
        layout.sliderBounds = { 0, 0, slider.getWidth(), 56 };
        layout.textBoxBounds = { (slider.getWidth() - 78) / 2, 58, 78, 18 };
        return layout;
    }

    juce::Label* createSliderTextBox(juce::Slider& slider) override {
        auto* label = juce::LookAndFeel_V4::createSliderTextBox(slider);
        label->setFont(juce::Font(11.0f));
        label->setTitle(slider.getTitle() + " value");
        return label;
    }

    void drawLabel(juce::Graphics& g, juce::Label& label) override {
        // Disabled controls use component alpha once, including their text fields.
        g.setColour(label.findColour(juce::Label::backgroundColourId));
        g.fillRoundedRectangle(label.getLocalBounds().toFloat(), 2.0f);
        if (!label.isBeingEdited()) {
            g.setColour(label.findColour(juce::Label::textColourId));
            g.setFont(label.getFont());
            g.drawFittedText(label.getText(), label.getBorderSize().subtractedFrom(label.getLocalBounds()),
                             label.getJustificationType(), 1);
        }
    }

    juce::Font getComboBoxFont(juce::ComboBox&) override { return juce::Font(12.0f); }
    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override {
        label.setBounds(8, 0, box.getWidth() - 28, box.getHeight());
        label.setFont(getComboBoxFont(box));
    }
    void drawComboBox(juce::Graphics& g, int width, int height, bool,
                      int, int, int, int, juce::ComboBox& box) override {
        auto bounds = juce::Rectangle<float>(0.5f, 0.5f, (float)width - 1.0f, (float)height - 1.0f);
        g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle(bounds, 2.0f);
        g.setColour(box.hasKeyboardFocus(true) ? accentColour() : edgeColour());
        g.drawRoundedRectangle(bounds, 2.0f, 1.0f);
        juce::Path arrow;
        const float left = (float)width - 24.0f;
        const float top = (float)height * 0.5f - 3.0f;
        arrow.startNewSubPath(left, top);
        arrow.lineTo(left + 5.0f, top + 5.0f);
        arrow.lineTo(left + 10.0f, top);
        g.setColour(mutedColour());
        g.strokePath(arrow, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& colour,
                              bool highlighted, bool down) override {
        const auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(down ? colour.brighter(0.15f) : highlighted ? colour.brighter(0.07f) : colour);
        g.fillRoundedRectangle(bounds, 2.0f);
        g.setColour(button.hasKeyboardFocus(true) ? accentColour() : edgeColour());
        g.drawRoundedRectangle(bounds, 2.0f, 1.0f);
    }
    void drawButtonText(juce::Graphics& g, juce::TextButton& button, bool, bool) override {
        g.setColour(button.findColour(button.getToggleState() ? juce::TextButton::textColourOnId
                                                              : juce::TextButton::textColourOffId));
        g.setFont(juce::Font(11.0f, juce::Font::bold));
        g.drawText(button.getButtonText(), button.getLocalBounds().reduced(3), juce::Justification::centred);
    }
    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button, bool, bool) override {
        const auto box = juce::Rectangle<float>(0.5f, 6.5f, 15.0f, 15.0f);
        g.setColour(fieldColour());
        g.fillRoundedRectangle(box, 2.0f);
        g.setColour(button.hasKeyboardFocus(true) ? accentColour() : edgeColour());
        g.drawRoundedRectangle(box, 2.0f, 1.0f);
        if (button.getToggleState()) {
            juce::Path tick;
            tick.startNewSubPath(4.0f, 14.0f);
            tick.lineTo(8.0f, 18.0f);
            tick.lineTo(14.0f, 10.0f);
            g.setColour(button.isEnabled() ? accentColour() : edgeColour());
            g.strokePath(tick, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour(mutedColour());
        g.setFont(juce::Font(11.0f, juce::Font::bold));
        g.drawText(button.getButtonText(), button.getLocalBounds().withTrimmedLeft(24), juce::Justification::centredLeft);
    }
};

Pico2VibeAudioProcessorEditor::Pico2VibeAudioProcessorEditor(Pico2VibeAudioProcessor& owner)
    : AudioProcessorEditor(&owner), audioProcessor(owner),
      interfaceLookAndFeel(std::make_unique<InterfaceLookAndFeel>()) {
    setLookAndFeel(interfaceLookAndFeel.get());
    setTitle("pico2vibe optical phase chorus / vibrato");
    addAndMakeVisible(content);
    content.setFocusContainerType(juce::Component::FocusContainerType::keyboardFocusContainer);

    titleLabel.setText("pico2vibe", juce::dontSendNotification);
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    titleLabel.setColour(juce::Label::textColourId, accentColour());
    titleLabel.setFont(juce::Font(25.0f, juce::Font::bold));
    content.addAndMakeVisible(titleLabel);

    subtitleLabel.setText("optical phase chorus / vibrato", juce::dontSendNotification);
    subtitleLabel.setJustificationType(juce::Justification::centredLeft);
    subtitleLabel.setColour(juce::Label::textColourId, mutedColour());
    subtitleLabel.setFont(juce::Font(13.0f));
    content.addAndMakeVisible(subtitleLabel);

    presetBox.addItemList(Pico2VibeAudioProcessor::factoryPresetNames(), 1);
    presetBox.addItem("Custom", Pico2VibeAudioProcessor::customProgramIndex() + 1);
    presetBox.setSelectedItemIndex(audioProcessor.getCurrentProgram(), juce::dontSendNotification);
    presetBox.onChange = [this] {
        const int selected = presetBox.getSelectedItemIndex();
        if (selected >= 0) audioProcessor.selectProgramFromEditor(selected);
    };
    content.addAndMakeVisible(presetBox);

    for (auto* button : { &comparisonAButton, &comparisonBButton }) {
        button->setClickingTogglesState(false);
        button->setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(32, 31, 26));
        button->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff31604c));
        button->setColour(juce::TextButton::textColourOffId, mutedColour());
        button->setColour(juce::TextButton::textColourOnId, textColour());
        content.addAndMakeVisible(button);
    }
    comparisonAButton.setTooltip("Recall comparison slot A");
    comparisonBButton.setTooltip("Recall comparison slot B");
    comparisonAButton.onClick = [this] { audioProcessor.selectComparisonSlot(0); };
    comparisonBButton.onClick = [this] { audioProcessor.selectComparisonSlot(1); };

    voicingBox.addItemList(Pico2VibeAudioProcessor::voicingChoices(), 1);
    content.addAndMakeVisible(voicingBox);
    voicingAttachment = std::make_unique<ComboAttachment>(audioProcessor.parameters, "voicing", voicingBox);

    qualityBox.addItemList(Pico2VibeAudioProcessor::qualityChoices(), 1);
    content.addAndMakeVisible(qualityBox);
    qualityAttachment = std::make_unique<ComboAttachment>(audioProcessor.parameters, "quality", qualityBox);

    tempoSyncButton.setButtonText("SYNC");
    tempoSyncButton.setClickingTogglesState(true);
    tempoSyncButton.setTooltip("Use the DAW tempo when available");
    tempoSyncButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(32, 31, 26));
    tempoSyncButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff31604c));
    tempoSyncButton.setColour(juce::TextButton::textColourOffId, mutedColour());
    tempoSyncButton.setColour(juce::TextButton::textColourOnId, textColour());
    content.addAndMakeVisible(tempoSyncButton);
    tempoSyncAttachment = std::make_unique<ButtonAttachment>(audioProcessor.parameters, "tempo_sync", tempoSyncButton);

    phaseLockButton.setButtonText("PHASE LOCK");
    phaseLockButton.setTooltip("Align the LFO cycle to the DAW timeline");
    phaseLockButton.setColour(juce::ToggleButton::textColourId, mutedColour());
    phaseLockButton.setColour(juce::ToggleButton::tickColourId, accentColour());
    phaseLockButton.setColour(juce::ToggleButton::tickDisabledColourId, edgeColour());
    content.addAndMakeVisible(phaseLockButton);
    phaseLockAttachment = std::make_unique<ButtonAttachment>(audioProcessor.parameters, "phase_lock", phaseLockButton);

    bypassButton.setButtonText("BYPASS");
    bypassButton.setClickingTogglesState(true);
    bypassButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(32, 31, 26));
    bypassButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour::fromRGB(64, 31, 28));
    bypassButton.setColour(juce::TextButton::textColourOffId, mutedColour());
    bypassButton.setColour(juce::TextButton::textColourOnId, juce::Colour::fromRGB(236, 112, 93));
    content.addAndMakeVisible(bypassButton);
    bypassAttachment = std::make_unique<ButtonAttachment>(audioProcessor.parameters, "bypass", bypassButton);

    for (size_t i = 0; i < sliders.size(); ++i) {
        sliders[i].setTitle(kSliderSpecs[i].label);
        configureSlider(sliders[i], kSliderSpecs[i].suffix);
        configureLabel(sliderLabels[i], kSliderSpecs[i].label);
        content.addAndMakeVisible(sliders[i]);
        content.addAndMakeVisible(sliderLabels[i]);
        sliderAttachments[i] = std::make_unique<SliderAttachment>(audioProcessor.parameters, kSliderSpecs[i].id, sliders[i]);
    }

    int focusOrder = 1;
    for (auto* component : std::initializer_list<juce::Component*> {
             &presetBox, &comparisonAButton, &comparisonBButton, &voicingBox,
             &qualityBox, &tempoSyncButton, &bypassButton }) {
        component->setWantsKeyboardFocus(true);
        component->setExplicitFocusOrder(focusOrder++);
    }
    presetBox.setTitle("Factory preset");
    presetBox.setComponentID("preset");
    presetBox.setTooltip("Recall a factory preset; sound edits select Custom");
    voicingBox.setTitle("Voicing");
    voicingBox.setComponentID("voicing");
    voicingBox.setTooltip("Optical circuit voicing");
    qualityBox.setTitle("Quality");
    qualityBox.setComponentID("quality");
    qualityBox.setTooltip("Processing quality: Eco, Standard or High");
    comparisonAButton.setTitle("Comparison slot A");
    comparisonAButton.setComponentID("comparison_a");
    comparisonBButton.setTitle("Comparison slot B");
    comparisonBButton.setComponentID("comparison_b");
    tempoSyncButton.setTitle("Tempo sync");
    tempoSyncButton.setComponentID("tempo_sync");
    bypassButton.setTitle("Bypass");
    bypassButton.setComponentID("bypass");
    bypassButton.setTooltip("Bypass the effect");
    for (size_t i = 0; i < sliders.size(); ++i) {
        auto& slider = sliders[i];
        slider.setComponentID(kSliderSpecs[i].id);
        slider.setName(kSliderSpecs[i].label);
        slider.setTitle(kSliderSpecs[i].label);
        slider.setDescription("Drag or use arrow keys; edit the value directly; double-click to reset");
        slider.setTooltip(kSliderSpecs[i].label + juce::String(" - double-click to reset to default"));
        slider.setWantsKeyboardFocus(true);
        slider.setExplicitFocusOrder(focusOrder++);
        slider.setNumDecimalPlacesToDisplay(i == kTempoBpmSliderIndex ? 0 : 2);
        // The attachment supplies parameter formatting; use the mockup's compact
        // precision here while retaining its range, parsing and host gestures.
        slider.textFromValueFunction = [decimals = i == kTempoBpmSliderIndex ? 0 : 2](double value) {
            return juce::String(value, decimals);
        };
        slider.updateText();
        if (auto* parameter = audioProcessor.parameters.getParameter(kSliderSpecs[i].id))
            slider.setDoubleClickReturnValue(true, parameter->convertFrom0to1(parameter->getDefaultValue()));
        sliderLabels[i].setAccessible(false);
    }
    phaseLockButton.setTitle("Transport phase lock");
    phaseLockButton.setComponentID("phase_lock");
    phaseLockButton.setWantsKeyboardFocus(true);
    phaseLockButton.setExplicitFocusOrder(focusOrder);
    for (auto* label : { &meterLeftLabel, &meterRightLabel }) {
        configureLabel(*label, "");
        label->setJustificationType(juce::Justification::centredLeft);
        content.addAndMakeVisible(label);
    }
    meterLeftLabel.setTitle("Left output peak");
    meterRightLabel.setTitle("Right output peak");
    setResizable(true, true);
    setResizeLimits(kDesignWidth, kDesignHeight, kDesignWidth * 2, kDesignHeight * 2);
    getConstrainer()->setFixedAspectRatio((double)kDesignWidth / kDesignHeight);
    setSize(kDesignWidth, kDesignHeight);
    timerCallback(); // Correct disabled and A/B states before the first paint.
    startTimerHz(30);
}

Pico2VibeAudioProcessorEditor::~Pico2VibeAudioProcessorEditor() {
    stopTimer();
    setLookAndFeel(nullptr);
}

void Pico2VibeAudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(backgroundColour());
    g.addTransform(layoutTransform);
    const juce::Rectangle<float> panel(10.5f, 10.5f, 819.0f, 489.0f);
    g.setColour(panelColour());
    g.fillRoundedRectangle(panel, 8.0f);
    g.setColour(edgeColour());
    g.drawRoundedRectangle(panel, 8.0f, 1.0f);
    g.setColour(juce::Colour(0xff26241d));
    g.fillRect(22, 104, 796, 1);
    drawMeter(g, { 22, 458, 796, 12 }, meterLeft, "L");
    drawMeter(g, { 22, 475, 796, 12 }, meterRight, "R");
}

void Pico2VibeAudioProcessorEditor::resized() {
    // Also fit safely if a host supplies a size outside the aspect-ratio constraint.
    const float scale = juce::jmin((float)getWidth() / kDesignWidth, (float)getHeight() / kDesignHeight);
    layoutTransform = juce::AffineTransform::scale(scale).translated(
        ((float)getWidth() - kDesignWidth * scale) * 0.5f,
        ((float)getHeight() - kDesignHeight * scale) * 0.5f);
    content.setBounds(0, 0, kDesignWidth, kDesignHeight);
    content.setTransform(layoutTransform);
    titleLabel.setBounds(17, 22, 275, 34);
    subtitleLabel.setBounds(17, 56, 275, 24);
    presetBox.setBounds(298, 22, 438, 30);
    comparisonAButton.setBounds(744, 22, 34, 30);
    comparisonBButton.setBounds(784, 22, 34, 30);
    voicingBox.setBounds(298, 60, 130, 40);
    qualityBox.setBounds(436, 60, 122, 40);
    tempoSyncButton.setBounds(566, 60, 122, 30);
    bypassButton.setBounds(696, 60, 122, 30);
    for (size_t i = 0; i < sliders.size(); ++i) {
        const int x = 30 + (int)(i % 4) * 199;
        const int y = 138 + (int)(i / 4) * 103;
        sliderLabels[i].setBounds(x, y, 183, 18);
        sliders[i].setBounds(x, y + 18, 183, 76);
    }
    phaseLockButton.setBounds(648, 369, 140, 28);
    // Invisible text alternatives supplement the painted stereo meters.
    meterLeftLabel.setBounds(22, 458, 18, 12);
    meterRightLabel.setBounds(22, 475, 18, 12);
}

void Pico2VibeAudioProcessorEditor::configureSlider(juce::Slider& slider, const juce::String& suffix) {
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 78, 18);
    slider.setTextValueSuffix(suffix);
    slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f,
                               juce::MathConstants<float>::pi * 2.75f, true);
    slider.setColour(juce::Slider::textBoxBackgroundColourId, fieldColour());
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
    g.setFont(juce::Font(11.0f));
    g.drawText(label, labelArea, juce::Justification::centredLeft);

    auto meterBounds = bounds.reduced(0, 2);
    g.setColour(juce::Colour::fromRGB(28, 27, 23));
    g.fillRect(meterBounds);
    const auto inner = meterBounds.reduced(1);
    const int fillWidth = juce::roundToInt((float)inner.getWidth() * juce::jlimit(0.0f, 1.0f, value));
    g.setColour(accentColour());
    g.fillRect(inner.withWidth(fillWidth));
    g.setColour(edgeColour());
    g.drawRect(meterBounds);
}

void Pico2VibeAudioProcessorEditor::timerCallback() {
    const float decay = 0.82f;
    meterLeft = juce::jmax(audioProcessor.getOutputMeterLeft(), meterLeft * decay);
    meterRight = juce::jmax(audioProcessor.getOutputMeterRight(), meterRight * decay);

    const int program = audioProcessor.getCurrentProgram();
    if (presetBox.getSelectedItemIndex() != program) {
        presetBox.setSelectedItemIndex(program, juce::dontSendNotification);
    }
    const int comparisonSlot = audioProcessor.getComparisonSlot();
    comparisonAButton.setToggleState(comparisonSlot == 0, juce::dontSendNotification);
    comparisonBButton.setToggleState(comparisonSlot == 1, juce::dontSendNotification);

    const auto* syncValue = audioProcessor.parameters.getRawParameterValue("tempo_sync");
    const bool syncEnabled = syncValue != nullptr && syncValue->load() >= 0.5f;
    const float hostBpm = audioProcessor.getHostTempoBpm();
    const auto enableSlider = [this](size_t index, bool enabled) {
        sliders[index].setEnabled(enabled);
        sliders[index].setAlpha(enabled ? 1.0f : kDisabledAlpha);
        sliderLabels[index].setAlpha(enabled ? 1.0f : kDisabledAlpha);
    };
    enableSlider(kRateSliderIndex, !syncEnabled);
    enableSlider(kTempoBpmSliderIndex, syncEnabled && hostBpm <= 0.0f);
    enableSlider(kTempoDivisionSliderIndex, syncEnabled);
    phaseLockButton.setEnabled(syncEnabled);
    phaseLockButton.setAlpha(syncEnabled ? 1.0f : kDisabledAlpha);
    sliders[kRateSliderIndex].setTooltip(syncEnabled ? "Rate follows tempo and beats per cycle while sync is on"
                                                       : "LFO rate in Hz - double-click to reset");
    sliders[kTempoBpmSliderIndex].setTooltip(!syncEnabled ? "Enable Sync to use fallback BPM"
        : hostBpm > 0.0f ? "Host tempo is available; fallback BPM is preserved for hosts without tempo"
                         : "Tempo in BPM used when the host does not provide tempo");
    sliders[kTempoDivisionSliderIndex].setTooltip("Beats per LFO cycle; available with Sync enabled");
    meterLeftLabel.setDescription("Peak " + juce::String(juce::Decibels::gainToDecibels(meterLeft, -100.0f), 1) + " dBFS");
    meterRightLabel.setDescription("Peak " + juce::String(juce::Decibels::gainToDecibels(meterRight, -100.0f), 1) + " dBFS");
    tempoSyncButton.setButtonText(syncEnabled && hostBpm > 0.0f
                                      ? juce::String(hostBpm, 1) + " BPM"
                                      : "SYNC");
    repaint();
}
