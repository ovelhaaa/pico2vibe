#include <cstdint>
#include <cstring>
#include <memory>

#include "dsp/vibe_core.hpp"

extern "C" {

struct VibeHandle {
    float out_l[PERIOD]{};
    float out_r[PERIOD]{};
    Vibe engine{out_l, out_r};
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
uint32_t vibe_get_block_size() { return PERIOD; }
uint32_t vibe_get_param_count() { return static_cast<uint32_t>(VibeParamId::SatOutTrim) + 1u; }

const char* vibe_get_param_name(uint32_t id) { return param_name(static_cast<VibeParamId>(id)); }
float vibe_get_param_min(uint32_t id) { return vibe_param_spec(static_cast<VibeParamId>(id)).min_value; }
float vibe_get_param_max(uint32_t id) { return vibe_param_spec(static_cast<VibeParamId>(id)).max_value; }
float vibe_get_param_default(uint32_t id) { return vibe_param_spec(static_cast<VibeParamId>(id)).default_value; }
float vibe_get_param(VibeHandle* h, uint32_t id) { return h->engine.get_param(static_cast<VibeParamId>(id)); }
void vibe_set_param(VibeHandle* h, uint32_t id, float v) { h->engine.set_param(static_cast<VibeParamId>(id), v); }

uint32_t vibe_get_voicing_count() { return 4u; }
const char* vibe_get_voicing_name(uint32_t id) {
    switch (static_cast<VibeVoicing>(id)) {
        case VibeVoicing::ClassicChorus: return "Classic Chorus";
        case VibeVoicing::ClassicVibrato: return "Classic Vibrato";
        case VibeVoicing::DeepThrob: return "Deep Throb";
        case VibeVoicing::ModernWide: return "Modern Wide";
        default: return "Unknown";
    }
}
void vibe_set_voicing(VibeHandle* h, uint32_t id) { h->engine.set_voicing(static_cast<VibeVoicing>(id)); }

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
      std::memcpy(out_l + pos, h->out_l, n * sizeof(float));
      std::memcpy(out_r + pos, h->out_r, n * sizeof(float));
      pos += n;
    }
}

}
