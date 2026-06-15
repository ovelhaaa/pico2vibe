#include "processor.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
constexpr uint32_t kSampleRate = 44100u;
constexpr float kPi = 3.14159265358979323846f;

float rms_tail(const std::vector<float>& x, size_t skip) {
    if (skip >= x.size()) return 0.0f;
    long double acc = 0.0;
    for (size_t i = skip; i < x.size(); ++i) acc += static_cast<long double>(x[i]) * x[i];
    return static_cast<float>(std::sqrt(acc / static_cast<long double>(x.size() - skip)));
}

float gain_db_at(float freq_hz) {
    constexpr size_t kFrames = kSampleRate * 3u;
    constexpr size_t kSkip = kSampleRate;
    constexpr float kAmp = 0.125f;
    std::vector<float> left(kFrames), right(kFrames);
    const float w = 2.0f * kPi * freq_hz / static_cast<float>(kSampleRate);
    for (size_t i = 0; i < kFrames; ++i) {
        const float s = kAmp * std::sin(w * static_cast<float>(i));
        left[i] = s;
        right[i] = s;
    }

    UnivibeParams params;
    params.engine_mode = UnivibeParams::EngineMode::improved;
    params.preset = UnivibeParams::Preset::classic_chorus;
    params.mode_chorus = true;
    params.rate_hz = 0.85f;
    params.depth = 0.86f;
    params.feedback = 0.18f;
    params.mix = 0.52f;
    params.seed = 1;

    DesktopUnivibeProcessor processor(params);
    processor.process_in_place(left, right);
    const float out_rms = rms_tail(left, kSkip);
    const float in_rms = kAmp * 0.70710678f;
    return 20.0f * std::log10((out_rms + 1.0e-9f) / (in_rms + 1.0e-9f));
}

void require_silence_stable() {
    constexpr size_t kFrames = kSampleRate * 5u;
    std::vector<float> left(kFrames, 0.0f), right(kFrames, 0.0f);
    UnivibeParams params;
    params.engine_mode = UnivibeParams::EngineMode::improved;
    params.preset = UnivibeParams::Preset::classic_chorus;
    params.mode_chorus = true;
    params.seed = 1;
    DesktopUnivibeProcessor processor(params);
    processor.process_in_place(left, right);
    const float peak = std::max(*std::max_element(left.begin(), left.end()), -*std::min_element(left.begin(), left.end()));
    if (peak >= 1.0e-6f) {
        throw std::runtime_error("silence produced non-zero output");
    }
}
}  // namespace

int main() {
    try {
        require_silence_stable();
        const float freqs[] = {80.0f, 140.0f, 220.0f, 360.0f, 560.0f, 820.0f, 1200.0f, 1800.0f, 2700.0f, 3900.0f};
        float min_db = 100.0f;
        float max_db = -100.0f;
        for (float f : freqs) {
            const float g = gain_db_at(f);
            std::cout << "freq_hz=" << f << " gain_db=" << g << "\n";
            min_db = std::min(min_db, g);
            max_db = std::max(max_db, g);
        }
        std::cout << "min_gain_db=" << min_db << " max_gain_db=" << max_db
                  << " spread_db=" << (max_db - min_db) << "\n";
        if (!(min_db < -2.5f && (max_db - min_db) > 3.5f)) {
            throw std::runtime_error("classic chorus did not produce an objective phaser notch");
        }
        return EXIT_SUCCESS;
    } catch (const std::exception& e) {
        std::cerr << "phase_notch_test failed: " << e.what() << "\n";
        return EXIT_FAILURE;
    }
}
