#include "PluginProcessor.h"
#include "PluginEditor.h"

#include "dsp/vibe_core.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>

namespace {
constexpr const char* kVoicingParamId = "voicing";
constexpr const char* kQualityParamId = "quality";
constexpr const char* kBypassParamId = "bypass";
constexpr const char* kPhaseLockParamId = "phase_lock";
constexpr const char* kStateVersionProperty = "state_version";
constexpr const char* kComparisonSlotProperty = "comparison_slot";
constexpr const char* kComparisonStateAProperty = "comparison_state_a";
constexpr const char* kComparisonStateBProperty = "comparison_state_b";
constexpr int kCurrentStateVersion = 1;
constexpr int64_t kMaxComparisonStateBytes = 128 * 1024;

struct FactoryPreset {
    const char* name;
    VibeVoicing voicing;
    VibeQualityMode quality;
    float depth;
    float feedback;
    float mix;
    float rateHz;
    float drive;
    float width;
    float tone;
    float noise;
    float outputGain;
    float sweepMin;
    float sweepMax;
    float driftAmount;
    float driftRateHz;
    float preHpfHz;
    float satAsymmetry;
    float satOutTrim;
    float lampLag;
};

constexpr FactoryPreset kFactoryPresets[] = {
    // name, voicing, quality, depth, feedback, mix, rate, drive, width, tone, noise,
    // output, sweep min/max, drift amount/rate, HPF, asymmetry, saturation trim, lamp lag
    { "Classic Uni-Vibe", VibeVoicing::ClassicChorus, VibeQualityMode::Standard,
      0.74f, 0.22f, 0.50f, 0.78f, 1.55f, 0.46f, -0.08f, 0.000f,
      0.98f, 0.56f, 0.96f, 0.014f, 0.070f, 24.0f, 0.045f, 0.94f, 1.10f },
    { "Shin-ei Dark", VibeVoicing::VintageUniVibeChorus, VibeQualityMode::High,
      0.80f, 0.31f, 0.54f, 0.88f, 1.82f, 0.42f, -0.22f, 0.006f,
      0.97f, 0.53f, 0.95f, 0.019f, 0.060f, 28.0f, 0.065f, 0.90f, 1.18f },
    { "Deja Lead", VibeVoicing::TrowerLead, VibeQualityMode::High,
      0.73f, 0.30f, 0.47f, 1.32f, 2.15f, 0.42f, -0.06f, 0.000f,
      0.96f, 0.54f, 0.97f, 0.010f, 0.080f, 32.0f, 0.085f, 0.89f, 0.94f },
    { "Voodoo Wide", VibeVoicing::WideStereoDream, VibeQualityMode::High,
      0.66f, 0.17f, 0.56f, 0.64f, 1.28f, 1.00f, 0.04f, 0.000f,
      0.98f, 0.58f, 0.93f, 0.008f, 0.050f, 20.0f, 0.020f, 0.98f, 0.90f },
    { "Modern Hi-Fi", VibeVoicing::ModernHiFiPhaseVibe, VibeQualityMode::High,
      0.56f, 0.12f, 0.42f, 1.08f, 0.92f, 0.78f, 0.08f, 0.000f,
      1.00f, 0.60f, 0.90f, 0.002f, 0.080f, 26.0f, 0.000f, 1.00f, 0.82f },
    { "Classic Vibrato", VibeVoicing::ClassicVibrato, VibeQualityMode::Standard,
      0.62f, 0.16f, 1.00f, 1.05f, 1.35f, 0.35f, -0.10f, 0.000f,
      0.94f, 0.58f, 0.92f, 0.012f, 0.065f, 24.0f, 0.035f, 0.95f, 1.12f },
    { "Hendrix Deep", VibeVoicing::DeepHendrixSwirl, VibeQualityMode::High,
      0.90f, 0.44f, 0.60f, 0.72f, 2.00f, 0.52f, -0.17f, 0.004f,
      0.90f, 0.48f, 1.00f, 0.022f, 0.045f, 28.0f, 0.075f, 0.86f, 1.32f },
    { "Gentle Clean", VibeVoicing::GentleCleanVibe, VibeQualityMode::Standard,
      0.42f, 0.08f, 0.30f, 0.55f, 1.00f, 0.38f, -0.02f, 0.000f,
      1.00f, 0.62f, 0.86f, 0.004f, 0.070f, 18.0f, 0.000f, 1.00f, 0.85f },
    { "Rotary Fast", VibeVoicing::FastRotaryVibe, VibeQualityMode::High,
      0.44f, 0.12f, 0.40f, 4.60f, 1.35f, 0.62f, 0.05f, 0.000f,
      0.98f, 0.60f, 0.88f, 0.003f, 0.100f, 30.0f, 0.015f, 0.98f, 0.55f },
    { "Bass Anchor", VibeVoicing::BassSynthFriendly, VibeQualityMode::High,
      0.48f, 0.06f, 0.30f, 0.72f, 0.95f, 0.30f, -0.05f, 0.000f,
      0.96f, 0.62f, 0.90f, 0.002f, 0.060f, 8.0f, 0.000f, 0.98f, 0.90f },
    { "Lamp Drift", VibeVoicing::LoFiLampDrift, VibeQualityMode::Standard,
      0.66f, 0.23f, 0.52f, 0.58f, 1.85f, 0.48f, -0.14f, 0.012f,
      0.93f, 0.55f, 0.94f, 0.030f, 0.035f, 24.0f, 0.110f, 0.88f, 1.45f },
    { "Psychedelic Slow", VibeVoicing::PsychedelicSlowSweep, VibeQualityMode::High,
      0.90f, 0.46f, 0.62f, 0.16f, 1.65f, 0.58f, -0.18f, 0.004f,
      0.88f, 0.46f, 0.99f, 0.016f, 0.025f, 28.0f, 0.060f, 0.86f, 1.60f }
};

juce::NormalisableRange<float> rangeFor(VibeParamId id) {
    const VibeParamSpec spec = vibe_param_spec(id);
    juce::NormalisableRange<float> range(spec.min_value, spec.max_value);
    const auto meta = vibe_param_metadata(id);
    if ((meta.flags & VibeParamFlagLogScale) != 0 && spec.min_value > 0.0f && spec.max_value > spec.min_value) {
        range.setSkewForCentre(std::sqrt(spec.min_value * spec.max_value));
    }
    return range;
}

juce::String unitSuffix(VibeParamId id) {
    const auto meta = vibe_param_metadata(id);
    if (std::strcmp(meta.unit, "Hz") == 0) return " Hz";
    if (std::strcmp(meta.unit, "BPM") == 0) return " BPM";
    if (std::strcmp(meta.unit, "x") == 0) return "x";
    return {};
}

VibeParamId paramIdFromIndex(uint32_t i) {
    return static_cast<VibeParamId>(i);
}

float pluginDefaultFor(VibeParamId id) {
    const auto& preset = kFactoryPresets[0];
    if (id == VibeParamId::Depth) return preset.depth;
    if (id == VibeParamId::Feedback) return preset.feedback;
    if (id == VibeParamId::Mix) return preset.mix;
    if (id == VibeParamId::LfoRateHz) return preset.rateHz;
    if (id == VibeParamId::InputDrive) return preset.drive;
    if (id == VibeParamId::StereoWidth) return preset.width;
    if (id == VibeParamId::ToneTilt) return preset.tone;
    if (id == VibeParamId::NoiseAmount) return preset.noise;
    if (id == VibeParamId::OutputGain) return preset.outputGain;
    if (id == VibeParamId::SweepMin) return preset.sweepMin;
    if (id == VibeParamId::SweepMax) return preset.sweepMax;
    if (id == VibeParamId::DriftAmount) return preset.driftAmount;
    if (id == VibeParamId::DriftRateHz) return preset.driftRateHz;
    if (id == VibeParamId::PreHpfHz) return preset.preHpfHz;
    if (id == VibeParamId::SatAsymmetry) return preset.satAsymmetry;
    if (id == VibeParamId::SatOutTrim) return preset.satOutTrim;
    if (id == VibeParamId::LampLag) return preset.lampLag;
    return vibe_param_spec(id).default_value;
}

juce::ValueTree addMissingParameterDefaults(
    const juce::ValueTree& incoming,
    juce::AudioProcessorValueTreeState& parameters) {
    auto migrated = incoming.createCopy();
    const auto templateState = parameters.copyState();

    for (const auto& templateChild : templateState) {
        const auto id = templateChild.getProperty("id").toString();
        if (id.isEmpty() || migrated.getChildWithProperty("id", id).isValid()) continue;

        auto defaultChild = templateChild.createCopy();
        if (auto* parameter = parameters.getParameter(id)) {
            defaultChild.setProperty(
                "value", parameter->convertFrom0to1(parameter->getDefaultValue()), nullptr);
        }
        migrated.appendChild(defaultChild, nullptr);
    }

    return migrated;
}

void forceParameterValuesFromState(
    const juce::ValueTree& state,
    juce::AudioProcessorValueTreeState& parameters) {
    for (const auto& child : state) {
        const auto id = child.getProperty("id").toString();
        auto* parameter = parameters.getParameter(id);
        if (parameter == nullptr || !child.hasProperty("value")) continue;

        const float denormalizedValue = static_cast<float>(child.getProperty("value"));
        parameter->setValueNotifyingHost(parameter->convertTo0to1(denormalizedValue));
    }
}

juce::MemoryBlock serializeValueTree(const juce::ValueTree& state) {
    juce::MemoryBlock result;
    if (auto xml = state.createXml()) {
        juce::AudioProcessor::copyXmlToBinary(*xml, result);
    }
    return result;
}

bool decodeComparisonState(
    const juce::var& encodedValue,
    const juce::Identifier& expectedType,
    juce::MemoryBlock& result) {
    const auto encoded = encodedValue.toString();
    const int separator = encoded.indexOfChar('.');
    if (separator <= 0 || separator > 12) return false;

    const int64_t declaredBytes = encoded.substring(0, separator).getLargeIntValue();
    if (declaredBytes <= 0 || declaredBytes > kMaxComparisonStateBytes) return false;
    if (!result.fromBase64Encoding(encoded)
        || static_cast<int64_t>(result.getSize()) != declaredBytes) {
        result.reset();
        return false;
    }

    const auto xml = juce::AudioProcessor::getXmlFromBinary(
        result.getData(), static_cast<int>(result.getSize()));
    if (xml == nullptr || !xml->hasTagName(expectedType)) {
        result.reset();
        return false;
    }
    return true;
}
}  // namespace

struct Pico2VibeAudioProcessor::DspState {
    std::array<float, PERIOD> inL {};
    std::array<float, PERIOD> inR {};
    std::array<float, PERIOD> outL {};
    std::array<float, PERIOD> outR {};
    Vibe vibe { outL.data(), outR.data() };
    VibeOutputConditioner outputConditioner;
};

Pico2VibeAudioProcessor::Pico2VibeAudioProcessor()
    : AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "PARAMETERS", createParameterLayout()),
      dsp(std::make_unique<DspState>()) {
    dsp->vibe.prepare(kDefaultVibeSampleRateHz);
    dsp->vibe.reseed(1u);
    dsp->outputConditioner.reset(kDefaultVibeSampleRateHz, 0xC001C0DEu);
    syncParametersToDsp();
    setProgramTrackingEnabled(true);
    const auto initialState = serializeSoundState();
    comparisonStates[0] = initialState;
    comparisonStates[1] = initialState;
}

Pico2VibeAudioProcessor::~Pico2VibeAudioProcessor() {
    setProgramTrackingEnabled(false);
}

void Pico2VibeAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    juce::ignoreUnused(samplesPerBlock);
    const float safeSampleRate = static_cast<float>(sampleRate > 1000.0 ? sampleRate : kDefaultVibeSampleRateHz);
    dsp->vibe.prepare(safeSampleRate);
    dsp->outputConditioner.reset(safeSampleRate, 0xC001C0DEu);
    bypassSmoothCoeff = static_cast<float>(1.0 - std::exp(-1.0 / (0.020 * safeSampleRate)));
    bypassEffectLevel = getBoolParam(kBypassParamId) ? 0.0f : 1.0f;
    hostTempoBpm.store(0.0f, std::memory_order_relaxed);
    transportPhase.store(-1.0f, std::memory_order_relaxed);
    syncParametersToDsp();
}

void Pico2VibeAudioProcessor::releaseResources() {
}

bool Pico2VibeAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto& mainIn = layouts.getMainInputChannelSet();
    const auto& mainOut = layouts.getMainOutputChannelSet();
    return mainIn == mainOut
        && (mainOut == juce::AudioChannelSet::mono() || mainOut == juce::AudioChannelSet::stereo());
}

void Pico2VibeAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    juce::ignoreUnused(midiMessages);
    juce::ScopedNoDenormals noDenormals;

    const HostTransportInfo transport = queryHostTransport();
    hostTempoBpm.store(transport.bpm, std::memory_order_relaxed);
    syncParametersToDsp(transport.bpm);
    syncLfoPhaseToHost(transport);

    const int totalFrames = buffer.getNumSamples();
    const int channels = buffer.getNumChannels();
    const float bypassTarget = getBoolParam(kBypassParamId) ? 0.0f : 1.0f;
    float blockPeakL = 0.0f;
    float blockPeakR = 0.0f;

    int pos = 0;
    while (pos < totalFrames) {
        const int n = juce::jmin<int>(PERIOD, totalFrames - pos);
        std::fill(dsp->inL.begin(), dsp->inL.end(), 0.0f);
        std::fill(dsp->inR.begin(), dsp->inR.end(), 0.0f);

        const float* srcL = buffer.getReadPointer(0, pos);
        const float* srcR = (channels > 1) ? buffer.getReadPointer(1, pos) : srcL;
        for (int i = 0; i < n; ++i) {
            dsp->inL[(size_t)i] = srcL[i];
            dsp->inR[(size_t)i] = srcR[i];
        }

        dsp->vibe.out(dsp->inL.data(), dsp->inR.data(), n);

        float* dstL = buffer.getWritePointer(0, pos);
        float* dstR = (channels > 1) ? buffer.getWritePointer(1, pos) : nullptr;
        for (int i = 0; i < n; ++i) {
            float wetL = dsp->outL[(size_t)i];
            float wetR = dsp->outR[(size_t)i];
            dsp->outputConditioner.process_frame(wetL, wetR, &wetL, &wetR);
            bypassEffectLevel += bypassSmoothCoeff * (bypassTarget - bypassEffectLevel);
            const float dryL = dsp->inL[(size_t)i];
            const float dryR = dsp->inR[(size_t)i];
            const float mixedL = dryL + bypassEffectLevel * (wetL - dryL);
            const float mixedR = dryR + bypassEffectLevel * (wetR - dryR);
            dstL[i] = mixedL;
            if (dstR != nullptr) {
                dstR[i] = mixedR;
            }
            blockPeakL = juce::jmax(blockPeakL, std::abs(mixedL));
            blockPeakR = juce::jmax(blockPeakR, std::abs(mixedR));
        }
        pos += n;
    }

    outputMeterLeft.store(juce::jlimit(0.0f, 1.0f, blockPeakL), std::memory_order_relaxed);
    outputMeterRight.store(juce::jlimit(0.0f, 1.0f, blockPeakR), std::memory_order_relaxed);
}

juce::AudioProcessorEditor* Pico2VibeAudioProcessor::createEditor() {
    return new Pico2VibeAudioProcessorEditor(*this);
}

bool Pico2VibeAudioProcessor::hasEditor() const { return true; }
const juce::String Pico2VibeAudioProcessor::getName() const { return JucePlugin_Name; }
bool Pico2VibeAudioProcessor::acceptsMidi() const { return false; }
bool Pico2VibeAudioProcessor::producesMidi() const { return false; }
bool Pico2VibeAudioProcessor::isMidiEffect() const { return false; }
double Pico2VibeAudioProcessor::getTailLengthSeconds() const { return 0.05; }

juce::AudioProcessorParameter* Pico2VibeAudioProcessor::getBypassParameter() const {
    return parameters.getParameter(kBypassParamId);
}

int Pico2VibeAudioProcessor::getNumPrograms() { return customProgramIndex() + 1; }
int Pico2VibeAudioProcessor::getCurrentProgram() { return currentProgram.load(std::memory_order_relaxed); }
void Pico2VibeAudioProcessor::setCurrentProgram(int index) { applyFactoryPreset(index); }

void Pico2VibeAudioProcessor::selectProgramFromEditor(int index) {
    setCurrentProgram(index);
    updateHostDisplay(juce::AudioProcessorListener::ChangeDetails {}
                          .withProgramChanged(true)
                          .withNonParameterStateChanged(true));
}

const juce::String Pico2VibeAudioProcessor::getProgramName(int index) {
    if (index == customProgramIndex()) return "Custom";
    if (index < 0 || index >= customProgramIndex()) return {};
    return kFactoryPresets[index].name;
}

void Pico2VibeAudioProcessor::changeProgramName(int index, const juce::String& newName) {
    juce::ignoreUnused(index, newName);
}

void Pico2VibeAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    if (auto state = createSoundState(); state.isValid()) {
        const int activeSlot = currentComparisonSlot.load(std::memory_order_relaxed);
        const auto currentSound = serializeValueTree(state);
        std::array<juce::MemoryBlock, 2> slots;
        {
            const juce::ScopedLock lock(comparisonStateLock);
            comparisonStates[(size_t)activeSlot] = currentSound;
            slots = comparisonStates;
        }
        state.setProperty(kComparisonSlotProperty, activeSlot, nullptr);
        state.setProperty(kComparisonStateAProperty, slots[0].toBase64Encoding(), nullptr);
        state.setProperty(kComparisonStateBProperty, slots[1].toBase64Encoding(), nullptr);
        std::unique_ptr<juce::XmlElement> xml(state.createXml());
        copyXmlToBinary(*xml, destData);
    }
}

void Pico2VibeAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName(parameters.state.getType())) {
        auto restoredState = juce::ValueTree::fromXml(*xml);
        const int activeSlot = juce::jlimit(
            0, 1, static_cast<int>(restoredState.getProperty(kComparisonSlotProperty, 0)));
        std::array<juce::MemoryBlock, 2> slots;
        const bool hasSlotA = decodeComparisonState(
            restoredState.getProperty(kComparisonStateAProperty), parameters.state.getType(), slots[0]);
        const bool hasSlotB = decodeComparisonState(
            restoredState.getProperty(kComparisonStateBProperty), parameters.state.getType(), slots[1]);
        restoredState.removeProperty(kComparisonSlotProperty, nullptr);
        restoredState.removeProperty(kComparisonStateAProperty, nullptr);
        restoredState.removeProperty(kComparisonStateBProperty, nullptr);

        if (!restoreSoundState(restoredState, false)) return;

        const auto restoredSound = serializeSoundState();
        {
            const juce::ScopedLock lock(comparisonStateLock);
            comparisonStates[0] = hasSlotA ? slots[0] : restoredSound;
            comparisonStates[1] = hasSlotB ? slots[1] : restoredSound;
        }
        currentComparisonSlot.store(activeSlot, std::memory_order_relaxed);
    }
}

juce::ValueTree Pico2VibeAudioProcessor::createSoundState() {
    auto state = parameters.copyState();
    state.removeProperty(kComparisonSlotProperty, nullptr);
    state.removeProperty(kComparisonStateAProperty, nullptr);
    state.removeProperty(kComparisonStateBProperty, nullptr);
    state.setProperty(kStateVersionProperty, kCurrentStateVersion, nullptr);
    state.setProperty("program", currentProgram.load(std::memory_order_relaxed), nullptr);
    return state;
}

juce::MemoryBlock Pico2VibeAudioProcessor::serializeSoundState() {
    return serializeValueTree(createSoundState());
}

bool Pico2VibeAudioProcessor::restoreSoundState(
    const juce::ValueTree& state,
    bool preserveBypass) {
    if (!state.isValid() || state.getType() != parameters.state.getType()) return false;

    const float bypass = getFloatParam(kBypassParamId);
    const int restoredProgram = juce::jlimit(
        0, getNumPrograms() - 1,
        static_cast<int>(state.getProperty(
            "program", currentProgram.load(std::memory_order_relaxed))));
    auto restoredState = addMissingParameterDefaults(state, parameters);
    restoredState.setProperty(kStateVersionProperty, kCurrentStateVersion, nullptr);
    restoredState.setProperty("program", restoredProgram, nullptr);
    suppressProgramTracking.store(true, std::memory_order_release);
    parameters.replaceState(restoredState);
    forceParameterValuesFromState(restoredState, parameters);
    if (preserveBypass) setParamValue(kBypassParamId, bypass);
    currentProgram.store(restoredProgram, std::memory_order_relaxed);
    suppressProgramTracking.store(false, std::memory_order_release);
    currentVoicing = -1;
    currentQuality = -1;
    syncParametersToDsp();
    return true;
}

Pico2VibeAudioProcessor::ValueTreeState::ParameterLayout Pico2VibeAudioProcessor::createParameterLayout() {
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(kVoicingParamId, 1), "Voicing", voicingChoices(), 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(kQualityParamId, 1), "Quality", qualityChoices(), 1));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(kBypassParamId, 1), "Bypass", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(kPhaseLockParamId, 1), "Transport Phase Lock", true));

    for (uint32_t i = 0; i < vibe_param_count(); ++i) {
        const auto id = paramIdFromIndex(i);
        const auto meta = vibe_param_metadata(id);
        const float defaultValue = pluginDefaultFor(id);
        if ((meta.flags & VibeParamFlagBoolean) != 0) {
            params.push_back(std::make_unique<juce::AudioParameterBool>(
                juce::ParameterID(meta.stable_name, 1), meta.label, defaultValue >= 0.5f));
        } else {
            params.push_back(std::make_unique<juce::AudioParameterFloat>(
                juce::ParameterID(meta.stable_name, 1),
                meta.label,
                rangeFor(id),
                defaultValue,
                unitSuffix(id),
                juce::AudioProcessorParameter::genericParameter,
                nullptr,
                nullptr));
        }
    }

    return { params.begin(), params.end() };
}

juce::StringArray Pico2VibeAudioProcessor::factoryPresetNames() {
    juce::StringArray names;
    for (const auto& preset : kFactoryPresets) names.add(preset.name);
    return names;
}

int Pico2VibeAudioProcessor::customProgramIndex() {
    return static_cast<int>(std::size(kFactoryPresets));
}

juce::StringArray Pico2VibeAudioProcessor::voicingChoices() {
    return { "Classic Chorus", "Classic Vibrato", "Deep Throb", "Modern Wide", "Vintage Uni-Vibe Chorus",
             "Deep Hendrix Swirl", "Trower Lead", "Gentle Clean Vibe", "Wide Stereo Dream", "Vintage Vibrato",
             "Shallow Always-On", "Psychedelic Slow Sweep", "Fast Rotary-ish Vibe", "Bass/Synth Friendly",
             "Lo-Fi Lamp Drift", "Modern Hi-Fi Phase Vibe" };
}

juce::StringArray Pico2VibeAudioProcessor::qualityChoices() {
    return { "Eco", "Standard", "High" };
}

float Pico2VibeAudioProcessor::getOutputMeterLeft() const {
    return outputMeterLeft.load(std::memory_order_relaxed);
}

float Pico2VibeAudioProcessor::getOutputMeterRight() const {
    return outputMeterRight.load(std::memory_order_relaxed);
}

float Pico2VibeAudioProcessor::getHostTempoBpm() const {
    return hostTempoBpm.load(std::memory_order_relaxed);
}

float Pico2VibeAudioProcessor::getTransportPhase() const {
    return transportPhase.load(std::memory_order_relaxed);
}

int Pico2VibeAudioProcessor::getComparisonSlot() const {
    return currentComparisonSlot.load(std::memory_order_relaxed);
}

void Pico2VibeAudioProcessor::selectComparisonSlot(int slot) {
    const int selectedSlot = juce::jlimit(0, 1, slot);
    const int previousSlot = currentComparisonSlot.load(std::memory_order_relaxed);
    if (selectedSlot == previousSlot) return;

    const auto currentSound = serializeSoundState();
    juce::MemoryBlock targetSound;
    {
        const juce::ScopedLock lock(comparisonStateLock);
        comparisonStates[(size_t)previousSlot] = currentSound;
        targetSound = comparisonStates[(size_t)selectedSlot];
    }

    auto xml = getXmlFromBinary(targetSound.getData(), static_cast<int>(targetSound.getSize()));
    if (xml == nullptr) return;
    if (!restoreSoundState(juce::ValueTree::fromXml(*xml), true)) return;

    currentComparisonSlot.store(selectedSlot, std::memory_order_relaxed);
    updateHostDisplay(juce::AudioProcessorListener::ChangeDetails {}
                          .withProgramChanged(true)
                          .withNonParameterStateChanged(true));
}

void Pico2VibeAudioProcessor::syncParametersToDsp(float blockHostTempoBpm) {
    const int nextVoicing = juce::jlimit(0, (int)kVibeVoicingCount - 1, getChoiceParam(kVoicingParamId, 0));
    if (nextVoicing != currentVoicing) {
        currentVoicing = nextVoicing;
        dsp->vibe.set_voicing(static_cast<VibeVoicing>(currentVoicing));
    }

    const int nextQuality = juce::jlimit(0, 2, getChoiceParam(kQualityParamId, 1));
    if (nextQuality != currentQuality) {
        currentQuality = nextQuality;
        dsp->vibe.set_quality_mode(static_cast<VibeQualityMode>(currentQuality));
    }

    const bool useHostTempo = getBoolParam("tempo_sync") && blockHostTempoBpm > 0.0f;
    for (uint32_t i = 0; i < vibe_param_count(); ++i) {
        const auto id = paramIdFromIndex(i);
        const auto meta = vibe_param_metadata(id);
        if ((meta.flags & VibeParamFlagBoolean) != 0) {
            dsp->vibe.set_param(id, getBoolParam(meta.stable_name) ? 1.0f : 0.0f);
        } else if (id == VibeParamId::TempoBpm && useHostTempo) {
            dsp->vibe.set_param(id, blockHostTempoBpm);
        } else {
            dsp->vibe.set_param(id, getFloatParam(meta.stable_name));
        }
    }
}

Pico2VibeAudioProcessor::HostTransportInfo Pico2VibeAudioProcessor::queryHostTransport() const {
    HostTransportInfo result;
    const auto* hostPlayHead = getPlayHead();
    if (hostPlayHead == nullptr) return result;

    const auto position = hostPlayHead->getPosition();
    if (!position.hasValue()) return result;

    const auto bpm = position->getBpm();
    if (bpm.hasValue() && std::isfinite(*bpm) && *bpm > 0.0) {
        result.bpm = juce::jlimit(30.0f, 300.0f, static_cast<float>(*bpm));
    }

    const auto ppq = position->getPpqPosition();
    if (ppq.hasValue() && std::isfinite(*ppq)) {
        result.ppq = *ppq;
        result.hasPpq = true;
    }
    result.isPlaying = position->getIsPlaying();
    return result;
}

void Pico2VibeAudioProcessor::syncLfoPhaseToHost(const HostTransportInfo& transport) {
    transportPhase.store(-1.0f, std::memory_order_relaxed);
    if (!getBoolParam("tempo_sync") || !getBoolParam(kPhaseLockParamId)
        || transport.bpm <= 0.0f || !transport.hasPpq || !transport.isPlaying) {
        return;
    }

    const double division = juce::jlimit(0.25, 16.0, static_cast<double>(getFloatParam("tempo_division_beats")));
    double phase = std::fmod(transport.ppq / division, 1.0);
    if (phase < 0.0) phase += 1.0;
    const float normalizedPhase = static_cast<float>(phase);
    dsp->vibe.set_lfo_phase(normalizedPhase);
    transportPhase.store(normalizedPhase, std::memory_order_relaxed);
}

float Pico2VibeAudioProcessor::getFloatParam(const char* id) const {
    const auto* value = parameters.getRawParameterValue(id);
    return value != nullptr ? value->load() : 0.0f;
}

bool Pico2VibeAudioProcessor::getBoolParam(const char* id) const {
    return getFloatParam(id) >= 0.5f;
}

int Pico2VibeAudioProcessor::getChoiceParam(const char* id, int fallback) const {
    const auto* value = parameters.getRawParameterValue(id);
    return value != nullptr ? juce::roundToInt(value->load()) : fallback;
}

void Pico2VibeAudioProcessor::applyFactoryPreset(int index) {
    const int selectedProgram = juce::jlimit(0, getNumPrograms() - 1, index);
    if (selectedProgram == customProgramIndex()) {
        currentProgram.store(selectedProgram, std::memory_order_relaxed);
        return;
    }

    suppressProgramTracking.store(true, std::memory_order_release);
    const auto& preset = kFactoryPresets[selectedProgram];
    setParamValue(kVoicingParamId, static_cast<float>(preset.voicing));
    setParamValue(kQualityParamId, static_cast<float>(preset.quality));
    setParamValue(kPhaseLockParamId, 1.0f);
    for (uint32_t i = 0; i < vibe_param_count(); ++i) {
        const auto id = paramIdFromIndex(i);
        setParamValue(vibe_param_metadata(id).stable_name, pluginDefaultFor(id));
    }
    setParamValue("depth", preset.depth);
    setParamValue("feedback", preset.feedback);
    setParamValue("mix", preset.mix);
    setParamValue("lfo_rate_hz", preset.rateHz);
    setParamValue("input_drive", preset.drive);
    setParamValue("stereo_width", preset.width);
    setParamValue("tone_tilt", preset.tone);
    setParamValue("noise_amount", preset.noise);
    setParamValue("output_gain", preset.outputGain);
    setParamValue("sweep_min", preset.sweepMin);
    setParamValue("sweep_max", preset.sweepMax);
    setParamValue("drift_amount", preset.driftAmount);
    setParamValue("drift_rate_hz", preset.driftRateHz);
    setParamValue("pre_hpf_hz", preset.preHpfHz);
    setParamValue("sat_asymmetry", preset.satAsymmetry);
    setParamValue("sat_out_trim", preset.satOutTrim);
    setParamValue("lamp_lag", preset.lampLag);
    setParamValue(kBypassParamId, 0.0f);
    currentProgram.store(selectedProgram, std::memory_order_relaxed);
    suppressProgramTracking.store(false, std::memory_order_release);
    syncParametersToDsp();
}

void Pico2VibeAudioProcessor::setParamValue(const char* id, float value) {
    if (auto* param = parameters.getParameter(id)) {
        param->beginChangeGesture();
        param->setValueNotifyingHost(param->convertTo0to1(value));
        param->endChangeGesture();
    }
}

void Pico2VibeAudioProcessor::setProgramTrackingEnabled(bool enabled) {
    const auto updateListener = [this, enabled](const char* id) {
        if (enabled) parameters.addParameterListener(id, this);
        else parameters.removeParameterListener(id, this);
    };

    updateListener(kVoicingParamId);
    updateListener(kQualityParamId);
    updateListener(kPhaseLockParamId);
    for (uint32_t i = 0; i < vibe_param_count(); ++i) {
        updateListener(vibe_param_metadata(paramIdFromIndex(i)).stable_name);
    }
}

void Pico2VibeAudioProcessor::parameterChanged(const juce::String& parameterID, float newValue) {
    juce::ignoreUnused(parameterID, newValue);
    if (suppressProgramTracking.load(std::memory_order_acquire)) return;

    const int custom = customProgramIndex();
    currentProgram.store(custom, std::memory_order_relaxed);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new Pico2VibeAudioProcessor();
}
