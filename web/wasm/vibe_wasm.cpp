#include <cstdint>
#include <cstring>
#include <memory>

#include "dsp/vibe_core.hpp"

extern "C" {

struct VibeHandle {
    float out_l[PERIOD]{};
    float out_r[PERIOD]{};
    Vibe engine{out_l, out_r};
    VibeOutputConditioner output_conditioner{};
    bool output_conditioning = false;
};

static const char* param_name(VibeParamId id) {
    switch (id) {
        case VibeParamId::Depth: return "depth";
        case VibeParamId::Feedback: return "feedback";
        case VibeParamId::Mix: return "mix";
        case VibeParamId::InputDrive: return "input_drive";
        case VibeParamId::OutputGain: return "output_gain";
        case VibeParamId::SweepMin: return "sweep_min";
        case VibeParamId::SweepMax: return "sweep_max";
        case VibeParamId::LfoRateHz: return "lfo_rate_hz";
        case VibeParamId::DriftAmount: return "drift_amount";
        case VibeParamId::DriftRateHz: return "drift_rate_hz";
        case VibeParamId::PreHpfHz: return "pre_hpf_hz";
        case VibeParamId::ToneTilt: return "tone_tilt";
        case VibeParamId::SatAsymmetry: return "sat_asymmetry";
        case VibeParamId::SatOutTrim: return "sat_out_trim";
        default: return "unknown";
    }
}

VibeHandle* vibe_create() {
    auto* h = new VibeHandle();
    h->engine.reseed(1);
    h->engine.set_voicing(VibeVoicing::ClassicChorus);
    return h;
}

void vibe_destroy(VibeHandle* h) { delete h; }

uint32_t vibe_get_sample_rate() { return SAMPLE_RATE_HZ; }
float vibe_get_engine_sample_rate(VibeHandle* h) { return h->engine.sample_rate(); }
void vibe_prepare(VibeHandle* h, float sample_rate_hz) { h->engine.prepare(sample_rate_hz); h->output_conditioner.reset(sample_rate_hz, 0xC001C0DEu); }
uint32_t vibe_get_block_size() { return PERIOD; }
uint32_t vibe_get_param_count() { return static_cast<uint32_t>(VibeParamId::SatOutTrim) + 1u; }

const char* vibe_get_param_name(uint32_t id) { return param_name(static_cast<VibeParamId>(id)); }
float vibe_get_param_min(uint32_t id) { return vibe_param_spec(static_cast<VibeParamId>(id)).min_value; }
float vibe_get_param_max(uint32_t id) { return vibe_param_spec(static_cast<VibeParamId>(id)).max_value; }
float vibe_get_param_default(uint32_t id) { return vibe_param_spec(static_cast<VibeParamId>(id)).default_value; }
float vibe_get_param(VibeHandle* h, uint32_t id) { return h->engine.get_param(static_cast<VibeParamId>(id)); }
float vibe_get_param_normalized(VibeHandle* h, uint32_t id) { return h->engine.get_param_normalized(static_cast<VibeParamId>(id)); }
void vibe_set_param(VibeHandle* h, uint32_t id, float v) { h->engine.set_param(static_cast<VibeParamId>(id), v); }
void vibe_set_param_normalized(VibeHandle* h, uint32_t id, float v) { h->engine.set_param_normalized(static_cast<VibeParamId>(id), v); }

uint32_t vibe_get_voicing_count() { return kVibeVoicingCount; }
const char* vibe_get_voicing_name(uint32_t id) {
    switch (static_cast<VibeVoicing>(id)) {
        case VibeVoicing::ClassicChorus: return "Classic Chorus";
        case VibeVoicing::ClassicVibrato: return "Classic Vibrato";
        case VibeVoicing::DeepThrob: return "Deep Throb";
        case VibeVoicing::ModernWide: return "Modern Wide";
        case VibeVoicing::VintageUniVibeChorus: return "Vintage Uni-Vibe Chorus";
        case VibeVoicing::DeepHendrixSwirl: return "Deep Hendrix Swirl";
        case VibeVoicing::TrowerLead: return "Trower Lead";
        case VibeVoicing::GentleCleanVibe: return "Gentle Clean Vibe";
        case VibeVoicing::WideStereoDream: return "Wide Stereo Dream";
        case VibeVoicing::VintageVibrato: return "Vintage Vibrato";
        case VibeVoicing::ShallowAlwaysOn: return "Shallow Always-On";
        case VibeVoicing::PsychedelicSlowSweep: return "Psychedelic Slow Sweep";
        case VibeVoicing::FastRotaryVibe: return "Fast Rotary-ish Vibe";
        case VibeVoicing::BassSynthFriendly: return "Bass/Synth Friendly Vibe";
        case VibeVoicing::LoFiLampDrift: return "Lo-Fi Lamp Drift";
        case VibeVoicing::ModernHiFiPhaseVibe: return "Modern Hi-Fi Phase Vibe";
        default: return "Unknown";
    }
}
void vibe_set_voicing(VibeHandle* h, uint32_t id) { h->engine.set_voicing(static_cast<VibeVoicing>(id)); }

uint32_t vibe_get_quality_mode_count() { return 3u; }
const char* vibe_get_quality_mode_name(uint32_t id) {
    switch (static_cast<VibeQualityMode>(id)) {
        case VibeQualityMode::Eco: return "Eco";
        case VibeQualityMode::High: return "High";
        case VibeQualityMode::Standard:
        default: return "Standard";
    }
}
void vibe_set_quality_mode(VibeHandle* h, uint32_t id) { h->engine.set_quality_mode(static_cast<VibeQualityMode>(id)); }
void vibe_set_output_conditioning(VibeHandle* h, uint32_t enabled) { h->output_conditioning = enabled != 0u; }

void vibe_reset(VibeHandle* h, uint32_t seed) { h->engine.reseed(seed); }

void vibe_process_stereo(VibeHandle* h, const float* in_l, const float* in_r, float* out_l, float* out_r, uint32_t frames) {
    uint32_t pos = 0;
    float block_l[PERIOD]{};
    float block_r[PERIOD]{};
    while (pos < frames) {
      const uint32_t remain = frames - pos;
      const uint32_t n = remain > PERIOD ? PERIOD : remain;
      std::memset(block_l, 0, sizeof(block_l));
      std::memset(block_r, 0, sizeof(block_r));
      std::memcpy(block_l, in_l + pos, n * sizeof(float));
      std::memcpy(block_r, in_r + pos, n * sizeof(float));
      h->engine.out(block_l, block_r);
      if (h->output_conditioning) {
        for (uint32_t i = 0; i < n; ++i) {
          h->output_conditioner.process_frame(h->out_l[i], h->out_r[i], out_l + pos + i, out_r + pos + i);
        }
      } else {
        std::memcpy(out_l + pos, h->out_l, n * sizeof(float));
        std::memcpy(out_r + pos, h->out_r, n * sizeof(float));
      }
      pos += n;
    }
}

}
