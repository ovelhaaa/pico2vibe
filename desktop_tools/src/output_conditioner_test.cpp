#include "dsp/vibe_core.hpp"
#include <array>
#include <iostream>
#include <stdexcept>

// Independent stage combinations must retain their documented order and defaults.
int main() {
    try {
        for (float sr : {44100.0f, 48000.0f, 96000.0f, 192000.0f}) {
            for (int mask = 0; mask < 8; ++mask) {
                VibeOutputConditioner conditioner;
                conditioner.configure({(mask & 1) != 0, (mask & 2) != 0, (mask & 4) != 0});
                conditioner.reset(sr);
                std::array<float, 2> x1{}, y1{};
                float envelope = 0.0f, trim = 1.0f;
                const float dc = expf(-2.0f * kPi * 8.0f / sr);
                for (int i = 0; i < 4096; ++i) {
                    const std::array<float, 2> input {i < 100 ? 1.8f : 0.18f, i < 200 ? -1.2f : 0.09f};
                    auto expected = input;
                    for (int ch = 0; ch < 2; ++ch) {
                        if (mask & 1) {
                            expected[ch] = (input[ch] - x1[ch]) + dc * y1[ch];
                            x1[ch] = input[ch];
                            y1[ch] = expected[ch];
                        }
                    }
                    if (mask & 2) {
                        const float peak = fmaxf(fabsf(expected[0]), fabsf(expected[1]));
                        envelope += (peak > envelope ? 0.14f : 0.003f) * (peak - envelope);
                        const float target = envelope > 0.92f ? 0.92f / (envelope + 1e-12f) : 1.0f;
                        trim += (target < trim ? 0.20f : 0.0015f) * (target - trim);
                        for (auto& sample : expected) sample *= trim;
                    }
                    if (mask & 4) {
                        for (auto& sample : expected) {
                            const float x = sample * 1.25f;
                            sample = x * (27.0f + x * x) / (27.0f + 9.0f * x * x) * (1.0f / 1.25f);
                        }
                    }
                    float left, right;
                    conditioner.process_frame(input[0], input[1], &left, &right);
                    if (!std::isfinite(left) || !std::isfinite(right)
                        || fabsf(left - expected[0]) > 2e-5f || fabsf(right - expected[1]) > 2e-5f)
                        throw std::runtime_error("output stage configuration/order regression");
                }
                conditioner.reset(sr);
                float left, right;
                conditioner.process_frame(0.0f, 0.0f, &left, &right);
                if (left != 0.0f || right != 0.0f) throw std::runtime_error("reset retained output history");
            }
        }
        VibeOutputConditioner defaults;
        const auto config = defaults.configuration();
        if (!config.dc_blocker || !config.auto_headroom || !config.soft_limiter)
            throw std::runtime_error("production defaults changed");
        std::cout << "output_conditioner_test passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
