#include "dsp/vibe_core.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {

void require_near(float actual, float expected, float tolerance, const char* message) {
    if (!std::isfinite(actual) || std::fabs(actual - expected) > tolerance) {
        throw std::runtime_error(message);
    }
}

float dc_gain(const GreyBoxStageCoefficients& c) {
    return (c.b0 + c.b1) / (1.0f + c.a1);
}

float nyquist_gain(const GreyBoxStageCoefficients& c) {
    return (c.b0 - c.b1) / (1.0f - c.a1);
}

}  // namespace

int main() {
    try {
        constexpr float cp = 0.015e-6f;
        constexpr float cdc = 1.0e-6f;
        constexpr float emitter_gain = 1.01f;
        constexpr float collector_gain = 1.11f;
        const auto stage1 = greybox_stage_coefficients(409700.0f, cp, cdc,
                                                       emitter_gain, collector_gain, 44100.0f);
        const float kc = cp / (cp + cdc);
        const float ke = cdc / (cp + cdc);
        require_near(dc_gain(stage1), emitter_gain * ke - collector_gain * kc, 1.0e-4f,
                     "grey-box DC gain does not match the analog transfer function");
        require_near(nyquist_gain(stage1), -collector_gain, 1.0e-4f,
                     "grey-box high-frequency gain does not match the collector leg");
        if (!(std::fabs(stage1.a1) < 1.0f)) {
            throw std::runtime_error("grey-box pole is not stable");
        }

        // The large 220 nF cell must not collapse to an ideal all-pass: its
        // finite blocking capacitor creates the strong shelf described in the paper.
        const auto stage2 = greybox_stage_coefficients(237700.0f, 0.22e-6f, cdc,
                                                       0.98f, 1.09f, 44100.0f);
        const float shelf_db = 20.0f * std::log10(std::fabs(nyquist_gain(stage2) / dc_gain(stage2)));
        if (!(shelf_db > 4.0f && shelf_db < 7.0f)) {
            throw std::runtime_error("grey-box shelf lost its measured Uni-Vibe character");
        }

        std::cout << "greybox_stage_test passed: stage2_shelf_db=" << shelf_db << '\n';
        return EXIT_SUCCESS;
    } catch (const std::exception& e) {
        std::cerr << "greybox_stage_test failed: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
}
