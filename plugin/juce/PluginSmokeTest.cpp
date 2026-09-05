#include "PluginProcessor.h"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
constexpr double kSampleRate = 48000.0;

class MockPlayHead final : public juce::AudioPlayHead {
public:
    Optional<PositionInfo> getPosition() const override {
        return available ? Optional<PositionInfo>(position) : Optional<PositionInfo>();
    }

    void set(double bpm, double ppq, bool playing, bool looping = false) {
        position = {};
        position.setBpm(bpm);
        position.setPpqPosition(ppq);
        position.setIsPlaying(playing);
        position.setIsLooping(looping);
        available = true;
    }

    void setWithoutTempo(double ppq, bool playing) {
        position = {};
        position.setPpqPosition(ppq);
        position.setIsPlaying(playing);
        available = true;
    }

    void clear() {
        available = false;
    }

private:
    PositionInfo position;
    bool available = false;
};

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void requireNear(float actual, float expected, float tolerance, const std::string& message) {
    if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance) {
        throw std::runtime_error(message + "; actual=" + std::to_string(actual)
                                 + ", expected=" + std::to_string(expected));
    }
}

void setParameter(Pico2VibeAudioProcessor& processor, const char* id, float value) {
    auto* parameter = processor.parameters.getParameter(id);
    require(parameter != nullptr, std::string("missing parameter: ") + id);
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

float getParameterValue(Pico2VibeAudioProcessor& processor, const char* id) {
    auto* parameter = processor.parameters.getParameter(id);
    require(parameter != nullptr, std::string("missing parameter: ") + id);
    return parameter->convertFrom0to1(parameter->getValue());
}

float getParameterDefault(Pico2VibeAudioProcessor& processor, const char* id) {
    auto* parameter = processor.parameters.getParameter(id);
    require(parameter != nullptr, std::string("missing parameter: ") + id);
    return parameter->convertFrom0to1(parameter->getDefaultValue());
}

void fillInput(juce::AudioBuffer<float>& buffer, int64_t timelineSample) {
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel) {
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample) {
            const double t = static_cast<double>(timelineSample + sample) / kSampleRate;
            const float frequency = channel == 0 ? 173.0f : 227.0f;
            buffer.setSample(channel, sample, 0.20f * std::sin(2.0 * juce::MathConstants<double>::pi * frequency * t));
        }
    }
}

void requireFiniteAudio(const juce::AudioBuffer<float>& buffer, const std::string& context) {
    float peak = 0.0f;
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel) {
        const float* samples = buffer.getReadPointer(channel);
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample) {
            require(std::isfinite(samples[sample]), context + ": non-finite audio sample");
            peak = std::max(peak, std::abs(samples[sample]));
        }
    }
    require(peak > 1.0e-6f, context + ": unexpected silence");
    require(peak < 4.0f, context + ": unsafe output peak");
}

void processAt(Pico2VibeAudioProcessor& processor,
               juce::AudioBuffer<float>& buffer,
               int64_t timelineSample,
               const std::string& context) {
    fillInput(buffer, timelineSample);
    juce::MidiBuffer midi;
    processor.processBlock(buffer, midi);
    requireFiniteAudio(buffer, context);
}

void runStereoTransportTest() {
    MockPlayHead playHead;
    Pico2VibeAudioProcessor processor;
    processor.setPlayHead(&playHead);
    processor.prepareToPlay(kSampleRate, 127);

    setParameter(processor, "tempo_sync", 1.0f);
    setParameter(processor, "phase_lock", 1.0f);
    setParameter(processor, "tempo_division_beats", 2.0f);

    juce::AudioBuffer<float> buffer(2, 127);

    playHead.set(128.0, 5.0, true);
    processAt(processor, buffer, 0, "initial transport");
    requireNear(processor.getHostTempoBpm(), 128.0f, 1.0e-4f, "host BPM was not applied");
    requireNear(processor.getTransportPhase(), 0.5f, 1.0e-5f, "incorrect PPQ phase");

    playHead.set(128.0, 2.25, true, true);
    processAt(processor, buffer, 127, "loop seek");
    requireNear(processor.getTransportPhase(), 0.125f, 1.0e-5f, "loop PPQ phase was not realigned");

    playHead.set(128.0, -0.5, true);
    processAt(processor, buffer, 254, "negative seek");
    requireNear(processor.getTransportPhase(), 0.75f, 1.0e-5f, "negative PPQ wrapping failed");

    playHead.set(128.0, 6.0, false);
    processAt(processor, buffer, 381, "stopped transport");
    requireNear(processor.getTransportPhase(), -1.0f, 1.0e-5f, "phase lock remained active while stopped");

    playHead.setWithoutTempo(7.0, true);
    processAt(processor, buffer, 508, "missing host tempo");
    requireNear(processor.getHostTempoBpm(), 0.0f, 1.0e-5f, "missing BPM did not select manual fallback");
    requireNear(processor.getTransportPhase(), -1.0f, 1.0e-5f, "phase lock activated without BPM");

    setParameter(processor, "phase_lock", 0.0f);
    setParameter(processor, "bypass", 1.0f);
    playHead.set(96.0, 3.0, true);
    processAt(processor, buffer, 635, "free-running synchronized mode");
    requireNear(processor.getHostTempoBpm(), 96.0f, 1.0e-4f, "BPM sync stopped with phase lock disabled");
    requireNear(processor.getTransportPhase(), -1.0f, 1.0e-5f, "disabled phase lock reported active");

    juce::MemoryBlock state;
    processor.getStateInformation(state);
    require(state.getSize() > 0, "plugin state was empty");

    Pico2VibeAudioProcessor restored;
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    const auto* sync = restored.parameters.getRawParameterValue("tempo_sync");
    const auto* phaseLock = restored.parameters.getRawParameterValue("phase_lock");
    const auto* bypass = restored.parameters.getRawParameterValue("bypass");
    require(sync != nullptr && sync->load() >= 0.5f, "tempo sync did not survive state restore");
    require(phaseLock != nullptr && phaseLock->load() < 0.5f, "phase lock did not survive state restore");
    require(bypass != nullptr && bypass->load() >= 0.5f, "bypass did not survive state restore");
    require(restored.getBypassParameter() == restored.parameters.getParameter("bypass"),
            "JUCE bypass parameter is not backed by the saved APVTS parameter");
}

void runMonoSmokeTest() {
    Pico2VibeAudioProcessor processor;
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add(juce::AudioChannelSet::mono());
    layout.outputBuses.add(juce::AudioChannelSet::mono());
    require(processor.setBusesLayout(layout), "mono bus layout was rejected");
    processor.prepareToPlay(44100.0, 31);

    juce::AudioBuffer<float> buffer(1, 31);
    processAt(processor, buffer, 0, "mono processing");
}

void runStateMigrationTest() {
    Pico2VibeAudioProcessor source;
    requireNear(getParameterValue(source, "depth"), 0.74f, 1.0e-5f,
                "initial depth does not match the default factory preset");
    requireNear(getParameterDefault(source, "depth"), 0.74f, 1.0e-5f,
                "host depth default does not match the default factory preset");
    setParameter(source, "depth", 0.42f);

    juce::MemoryBlock currentState;
    source.getStateInformation(currentState);
    auto legacyXml = juce::AudioProcessor::getXmlFromBinary(
        currentState.getData(), static_cast<int>(currentState.getSize()));
    require(legacyXml != nullptr, "current state did not decode as XML");
    require(legacyXml->getIntAttribute("state_version", 0) == 1,
            "current state schema version is missing");

    legacyXml->removeAttribute("state_version");
    auto* phaseLockElement = legacyXml->getChildByAttribute("id", "phase_lock");
    require(phaseLockElement != nullptr, "phase lock was missing from current state");
    legacyXml->removeChildElement(phaseLockElement, true);

    juce::MemoryBlock legacyState;
    juce::AudioProcessor::copyXmlToBinary(*legacyXml, legacyState);

    Pico2VibeAudioProcessor restored;
    setParameter(restored, "depth", 0.91f);
    setParameter(restored, "phase_lock", 0.0f);
    restored.setStateInformation(legacyState.getData(), static_cast<int>(legacyState.getSize()));
    requireNear(getParameterValue(restored, "depth"), 0.42f, 1.0e-5f,
                "legacy state did not restore an existing parameter");
    requireNear(getParameterValue(restored, "phase_lock"), 1.0f, 1.0e-5f,
                "legacy state did not initialize a missing parameter from its default");

    const std::array<unsigned char, 4> corruptState { 0xde, 0xad, 0xbe, 0xef };
    restored.setStateInformation(corruptState.data(), static_cast<int>(corruptState.size()));
    requireNear(getParameterValue(restored, "depth"), 0.42f, 1.0e-5f,
                "corrupt state changed plugin parameters");
}

void runProgramTrackingTest() {
    Pico2VibeAudioProcessor processor;
    const int custom = Pico2VibeAudioProcessor::customProgramIndex();
    require(processor.getNumPrograms() == custom + 1, "custom program is not exposed to the host");
    require(processor.getCurrentProgram() == 0, "default factory program was not selected");
    require(processor.getProgramName(custom) == "Custom", "custom program name is missing");

    setParameter(processor, "depth", 0.31f);
    require(processor.getCurrentProgram() == custom, "parameter edit did not select Custom");

    processor.setCurrentProgram(1);
    require(processor.getCurrentProgram() == 1, "factory program selection was not retained");
    requireNear(getParameterValue(processor, "depth"), 0.80f, 1.0e-5f,
                "factory program did not restore its depth");

    setParameter(processor, "output_gain", 1.47f);
    require(processor.getCurrentProgram() == custom, "secondary parameter edit did not select Custom");
    processor.setCurrentProgram(2);
    requireNear(getParameterValue(processor, "output_gain"), 1.0f, 1.0e-5f,
                "factory program retained a parameter from the previous custom state");

    setParameter(processor, "bypass", 1.0f);
    require(processor.getCurrentProgram() == 2, "global bypass incorrectly selected Custom");

    setParameter(processor, "tone_tilt", 0.37f);
    require(processor.getCurrentProgram() == custom, "tone edit did not select Custom");
    juce::MemoryBlock state;
    processor.getStateInformation(state);
    Pico2VibeAudioProcessor restored;
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    require(restored.getCurrentProgram() == custom, "Custom program did not survive state restore");
    requireNear(getParameterValue(restored, "tone_tilt"), 0.37f, 1.0e-5f,
                "Custom program parameter did not survive state restore");
}

void runRepeatedStateRestorationTest() {
    Pico2VibeAudioProcessor processor;
    juce::MemoryBlock originalState;
    processor.getStateInformation(originalState);

    for (auto* parameter : processor.getParameters()) {
        if (parameter == processor.getBypassParameter()) continue;

        const float originalValue = parameter->getValue();
        parameter->setValue(0.496181f);
        processor.setStateInformation(
            originalState.getData(), static_cast<int>(originalState.getSize()));
        requireNear(parameter->getValue(), originalValue, 1.0e-5f,
                    "repeated state restore failed for " + parameter->getName(128).toStdString());
    }
}

void runComparisonStateTest() {
    Pico2VibeAudioProcessor processor;
    require(processor.getComparisonSlot() == 0, "comparison did not start on slot A");
    setParameter(processor, "depth", 0.24f);
    setParameter(processor, "bypass", 1.0f);

    processor.selectComparisonSlot(1);
    require(processor.getComparisonSlot() == 1, "comparison did not switch to slot B");
    requireNear(getParameterValue(processor, "depth"), 0.74f, 1.0e-5f,
                "slot B did not retain its independent initial sound");
    requireNear(getParameterValue(processor, "bypass"), 1.0f, 1.0e-5f,
                "comparison switch did not preserve global bypass");
    setParameter(processor, "depth", 0.66f);

    processor.selectComparisonSlot(0);
    requireNear(getParameterValue(processor, "depth"), 0.24f, 1.0e-5f,
                "slot A sound was not recalled");
    processor.selectComparisonSlot(1);
    requireNear(getParameterValue(processor, "depth"), 0.66f, 1.0e-5f,
                "slot B sound was not recalled");

    juce::MemoryBlock projectState;
    processor.getStateInformation(projectState);
    Pico2VibeAudioProcessor restored;
    restored.setStateInformation(projectState.getData(), static_cast<int>(projectState.getSize()));
    require(restored.getComparisonSlot() == 1, "active comparison slot was not restored");
    requireNear(getParameterValue(restored, "depth"), 0.66f, 1.0e-5f,
                "active comparison sound was not restored");
    restored.selectComparisonSlot(0);
    requireNear(getParameterValue(restored, "depth"), 0.24f, 1.0e-5f,
                "stored slot A did not survive project restore");

    auto malformedXml = juce::AudioProcessor::getXmlFromBinary(
        projectState.getData(), static_cast<int>(projectState.getSize()));
    require(malformedXml != nullptr, "A/B project state did not decode as XML");
    malformedXml->setAttribute("comparison_state_a", "999999999.invalid");
    juce::MemoryBlock malformedState;
    juce::AudioProcessor::copyXmlToBinary(*malformedXml, malformedState);
    Pico2VibeAudioProcessor recovered;
    recovered.setStateInformation(malformedState.getData(), static_cast<int>(malformedState.getSize()));
    recovered.selectComparisonSlot(0);
    requireNear(getParameterValue(recovered, "depth"), 0.66f, 1.0e-5f,
                "malformed comparison slot did not fall back to the active sound");
}

void runFactoryPresetAudioTest() {
    const auto names = Pico2VibeAudioProcessor::factoryPresetNames();
    require(names.size() == 12, "unexpected factory preset count");

    juce::StringArray uniqueNames;
    for (const auto& name : names) {
        require(name.isNotEmpty() && !uniqueNames.contains(name), "factory preset names are not unique");
        uniqueNames.add(name);
    }

    constexpr int blockSize = 127;
    constexpr int totalSamples = 96000;
    constexpr int warmupSamples = 12000;
    float quietestRms = std::numeric_limits<float>::max();
    float loudestRms = 0.0f;
    for (int preset = 0; preset < names.size(); ++preset) {
        Pico2VibeAudioProcessor processor;
        processor.prepareToPlay(kSampleRate, blockSize);
        processor.setCurrentProgram(preset);

        double sumSquares = 0.0;
        double monoSumSquares = 0.0;
        float peak = 0.0f;
        int measuredSamples = 0;
        int timelineSample = 0;
        while (timelineSample < totalSamples) {
            const int frames = juce::jmin(blockSize, totalSamples - timelineSample);
            juce::AudioBuffer<float> buffer(2, frames);
            for (int sample = 0; sample < frames; ++sample) {
                const double t = static_cast<double>(timelineSample + sample) / kSampleRate;
                const float input = 0.075f * (
                    std::sin(2.0 * juce::MathConstants<double>::pi * 110.0 * t)
                    + 0.58 * std::sin(2.0 * juce::MathConstants<double>::pi * 220.0 * t)
                    + 0.36 * std::sin(2.0 * juce::MathConstants<double>::pi * 329.63 * t));
                buffer.setSample(0, sample, input);
                buffer.setSample(1, sample, input);
            }

            juce::MidiBuffer midi;
            processor.processBlock(buffer, midi);
            for (int sample = 0; sample < frames; ++sample) {
                const float left = buffer.getSample(0, sample);
                const float right = buffer.getSample(1, sample);
                require(std::isfinite(left) && std::isfinite(right),
                        "non-finite output in factory preset " + names[preset].toStdString());
                if (timelineSample + sample < warmupSamples) continue;

                const float mono = 0.5f * (left + right);
                sumSquares += 0.5 * (static_cast<double>(left) * left
                                    + static_cast<double>(right) * right);
                monoSumSquares += static_cast<double>(mono) * mono;
                peak = juce::jmax(peak, std::abs(left), std::abs(right));
                ++measuredSamples;
            }
            timelineSample += frames;
        }

        const float rms = static_cast<float>(std::sqrt(sumSquares / measuredSamples));
        const float monoRms = static_cast<float>(std::sqrt(monoSumSquares / measuredSamples));
        const float monoRetention = monoRms / juce::jmax(1.0e-9f, rms);
        quietestRms = juce::jmin(quietestRms, rms);
        loudestRms = juce::jmax(loudestRms, rms);
        std::cout << "preset " << preset << " " << names[preset]
                  << ": rms=" << rms << ", peak=" << peak
                  << ", mono=" << monoRetention << std::endl;
        require(rms > 0.015f && rms < 0.40f,
                "unsafe RMS for factory preset " + names[preset].toStdString());
        require(peak < 1.25f, "unsafe peak for factory preset " + names[preset].toStdString());
        require(monoRetention > 0.45f,
                "poor mono compatibility for factory preset " + names[preset].toStdString());
    }
    require(loudestRms / quietestRms < 1.55f,
            "factory preset loudness spread is too large");
}
}  // namespace

int main() {
    try {
        juce::ScopedJuceInitialiser_GUI initialiseJuce;
        runStereoTransportTest();
        runMonoSmokeTest();
        runStateMigrationTest();
        runProgramTrackingTest();
        runRepeatedStateRestorationTest();
        runComparisonStateTest();
        runFactoryPresetAudioTest();
        std::cout << "juce_plugin_smoke_test passed" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "juce_plugin_smoke_test failed: " << e.what() << std::endl;
        return 1;
    }
}
