#include <stdio.h>
#include <math.h>

#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/sync.h"

#include "i2s_bidir_dma.pio.h"

// ============================================================================
// Hardware / áudio
// ============================================================================

#define PIN_MCLK   21
#define PIN_BCLK   16
#define PIN_LRCLK  17
#define PIN_DOUT   18
#define PIN_DIN    19

#ifndef USER_INTERFACE
#define USER_INTERFACE 1
#endif

#if USER_INTERFACE
#define UI_PIN_ENC_A        2
#define UI_PIN_ENC_B        3
#define UI_PIN_ENC_SW       4
#define UI_PIN_LED_PARAM    6
#define UI_PIN_LED_MODE     7
#define UI_ENCODER_REVERSE  0
#endif

#define SAMPLE_RATE     44100.0f
#define SAMPLE_RATE_HZ  44100u
#define SLOT_BITS       32u
#define CHANNELS        2u
#define PERIOD          32
#define DMA_WORDS_PER_BLOCK   (PERIOD * CHANNELS)

#define DENORMAL_GUARD  1e-18f
#define cSAMPLE_RATE    (1.0f / SAMPLE_RATE)
#define fSAMPLE_RATE    SAMPLE_RATE
#define fPERIOD         ((float)PERIOD)

#define AUDIO_SYS_CLOCK_HZ 135475200u

// Final output conditioning toggles (keep lightweight, easy to bypass if desired).
#ifndef ENABLE_OUTPUT_DC_BLOCKER
#define ENABLE_OUTPUT_DC_BLOCKER 1
#endif

#ifndef ENABLE_OUTPUT_AUTO_HEADROOM
#define ENABLE_OUTPUT_AUTO_HEADROOM 1
#endif

#ifndef ENABLE_OUTPUT_SOFT_LIMITER
#define ENABLE_OUTPUT_SOFT_LIMITER 1
#endif

#ifndef ENABLE_TPDF_DITHER
#define ENABLE_TPDF_DITHER 1
#endif

static inline float fast_soft_clip(float x) {
    return x / (1.0f + fabsf(x));
}

static inline float clampf(float v, float lo, float hi) {
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

static inline float noise_bipolar(uint32_t &state) {
    state = state * 1664525u + 1013904223u;
    const float uni = (float)((state >> 8) & 0x00FFFFFFu) * (1.0f / 16777215.0f);
    return uni * 2.0f - 1.0f;
}

static inline float fast_sqrt01(float v) {
    return sqrtf(clampf(v, 0.0f, 1.0f));
}

static inline float soft_clip_cubic(float x) {
    const float xx = x * x;
    return x * (27.0f + xx) / (27.0f + 9.0f * xx);
}

static inline float lerpf(float a, float b, float t) {
    return a + (b - a) * t;
}

// Keeps low/mid feedback behavior familiar while giving the top of the knob
// more useful sustain range for audible repeats.
static inline float feedback_musical_gain(float knob) {
    const float k = clampf(knob, 0.0f, 0.65f);
    const float normalized = k * (1.0f / 0.65f);
    const float shaped = normalized * normalized * (3.0f - 2.0f * normalized);
    const float boosted = k + 0.10f * shaped * k;
    return clampf(boosted, 0.0f, 0.70f);
}

constexpr float kPi = 3.14159265358979323846f;

struct ParamRamp {
    float value = 0.0f;
    float step = 0.0f;

    void begin(float current, float target, int samples) {
        value = current;
        step = (samples > 0) ? ((target - current) / (float)samples) : 0.0f;
    }

    float tick() {
        const float out = value;
        value += step;
        return out;
    }
};

struct VibeUserParams {
    float depth = 0.85f;
    float feedback = 0.42f;
    float mix = 0.50f;
    float input_drive = 2.3f;
    float output_gain = 1.0f;
    float sweep_min = 0.58f;
    float sweep_max = 0.98f;
    float lfo_rate_hz = 1.20f;
    float drift_amount = 0.018f;
    float drift_rate_hz = 0.08f;
    float pre_hpf_hz = 22.0f;
    float tone_tilt = 0.0f;
    float sat_asymmetry = 0.08f;
    float sat_out_trim = 0.95f;
};

struct VibeTuningParams {
    float lamp_attack_sec = 0.010f;
    float lamp_release_sec = 0.040f;
    float ldr_dark_ohms = 1000000.0f;
    float ldr_curve = 7.6009f;
    float ldr_min_ohms = 3900.0f;
    float ldr_max_ohms = 1000000.0f;
    float emitter_fb_scale = 12500.0f;
    float emitter_fb_min = 0.01f;
    float emitter_fb_max = 2.50f;
    float stage_state_limit = 6.0f;
    float bjt_gain_trim = 0.35f;
    float lfo_shape_smoothing = 0.20f;
    float stereo_phase_offset = 0.25f;
    float control_smoothing_hz = 18.0f;
    float lamp_hysteresis = 0.020f;
    float stage_time_spread = 0.012f;
    float feedback_sat = 0.55f;
    float gain_comp_depth = 0.18f;
    float tilt_hz = 850.0f;
    float pre_hpf_hz_min = 8.0f;
    float pre_hpf_hz_max = 160.0f;
};

enum class LfoShape : uint8_t {
    Sine = 0,
    TriangleSmooth,
};

enum class FeedbackProfile : uint8_t {
    ClassicFeedback = 0,
    ModernFeedback,
};

struct FeedbackProfileCoefs {
    float hp_fc_hz;
    float lp_fc_hz;
    float dry;
    float mid;
    float high;
};

static inline FeedbackProfileCoefs feedback_profile_coefs(FeedbackProfile profile) {
    switch (profile) {
        case FeedbackProfile::ModernFeedback:
            return {520.0f, 1550.0f, 0.78f, 0.40f, 0.18f};
        case FeedbackProfile::ClassicFeedback:
        default:
            return {330.0f, 1320.0f, 0.92f, 0.26f, 0.08f};
    }
}

enum class VibeVoicing : uint8_t {
    ClassicChorus = 0,
    ClassicVibrato,
    DeepThrob,
    ModernWide,
};

struct VibePreset {
    VibeVoicing voicing = VibeVoicing::ClassicChorus;
    VibeUserParams user{};
    VibeTuningParams tuning{};
    bool chorus_mode = true;
    LfoShape lfo_shape = LfoShape::Sine;
    FeedbackProfile feedback_profile = FeedbackProfile::ClassicFeedback;
    bool legacy_saturation = false;
};

struct VibeParams {
    VibeUserParams user;
    VibeTuningParams tuning;
    LfoShape lfo_shape = LfoShape::Sine;
    FeedbackProfile feedback_profile = FeedbackProfile::ClassicFeedback;
    bool legacy_saturation = false;
    VibeVoicing voicing = VibeVoicing::ClassicChorus;
};

enum class VibeParamId : uint8_t {
    Depth = 0,
    Feedback,
    Mix,
    InputDrive,
    OutputGain,
    SweepMin,
    SweepMax,
    LfoRateHz,
    DriftAmount,
    DriftRateHz,
    PreHpfHz,
    ToneTilt,
    SatAsymmetry,
    SatOutTrim,
};

struct VibeParamSpec {
    float min_value;
    float max_value;
    float default_value;
};

static inline VibeParamSpec vibe_param_spec(VibeParamId id) {
    switch (id) {
        case VibeParamId::Depth:      return {0.0f, 1.0f, 0.85f};
        case VibeParamId::Feedback:   return {0.0f, 0.65f, 0.42f};
        case VibeParamId::Mix:        return {0.0f, 1.0f, 0.50f};
        case VibeParamId::InputDrive: return {0.5f, 6.0f, 2.3f};
        case VibeParamId::OutputGain: return {0.25f, 2.0f, 1.0f};
        case VibeParamId::SweepMin:   return {0.0f, 1.0f, 0.58f};
        case VibeParamId::SweepMax:   return {0.0f, 1.0f, 0.98f};
        case VibeParamId::LfoRateHz:  return {0.02f, 12.0f, 1.20f};
        case VibeParamId::DriftAmount:return {0.0f, 0.05f, 0.018f};
        case VibeParamId::DriftRateHz:return {0.005f, 0.5f, 0.08f};
        case VibeParamId::PreHpfHz:   return {8.0f, 160.0f, 22.0f};
        case VibeParamId::ToneTilt:   return {-1.0f, 1.0f, 0.0f};
        case VibeParamId::SatAsymmetry:return {-0.25f, 0.25f, 0.08f};
        case VibeParamId::SatOutTrim: return {0.60f, 1.20f, 0.95f};
        default:                      return {0.0f, 1.0f, 0.0f};
    }
}

static inline float *vibe_param_slot(VibeUserParams &params, VibeParamId id) {
    switch (id) {
        case VibeParamId::Depth:       return &params.depth;
        case VibeParamId::Feedback:    return &params.feedback;
        case VibeParamId::Mix:         return &params.mix;
        case VibeParamId::InputDrive:  return &params.input_drive;
        case VibeParamId::OutputGain:  return &params.output_gain;
        case VibeParamId::SweepMin:    return &params.sweep_min;
        case VibeParamId::SweepMax:    return &params.sweep_max;
        case VibeParamId::LfoRateHz:   return &params.lfo_rate_hz;
        case VibeParamId::DriftAmount: return &params.drift_amount;
        case VibeParamId::DriftRateHz: return &params.drift_rate_hz;
        case VibeParamId::PreHpfHz:    return &params.pre_hpf_hz;
        case VibeParamId::ToneTilt:    return &params.tone_tilt;
        case VibeParamId::SatAsymmetry:return &params.sat_asymmetry;
        case VibeParamId::SatOutTrim:  return &params.sat_out_trim;
        default:                       return nullptr;
    }
}

static inline const float *vibe_param_slot(const VibeUserParams &params, VibeParamId id) {
    switch (id) {
        case VibeParamId::Depth:       return &params.depth;
        case VibeParamId::Feedback:    return &params.feedback;
        case VibeParamId::Mix:         return &params.mix;
        case VibeParamId::InputDrive:  return &params.input_drive;
        case VibeParamId::OutputGain:  return &params.output_gain;
        case VibeParamId::SweepMin:    return &params.sweep_min;
        case VibeParamId::SweepMax:    return &params.sweep_max;
        case VibeParamId::LfoRateHz:   return &params.lfo_rate_hz;
        case VibeParamId::DriftAmount: return &params.drift_amount;
        case VibeParamId::DriftRateHz: return &params.drift_rate_hz;
        case VibeParamId::PreHpfHz:    return &params.pre_hpf_hz;
        case VibeParamId::ToneTilt:    return &params.tone_tilt;
        case VibeParamId::SatAsymmetry:return &params.sat_asymmetry;
        case VibeParamId::SatOutTrim:  return &params.sat_out_trim;
        default:                       return nullptr;
    }
}

static inline void sanitize_user_params(VibeUserParams *params) {
    params->depth = clampf(params->depth, 0.0f, 1.0f);
    params->feedback = clampf(params->feedback, 0.0f, 0.65f);
    params->mix = clampf(params->mix, 0.0f, 1.0f);
    params->input_drive = clampf(params->input_drive, 0.5f, 6.0f);
    params->output_gain = clampf(params->output_gain, 0.25f, 2.0f);
    params->sweep_min = clampf(params->sweep_min, 0.0f, 1.0f);
    params->sweep_max = clampf(params->sweep_max, params->sweep_min, 1.0f);
    params->lfo_rate_hz = clampf(params->lfo_rate_hz, 0.02f, 12.0f);
    params->drift_amount = clampf(params->drift_amount, 0.0f, 0.05f);
    params->drift_rate_hz = clampf(params->drift_rate_hz, 0.005f, 0.5f);
    params->pre_hpf_hz = clampf(params->pre_hpf_hz, 8.0f, 160.0f);
    params->tone_tilt = clampf(params->tone_tilt, -1.0f, 1.0f);
    params->sat_asymmetry = clampf(params->sat_asymmetry, -0.25f, 0.25f);
    params->sat_out_trim = clampf(params->sat_out_trim, 0.60f, 1.20f);
}

static VibePreset make_vibe_preset(VibeVoicing voicing) {
    VibePreset preset;
    preset.voicing = voicing;
    preset.user = VibeUserParams{};
    preset.tuning = VibeTuningParams{};

    switch (voicing) {
        case VibeVoicing::ClassicVibrato:
            preset.chorus_mode = false;
            preset.user.mix = 1.0f;
            preset.user.depth = 0.86f;
            preset.user.feedback = 0.38f;
            preset.user.lfo_rate_hz = 1.25f;
            preset.lfo_shape = LfoShape::Sine;
            break;
        case VibeVoicing::DeepThrob:
            preset.chorus_mode = true;
            preset.user.depth = 0.98f;
            preset.user.feedback = 0.48f;
            preset.user.mix = 0.58f;
            preset.user.lfo_rate_hz = 0.92f;
            preset.user.sweep_min = 0.50f;
            preset.user.sweep_max = 1.0f;
            preset.user.pre_hpf_hz = 28.0f;
            preset.tuning.lamp_hysteresis = 0.032f;
            preset.feedback_profile = FeedbackProfile::ClassicFeedback;
            preset.lfo_shape = LfoShape::TriangleSmooth;
            break;
        case VibeVoicing::ModernWide:
            preset.chorus_mode = true;
            preset.user.depth = 0.90f;
            preset.user.feedback = 0.36f;
            preset.user.mix = 0.60f;
            preset.user.input_drive = 2.9f;
            preset.user.pre_hpf_hz = 35.0f;
            preset.user.tone_tilt = 0.14f;
            preset.user.sat_asymmetry = 0.12f;
            preset.user.lfo_rate_hz = 1.05f;
            preset.user.drift_amount = 0.022f;
            preset.tuning.stereo_phase_offset = 0.31f;
            preset.feedback_profile = FeedbackProfile::ModernFeedback;
            preset.lfo_shape = LfoShape::Sine;
            break;
        case VibeVoicing::ClassicChorus:
        default:
            preset.chorus_mode = true;
            preset.user.mix = 0.50f;
            preset.user.tone_tilt = 0.0f;
            preset.feedback_profile = FeedbackProfile::ClassicFeedback;
            preset.lfo_shape = LfoShape::Sine;
            break;
    }

    sanitize_user_params(&preset.user);
    return preset;
}

// ============================================================================
// LFO
// ============================================================================

class EffectLFO {
private:
    float phase = 0.0f;
    float shape_state_l = 0.0f;
    float shape_state_r = 0.0f;
    float drift_state = 0.0f;
    uint32_t drift_rng = 0xA341316Cu;
    float drift_lfo_phase = 0.0f;

    static float shape_triangle_smooth(float p) {
        const float tri = (p < 0.5f) ? (p * 2.0f) : (2.0f - p * 2.0f);
        return tri * tri * (3.0f - 2.0f * tri);
    }

    static float apply_shape(LfoShape shape, float phase_v) {
        switch (shape) {
            case LfoShape::TriangleSmooth: return shape_triangle_smooth(phase_v);
            case LfoShape::Sine:
            default:                       return 0.5f + 0.5f * sinf(2.0f * kPi * (phase_v - 0.25f));
        }
    }

public:
    void reseed(uint32_t seed) {
        drift_rng = seed ? seed : 0xA341316Cu;
        drift_state = 0.0f;
        phase = 0.0f;
        shape_state_l = 0.0f;
        shape_state_r = 0.0f;
        drift_lfo_phase = 0.0f;
    }

    void processSample(float *l, float *r, const VibeUserParams &user, const VibeTuningParams &tuning, LfoShape shape) {
        const float freq = clampf(user.lfo_rate_hz, 0.02f, 12.0f);
        const float drift_amount = clampf(user.drift_amount, 0.0f, 0.05f);
        const float drift_rate_hz = clampf(user.drift_rate_hz, 0.005f, 0.5f);
        const float drift_alpha = 1.0f - expf(-2.0f * kPi * drift_rate_hz * cSAMPLE_RATE);

        drift_state += drift_alpha * (noise_bipolar(drift_rng) - drift_state);
        drift_state = clampf(drift_state, -1.0f, 1.0f);

        drift_lfo_phase += drift_rate_hz * cSAMPLE_RATE;
        if (drift_lfo_phase >= 1.0f) drift_lfo_phase -= 1.0f;
        const float drift = 1.0f + drift_amount * (0.65f * drift_state + 0.35f * sinf(2.0f * kPi * drift_lfo_phase));

        phase += freq * drift * cSAMPLE_RATE;
        if (phase >= 1.0f) phase -= 1.0f;

        float p_r = phase + clampf(tuning.stereo_phase_offset, -0.5f, 0.5f);
        if (p_r < 0.0f) p_r += 1.0f;
        if (p_r >= 1.0f) p_r -= 1.0f;

        const float raw_l = apply_shape(shape, phase);
        const float raw_r = apply_shape(shape, p_r);

        const float smooth_hz = 4.0f + 120.0f * clampf(tuning.lfo_shape_smoothing, 0.01f, 1.0f);
        const float smoothing = 1.0f - expf(-2.0f * kPi * smooth_hz * cSAMPLE_RATE);
        shape_state_l += smoothing * (raw_l - shape_state_l);
        shape_state_r += smoothing * (raw_r - shape_state_r);

        *l = clampf(shape_state_l, 0.0f, 1.0f);
        *r = clampf(shape_state_r, 0.0f, 1.0f);
    }
};

struct fparams {
    float x1 = 0.0f, y1 = 0.0f, n0 = 0.0f, n1 = 0.0f, d0 = 0.0f, d1 = 0.0f;
};

struct PhaseStage {
    fparams vc, vcvo, ecvc, vevo;
    float oldcvolt = 0.0f;
    float ldr_mismatch = 1.0f;
};

struct FeedbackMidState {
    float hp_x1 = 0.0f;
    float hp_y1 = 0.0f;
    float lp_y1 = 0.0f;
};

// ============================================================================
// Univibe
// ============================================================================

class Vibe {
public:
    Vibe(float *efxoutl_, float *efxoutr_);
    void out(float *smpsl, float *smpsr);
    void init_vibes();
    void reseed(uint32_t seed);
    void set_user_params(const VibeUserParams &user);
    void set_param(VibeParamId id, float value);
    void set_param_normalized(VibeParamId id, float normalized);
    float get_param(VibeParamId id) const;
    float get_param_normalized(VibeParamId id) const;
    void set_voicing(VibeVoicing voicing);
    VibeVoicing voicing() const { return params.voicing; }

    const VibeUserParams &user_params() const { return params.user; }
    const VibeUserParams &smoothed_user_params() const { return smoothed_user; }
    VibeTuningParams &tuning_params() { return params.tuning; }

    bool mode_chorus = true;

private:
    float *efxoutl;
    float *efxoutr;
    float lpanning = 1.0f, rpanning = 1.0f;
    VibeParams params;

    EffectLFO lfo;

    float lamp_state_l = 0.0f, lamp_state_r = 0.0f;
    float lamp_attack, lamp_release;

    PhaseStage stage[8];

    float fbr = 0.0f, fbl = 0.0f;
    float gain_bjt = 0.0f, k = 0.0f, R1 = 0.0f, C2 = 0.0f, C1[8] = {0}, beta = 0.0f;
    float cn0[8], cn1[8], cd0[8], cd1[8];
    float en0[8], en1[8], ed0[8], ed1[8];
    float ecn0[8], ecn1[8], ecd0[8], ecd1[8];
    float on0[8], on1[8], od0[8], od1[8];
    uint32_t rng_seed = 0x13579BDFu;
    VibeUserParams smoothed_user;
    float output_trim_smoothed = 1.0f;
    float pre_hpf_x1_l = 0.0f, pre_hpf_y1_l = 0.0f;
    float pre_hpf_x1_r = 0.0f, pre_hpf_y1_r = 0.0f;
    FeedbackMidState fb_mid_l{};
    FeedbackMidState fb_mid_r{};
    float fb_env_l = 0.0f, fb_env_r = 0.0f;
    float input_env_l = 0.0f, input_env_r = 0.0f;
    float wet_env_l = 0.0f, wet_env_r = 0.0f;
    float tone_lp_l = 0.0f, tone_lp_r = 0.0f;
    float lamp_memory_l = 0.0f, lamp_memory_r = 0.0f;
    float stage_lamp_slew[8] = {0};
    ParamRamp depth_ramp, fb_ramp, mix_ramp, drive_ramp, gain_ramp, sweep_min_ramp, sweep_max_ramp;
    ParamRamp pre_hpf_ramp, tone_tilt_ramp, sat_asym_ramp, sat_trim_ramp;

    float vibefilter(float data, fparams *ftype);
    void modulate(float res_l, float res_r);
    void update_time_constants();
    void update_smoothed_user_params();
    float bjt_shape(float data, float drive);
    float hp_pre(float x, float hz, float &x1, float &y1);
    float feedback_profile_process(float x, FeedbackProfile profile, FeedbackMidState &mid_state);
    float tone_tilt_process(float x, float tilt, float &lp);
};

#if USER_INTERFACE
class Ky040Encoder {
public:
    void init(uint pin_a_, uint pin_b_, bool reverse_) {
        pin_a = pin_a_;
        pin_b = pin_b_;
        reverse = reverse_;

        gpio_init(pin_a);
        gpio_set_dir(pin_a, GPIO_IN);
        gpio_pull_up(pin_a);

        gpio_init(pin_b);
        gpio_set_dir(pin_b, GPIO_IN);
        gpio_pull_up(pin_b);

        state = read_state();
        transition_state = R_START;
    }

    int poll() {
        const uint8_t pin_state = read_state();
        transition_state = kStateTable[transition_state & 0x0Fu][pin_state];

        int delta = 0;
        if (transition_state & DIR_CW) {
            delta = 1;
        } else if (transition_state & DIR_CCW) {
            delta = -1;
        }

        return reverse ? -delta : delta;
    }

private:
    enum : uint8_t {
        R_START = 0x0,
        R_CW_FINAL = 0x1,
        R_CW_BEGIN = 0x2,
        R_CW_NEXT = 0x3,
        R_CCW_BEGIN = 0x4,
        R_CCW_FINAL = 0x5,
        R_CCW_NEXT = 0x6,
        DIR_NONE = 0x00,
        DIR_CW = 0x10,
        DIR_CCW = 0x20,
    };

    static constexpr uint8_t kStateTable[7][4] = {
        {R_START,    R_CW_BEGIN,  R_CCW_BEGIN, R_START},
        {R_CW_NEXT,  R_START,     R_CW_FINAL,  R_START | DIR_CW},
        {R_CW_NEXT,  R_CW_BEGIN,  R_START,     R_START},
        {R_CW_NEXT,  R_CW_BEGIN,  R_CW_FINAL,  R_START},
        {R_CCW_NEXT, R_START,     R_CCW_BEGIN, R_START},
        {R_CCW_NEXT, R_CCW_FINAL, R_START,     R_START | DIR_CCW},
        {R_CCW_NEXT, R_CCW_FINAL, R_CCW_BEGIN, R_START},
    };

    uint pin_a = 0;
    uint pin_b = 0;
    bool reverse = false;
    uint8_t state = 0;
    uint8_t transition_state = R_START;

    uint8_t read_state() {
        const uint8_t a = gpio_get(pin_a) ? 1u : 0u;
        const uint8_t b = gpio_get(pin_b) ? 1u : 0u;
        state = (a << 1) | b;
        return state;
    }
};

class UiButton {
public:
    void init(uint pin_) {
        pin = pin_;
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_IN);
        gpio_pull_up(pin);

        const bool raw = raw_pressed();
        stable_pressed = raw;
        last_raw = raw;
        last_change_us = time_us_32();
    }

    struct Events {
        bool pressed = false;
        bool released = false;
        bool stable_down = false;
    };

    Events poll(uint32_t now_us) {
        const bool raw = raw_pressed();
        if (raw != last_raw) {
            last_raw = raw;
            last_change_us = now_us;
        }

        Events events;
        if ((raw != stable_pressed) && ((uint32_t)(now_us - last_change_us) >= kDebounceUs)) {
            stable_pressed = raw;
            events.pressed = stable_pressed;
            events.released = !stable_pressed;
        }

        events.stable_down = stable_pressed;
        return events;
    }

private:
    static constexpr uint32_t kDebounceUs = 15000u;

    uint pin = 0;
    bool stable_pressed = false;
    bool last_raw = false;
    uint32_t last_change_us = 0;

    bool raw_pressed() const {
        return gpio_get(pin) == 0;
    }
};

class VibeUi {
public:
    void init(Vibe *effect_) {
        effect = effect_;
        mode_is_depth = false;
        long_press_handled = false;
        press_started_us = 0;
        last_encoder_event_us = 0;
        led_param.base_on = false;
        led_mode.base_on = false;

        encoder.init(UI_PIN_ENC_A, UI_PIN_ENC_B, UI_ENCODER_REVERSE != 0);
        button.init(UI_PIN_ENC_SW);

        gpio_init(UI_PIN_LED_PARAM);
        gpio_set_dir(UI_PIN_LED_PARAM, GPIO_OUT);

        gpio_init(UI_PIN_LED_MODE);
        gpio_set_dir(UI_PIN_LED_MODE, GPIO_OUT);

        sync_leds(time_us_32());
    }

    void poll() {
        if (!effect) {
            return;
        }

        const uint32_t now_us = time_us_32();
        const int delta = encoder.poll();
        if (delta != 0) {
            apply_encoder_delta(delta, now_us);
        }

        const UiButton::Events button_events = button.poll(now_us);

        if (button_events.pressed) {
            press_started_us = now_us;
            long_press_handled = false;
        }

        if (button_events.stable_down && !long_press_handled &&
            ((uint32_t)(now_us - press_started_us) >= kLongPressUs)) {
            effect->mode_chorus = !effect->mode_chorus;
            long_press_handled = true;
            start_blink(led_mode, now_us, effect->mode_chorus ? 1 : 2);
            sync_leds(now_us);
        }

        if (button_events.released && !long_press_handled) {
            mode_is_depth = !mode_is_depth;
            start_blink(led_param, now_us, 1);
            sync_leds(now_us);
        }

        update_leds(now_us);
    }

private:
    struct LedBlinkState {
        bool base_on = false;
        bool blink_on = false;
        uint8_t toggles_remaining = 0;
        uint32_t next_toggle_us = 0;
    };

    static constexpr uint32_t kLongPressUs = 700000u;
    static constexpr uint32_t kBlinkHalfPeriodUs = 80000u;

    Vibe *effect = nullptr;
    Ky040Encoder encoder;
    UiButton button;
    bool mode_is_depth = false;
    bool long_press_handled = false;
    uint32_t press_started_us = 0;
    uint32_t last_encoder_event_us = 0;
    LedBlinkState led_param;
    LedBlinkState led_mode;

    static float normalized_shape(float x) {
        x = clampf(x, 0.0f, 1.0f);
        return x * x * (3.0f - 2.0f * x);
    }

    static float encoder_acceleration(uint32_t dt_us) {
        if (dt_us == 0u) return 1.0f;
        if (dt_us < 12000u) return 4.0f;
        if (dt_us < 22000u) return 3.0f;
        if (dt_us < 45000u) return 2.0f;
        if (dt_us < 80000u) return 1.4f;
        return 1.0f;
    }

    void apply_encoder_delta(int delta, uint32_t now_us) {
        const uint32_t dt_us = (last_encoder_event_us == 0u) ? 0u : (uint32_t)(now_us - last_encoder_event_us);
        last_encoder_event_us = now_us;
        const float accel = encoder_acceleration(dt_us);

        if (mode_is_depth) {
            const float normalized = effect->get_param_normalized(VibeParamId::Depth);
            const float base_step = 0.010f + 0.010f * normalized_shape(normalized);
            const float next = effect->get_param(VibeParamId::Depth) + delta * base_step * (1.0f + 0.35f * (accel - 1.0f));
            effect->set_param(VibeParamId::Depth, next);
        } else {
            const float normalized = effect->get_param_normalized(VibeParamId::LfoRateHz);
            const float curved = normalized_shape(normalized);
            // Smaller steps down low, broader steps up high, then scale by turn speed for a more natural feel.
            const float base_step = 0.025f + 0.050f * curved + 0.145f * curved * curved;
            const float next = effect->get_param(VibeParamId::LfoRateHz) + delta * base_step * accel;
            effect->set_param(VibeParamId::LfoRateHz, next);
        }
    }

    void start_blink(LedBlinkState &led, uint32_t now_us, uint8_t flashes) {
        if (flashes == 0) {
            return;
        }

        led.blink_on = !led.base_on;
        led.toggles_remaining = (uint8_t)(flashes * 2u - 1u);
        led.next_toggle_us = now_us + kBlinkHalfPeriodUs;
    }

    void update_leds(uint32_t now_us) {
        step_led(led_param, UI_PIN_LED_PARAM, now_us);
        step_led(led_mode, UI_PIN_LED_MODE, now_us);
    }

    void step_led(LedBlinkState &led, uint pin, uint32_t now_us) {
        if (led.toggles_remaining > 0u && (int32_t)(now_us - led.next_toggle_us) >= 0) {
            led.blink_on = !led.blink_on;
            led.toggles_remaining--;
            led.next_toggle_us += kBlinkHalfPeriodUs;
        }

        const bool out = (led.toggles_remaining > 0u) ? led.blink_on : led.base_on;
        gpio_put(pin, out ? 1 : 0);
    }

    void sync_leds(uint32_t now_us) {
        // Param LED: off = speed, on = depth.
        led_param.base_on = mode_is_depth;
        // Mode LED: off = chorus, on = vibrato.
        led_mode.base_on = effect && !effect->mode_chorus;
        update_leds(now_us);
    }
};
#endif

Vibe::Vibe(float *efxoutl_, float *efxoutr_) : efxoutl(efxoutl_), efxoutr(efxoutr_) {
    set_voicing(VibeVoicing::ClassicChorus);
    smoothed_user = params.user;
    lfo.reseed(rng_seed ^ 0xA511E9B3u);
    update_time_constants();
    init_vibes();
}

float Vibe::vibefilter(float data, fparams *ftype) {
    float y0 = data * ftype->n0 + ftype->x1 * ftype->n1 - ftype->y1 * ftype->d1;
    ftype->y1 = y0 + DENORMAL_GUARD;
    ftype->x1 = data;
    return y0;
}

void Vibe::update_time_constants() {
    const float block_time = fPERIOD / fSAMPLE_RATE;
    const float attack_sec = clampf(params.tuning.lamp_attack_sec, 0.001f, 0.250f);
    const float release_sec = clampf(params.tuning.lamp_release_sec, 0.001f, 0.500f);

    lamp_attack = 1.0f - expf(-block_time / attack_sec);
    lamp_release = 1.0f - expf(-block_time / release_sec);
}

void Vibe::reseed(uint32_t seed) {
    rng_seed = seed ? seed : 0x13579BDFu;
    lamp_state_l = 0.0f;
    lamp_state_r = 0.0f;
    fbl = 0.0f;
    fbr = 0.0f;
    fb_mid_l = {};
    fb_mid_r = {};
    fb_env_l = 0.0f;
    fb_env_r = 0.0f;
    input_env_l = 0.0f;
    input_env_r = 0.0f;
    sanitize_user_params(&params.user);
    smoothed_user = params.user;
    lfo.reseed(rng_seed ^ 0xA511E9B3u);
    update_time_constants();
    init_vibes();
}

void Vibe::set_user_params(const VibeUserParams &user) {
    params.user = user;
    sanitize_user_params(&params.user);
}

void Vibe::set_voicing(VibeVoicing voicing_id) {
    const VibePreset preset = make_vibe_preset(voicing_id);
    params.user = preset.user;
    params.tuning = preset.tuning;
    params.lfo_shape = preset.lfo_shape;
    params.feedback_profile = preset.feedback_profile;
    params.legacy_saturation = preset.legacy_saturation;
    params.voicing = voicing_id;
    mode_chorus = preset.chorus_mode;
    sanitize_user_params(&params.user);
}

void Vibe::set_param(VibeParamId id, float value) {
    float *slot = vibe_param_slot(params.user, id);
    if (!slot) {
        return;
    }

    const VibeParamSpec spec = vibe_param_spec(id);
    *slot = clampf(value, spec.min_value, spec.max_value);
    sanitize_user_params(&params.user);
}

void Vibe::set_param_normalized(VibeParamId id, float normalized) {
    const VibeParamSpec spec = vibe_param_spec(id);
    const float t = clampf(normalized, 0.0f, 1.0f);
    set_param(id, spec.min_value + t * (spec.max_value - spec.min_value));
}

float Vibe::get_param(VibeParamId id) const {
    const float *slot = vibe_param_slot(params.user, id);
    return slot ? *slot : 0.0f;
}

float Vibe::get_param_normalized(VibeParamId id) const {
    const VibeParamSpec spec = vibe_param_spec(id);
    const float value = get_param(id);
    const float span = spec.max_value - spec.min_value;
    return (span > 0.0f) ? clampf((value - spec.min_value) / span, 0.0f, 1.0f) : 0.0f;
}

void Vibe::update_smoothed_user_params() {
    sanitize_user_params(&params.user);

    const float block_time = fPERIOD / fSAMPLE_RATE;
    const float smooth_hz = clampf(params.tuning.control_smoothing_hz, 1.0f, 80.0f);
    const float alpha = 1.0f - expf(-2.0f * kPi * smooth_hz * block_time);

    auto smooth_to_target = [alpha](float &current, float target) {
        current += alpha * (target - current);
    };

    // Encoder-facing controls are smoothed per block to avoid zipper noise and coefficient jumps.
    smooth_to_target(smoothed_user.depth, params.user.depth);
    smooth_to_target(smoothed_user.feedback, params.user.feedback);
    smooth_to_target(smoothed_user.mix, params.user.mix);
    smooth_to_target(smoothed_user.input_drive, params.user.input_drive);
    smooth_to_target(smoothed_user.output_gain, params.user.output_gain);
    smooth_to_target(smoothed_user.sweep_min, params.user.sweep_min);
    smooth_to_target(smoothed_user.sweep_max, params.user.sweep_max);
    smooth_to_target(smoothed_user.lfo_rate_hz, params.user.lfo_rate_hz);
    smooth_to_target(smoothed_user.drift_amount, params.user.drift_amount);
    smooth_to_target(smoothed_user.drift_rate_hz, params.user.drift_rate_hz);
    smooth_to_target(smoothed_user.pre_hpf_hz, params.user.pre_hpf_hz);
    smooth_to_target(smoothed_user.tone_tilt, params.user.tone_tilt);
    smooth_to_target(smoothed_user.sat_asymmetry, params.user.sat_asymmetry);
    smooth_to_target(smoothed_user.sat_out_trim, params.user.sat_out_trim);
    sanitize_user_params(&smoothed_user);
}

float Vibe::bjt_shape(float data, float drive) {
    if (params.legacy_saturation) {
        // Legacy path kept only for A/B comparison with historical behavior.
        return fast_soft_clip(data * drive) * params.tuning.bjt_gain_trim;
    }
    const float asym = smoothed_user.sat_asymmetry;
    const float headroom = 0.84f; // Higher base headroom before adaptive drive takes over.
    const float x = (data * headroom + asym) * drive;
    const float x2 = x * x;
    // Cubic-rational saturator: musical rounding at low CPU cost, no oversampling required.
    const float sat = x * (27.0f + x2) / (27.0f + 9.0f * x2);
    const float xa = asym * drive;
    const float xa2 = xa * xa;
    const float sat_bias = xa * (27.0f + xa2) / (27.0f + 9.0f * xa2);
    return (sat - sat_bias) * params.tuning.bjt_gain_trim * smoothed_user.sat_out_trim;
}

float Vibe::hp_pre(float x, float hz, float &x1, float &y1) {
    const float h = clampf(hz, params.tuning.pre_hpf_hz_min, params.tuning.pre_hpf_hz_max);
    const float a = expf(-2.0f * kPi * h * cSAMPLE_RATE);
    const float y = a * (y1 + x - x1);
    x1 = x;
    y1 = y;
    return y;
}

float Vibe::feedback_profile_process(float x, FeedbackProfile profile, FeedbackMidState &mid_state) {
    const FeedbackProfileCoefs coefs = feedback_profile_coefs(profile);
    const float hp_fc = clampf(coefs.hp_fc_hz, 250.0f, 700.0f);
    const float lp_fc = clampf(coefs.lp_fc_hz, 1000.0f, 1800.0f);
    const float hp_a = expf(-2.0f * kPi * hp_fc * cSAMPLE_RATE);
    const float lp_a = 1.0f - expf(-2.0f * kPi * lp_fc * cSAMPLE_RATE);

    // Cascaded 1-pole HP + 1-pole LP yields a lightweight mid band around ~700-1.2 kHz.
    const float hp_y = hp_a * (mid_state.hp_y1 + x - mid_state.hp_x1);
    mid_state.hp_x1 = x;
    mid_state.hp_y1 = hp_y;

    mid_state.lp_y1 += lp_a * (hp_y - mid_state.lp_y1);
    const float mid = mid_state.lp_y1;
    const float high = hp_y - mid;
    const float shaped = coefs.dry * x + coefs.mid * mid + coefs.high * high;
    return clampf(shaped, -1.25f, 1.25f);
}

float Vibe::tone_tilt_process(float x, float tilt, float &lp) {
    const float fc = clampf(params.tuning.tilt_hz, 350.0f, 2600.0f);
    const float a = 1.0f - expf(-2.0f * kPi * fc * cSAMPLE_RATE);
    lp += a * (x - lp);
    const float high = x - lp;
    const float amt = clampf(tilt, -1.0f, 1.0f);
    return x + amt * (0.85f * high - 0.65f * lp);
}

void Vibe::init_vibes() {
    k = 2.0f * fSAMPLE_RATE;
    R1 = 4700.0f;
    C2 = 1e-6f;
    beta = 150.0f;
    gain_bjt = -beta / (beta + 1.0f);

    float base_C1[8] = {
        0.015e-6f, 0.22e-6f, 470e-12f, 0.0047e-6f,
        0.015e-6f, 0.22e-6f, 470e-12f, 0.0047e-6f
    };

    uint32_t component_rng = rng_seed ^ 0x51F15EEDu;
    for (int i = 0; i < 8; i++) {
        float cap_var = 1.0f + 0.10f * noise_bipolar(component_rng);
        C1[i] = base_C1[i] * cap_var;
        // Pequeno mismatch físico (aproximação) para quebrar simetria perfeita entre células.
        stage[i].ldr_mismatch = 1.0f + 0.025f * noise_bipolar(component_rng);
        // Pequeno espalhamento criativo para "chewiness" sem fugir do voicing clássico.
        stage_lamp_slew[i] = 1.0f + params.tuning.stage_time_spread * noise_bipolar(component_rng);
        stage_lamp_slew[i] = clampf(stage_lamp_slew[i], 0.85f, 1.15f);
        stage[i].oldcvolt = 0.0f;
        en1[i] = k * R1 * C1[i];
        en0[i] = 1.0f;
        stage[i].vc = {};
        stage[i].vcvo = {};
        stage[i].ecvc = {};
        stage[i].vevo = {};
    }
}

void Vibe::modulate(float res_l, float res_r) {
    for (int i = 0; i < 8; i++) {
        float base_res = (i < 4) ? res_l : res_r;
        // Clamp the stage LDR emulation after mismatch so the network never sees non-physical extremes.
        float stage_res = clampf(base_res * stage[i].ldr_mismatch,
                                 params.tuning.ldr_min_ohms,
                                 params.tuning.ldr_max_ohms);
        float currentRv = 4700.0f + stage_res;

        float R1pRv = R1 + currentRv;
        float C2pC1 = C2 + C1[i];

        float cd1_val  = k * R1pRv * C1[i];
        float cn1_val  = k * gain_bjt * currentRv * C1[i];
        float ecd1_val = k * cd1_val * C2 / C2pC1;
        float ecn1_val = k * gain_bjt * R1 * cd1_val * C2 / (currentRv * C2pC1);
        float on1_val  = k * currentRv * C2;

        float cd0_val  = 1.0f + C1[i] / C2;
        float ecd0_val = 1.0f;
        float od0_val  = 1.0f + C2 / C1[i];
        float ed0_val  = 1.0f + C1[i] / C2;
        float cn0_val  = gain_bjt * (1.0f + C1[i] / C2);
        float on0_val  = 1.0f;
        float ecn0_val = 0.0f;

        float tmp = 1.0f / (cd1_val + cd0_val);
        stage[i].vc.n1 = tmp * (cn0_val - cn1_val);
        stage[i].vc.n0 = tmp * (cn1_val + cn0_val);
        stage[i].vc.d1 = tmp * (cd0_val - cd1_val);

        tmp = 1.0f / (ecd1_val + ecd0_val);
        stage[i].ecvc.n1 = tmp * (ecn0_val - ecn1_val);
        stage[i].ecvc.n0 = tmp * (ecn1_val + ecn0_val);
        stage[i].ecvc.d1 = tmp * (ecd0_val - ecd1_val);

        tmp = 1.0f / (on1_val + od0_val);
        stage[i].vcvo.n1 = tmp * (on0_val - on1_val);
        stage[i].vcvo.n0 = tmp * (on1_val + on0_val);
        stage[i].vcvo.d1 = tmp * (od0_val - on1_val);

        float ed1_val = k * R1pRv * C1[i];
        tmp = 1.0f / (ed1_val + ed0_val);
        stage[i].vevo.n1 = tmp * (en0[i] - en1[i]);
        stage[i].vevo.n0 = tmp * (en1[i] + en0[i]);
        stage[i].vevo.d1 = tmp * (ed0_val - ed1_val);
    }
}

void Vibe::out(float *smpsl, float *smpsr) {
    update_time_constants();
    const VibeUserParams prev = smoothed_user;
    update_smoothed_user_params();
    const float stage_limit = clampf(params.tuning.stage_state_limit, 2.0f, 12.0f);

    depth_ramp.begin(prev.depth, smoothed_user.depth, PERIOD);
    fb_ramp.begin(prev.feedback, smoothed_user.feedback, PERIOD);
    mix_ramp.begin(prev.mix, smoothed_user.mix, PERIOD);
    drive_ramp.begin(prev.input_drive, smoothed_user.input_drive, PERIOD);
    gain_ramp.begin(prev.output_gain, smoothed_user.output_gain, PERIOD);
    sweep_min_ramp.begin(prev.sweep_min, smoothed_user.sweep_min, PERIOD);
    sweep_max_ramp.begin(prev.sweep_max, smoothed_user.sweep_max, PERIOD);
    pre_hpf_ramp.begin(prev.pre_hpf_hz, smoothed_user.pre_hpf_hz, PERIOD);
    tone_tilt_ramp.begin(prev.tone_tilt, smoothed_user.tone_tilt, PERIOD);
    sat_asym_ramp.begin(prev.sat_asymmetry, smoothed_user.sat_asymmetry, PERIOD);
    sat_trim_ramp.begin(prev.sat_out_trim, smoothed_user.sat_out_trim, PERIOD);

    for (int i = 0; i < PERIOD; i++) {
        smoothed_user.sat_asymmetry = sat_asym_ramp.tick();
        smoothed_user.sat_out_trim = sat_trim_ramp.tick();

        float lfol = 0.0f, lfor = 0.0f;
        lfo.processSample(&lfol, &lfor, smoothed_user, params.tuning, params.lfo_shape);

        const float depth = depth_ramp.tick();
        const float sweep_min = sweep_min_ramp.tick();
        const float sweep_max = sweep_max_ramp.tick();
        const float sweep_span = clampf(sweep_max - sweep_min, 0.0f, 1.0f);
        const float target_l = sweep_min + depth * lfol * sweep_span;
        const float target_r = sweep_min + depth * lfor * sweep_span;
        const float hyst = clampf(params.tuning.lamp_hysteresis, 0.0f, 0.20f);
        const float mem_base = clampf(0.08f + 7.0f * hyst, 0.02f, 0.75f);

        const float mem_a_up_l = mem_base;
        const float mem_a_dn_l = mem_base * 0.58f;
        const float mem_a_up_r = mem_base;
        const float mem_a_dn_r = mem_base * 0.58f;
        lamp_memory_l += ((target_l > lamp_memory_l) ? mem_a_up_l : mem_a_dn_l) * (target_l - lamp_memory_l);
        lamp_memory_r += ((target_r > lamp_memory_r) ? mem_a_up_r : mem_a_dn_r) * (target_r - lamp_memory_r);
        lamp_memory_l = clampf(lamp_memory_l, 0.0f, 1.0f);
        lamp_memory_r = clampf(lamp_memory_r, 0.0f, 1.0f);

        const float target_l_h = 0.5f + 0.5f * tanhf((lamp_memory_l - 0.5f) * 2.3f);
        const float target_r_h = 0.5f + 0.5f * tanhf((lamp_memory_r - 0.5f) * 2.3f);

        const float stage_slew_l = stage_lamp_slew[0];
        const float stage_slew_r = stage_lamp_slew[4];
        const float atk_l = clampf(lamp_attack * stage_slew_l, 0.0001f, 1.0f);
        const float rel_l = clampf(lamp_release / stage_slew_l, 0.0001f, 1.0f);
        const float atk_r = clampf(lamp_attack * stage_slew_r, 0.0001f, 1.0f);
        const float rel_r = clampf(lamp_release / stage_slew_r, 0.0001f, 1.0f);
        lamp_state_l += ((target_l_h > lamp_state_l) ? atk_l : rel_l) * (target_l_h - lamp_state_l);
        lamp_state_r += ((target_r_h > lamp_state_r) ? atk_r : rel_r) * (target_r_h - lamp_state_r);
        lamp_state_l = clampf(lamp_state_l, 0.0f, 1.0f);
        lamp_state_r = clampf(lamp_state_r, 0.0f, 1.0f);

        const float bright_l = lamp_state_l * sqrtf(lamp_state_l);
        const float bright_r = lamp_state_r * sqrtf(lamp_state_r);
        float res_l = params.tuning.ldr_dark_ohms * expf(-params.tuning.ldr_curve * bright_l);
        float res_r = params.tuning.ldr_dark_ohms * expf(-params.tuning.ldr_curve * bright_r);
        res_l = clampf(res_l, params.tuning.ldr_min_ohms, params.tuning.ldr_max_ohms);
        res_r = clampf(res_r, params.tuning.ldr_min_ohms, params.tuning.ldr_max_ohms);
        modulate(res_l, res_r);

        const float emitterfb_l = clampf(params.tuning.emitter_fb_scale / res_l, params.tuning.emitter_fb_min, params.tuning.emitter_fb_max);
        const float emitterfb_r = clampf(params.tuning.emitter_fb_scale / res_r, params.tuning.emitter_fb_min, params.tuning.emitter_fb_max);
        const float feedback_knob = fb_ramp.tick();
        const float feedback = clampf(feedback_musical_gain(feedback_knob), 0.0f, 0.70f);
        const float input_drive = drive_ramp.tick();
        const float mix = mix_ramp.tick();
        const float output_gain = gain_ramp.tick();
        const float pre_hpf_hz = pre_hpf_ramp.tick();
        const float tone_tilt = tone_tilt_ramp.tick();
        const float wet_gain = fast_sqrt01(mix);
        const float dry_gain = fast_sqrt01(1.0f - mix);
        const float stress = 0.55f * clampf((input_drive - 0.5f) / 5.5f, 0.0f, 1.0f)
                           + 0.18f * clampf(feedback / 0.70f, 0.0f, 1.0f)
                           + 0.27f * mix * mix;
        const float target_trim = (1.0f / (1.0f + 0.35f * stress)) * (1.0f - params.tuning.gain_comp_depth * (mix - 0.5f));
        output_trim_smoothed += 0.06f * (target_trim - output_trim_smoothed);
        const float final_gain = output_gain * output_trim_smoothed;
        const float hi_fb = clampf((feedback_knob - 0.42f) * (1.0f / 0.23f), 0.0f, 1.0f);
        const float clarity_boost = hi_fb * (0.70f + 0.30f * (1.0f - mix));
        const float fb_sat_drive = clampf((1.0f + params.tuning.feedback_sat) * (1.0f - 0.30f * clarity_boost), 0.75f, 2.60f);
        const float wet_core_blend = 0.24f * clarity_boost;
        const float fb_lim_threshold = 0.78f;
        const float fb_lim_floor = 0.30f;
        const float fb_env_attack = 1.0f - expf(-2.0f * kPi * 320.0f * cSAMPLE_RATE);
        const float fb_env_release = 1.0f - expf(-2.0f * kPi * 48.0f * cSAMPLE_RATE);
        const float wet_env_attack = 1.0f - expf(-2.0f * kPi * 45.0f * cSAMPLE_RATE);
        const float wet_env_release = 1.0f - expf(-2.0f * kPi * 6.0f * cSAMPLE_RATE);
        const float wet_env_floor = 1.0e-4f;
        const float wet_comp_min = 0.84140f; // -1.5 dB
        const float wet_comp_max = 1.18850f; // +1.5 dB
        const float input_env_attack = 1.0f - expf(-2.0f * kPi * 180.0f * cSAMPLE_RATE);
        const float input_env_release = 1.0f - expf(-2.0f * kPi * 16.0f * cSAMPLE_RATE);
        // Adaptive drive coefficients: trade-off musical feel vs CPU/complexity.
        const float dyn_base = clampf(0.80f + 0.34f * input_drive, 0.80f, 2.20f);
        const float dyn_k1 = 1.05f; // input envelope weight
        const float dyn_k2 = 0.92f; // feedback weight
        const float dyn_k3 = 0.36f; // depth weight
        const float dyn_min = 0.75f;
        const float dyn_max = 3.10f;
        const bool classic_chorus_profile = mode_chorus && (params.voicing == VibeVoicing::ClassicChorus);
        const float classic_stereo_reduction = classic_chorus_profile ? 0.88f : 1.0f;

        float dry_l = smpsl[i];
        dry_l = hp_pre(dry_l, pre_hpf_hz, pre_hpf_x1_l, pre_hpf_y1_l);
        const float fb_in_l = feedback_profile_process(fbl, params.feedback_profile, fb_mid_l);
        const float in_probe_l = fabsf(fb_in_l + dry_l);
        input_env_l += ((in_probe_l > input_env_l) ? input_env_attack : input_env_release) * (in_probe_l - input_env_l);
        const float dynamic_drive_l = clampf(dyn_base + dyn_k1 * input_env_l + dyn_k2 * feedback + dyn_k3 * depth, dyn_min, dyn_max);
        float input = bjt_shape(fb_in_l + dry_l, dynamic_drive_l);

        for (int j = 0; j < 4; j++) {
            float cvolt = vibefilter(input, &stage[j].ecvc) +
                          vibefilter(input + emitterfb_l * stage[j].oldcvolt, &stage[j].vc);
            cvolt = clampf(cvolt, -stage_limit, stage_limit);
            float ocvolt = clampf(vibefilter(cvolt, &stage[j].vcvo), -stage_limit, stage_limit);
            stage[j].oldcvolt = ocvolt;
            input = bjt_shape(ocvolt + vibefilter(input, &stage[j].vevo), dynamic_drive_l);
        }

        float fb_raw_l = clampf(stage[3].oldcvolt * feedback, -1.20f, 1.20f);
        float fb_sat_l = soft_clip_cubic(fb_raw_l * fb_sat_drive);
        fb_sat_l = clampf(fb_sat_l, -1.15f, 1.15f);
        const float fb_abs_l = fabsf(fb_sat_l);
        fb_env_l += ((fb_abs_l > fb_env_l) ? fb_env_attack : fb_env_release) * (fb_abs_l - fb_env_l);
        const float fb_gain_l = (fb_env_l > fb_lim_threshold)
                                ? clampf(fb_lim_threshold / (fb_env_l + 1e-9f), fb_lim_floor, 1.0f)
                                : 1.0f;
        fbl = clampf(fb_sat_l * fb_gain_l, -0.95f, 0.95f);
        const float wet_core_l = input;
        const float wet_air_l = tone_tilt_process(input, tone_tilt, tone_lp_l);
        float wet_l = wet_air_l + wet_core_blend * wet_core_l;

        float dry_r = smpsr[i];
        dry_r = hp_pre(dry_r, pre_hpf_hz, pre_hpf_x1_r, pre_hpf_y1_r);
        const float fb_in_r = feedback_profile_process(fbr, params.feedback_profile, fb_mid_r);
        const float in_probe_r = fabsf(fb_in_r + dry_r);
        input_env_r += ((in_probe_r > input_env_r) ? input_env_attack : input_env_release) * (in_probe_r - input_env_r);
        const float dynamic_drive_r = clampf(dyn_base + dyn_k1 * input_env_r + dyn_k2 * feedback + dyn_k3 * depth, dyn_min, dyn_max);
        input = bjt_shape(fb_in_r + dry_r, dynamic_drive_r);

        for (int j = 4; j < 8; j++) {
            float cvolt = vibefilter(input, &stage[j].ecvc) +
                          vibefilter(input + emitterfb_r * stage[j].oldcvolt, &stage[j].vc);
            cvolt = clampf(cvolt, -stage_limit, stage_limit);
            float ocvolt = clampf(vibefilter(cvolt, &stage[j].vcvo), -stage_limit, stage_limit);
            stage[j].oldcvolt = ocvolt;
            input = bjt_shape(ocvolt + vibefilter(input, &stage[j].vevo), dynamic_drive_r);
        }

        float fb_raw_r = clampf(stage[7].oldcvolt * feedback, -1.20f, 1.20f);
        float fb_sat_r = soft_clip_cubic(fb_raw_r * fb_sat_drive);
        fb_sat_r = clampf(fb_sat_r, -1.15f, 1.15f);
        const float fb_abs_r = fabsf(fb_sat_r);
        fb_env_r += ((fb_abs_r > fb_env_r) ? fb_env_attack : fb_env_release) * (fb_abs_r - fb_env_r);
        const float fb_gain_r = (fb_env_r > fb_lim_threshold)
                                ? clampf(fb_lim_threshold / (fb_env_r + 1e-9f), fb_lim_floor, 1.0f)
                                : 1.0f;
        fbr = clampf(fb_sat_r * fb_gain_r, -0.95f, 0.95f);
        const float wet_core_r = input;
        const float wet_air_r = tone_tilt_process(input, tone_tilt, tone_lp_r);
        float wet_r = wet_air_r + wet_core_blend * wet_core_r;

        // Light stereo narrowing for classic chorus voicing to keep the image closer to vintage behavior.
        const float wet_mid = 0.5f * (wet_l + wet_r);
        const float wet_side = 0.5f * (wet_l - wet_r) * classic_stereo_reduction;
        wet_l = wet_mid + wet_side;
        wet_r = wet_mid - wet_side;

        // Cheap one-pole wet energy estimator + bounded wet compensation driven by depth.
        const float wet_energy_l = wet_l * wet_l;
        const float wet_energy_r = wet_r * wet_r;
        wet_env_l += ((wet_energy_l > wet_env_l) ? wet_env_attack : wet_env_release) * (wet_energy_l - wet_env_l);
        wet_env_r += ((wet_energy_r > wet_env_r) ? wet_env_attack : wet_env_release) * (wet_energy_r - wet_env_r);
        wet_env_l = clampf(wet_env_l, 0.0f, 64.0f);
        wet_env_r = clampf(wet_env_r, 0.0f, 64.0f);

        const float wet_ref = 0.12f + 0.22f * clampf(depth, 0.0f, 1.0f);
        const float inv_env_l = wet_ref / fmaxf(wet_env_l, wet_env_floor);
        const float inv_env_r = wet_ref / fmaxf(wet_env_r, wet_env_floor);
        const float depth_comp_amt = clampf(depth * (0.55f + 0.45f * mix), 0.0f, 1.0f);
        const float wet_comp_l_raw = clampf(powf(inv_env_l, 0.20f), wet_comp_min, wet_comp_max);
        const float wet_comp_r_raw = clampf(powf(inv_env_r, 0.20f), wet_comp_min, wet_comp_max);
        const float wet_comp_l = 1.0f + (wet_comp_l_raw - 1.0f) * depth_comp_amt;
        const float wet_comp_r = 1.0f + (wet_comp_r_raw - 1.0f) * depth_comp_amt;

        // Vibrato remains 100% wet, but trim depth-dependently to avoid overstatement at low rates/high depth.
        const float vibrato_trim = clampf(1.0f - 0.12f * depth * depth, 0.85f, 1.0f);
        const float wet_mode_trim = mode_chorus ? 1.0f : vibrato_trim;
        wet_l *= wet_comp_l * wet_mode_trim;
        wet_r *= wet_comp_r * wet_mode_trim;

        const float mixed_l = mode_chorus ? (dry_l * dry_gain + wet_l * wet_gain) : wet_l;
        const float mixed_r = mode_chorus ? (dry_r * dry_gain + wet_r * wet_gain) : wet_r;
        efxoutl[i] = final_gain * lpanning * mixed_l;
        efxoutr[i] = final_gain * rpanning * mixed_r;
    }
}

// ============================================================================
// Conversão PCM <-> float
// ============================================================================

static inline float pcm24_to_float(int32_t v) {
    v >>= 8;
    return (float)v * (1.0f / 8388608.0f);
}

static inline int32_t float_to_pcm24(float v, uint32_t &rng_state) {
#if ENABLE_TPDF_DITHER
    // TPDF dither at roughly +/-1 LSB before quantization reduces low-level truncation grit.
    const float tpdf = (noise_bipolar(rng_state) + noise_bipolar(rng_state)) * (0.5f / 8388607.0f);
    v += tpdf;
#else
    (void)rng_state;
#endif
    if (v > 1.0f) v = 1.0f;
    if (v < -1.0f) v = -1.0f;
    return ((int32_t)(v * 8388607.0f)) << 8;
}

struct OutputDcBlocker {
    float x1 = 0.0f;
    float y1 = 0.0f;
};

struct OutputConditioner {
    OutputDcBlocker dc_l;
    OutputDcBlocker dc_r;
    float env = 0.0f;
    float trim = 1.0f;
    uint32_t dither_rng = 0xC001C0DEu;
};

static OutputConditioner output_conditioner;

static inline float dc_block_sample(OutputDcBlocker &s, float x) {
#if ENABLE_OUTPUT_DC_BLOCKER
    // Very low cutoff (~8 Hz) removes slow bias drift while preserving guitar lows.
    constexpr float kDcR = 0.99886f;
    const float y = (x - s.x1) + kDcR * s.y1;
    s.x1 = x;
    s.y1 = y;
    return y;
#else
    (void)s;
    return x;
#endif
}

static inline float condition_output_sample(OutputConditioner &st, float x, OutputDcBlocker &dc_state) {
    float y = dc_block_sample(dc_state, x);

#if ENABLE_OUTPUT_AUTO_HEADROOM
    const float abs_y = fabsf(y);
    const float env_attack = 0.14f;
    const float env_release = 0.003f;
    st.env += (abs_y > st.env ? env_attack : env_release) * (abs_y - st.env);
    const float target = (st.env > 0.92f) ? (0.92f / (st.env + 1e-12f)) : 1.0f;
    const float trim_attack = 0.20f;
    const float trim_release = 0.0015f;
    st.trim += (target < st.trim ? trim_attack : trim_release) * (target - st.trim);
    y *= st.trim;
#endif

#if ENABLE_OUTPUT_SOFT_LIMITER
    // Safety limiter: soft knee near full-scale avoids edgy hard clipping.
    const float limit_drive = 1.25f;
    y = soft_clip_cubic(y * limit_drive) * (1.0f / limit_drive);
#endif
    return y;
}

// ============================================================================
// Buffers
// ============================================================================

static float dsp_in_l[PERIOD];
static float dsp_in_r[PERIOD];
static float dsp_out_l[PERIOD];
static float dsp_out_r[PERIOD];

static Vibe univibe(dsp_out_l, dsp_out_r);
#if USER_INTERFACE
static VibeUi vibe_ui;
#endif

alignas(4) static int32_t rx_dma_buf[2][DMA_WORDS_PER_BLOCK];
alignas(4) static int32_t tx_dma_buf[2][DMA_WORDS_PER_BLOCK];
alignas(4) static int32_t tx_silence_buf[DMA_WORDS_PER_BLOCK] = {0};

static volatile bool rx_block_ready[2] = {false, false};
static volatile bool tx_block_ready[2] = {false, false};

static volatile uint32_t rx_overruns  = 0;
static volatile uint32_t tx_underruns = 0;

static volatile int rx_fill_index = 0;
static volatile int tx_expected_index = 0;
static volatile int tx_bootstrap_remaining = 2;

// ============================================================================
// PIO / DMA state
// ============================================================================

static PIO pio = pio0;
static uint sm_clk = 0;
static uint sm_tx  = 1;
static uint sm_rx  = 2;

static int dma_rx_chan = -1;
static int dma_tx_chan = -1;

// ============================================================================
// Clock setup
// ============================================================================

static bool setup_audio_sys_clock(void) {
    return set_sys_clock_hz(AUDIO_SYS_CLOCK_HZ, false);
}

static void setup_mclk(uint gpio_pin) {
    gpio_set_function(gpio_pin, GPIO_FUNC_GPCK);

    clock_gpio_init_int_frac8(
        gpio_pin,
        CLOCKS_CLK_GPOUT0_CTRL_AUXSRC_VALUE_CLK_SYS,
        12,
        0
    );
}

// ============================================================================
// I2S PIO
// ============================================================================

static void i2s_init_pio(void) {
    uint off_clk = pio_add_program(pio, &i2s_master_clk_program);
    uint off_tx  = pio_add_program(pio, &i2s_tx_slave_program);
    uint off_rx  = pio_add_program(pio, &i2s_rx_slave_program);

    {
        pio_sm_config c = i2s_master_clk_program_get_default_config(off_clk);
        sm_config_set_clkdiv(&c, 24.0f);
        sm_config_set_sideset_pins(&c, PIN_BCLK); // GPIO16/17

        pio_gpio_init(pio, PIN_BCLK);
        pio_gpio_init(pio, PIN_LRCLK);
        pio_sm_set_consecutive_pindirs(pio, sm_clk, PIN_BCLK, 2, true);
        pio_sm_init(pio, sm_clk, off_clk, &c);
    }

    {
        pio_sm_config c = i2s_tx_slave_program_get_default_config(off_tx);
        sm_config_set_clkdiv(&c, 1.0f);
        sm_config_set_out_pins(&c, PIN_DOUT, 1);
        sm_config_set_out_shift(&c, false, true, 32);
        sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);

        pio_gpio_init(pio, PIN_DOUT);
        pio_sm_set_consecutive_pindirs(pio, sm_tx, PIN_DOUT, 1, true);
        pio_sm_init(pio, sm_tx, off_tx, &c);
    }

    {
        pio_sm_config c = i2s_rx_slave_program_get_default_config(off_rx);
        sm_config_set_clkdiv(&c, 1.0f);
        sm_config_set_in_pins(&c, PIN_DIN);
        sm_config_set_in_shift(&c, false, true, 32);
        sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);

        pio_gpio_init(pio, PIN_DIN);
        pio_sm_set_consecutive_pindirs(pio, sm_rx, PIN_DIN, 1, false);
        pio_sm_init(pio, sm_rx, off_rx, &c);
    }
}

static void i2s_start_pio(void) {
    pio_sm_set_enabled(pio, sm_tx, true);
    pio_sm_set_enabled(pio, sm_rx, true);
    pio_sm_set_enabled(pio, sm_clk, true);
}

// ============================================================================
// DMA
// ============================================================================

static void start_rx_dma_to_buffer(int index) {
    dma_channel_transfer_to_buffer_now(
        dma_rx_chan,
        rx_dma_buf[index],
        DMA_WORDS_PER_BLOCK
    );
}

static void start_tx_dma_from_buffer(const int32_t *buffer) {
    dma_channel_transfer_from_buffer_now(
        dma_tx_chan,
        buffer,
        DMA_WORDS_PER_BLOCK
    );
}

static void __isr dma_irq0_handler(void) {
    if (dma_channel_get_irq0_status(dma_rx_chan)) {
        dma_channel_acknowledge_irq0(dma_rx_chan);

        const int finished = rx_fill_index;
        const int next     = finished ^ 1;

        if (rx_block_ready[finished]) {
            rx_overruns++;
        }

        __dmb();
        rx_block_ready[finished] = true;
        rx_fill_index = next;

        start_rx_dma_to_buffer(next);
    }

    if (dma_channel_get_irq0_status(dma_tx_chan)) {
        dma_channel_acknowledge_irq0(dma_tx_chan);

        const int32_t *next_buf = tx_silence_buf;

        if (tx_bootstrap_remaining > 0) {
            tx_bootstrap_remaining--;
        } else {
            const int idx = tx_expected_index;

            if (tx_block_ready[idx]) {
                __dmb();
                tx_block_ready[idx] = false;
                next_buf = tx_dma_buf[idx];
                tx_expected_index ^= 1;
            } else {
                tx_underruns++;
                next_buf = tx_silence_buf;
            }
        }

        start_tx_dma_from_buffer(next_buf);
    }
}

static void dma_audio_init(void) {
    dma_rx_chan = dma_claim_unused_channel(true);
    dma_tx_chan = dma_claim_unused_channel(true);

    {
        dma_channel_config c = dma_channel_get_default_config(dma_rx_chan);
        channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
        channel_config_set_read_increment(&c, false);
        channel_config_set_write_increment(&c, true);
        channel_config_set_dreq(&c, pio_get_dreq(pio, sm_rx, false));
        channel_config_set_high_priority(&c, true);

        dma_channel_configure(
            dma_rx_chan,
            &c,
            rx_dma_buf[0],
            &pio->rxf[sm_rx],
            DMA_WORDS_PER_BLOCK,
            false
        );

        dma_channel_set_irq0_enabled(dma_rx_chan, true);
    }

    {
        dma_channel_config c = dma_channel_get_default_config(dma_tx_chan);
        channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
        channel_config_set_read_increment(&c, true);
        channel_config_set_write_increment(&c, false);
        channel_config_set_dreq(&c, pio_get_dreq(pio, sm_tx, true));
        channel_config_set_high_priority(&c, true);

        dma_channel_configure(
            dma_tx_chan,
            &c,
            &pio->txf[sm_tx],
            tx_silence_buf,
            DMA_WORDS_PER_BLOCK,
            false
        );

        dma_channel_set_irq0_enabled(dma_tx_chan, true);
    }

    irq_set_exclusive_handler(DMA_IRQ_0, dma_irq0_handler);
    irq_set_enabled(DMA_IRQ_0, true);
}

static void audio_start(void) {
    rx_fill_index = 0;
    tx_expected_index = 0;
    tx_bootstrap_remaining = 2;
    rx_block_ready[0] = false;
    rx_block_ready[1] = false;
    tx_block_ready[0] = false;
    tx_block_ready[1] = false;
    output_conditioner = {};
    output_conditioner.trim = 1.0f;
    output_conditioner.dither_rng = time_us_32() ^ 0xC001C0DEu;

    pio_sm_set_enabled(pio, sm_clk, false);
    pio_sm_set_enabled(pio, sm_tx, false);
    pio_sm_set_enabled(pio, sm_rx, false);

    pio_sm_clear_fifos(pio, sm_tx);
    pio_sm_clear_fifos(pio, sm_rx);

    pio_sm_restart(pio, sm_clk);
    pio_sm_restart(pio, sm_tx);
    pio_sm_restart(pio, sm_rx);

    start_rx_dma_to_buffer(0);
    start_tx_dma_from_buffer(tx_silence_buf);
    i2s_start_pio();
}

// ============================================================================
// DSP por bloco
// ============================================================================

static void process_block(int block_index) {
    for (int i = 0; i < PERIOD; i++) {
        dsp_in_l[i] = pcm24_to_float(rx_dma_buf[block_index][2 * i + 0]);
        dsp_in_r[i] = pcm24_to_float(rx_dma_buf[block_index][2 * i + 1]);
    }

    univibe.out(dsp_in_l, dsp_in_r);

    for (int i = 0; i < PERIOD; i++) {
        const float out_l = condition_output_sample(output_conditioner, dsp_out_l[i], output_conditioner.dc_l);
        const float out_r = condition_output_sample(output_conditioner, dsp_out_r[i], output_conditioner.dc_r);
        tx_dma_buf[block_index][2 * i + 0] = float_to_pcm24(out_l, output_conditioner.dither_rng);
        tx_dma_buf[block_index][2 * i + 1] = float_to_pcm24(out_r, output_conditioner.dither_rng);
    }

    __dmb();
    tx_block_ready[block_index] = true;
}

// ============================================================================
// main
// ============================================================================

int main() {
    if (!setup_audio_sys_clock()) {
        while (true) {
            tight_loop_contents();
        }
    }

    stdio_init_all();
    sleep_ms(80);

    univibe.reseed(time_us_32());

    setup_mclk(PIN_MCLK);
    sleep_ms(20);

    i2s_init_pio();
    dma_audio_init();

    univibe.mode_chorus = true;
#if USER_INTERFACE
    vibe_ui.init(&univibe);
#endif

    audio_start();

    absolute_time_t last_report = get_absolute_time();

    while (true) {
#if USER_INTERFACE
        vibe_ui.poll();
#endif

        if (rx_block_ready[0]) {
            __dmb();
            rx_block_ready[0] = false;
            process_block(0);
        } else if (rx_block_ready[1]) {
            __dmb();
            rx_block_ready[1] = false;
            process_block(1);
        } else {
            tight_loop_contents();
        }

        if (absolute_time_diff_us(last_report, get_absolute_time()) > 1000000) {
            last_report = get_absolute_time();
            printf("RX overruns=%lu  TX underruns=%lu\n",
                   (unsigned long)rx_overruns,
                   (unsigned long)tx_underruns);
        }
    }
}
