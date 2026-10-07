#include "PluginProcessor.h"
#include "PluginEditor.h"

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
    requireNear(getParameterValue(source, "depth"), 0.76f, 1.0e-5f,
                "initial depth does not match the default factory preset");
    requireNear(getParameterDefault(source, "depth"), 0.76f, 1.0e-5f,
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

void runMalformedNumericStateTest() {
    Pico2VibeAudioProcessor processor;
    auto tree = processor.parameters.copyState();
    tree.getChildWithProperty("id", "depth").setProperty("value", std::numeric_limits<double>::infinity(), nullptr);
    tree.getChildWithProperty("id", "output_gain").setProperty("value", 999.0f, nullptr);
    tree.appendChild(juce::ValueTree("PARAM"), nullptr); // Unknown incomplete child is ignored.
    juce::MemoryBlock state;
    const auto xml = tree.createXml();
    juce::AudioProcessor::copyXmlToBinary(*xml, state);
    processor.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    require(std::isfinite(getParameterValue(processor, "depth")), "malformed state retained non-finite depth");
    require(getParameterValue(processor, "output_gain") <= 2.0f, "state did not clamp output gain");
    processor.prepareToPlay(kSampleRate, 33);
    juce::AudioBuffer<float> buffer(2, 33);
    processAt(processor, buffer, 0, "malformed numeric recovery");
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
    requireNear(getParameterValue(processor, "depth"), 0.82f, 1.0e-5f,
                "factory program did not restore its depth");

    setParameter(processor, "output_gain", 1.47f);
    require(processor.getCurrentProgram() == custom, "secondary parameter edit did not select Custom");
    processor.setCurrentProgram(2);
    requireNear(getParameterValue(processor, "output_gain"), 1.09f, 1.0e-5f,
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

void runPresetGlobalControlsTest() {
    Pico2VibeAudioProcessor processor;
    for (int quality = 0; quality < 3; ++quality) {
        for (int bypass = 0; bypass < 2; ++bypass) {
            setParameter(processor, "quality", static_cast<float>(quality));
            setParameter(processor, "bypass", static_cast<float>(bypass));
            for (int preset = 0; preset < Pico2VibeAudioProcessor::customProgramIndex(); ++preset) {
                processor.setCurrentProgram(preset);
                requireNear(getParameterValue(processor, "bypass"), static_cast<float>(bypass), 0.0f,
                            "preset changed global bypass");
                requireNear(getParameterValue(processor, "quality"), static_cast<float>(quality), 0.0f,
                            "preset changed global Quality");
                setParameter(processor, "quality", static_cast<float>((quality + 1) % 3));
                require(processor.getCurrentProgram() == preset, "Quality edit selected Custom");
                setParameter(processor, "quality", static_cast<float>(quality));
            }
            juce::MemoryBlock saved;
            processor.getStateInformation(saved);
            Pico2VibeAudioProcessor restored;
            setParameter(restored, "quality", static_cast<float>((quality + 1) % 3));
            setParameter(restored, "bypass", static_cast<float>(1 - bypass));
            restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
            requireNear(getParameterValue(restored, "quality"), static_cast<float>(quality), 0.0f,
                        "project did not restore Quality");
            requireNear(getParameterValue(restored, "bypass"), static_cast<float>(bypass), 0.0f,
                        "project did not restore bypass");
        }
    }
}

void runProductProcessingMatrix() {
    for (double sampleRate : { 44100.0, 48000.0, 96000.0, 192000.0 }) {
        for (int channels : { 1, 2 }) {
            for (int quality = 0; quality < 3; ++quality) {
                Pico2VibeAudioProcessor processor;
                juce::AudioProcessor::BusesLayout layout;
                const auto channelSet = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
                layout.inputBuses.add(channelSet);
                layout.outputBuses.add(channelSet);
                require(processor.setBusesLayout(layout), "matrix bus layout rejected");
                setParameter(processor, "quality", static_cast<float>(quality));
                processor.prepareToPlay(sampleRate, 513);
                int64_t timeline = 0;
                for (int preset = 0; preset < Pico2VibeAudioProcessor::customProgramIndex(); ++preset) {
                    processor.setCurrentProgram(preset);
                    float presetPeak = 0.0f;
                    for (int frames : { 1, 7, 31, 32, 33, 127, 513, 2049 }) {
                        // Exercise block-boundary host automation while optical state runs.
                        setParameter(processor, "depth", (frames % 11) / 10.0f);
                        setParameter(processor, "mix", (frames % 7) / 6.0f);
                        juce::AudioBuffer<float> buffer(channels, frames);
                        juce::MidiBuffer midi;
                        for (int ch = 0; ch < channels; ++ch)
                            for (int i = 0; i < frames; ++i)
                                buffer.setSample(ch, i, 0.2f * std::sin(2.0 * juce::MathConstants<double>::pi
                                    * (ch == 0 ? 173.0 : 227.0) * (timeline + i) / sampleRate));
                        processor.processBlock(buffer, midi);
                        for (int ch = 0; ch < channels; ++ch)
                            for (int i = 0; i < frames; ++i)
                                {
                                    const float value = buffer.getSample(ch, i);
                                    require(std::isfinite(value) && std::abs(value) < 1.25f, "matrix non-finite/unsafe output");
                                    presetPeak = std::max(presetPeak, std::abs(value));
                                }
                        timeline += frames;
                    }
                    require(presetPeak > 1.0e-6f, "product matrix preset remained silent");
                }
                juce::AudioBuffer<float> empty(channels, 0);
                juce::MidiBuffer midi;
                processor.processBlock(empty, midi);
            }
        }
    }
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
    setParameter(processor, "quality", 2.0f);
    setParameter(processor, "depth", 0.24f);
    setParameter(processor, "bypass", 1.0f);

    processor.selectComparisonSlot(1);
    require(processor.getComparisonSlot() == 1, "comparison did not switch to slot B");
    requireNear(getParameterValue(processor, "depth"), 0.76f, 1.0e-5f,
                "slot B did not retain its independent initial sound");
    requireNear(getParameterValue(processor, "bypass"), 1.0f, 1.0e-5f,
                "comparison switch did not preserve global bypass");
    requireNear(getParameterValue(processor, "quality"), 2.0f, 0.0f, "A/B changed global Quality");
    setParameter(processor, "quality", 0.0f);
    setParameter(processor, "depth", 0.66f);

    processor.selectComparisonSlot(0);
    requireNear(getParameterValue(processor, "depth"), 0.24f, 1.0e-5f,
                "slot A sound was not recalled");
    requireNear(getParameterValue(processor, "quality"), 0.0f, 0.0f, "slot A restored stale Quality");
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
    requireNear(getParameterValue(restored, "quality"), 0.0f, 0.0f, "A/B project Quality not restored");
    restored.selectComparisonSlot(0);
    requireNear(getParameterValue(restored, "quality"), 0.0f, 0.0f, "legacy slot Quality was not global");
    requireNear(getParameterValue(restored, "depth"), 0.24f, 1.0e-5f,
                "stored slot A did not survive project restore");

    auto malformedXml = juce::AudioProcessor::getXmlFromBinary(
        projectState.getData(), static_cast<int>(projectState.getSize()));
    require(malformedXml != nullptr, "A/B project state did not decode as XML");
    // Migrate a v1 slot containing stale global controls from the old implementation.
    juce::MemoryBlock legacySlot;
    require(legacySlot.fromBase64Encoding(malformedXml->getStringAttribute("comparison_state_a")),
            "slot A did not decode");
    auto legacySlotXml = juce::AudioProcessor::getXmlFromBinary(
        legacySlot.getData(), static_cast<int>(legacySlot.getSize()));
    require(legacySlotXml != nullptr, "slot A XML missing");
    auto* qualityChild = legacySlotXml->createNewChildElement("PARAM");
    qualityChild->setAttribute("id", "quality");
    qualityChild->setAttribute("value", 2.0);
    auto* bypassChild = legacySlotXml->createNewChildElement("PARAM");
    bypassChild->setAttribute("id", "bypass");
    bypassChild->setAttribute("value", 0.0);
    juce::AudioProcessor::copyXmlToBinary(*legacySlotXml, legacySlot);
    malformedXml->setAttribute("comparison_state_a", legacySlot.toBase64Encoding());
    juce::MemoryBlock legacyProject;
    juce::AudioProcessor::copyXmlToBinary(*malformedXml, legacyProject);
    Pico2VibeAudioProcessor legacyRestored;
    legacyRestored.setStateInformation(legacyProject.getData(), static_cast<int>(legacyProject.getSize()));
    legacyRestored.selectComparisonSlot(0);
    requireNear(getParameterValue(legacyRestored, "quality"), 0.0f, 0.0f, "old slot changed global Quality");
    requireNear(getParameterValue(legacyRestored, "bypass"), 1.0f, 0.0f, "old slot changed global bypass");

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
        setParameter(processor, "quality", 2.0f); // Calibrated factory loudness uses High explicitly.
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
    require(loudestRms / quietestRms < 1.15f,
            "factory preset loudness spread is too large");
}
juce::Component* findControl(juce::Component& parent, const juce::String& id) {
    if (parent.getComponentID() == id) return &parent;
    for (auto* child : parent.getChildren())
        if (auto* result = findControl(*child, id)) return result;
    return nullptr;
}

void refreshEditor() {
    juce::Thread::sleep(50);
    juce::Timer::callPendingTimersSynchronously();
}

void runEditorTest(const juce::String& snapshotDirectory) {
    Pico2VibeAudioProcessor processor;
    processor.prepareToPlay(kSampleRate, 128);
    Pico2VibeAudioProcessorEditor editor(processor);
    auto slider = [&](const char* id) -> juce::Slider& {
        auto* control = dynamic_cast<juce::Slider*>(findControl(editor, id));
        require(control != nullptr, std::string("editor slider missing: ") + id);
        require(control->getTitle().isNotEmpty(), "slider accessibility title missing");
        return *control;
    };
    auto snapshot = [&](const char* name) {
        if (snapshotDirectory.isEmpty()) return;
        const juce::File directory(snapshotDirectory);
        require(directory.createDirectory().wasOk(), "cannot create snapshot directory");
        auto stream = directory.getChildFile(name).createOutputStream();
        require(stream != nullptr, "cannot write editor snapshot");
        stream->setPosition(0);
        stream->truncate();
        juce::PNGImageFormat png;
        require(png.writeImageToStream(editor.createComponentSnapshot(editor.getLocalBounds()), *stream),
                "cannot encode editor snapshot");
    };
    require(editor.isResizable(), "editor must be resizable");
    require(slider("lfo_rate_hz").isEnabled(), "free rate disabled with sync off");
    require(!slider("tempo_bpm").isEnabled(), "fallback enabled with sync off");
    require(!slider("tempo_division_beats").isEnabled(), "division enabled with sync off");
    require(!findControl(editor, "phase_lock")->isEnabled(), "phase lock enabled with sync off");
    if (snapshotDirectory.isNotEmpty()) {
        // Let JUCE's normal startup splash finish before capturing the design.
        editor.createComponentSnapshot(editor.getLocalBounds());
        for (int frame = 0; frame < 130; ++frame) refreshEditor();
    }
    snapshot("editor-default.png");

    // Exercise every exposed slider in both directions through the real attachments.
    for (const char* id : { "depth", "lfo_rate_hz", "mix", "feedback", "input_drive",
                           "stereo_width", "tone_tilt", "noise_amount", "output_gain",
                           "tempo_bpm", "tempo_division_beats" }) {
        auto* parameter = processor.parameters.getParameter(id);
        const float value = parameter->convertFrom0to1(0.63f);
        setParameter(processor, id, value);
        requireNear((float)slider(id).getValue(), getParameterValue(processor, id), 0.0001f,
                    std::string("host to slider: ") + id);
        slider(id).setValue(parameter->convertFrom0to1(0.31f), juce::sendNotificationSync);
        requireNear(getParameterValue(processor, id), (float)slider(id).getValue(), 0.0001f,
                    std::string("slider to host: ") + id);
    }
    for (const char* id : { "voicing", "quality" }) {
        auto* box = dynamic_cast<juce::ComboBox*>(findControl(editor, id));
        require(box != nullptr, "selector missing");
        box->setSelectedItemIndex(2, juce::sendNotificationSync);
        requireNear(getParameterValue(processor, id), 2.0f, 0.0f, "selector to host");
        setParameter(processor, id, 1.0f);
        require(box->getSelectedItemIndex() == 1, "host to selector");
    }
    for (const char* id : { "tempo_sync", "phase_lock", "bypass" }) {
        auto* button = dynamic_cast<juce::Button*>(findControl(editor, id));
        require(button != nullptr, "parameter button missing");
        setParameter(processor, id, 0.0f);
        require(!button->getToggleState(), "host to button");
        button->setToggleState(true, juce::sendNotificationSync);
        requireNear(getParameterValue(processor, id), 1.0f, 0.0f, "button to host");
    }
    refreshEditor();
    require(!slider("lfo_rate_hz").isEnabled(), "rate enabled with sync on");
    require(slider("tempo_bpm").isEnabled(), "fallback disabled without host tempo");
    require(slider("tempo_division_beats").isEnabled(), "division disabled with sync on");
    require(findControl(editor, "phase_lock")->isEnabled(), "phase lock disabled with sync on");
    snapshot("editor-sync-fallback-bypass.png");

    auto* preset = dynamic_cast<juce::ComboBox*>(findControl(editor, "preset"));
    require(preset != nullptr, "preset selector missing");
    preset->setSelectedItemIndex(3, juce::sendNotificationSync);
    require(processor.getCurrentProgram() == 3, "editor preset recall failed");
    slider("depth").keyPressed(juce::KeyPress(juce::KeyPress::rightKey));
    refreshEditor();
    require(preset->getSelectedItemIndex() == Pico2VibeAudioProcessor::customProgramIndex(),
            "keyboard edit did not select Custom");
    const float slotADepth = getParameterValue(processor, "depth");
    auto* comparisonA = dynamic_cast<juce::Button*>(findControl(editor, "comparison_a"));
    auto* comparisonB = dynamic_cast<juce::Button*>(findControl(editor, "comparison_b"));
    require(comparisonA != nullptr && comparisonB != nullptr, "comparison buttons missing");
    comparisonB->onClick();
    slider("depth").setValue(0.17, juce::sendNotificationSync);
    comparisonA->onClick();
    refreshEditor();
    require(comparisonA->getToggleState() && !comparisonB->getToggleState(), "A/B selection stale");
    requireNear((float)slider("depth").getValue(), slotADepth, 0.0001f, "A/B slider recall failed");
    setParameter(processor, "tempo_sync", 1.0f);

    MockPlayHead playHead;
    processor.setPlayHead(&playHead);
    playHead.set(137.0, 4.0, true);
    juce::AudioBuffer<float> buffer(2, 128);
    const float fallback = getParameterValue(processor, "tempo_bpm");
    processAt(processor, buffer, 0, "editor host tempo");
    refreshEditor();
    require(!slider("tempo_bpm").isEnabled(), "fallback enabled with host tempo");
    requireNear(getParameterValue(processor, "tempo_bpm"), fallback, 0.0f, "UI overwrote fallback BPM");
    auto* sync = dynamic_cast<juce::TextButton*>(findControl(editor, "tempo_sync"));
    require(sync->getButtonText() == "137.0 BPM", "host tempo display missing");
    snapshot("editor-sync-host.png");
    editor.setSize(1260, 765);
    snapshot("editor-large.png");
    editor.setSize(1000, 510); // A host can ignore the resize constraint.
    const auto* depth = findControl(editor, "depth");
    require(editor.getLocalBounds().contains(editor.getLocalArea(depth, depth->getLocalBounds())),
            "scaled control outside editor");
    processor.setPlayHead(nullptr);
    // Reopening must immediately reflect restored APVTS state, before any timer tick.
    Pico2VibeAudioProcessorEditor reopened(processor);
    require(!findControl(reopened, "lfo_rate_hz")->isEnabled(), "reopened editor has stale sync state");
}
}  // namespace

int main(int argc, char** argv) {
    try {
        juce::ScopedJuceInitialiser_GUI initialiseJuce;
        runStereoTransportTest();
        runMonoSmokeTest();
        runStateMigrationTest();
        runMalformedNumericStateTest();
        runProgramTrackingTest();
        runPresetGlobalControlsTest();
        runProductProcessingMatrix();
        runRepeatedStateRestorationTest();
        runComparisonStateTest();
        runFactoryPresetAudioTest();
        runEditorTest(argc > 1 ? juce::String::fromUTF8(argv[1]) : juce::String());
        std::cout << "juce_plugin_smoke_test passed" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "juce_plugin_smoke_test failed: " << e.what() << std::endl;
        return 1;
    }
}
