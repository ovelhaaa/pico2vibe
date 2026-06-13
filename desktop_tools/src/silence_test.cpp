#include "processor.hpp"

#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

struct ChannelMetrics {
    float peak = 0.0f;
    float rms = 0.0f;
    uint64_t non_zero = 0;
};

ChannelMetrics measure(const std::vector<float>& x) {
    ChannelMetrics m{};
    long double sum_sq = 0.0;
    for (float v : x) {
        if (v != 0.0f) ++m.non_zero;
        const float a = std::fabs(v);
        if (a > m.peak) m.peak = a;
        sum_sq += static_cast<long double>(v) * static_cast<long double>(v);
    }
    m.rms = x.empty() ? 0.0f : static_cast<float>(std::sqrt(sum_sq / static_cast<long double>(x.size())));
    return m;
}

void require_silent(const char* label, const ChannelMetrics& m) {
    constexpr float kMaxPeak = 1.0e-6f;
    constexpr float kMaxRms = 1.0e-8f;
    std::cout << label << " peak=" << m.peak
              << " rms=" << m.rms
              << " non_zero=" << m.non_zero << "\n";
    if (!(m.peak < kMaxPeak && m.rms < kMaxRms)) {
        throw std::runtime_error(std::string(label) + " silence invariant failed");
    }
}

}  // namespace

int main() {
    try {
        constexpr uint32_t kSampleRate = 44100u;
        constexpr uint32_t kSeconds = 20u;
        constexpr size_t kFrames = static_cast<size_t>(kSampleRate) * kSeconds;

        std::vector<float> left(kFrames, 0.0f);
        std::vector<float> right(kFrames, 0.0f);

        UnivibeParams params;
        params.engine_mode = UnivibeParams::EngineMode::improved;
        params.preset = UnivibeParams::Preset::classic_chorus;
        params.mode_chorus = true;
        params.seed = 1;

        DesktopUnivibeProcessor processor(params);
        processor.process_in_place(left, right);

        require_silent("L", measure(left));
        require_silent("R", measure(right));
    } catch (const std::exception& e) {
        std::cerr << "silence_test failed: " << e.what() << "\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
