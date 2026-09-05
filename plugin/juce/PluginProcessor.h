#pragma once

#include <array>
#include <atomic>
#include <memory>

#include <JuceHeader.h>

class Pico2VibeAudioProcessor final : public juce::AudioProcessor,
                                      private juce::AudioProcessorValueTreeState::Listener {
public:
    using ValueTreeState = juce::AudioProcessorValueTreeState;

    Pico2VibeAudioProcessor();
    ~Pico2VibeAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    using juce::AudioProcessor::processBlock;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;
    juce::AudioProcessorParameter* getBypassParameter() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    void selectProgramFromEditor(int index);
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    ValueTreeState parameters;

    static ValueTreeState::ParameterLayout createParameterLayout();
    static juce::StringArray factoryPresetNames();
    static int customProgramIndex();
    static juce::StringArray voicingChoices();
    static juce::StringArray qualityChoices();

    float getOutputMeterLeft() const;
    float getOutputMeterRight() const;
    float getHostTempoBpm() const;
    float getTransportPhase() const;
    int getComparisonSlot() const;
    void selectComparisonSlot(int slot);

private:
    struct DspState;
    struct HostTransportInfo {
        float bpm = 0.0f;
        double ppq = 0.0;
        bool hasPpq = false;
        bool isPlaying = false;
    };

    void syncParametersToDsp(float hostTempoBpm = 0.0f);
    HostTransportInfo queryHostTransport() const;
    void syncLfoPhaseToHost(const HostTransportInfo& transport);
    float getFloatParam(const char* id) const;
    bool getBoolParam(const char* id) const;
    int getChoiceParam(const char* id, int fallback) const;
    void applyFactoryPreset(int index);
    void setParamValue(const char* id, float value);
    void setProgramTrackingEnabled(bool enabled);
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    juce::ValueTree createSoundState();
    juce::MemoryBlock serializeSoundState();
    bool restoreSoundState(const juce::ValueTree& state, bool preserveBypass);

    std::unique_ptr<DspState> dsp;
    std::atomic<float> outputMeterLeft { 0.0f };
    std::atomic<float> outputMeterRight { 0.0f };
    std::atomic<float> hostTempoBpm { 0.0f };
    std::atomic<float> transportPhase { -1.0f };
    float bypassEffectLevel = 1.0f;
    float bypassSmoothCoeff = 1.0f;
    std::atomic<int> currentProgram { 0 };
    std::atomic<bool> suppressProgramTracking { false };
    juce::CriticalSection comparisonStateLock;
    std::array<juce::MemoryBlock, 2> comparisonStates;
    std::atomic<int> currentComparisonSlot { 0 };
    int currentVoicing = -1;
    int currentQuality = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Pico2VibeAudioProcessor)
};
