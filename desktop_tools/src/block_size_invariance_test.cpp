#include "dsp/vibe_core.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
constexpr std::size_t kFrames = 521;
constexpr float kTolerance = 2.0e-6f;

struct StereoRender {
    std::vector<float> left;
    std::vector<float> right;
};

StereoRender renderWithPattern(const std::vector<float>& sourceLeft,
                               const std::vector<float>& sourceRight,
                               const std::vector<int>& pattern) {
    std::array<float, PERIOD> inputLeft {};
    std::array<float, PERIOD> inputRight {};
    std::array<float, PERIOD> outputLeft {};
    std::array<float, PERIOD> outputRight {};

    Vibe vibe(outputLeft.data(), outputRight.data());
    vibe.prepare(48000.0f);
    vibe.set_voicing(VibeVoicing::ModernHiFiPhaseVibe);
    vibe.set_quality_mode(VibeQualityMode::Standard);
    vibe.set_param(VibeParamId::Depth, 0.73f);
    vibe.set_param(VibeParamId::Feedback, 0.31f);
    vibe.set_param(VibeParamId::Mix, 0.57f);
    vibe.set_param(VibeParamId::InputDrive, 1.8f);
    vibe.set_param(VibeParamId::TempoSync, 1.0f);
    vibe.set_param(VibeParamId::TempoBpm, 123.0f);
    vibe.set_param(VibeParamId::TempoDivisionBeats, 1.5f);
    vibe.set_param(VibeParamId::NoiseAmount, 0.0f);
    vibe.reseed(0x1234ABCDu);

    StereoRender result;
    result.left.resize(sourceLeft.size());
    result.right.resize(sourceRight.size());

    std::size_t pos = 0;
    std::size_t patternIndex = 0;
    while (pos < sourceLeft.size()) {
        const int requested = pattern[patternIndex % pattern.size()];
        const int frames = std::min<int>(std::max(requested, 1),
                                         std::min<std::size_t>(PERIOD, sourceLeft.size() - pos));
        std::fill(inputLeft.begin(), inputLeft.end(), 0.0f);
        std::fill(inputRight.begin(), inputRight.end(), 0.0f);
        std::copy_n(sourceLeft.data() + pos, frames, inputLeft.data());
        std::copy_n(sourceRight.data() + pos, frames, inputRight.data());

        vibe.out(inputLeft.data(), inputRight.data(), frames);
        std::copy_n(outputLeft.data(), frames, result.left.data() + pos);
        std::copy_n(outputRight.data(), frames, result.right.data() + pos);

        pos += static_cast<std::size_t>(frames);
        ++patternIndex;
    }

    return result;
}

float maxDifference(const StereoRender& a, const StereoRender& b) {
    float maximum = 0.0f;
    for (std::size_t i = 0; i < a.left.size(); ++i) {
        maximum = std::max(maximum, std::abs(a.left[i] - b.left[i]));
        maximum = std::max(maximum, std::abs(a.right[i] - b.right[i]));
    }
    return maximum;
}
}  // namespace

int main() {
    try {
        std::vector<float> sourceLeft(kFrames);
        std::vector<float> sourceRight(kFrames);
        for (std::size_t i = 0; i < kFrames; ++i) {
            const float t = static_cast<float>(i) / 48000.0f;
            sourceLeft[i] = 0.24f * std::sin(2.0f * kPi * 173.0f * t)
                          + 0.08f * std::sin(2.0f * kPi * 997.0f * t);
            sourceRight[i] = 0.21f * std::sin(2.0f * kPi * 211.0f * t)
                           - 0.07f * std::sin(2.0f * kPi * 733.0f * t);
        }

        const StereoRender fixed = renderWithPattern(sourceLeft, sourceRight, { PERIOD });
        const StereoRender irregular = renderWithPattern(sourceLeft, sourceRight, { 5, 1, 17, 31, 2, 9, 23 });
        const float error = maxDifference(fixed, irregular);

        if (!std::isfinite(error) || error > kTolerance) {
            throw std::runtime_error("output depends on callback partitioning; max error=" + std::to_string(error));
        }

        std::cout << "block_size_invariance_test passed; max error=" << error << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "block_size_invariance_test failed: " << e.what() << std::endl;
        return 1;
    }
}
