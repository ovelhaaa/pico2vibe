#pragma once

#include <cstdint>
#include <vector>

struct UnivibeParams {
    enum class EngineMode {
        legacy,
        improved,
    };
    enum class CompareMode {
        none,
        difference,
    };
    enum class Preset {
        classic_chorus,
        classic_vibrato,
        deep_throb,
        modern_wide,
    };

    bool mode_chorus = true;
    float rate_hz = 1.2f;
    float depth = 0.6f;
    float feedback = 0.45f;
    float mix = 0.5f;
    float input_drive = 3.5f;
    float output_gain = 1.0f;
    float tone_tilt = 0.0f;
    float pre_hpf_hz = 22.0f;
    float sat_asymmetry = 0.08f;
    float sat_out_trim = 0.95f;
    Preset preset = Preset::classic_chorus;
    EngineMode engine_mode = EngineMode::improved;
    CompareMode compare_mode = CompareMode::none;
    uint32_t seed = 1;
};

struct AudioMetrics {
    float peak = 0.0f;
    float rms = 0.0f;
    uint64_t clipping_count = 0;
    float dc_offset = 0.0f;
};

class DesktopUnivibeProcessor {
public:
    explicit DesktopUnivibeProcessor(const UnivibeParams& params);
    ~DesktopUnivibeProcessor();

    DesktopUnivibeProcessor(const DesktopUnivibeProcessor&) = delete;
    DesktopUnivibeProcessor& operator=(const DesktopUnivibeProcessor&) = delete;

    void process_in_place(std::vector<float>& left, std::vector<float>& right);
    AudioMetrics last_metrics() const;

private:
    struct Impl;
    Impl* impl_ = nullptr;
};
