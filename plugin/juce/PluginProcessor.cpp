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
};

constexpr FactoryPreset kFactoryPresets[] = {
    { "Classic Uni-Vibe", VibeVoicing::ClassicChorus, VibeQualityMode::Standard, 0.78f, 0.34f, 0.48f, 0.85f, 1.65f, 0.62f, -0.10f, 0.00f },
    { "Shin-ei Dark", VibeVoicing::VintageUniVibeChorus, VibeQualityMode::High, 0.84f, 0.38f, 0.56f, 0.92f, 1.95f, 0.58f, -0.26f, 0.02f },
    { "Deja Lead", VibeVoicing::TrowerLead, VibeQualityMode::High, 0.76f, 0.34f, 0.50f, 1.35f, 1.85f, 0.64f, -0.10f, 0.00f },
    { "Voodoo Wide", VibeVoicing::WideStereoDream, VibeQualityMode::High, 0.72f, 0.24f, 0.62f, 0.78f, 1.55f, 1.12f, 0.02f, 0.01f },
    { "Modern Hi-Fi", VibeVoicing::ModernHiFiPhaseVibe, VibeQualityMode::High, 0.62f, 0.18f, 0.48f, 1.25f, 0.95f, 0.90f, 0.12f, 0.00f },
    { "Classic Vibrato", VibeVoicing::ClassicVibrato, VibeQualityMode::Standard, 0.72f, 0.25f, 1.00f, 1.10f, 1.45f, 0.55f, -0.12f, 0.00f }
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
    applyFactoryPreset(0);
}

Pico2VibeAudioProcessor::~Pico2VibeAudioProcessor() = default;

void Pico2VibeAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    juce::ignoreUnused(samplesPerBlock);
    const float safeSampleRate = static_cast<float>(sampleRate > 1000.0 ? sampleRate : kDefaultVibeSampleRateHz);
    dsp->vibe.prepare(safeSampleRate);
    dsp->outputConditioner.reset(safeSampleRate, 0xC001C0DEu);
    bypassSmoothCoeff = static_cast<float>(1.0 - std::exp(-1.0 / (0.020 * safeSampleRate)));
    bypassEffectLevel = getBoolParam(kBypassParamId) ? 0.0f : 1.0f;
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

    syncParametersToDsp();

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

        dsp->vibe.out(dsp->inL.data(), dsp->inR.data());

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

int Pico2VibeAudioProcessor::getNumPrograms() { return (int)std::size(kFactoryPresets); }
int Pico2VibeAudioProcessor::getCurrentProgram() { return currentProgram; }
void Pico2VibeAudioProcessor::setCurrentProgram(int index) { applyFactoryPreset(index); }

const juce::String Pico2VibeAudioProcessor::getProgramName(int index) {
    if (index < 0 || index >= getNumPrograms()) return {};
    return kFactoryPresets[index].name;
}

void Pico2VibeAudioProcessor::changeProgramName(int index, const juce::String& newName) {
    juce::ignoreUnused(index, newName);
}

void Pico2VibeAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    if (auto state = parameters.copyState(); state.isValid()) {
        std::unique_ptr<juce::XmlElement> xml(state.createXml());
        xml->setAttribute("program", currentProgram);
        copyXmlToBinary(*xml, destData);
    }
}

void Pico2VibeAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName(parameters.state.getType())) {
        currentProgram = juce::jlimit(0, getNumPrograms() - 1, xml->getIntAttribute("program", currentProgram));
        parameters.replaceState(juce::ValueTree::fromXml(*xml));
        syncParametersToDsp();
    }
}

Pico2VibeAudioProcessor::ValueTreeState::ParameterLayout Pico2VibeAudioProcessor::createParameterLayout() {
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(kVoicingParamId, 1), "Voicing", voicingChoices(), 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(kQualityParamId, 1), "Quality", qualityChoices(), 1));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(kBypassParamId, 1), "Bypass", false));

    for (uint32_t i = 0; i < vibe_param_count(); ++i) {
        const auto id = paramIdFromIndex(i);
        const auto meta = vibe_param_metadata(id);
        const auto spec = vibe_param_spec(id);
        if ((meta.flags & VibeParamFlagBoolean) != 0) {
            params.push_back(std::make_unique<juce::AudioParameterBool>(
                juce::ParameterID(meta.stable_name, 1), meta.label, spec.default_value >= 0.5f));
        } else {
            params.push_back(std::make_unique<juce::AudioParameterFloat>(
                juce::ParameterID(meta.stable_name, 1),
                meta.label,
                rangeFor(id),
                spec.default_value,
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

void Pico2VibeAudioProcessor::syncParametersToDsp() {
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

    for (uint32_t i = 0; i < vibe_param_count(); ++i) {
        const auto id = paramIdFromIndex(i);
        const auto meta = vibe_param_metadata(id);
        if ((meta.flags & VibeParamFlagBoolean) != 0) {
            dsp->vibe.set_param(id, getBoolParam(meta.stable_name) ? 1.0f : 0.0f);
        } else {
            dsp->vibe.set_param(id, getFloatParam(meta.stable_name));
        }
    }
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
    currentProgram = juce::jlimit(0, getNumPrograms() - 1, index);
    const auto& preset = kFactoryPresets[currentProgram];
    setParamValue(kVoicingParamId, static_cast<float>(preset.voicing));
    setParamValue(kQualityParamId, static_cast<float>(preset.quality));
    setParamValue("depth", preset.depth);
    setParamValue("feedback", preset.feedback);
    setParamValue("mix", preset.mix);
    setParamValue("lfo_rate_hz", preset.rateHz);
    setParamValue("input_drive", preset.drive);
    setParamValue("stereo_width", preset.width);
    setParamValue("tone_tilt", preset.tone);
    setParamValue("noise_amount", preset.noise);
    setParamValue(kBypassParamId, 0.0f);
    syncParametersToDsp();
}

void Pico2VibeAudioProcessor::setParamValue(const char* id, float value) {
    if (auto* param = parameters.getParameter(id)) {
        param->beginChangeGesture();
        param->setValueNotifyingHost(param->convertTo0to1(value));
        param->endChangeGesture();
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new Pico2VibeAudioProcessor();
}