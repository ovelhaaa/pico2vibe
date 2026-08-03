#include "PluginProcessor.h"

#include <cmath>
#include <iostream>
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
    require(sync != nullptr && sync->load() >= 0.5f, "tempo sync did not survive state restore");
    require(phaseLock != nullptr && phaseLock->load() < 0.5f, "phase lock did not survive state restore");
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
}  // namespace

int main() {
    try {
        juce::ScopedJuceInitialiser_GUI initialiseJuce;
        runStereoTransportTest();
        runMonoSmokeTest();
        std::cout << "juce_plugin_smoke_test passed" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "juce_plugin_smoke_test failed: " << e.what() << std::endl;
        return 1;
    }
}
