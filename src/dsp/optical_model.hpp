#pragma once
#include <cmath>
#include <cstdint>

enum class OpticalTopology { SingleLampReference, StudioStereo };

enum class OpticalMode { LegacyOptical, ReferenceOptical };

// Table 1, Darabundit/Wedelich/Bischoff, DAFx-19. Summary constraints,
// not raw trajectory fitting. The last table row is mislabelled "2" in the PDF.
struct OpticalCellCalibration {
    float measuredMeanResistance, measuredMinResistance, measuredMaxResistance;
    float sensitivity = 1, responseScale = 1;
};
inline constexpr OpticalCellCalibration kDafxCells[4] = {
    {405000, 12700, 2790000}, {233000, 6860, 2590000},
    {290000, 7690, 3320000}, {240000, 6220, 4160000}
};
// DAFx-constrained parametric optical model.
struct OpticalCalibration {
    float lampAttack = 0.024f, lampRelease = 0.070f;
    float lampGamma = 1.5f, lampBias = 0.42f, intensityBias = 0.04f, lampDrive = 1.0f;
    float ldrAttack = 0.003f, ldrRelease = 0.012f, ldrCurve = 1.2f;
    OpticalCellCalibration cells[4] = {kDafxCells[0], kDafxCells[1], kDafxCells[2], kDafxCells[3]};
    // Musical sweep position -> equilibrium optical brightness.
    float regionPower = 3.5f;
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
        cachedRegionLo_ = cachedRegionHi_ = -1;

        seed_ = seed;
        uint32_t rng = seed ^ 0x75A42D19u;
        for (int i = 0; i < 4; ++i) {
            rng = rng * 1664525u + 1013904223u;
            float random = float(rng >> 8) / 16777215.0f * 2 - 1;
            auto& cell = calibration_.cells[i];
            cell.measuredMinResistance = bound(cell.measuredMinResistance, 100, 1e6f);
            cell.measuredMaxResistance = bound(cell.measuredMaxResistance, cell.measuredMinResistance, 5e6f);
            cell.measuredMeanResistance = bound(cell.measuredMeanResistance,
                cell.measuredMinResistance * 1.01f, cell.measuredMaxResistance / 1.01f);
            scale_[i] = 1 + bound(c.tolerance, 0, 0.025f) * random;
            // Log-resistance curve anchored at the measured mean at b=0.5.
            // This pivot is an engineering choice, not a measured brightness value.
            float span = std::log(cell.measuredMaxResistance / cell.measuredMinResistance);
            float pivot = std::log(cell.measuredMaxResistance / cell.measuredMeanResistance) / span;
            float exponent = bound(std::log(pivot) / std::log(0.5f) *
                c.ldrCurve / 1.2f * cell.sensitivity, 0.3f, 4);
            for (int j = 0; j <= 64; ++j) {
                float light = std::pow(float(j) / 64, exponent);
                conductance_[i][j] = std::exp(light * span) /
                    (cell.measuredMaxResistance * scale_[i]);
            }
            cellUp_[i] = coefficient(c.ldrAttack * bound(cell.responseScale, 0.5f, 2));
            cellDown_[i] = coefficient(c.ldrRelease * bound(cell.responseScale, 0.5f, 2));
        }
        for (int j = 0; j <= 64; ++j) {
            brightness_[j] = std::pow(float(j) / 64, bound(c.lampGamma, 0.5f, 4));
            float region = std::pow(float(j) / 64, bound(c.regionPower, 1, 6));
            // Inverse steady-state power/emission law; controls are optical
            // regions, not normalized measured lamp voltages.
            regionDrive_[j] = std::pow(region, 1 / (2 * bound(c.lampGamma, 0.5f, 4)));
        }
        heat_ = coefficient(calibration_.lampAttack * lag_);
        cool_ = coefficient(calibration_.lampRelease * lag_);
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
        return bound(1 / lookup(conductance_[cell], b),
            calibration_.cells[cell].measuredMinResistance * scale_[cell],
            calibration_.cells[cell].measuredMaxResistance * scale_[cell]);
    }
    float drive_for_region(float region) const { return lookup(regionDrive_, region); }
    const OpticalCellCalibration& cell_calibration(int cell) const { return calibration_.cells[cell]; }
    float cell_min(int cell) const { return calibration_.cells[cell].measuredMinResistance * scale_[cell]; }
    float cell_max(int cell) const { return calibration_.cells[cell].measuredMaxResistance * scale_[cell]; }
    // Phase belongs to EffectLFO; excitation is unipolar and independent of Depth.
    const OpticalFrame& process_sample(float phase, float excitation, float intensity,
                                      float sweepMin, float sweepMax) {
        float d = bound(intensity, 0, 1);
        float regionLo = bound(sweepMin, 0, 1), regionHi = bound(sweepMax, regionLo, 1);
        if (regionLo != cachedRegionLo_ || regionHi != cachedRegionHi_) {
            cachedRegionLo_ = regionLo; cachedRegionHi_ = regionHi;
            driveLo_ = drive_for_region(regionLo); driveHi_ = drive_for_region(regionHi);
        }
        float bias = bound(calibration_.lampBias, 0, 1) + bound(calibration_.intensityBias, -0.2f, 0.2f) * d;
        float u = bound(bias + d * bound(calibration_.lampDrive, 0, 1) * (bound(excitation, 0, 1) - 0.5f), 0, 1);
        frame_.phase = phase;
        frame_.drive = driveLo_ + (driveHi_ - driveLo_) * u;
        float power = frame_.drive * frame_.drive;
        temperature_ += (power > temperature_ ? heat_ : cool_) * (power - temperature_);
        temperature_ = bound(temperature_, 0, 1);
        frame_.brightness = lookup(brightness_, temperature_);
        for (int i = 0; i < 4; ++i) {
            light_[i] += (frame_.brightness > light_[i] ? cellUp_[i] : cellDown_[i]) * (frame_.brightness - light_[i]);
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
    float heat_ = 0, cool_ = 0;
    float cachedRegionLo_ = -1, cachedRegionHi_ = -1, driveLo_ = 0, driveHi_ = 0;
    float cellUp_[4]{}, cellDown_[4]{}, regionDrive_[65]{};
    float scale_[4]{}, light_[4]{}, brightness_[65]{}, conductance_[4][65]{};
    uint32_t seed_ = 1;
};
