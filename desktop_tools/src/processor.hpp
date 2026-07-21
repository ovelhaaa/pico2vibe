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
    enum class QualityMode {
        eco,
        standard,
        high,
    };
    enum class Preset {
        classic_chorus,
        classic_vibrato,
        deep_throb,
        modern_wide,
        vintage_univibe_chorus,
        deep_hendrix_swirl,
        trower_lead,
        gentle_clean_vibe,
        wide_stereo_dream,
        vintage_vibrato,
        shallow_always_on,
        psychedelic_slow_sweep,
        fast_rotary_vibe,
        bass_synth_friendly,
        lo_fi_lamp_drift,
        modern_hifi_phase_vibe,
    };

    bool mode_chorus = true;
    float sample_rate_hz = 44100.0f;
    float rate_hz = 0.85f;
    float depth = 0.78f;
    float feedback = 0.34f;
    float mix = 0.48f;
    float input_drive = 3.5f;
    float output_gain = 1.0f;
    float stereo_width = 0.75f;
    bool override_stereo_width = false;
    float lamp_lag = 1.0f;
    bool tempo_sync = false;
    float tempo_bpm = 120.0f;
    float tempo_division_beats = 1.0f;
    float tone_tilt = 0.0f;
    float pre_hpf_hz = 22.0f;
    float sat_asymmetry = 0.08f;
    float sat_out_trim = 0.95f;
    Preset preset = Preset::classic_chorus;
    EngineMode engine_mode = EngineMode::improved;
    CompareMode compare_mode = CompareMode::none;
    QualityMode quality_mode = QualityMode::standard;
    bool output_conditioning = false;
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
