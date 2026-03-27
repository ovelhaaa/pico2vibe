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

static inline float fast_soft_clip(float x) {
    return x / (1.0f + fabsf(x));
}

#ifndef VIBE_ENABLE_LOCAL_2X_OVERSAMPLE
#define VIBE_ENABLE_LOCAL_2X_OVERSAMPLE 1
#endif

enum SaturationMode : uint8_t {
    SAT_FALLBACK_SOFT = 0,
    SAT_CLASSIC_BJT = 1,
    SAT_SMOOTH_HIFI = 2
};

#ifndef VIBE_SATURATION_MODE
#define VIBE_SATURATION_MODE SAT_CLASSIC_BJT
#endif

static inline float clampf(float v, float lo, float hi) {
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

static inline float noise_bipolar(uint32_t &state) {
    state = state * 1664525u + 1013904223u;
    const float uni = (float)((state >> 8) & 0x00FFFFFFu) * (1.0f / 16777215.0f);
    return uni * 2.0f - 1.0f;
}

constexpr float kPi = 3.14159265358979323846f;

struct VibeUserParams {
    float depth = 0.85f;
    float feedback = 0.42f;
    float mix = 0.50f;
    float input_drive = 3.5f;
    float output_gain = 1.0f;
    float sweep_min = 0.58f;
    float sweep_max = 0.98f;
    float lfo_rate_hz = 1.20f;
    float drift_amount = 0.018f;
    float drift_rate_hz = 0.08f;
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
};

struct VibeParams {
    VibeUserParams user;
    VibeTuningParams tuning;
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
        case VibeParamId::InputDrive: return {0.5f, 6.0f, 3.5f};
        case VibeParamId::OutputGain: return {0.25f, 2.0f, 1.0f};
        case VibeParamId::SweepMin:   return {0.0f, 1.0f, 0.58f};
        case VibeParamId::SweepMax:   return {0.0f, 1.0f, 0.98f};
        case VibeParamId::LfoRateHz:  return {0.02f, 12.0f, 1.20f};
        case VibeParamId::DriftAmount:return {0.0f, 0.05f, 0.018f};
        case VibeParamId::DriftRateHz:return {0.005f, 0.5f, 0.08f};
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
}

// ============================================================================
// LFO
// ============================================================================

class EffectLFO {
private:
    float phase = 0.0f;
    float state_l = 0.0f;
    float state_r = 0.0f;
    float drift_state = 0.0f;
    uint32_t drift_rng = 0xA341316Cu;

public:
    void reseed(uint32_t seed) {
        drift_rng = seed ? seed : 0xA341316Cu;
        drift_state = 0.0f;
        phase = 0.0f;
        state_l = 0.0f;
        state_r = 0.0f;
    }

    void processBlock(float *l, float *r, const VibeUserParams &user, const VibeTuningParams &tuning) {
        const float block_time = fPERIOD / fSAMPLE_RATE;
        const float freq = clampf(user.lfo_rate_hz, 0.02f, 12.0f);
        const float drift_amount = clampf(user.drift_amount, 0.0f, 0.05f);
        const float drift_rate_hz = clampf(user.drift_rate_hz, 0.005f, 0.5f);
        const float drift_alpha = 1.0f - expf(-2.0f * kPi * drift_rate_hz * block_time);

        // Slow filtered noise gives continuous drift without block-rate stepping.
        drift_state += drift_alpha * (noise_bipolar(drift_rng) - drift_state);
        drift_state = clampf(drift_state, -1.0f, 1.0f);

        const float drift = 1.0f + drift_amount * drift_state;
        phase += freq * drift * cSAMPLE_RATE * fPERIOD;
        if (phase >= 1.0f) phase -= 1.0f;

        float tri_l = (phase < 0.4f) ? (phase * 2.5f) : (1.0f - (phase - 0.4f) * 1.6666f);

        float p_r = phase + clampf(tuning.stereo_phase_offset, 0.0f, 0.5f);
        if (p_r >= 1.0f) p_r -= 1.0f;
        float tri_r = (p_r < 0.4f) ? (p_r * 2.5f) : (1.0f - (p_r - 0.4f) * 1.6666f);

        const float smoothing = clampf(tuning.lfo_shape_smoothing, 0.01f, 1.0f);
        state_l += smoothing * (tri_l - state_l);
        state_r += smoothing * (tri_r - state_r);

        *l = clampf(state_l, 0.0f, 1.0f);
        *r = clampf(state_r, 0.0f, 1.0f);
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

    const VibeUserParams &user_params() const { return params.user; }
    const VibeUserParams &smoothed_user_params() const { return smoothed_user; }
    VibeTuningParams &tuning_params() { return params.tuning; }

    bool mode_chorus = true;

private:
    struct SaturationState {
        float prev_in = 0.0f;
        float aa_up = 0.0f;
        float aa_down = 0.0f;
        float dc_x1 = 0.0f;
        float dc_y1 = 0.0f;
    };

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
    SaturationState sat_state_l;
    SaturationState sat_state_r;
    float fb_lpf_l = 0.0f;
    float fb_lpf_r = 0.0f;

    float vibefilter(float data, fparams *ftype);
    void modulate(float res_l, float res_r);
    void update_time_constants();
    void update_smoothed_user_params();
    float nonlinear_shape(float data, float drive, SaturationState &state);
    float feedback_shape(float data, float feedback, SaturationState &state, float &fb_lpf_state);
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
    sanitize_user_params(&params.user);
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
    sat_state_l = {};
    sat_state_r = {};
    fb_lpf_l = 0.0f;
    fb_lpf_r = 0.0f;
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
    sanitize_user_params(&smoothed_user);
}

float Vibe::nonlinear_shape(float data, float drive, SaturationState &state) {
    auto saturate = [](float x) -> float {
        switch (VIBE_SATURATION_MODE) {
            case SAT_CLASSIC_BJT: {
                const float asym = x + 0.12f * x * x;
                return tanhf(1.35f * asym);
            }
            case SAT_SMOOTH_HIFI: {
                const float x2 = x * x;
                return x * (27.0f + x2) / (27.0f + 9.0f * x2);
            }
            case SAT_FALLBACK_SOFT:
            default:
                return fast_soft_clip(x);
        }
    };

    const float driven = data * drive;
#if VIBE_ENABLE_LOCAL_2X_OVERSAMPLE
    // Local 2x oversampling around the nonlinear shape to reduce foldback when driven hard.
    const float up_alpha = 0.62f;
    const float down_alpha = 0.55f;

    const float x_mid = 0.5f * (state.prev_in + driven);
    state.prev_in = driven;

    state.aa_up += up_alpha * (x_mid - state.aa_up);
    const float y0 = saturate(state.aa_up);

    state.aa_up += up_alpha * (driven - state.aa_up);
    const float y1 = saturate(state.aa_up);

    float out = 0.5f * (y0 + y1);
    state.aa_down += down_alpha * (out - state.aa_down);
    out = state.aa_down;
#else
    float out = saturate(driven);
#endif

    // Lightweight DC blocker avoids low-frequency accumulation through the feedback loop.
    const float dc_r = 0.995f;
    const float dc = out - state.dc_x1 + dc_r * state.dc_y1;
    state.dc_x1 = out;
    state.dc_y1 = dc;
    return dc * params.tuning.bjt_gain_trim;
}

float Vibe::feedback_shape(float data, float feedback, SaturationState &state, float &fb_lpf_state) {
    const float fb_drive = 0.9f + 0.5f * feedback;
    const float nonlinear_fb = nonlinear_shape(data, fb_drive, state);
    const float fb_lpf = 0.22f;
    fb_lpf_state += fb_lpf * (nonlinear_fb - fb_lpf_state);

    // Dynamic damping softens runaway behavior as internal level grows.
    const float level = fabsf(fb_lpf_state);
    const float safety = 1.0f / (1.0f + 0.40f * level);
    return clampf(fb_lpf_state * feedback * safety, -1.0f, 1.0f);
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
        stage[i].ldr_mismatch = 1.0f + 0.05f * noise_bipolar(component_rng);
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
    update_smoothed_user_params();

    float lfol, lfor;
    lfo.processBlock(&lfol, &lfor, smoothed_user, params.tuning);

    const float depth = smoothed_user.depth;
    const float sweep_min = smoothed_user.sweep_min;
    const float sweep_max = smoothed_user.sweep_max;
    const float feedback = smoothed_user.feedback;
    const float mix = smoothed_user.mix;
    const float input_drive = smoothed_user.input_drive;
    const float output_gain = smoothed_user.output_gain;
    const float stage_limit = clampf(params.tuning.stage_state_limit, 2.0f, 12.0f);

    float target_l = sweep_min + depth * lfol * (sweep_max - sweep_min);
    float target_r = sweep_min + depth * lfor * (sweep_max - sweep_min);

    if (target_l > lamp_state_l) lamp_state_l += lamp_attack * (target_l - lamp_state_l);
    else                         lamp_state_l += lamp_release * (target_l - lamp_state_l);

    if (target_r > lamp_state_r) lamp_state_r += lamp_attack * (target_r - lamp_state_r);
    else                         lamp_state_r += lamp_release * (target_r - lamp_state_r);

    lamp_state_l = clampf(lamp_state_l, 0.0f, 1.0f);
    lamp_state_r = clampf(lamp_state_r, 0.0f, 1.0f);

    float bright_l = lamp_state_l * sqrtf(lamp_state_l);
    float bright_r = lamp_state_r * sqrtf(lamp_state_r);

    // LDR clamp keeps the light-dependent network in a plausible range and avoids degenerate coefficient sets.
    float res_l = params.tuning.ldr_dark_ohms * expf(-params.tuning.ldr_curve * bright_l);
    float res_r = params.tuning.ldr_dark_ohms * expf(-params.tuning.ldr_curve * bright_r);
    res_l = clampf(res_l, params.tuning.ldr_min_ohms, params.tuning.ldr_max_ohms);
    res_r = clampf(res_r, params.tuning.ldr_min_ohms, params.tuning.ldr_max_ohms);

    modulate(res_l, res_r);

    // Clamp emitter feedback to keep the stage feedback lively but out of self-oscillating extremes.
    float emitterfb_l = clampf(params.tuning.emitter_fb_scale / res_l,
                               params.tuning.emitter_fb_min,
                               params.tuning.emitter_fb_max);
    float emitterfb_r = clampf(params.tuning.emitter_fb_scale / res_r,
                               params.tuning.emitter_fb_min,
                               params.tuning.emitter_fb_max);

    for (int i = 0; i < PERIOD; i++) {
        float dry_l = smpsl[i];
        float input = nonlinear_shape(fbl + dry_l, input_drive, sat_state_l);

        for (int j = 0; j < 4; j++) {
            float cvolt = vibefilter(input, &stage[j].ecvc) +
                          vibefilter(input + emitterfb_l * stage[j].oldcvolt, &stage[j].vc);
            cvolt = clampf(cvolt, -stage_limit, stage_limit);
            float ocvolt = clampf(vibefilter(cvolt, &stage[j].vcvo), -stage_limit, stage_limit);
            stage[j].oldcvolt = ocvolt;
            input = nonlinear_shape(ocvolt + vibefilter(input, &stage[j].vevo), input_drive, sat_state_l);
        }

        fbl = feedback_shape(stage[3].oldcvolt, feedback, sat_state_l, fb_lpf_l);
        efxoutl[i] = output_gain * (mode_chorus ? lpanning * (dry_l * (1.0f - mix) + input * mix)
                                                : lpanning * input);

        float dry_r = smpsr[i];
        input = nonlinear_shape(fbr + dry_r, input_drive, sat_state_r);

        for (int j = 4; j < 8; j++) {
            float cvolt = vibefilter(input, &stage[j].ecvc) +
                          vibefilter(input + emitterfb_r * stage[j].oldcvolt, &stage[j].vc);
            cvolt = clampf(cvolt, -stage_limit, stage_limit);
            float ocvolt = clampf(vibefilter(cvolt, &stage[j].vcvo), -stage_limit, stage_limit);
            stage[j].oldcvolt = ocvolt;
            input = nonlinear_shape(ocvolt + vibefilter(input, &stage[j].vevo), input_drive, sat_state_r);
        }

        fbr = feedback_shape(stage[7].oldcvolt, feedback, sat_state_r, fb_lpf_r);
        efxoutr[i] = output_gain * (mode_chorus ? rpanning * (dry_r * (1.0f - mix) + input * mix)
                                                : rpanning * input);
    }
}

// ============================================================================
// Conversão PCM <-> float
// ============================================================================

static inline float pcm24_to_float(int32_t v) {
    v >>= 8;
    return (float)v * (1.0f / 8388608.0f);
}

static inline int32_t float_to_pcm24(float v) {
    if (v > 1.0f) v = 1.0f;
    if (v < -1.0f) v = -1.0f;
    return ((int32_t)(v * 8388607.0f)) << 8;
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
        tx_dma_buf[block_index][2 * i + 0] = float_to_pcm24(dsp_out_l[i]);
        tx_dma_buf[block_index][2 * i + 1] = float_to_pcm24(dsp_out_r[i]);
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
