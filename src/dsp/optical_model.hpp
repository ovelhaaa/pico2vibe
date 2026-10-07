#pragma once
#include <cmath>
#include <cstdint>

enum class OpticalMode { LegacyOptical, ReferenceOptical };

// Engineering calibration, not a fit to unavailable DAFx trajectory samples.
struct OpticalCalibration {
    float lampAttack = 0.024f, lampRelease = 0.070f;
    float lampGamma = 1.5f, lampBias = 0.46f, intensityBias = 0.06f, lampDrive = 0.96f;
    float ldrAttack = 0.003f, ldrRelease = 0.012f, ldrCurve = 1.2f;
    float minResistance = 3900.0f, maxResistance = 1000000.0f;
    float cellScale[4] = {0.405f / 0.290f, 0.233f / 0.290f, 1.0f, 0.240f / 0.290f};
    float cellSensitivity[4] = {0.92f, 1.04f, 1.0f, 1.08f};
    float tolerance = 0.012f;
};

struct OpticalFrame {
    float phase = 0, drive = 0, brightness = 0;
    float resistance[4] = {1000000, 1000000, 1000000, 1000000};
};

class OpticalModel {
public:
    OpticalModel() { configure(calibration_, seed_); reset(); }
    static float bound(float x, float lo, float hi) {
        return !std::isfinite(x) ? lo : (x < lo ? lo : (x > hi ? hi : x));
    }
    void prepare(float sampleRate) {
        sampleRate_ = bound(sampleRate, 8000, 384000);
        configure(calibration_, seed_);
        reset();
    }
    void configure(const OpticalCalibration& c, uint32_t seed) {
        calibration_ = c;
        calibration_.minResistance = bound(c.minResistance, 100, 1e6f);
        calibration_.maxResistance = bound(c.maxResistance, calibration_.minResistance, 5e6f);
        seed_ = seed;
        uint32_t rng = seed ^ 0x75A42D19u;
        for (int i = 0; i < 4; ++i) {
            rng = rng * 1664525u + 1013904223u;
            float random = float(rng >> 8) / 16777215.0f * 2 - 1;
            scale_[i] = bound(c.cellScale[i], 0.5f, 2.0f) *
                (1 + bound(c.tolerance, 0, 0.025f) * random);
            for (int j = 0; j <= 64; ++j) {
                // LUT is in conductance, with per-cell sensitivity before inversion.
                float light = std::pow(float(j) / 64, bound(c.ldrCurve * c.cellSensitivity[i], 0.3f, 4));
                conductance_[i][j] = (1 / calibration_.maxResistance +
                    (1 / calibration_.minResistance - 1 / calibration_.maxResistance) * light) / scale_[i];
            }
        }
        for (int j = 0; j <= 64; ++j)
            brightness_[j] = std::pow(float(j) / 64, bound(c.lampGamma, 0.5f, 4));
        heat_ = coefficient(calibration_.lampAttack * lag_);
        cool_ = coefficient(calibration_.lampRelease * lag_);
        ldrUp_ = coefficient(calibration_.ldrAttack);
        ldrDown_ = coefficient(calibration_.ldrRelease);
    }
    void reset() {
        temperature_ = 0;
        frame_ = {};
        for (int i = 0; i < 4; ++i) {
            light_[i] = 0;
            frame_.resistance[i] = resistance_for_brightness(0, i);
        }
    }
    void set_lag(float lag) {
        float next = bound(lag, 0.35f, 2.5f);
        if (std::abs(next - lag_) < 1.0e-7f) return;
        lag_ = next;
        heat_ = coefficient(calibration_.lampAttack * lag_);
        cool_ = coefficient(calibration_.lampRelease * lag_);
    }
    float resistance_for_brightness(float b, int cell) const {
        cell = cell < 0 ? 0 : (cell > 3 ? 3 : cell);
        return bound(1 / lookup(conductance_[cell], b), calibration_.minResistance, calibration_.maxResistance);
    }
    // Phase belongs to EffectLFO; excitation is unipolar and independent of Depth.
    const OpticalFrame& process_sample(float phase, float excitation, float intensity,
                                      float sweepMin, float sweepMax) {
        float d = bound(intensity, 0, 1);
        float lo = bound(sweepMin, 0, 1), hi = bound(sweepMax, lo, 1);
        float bias = bound(calibration_.lampBias, 0, 1) + bound(calibration_.intensityBias, -0.2f, 0.2f) * d;
        float u = bound(bias + d * bound(calibration_.lampDrive, 0, 1) * (bound(excitation, 0, 1) - 0.5f), 0, 1);
        frame_.phase = phase;
        frame_.drive = lo + (hi - lo) * u;
        float power = frame_.drive * frame_.drive;
        temperature_ += (power > temperature_ ? heat_ : cool_) * (power - temperature_);
        temperature_ = bound(temperature_, 0, 1);
        frame_.brightness = lookup(brightness_, temperature_);
        for (int i = 0; i < 4; ++i) {
            light_[i] += (frame_.brightness > light_[i] ? ldrUp_ : ldrDown_) * (frame_.brightness - light_[i]);
            frame_.resistance[i] = resistance_for_brightness(light_[i], i);
        }
        return frame_;
    }
    const OpticalFrame& frame() const { return frame_; }
    float temperature() const { return temperature_; }
private:
    static float lookup(const float* lut, float value) {
        float p = bound(value, 0, 1) * 64;
        int i = int(p);
        return i == 64 ? lut[64] : lut[i] + (p - i) * (lut[i + 1] - lut[i]);
    }
    float coefficient(float seconds) const {
        // expm1 prevents cancellation at high sample rates. No artificial alpha floor.
        return -std::expm1(-1 / (sampleRate_ * bound(seconds, 0.0005f, 1.0f)));
    }
    OpticalCalibration calibration_{};
    OpticalFrame frame_{};
    float sampleRate_ = 44100, lag_ = 1, temperature_ = 0;
    float heat_ = 0, cool_ = 0, ldrUp_ = 0, ldrDown_ = 0;
    float scale_[4]{}, light_[4]{}, brightness_[65]{}, conductance_[4][65]{};
    uint32_t seed_ = 1;
};
