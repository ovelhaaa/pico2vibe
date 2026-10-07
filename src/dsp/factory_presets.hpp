#pragma once

// Musical programs only; include after vibe_core.hpp. Quality and bypass are global.
namespace pico2vibe {
struct FactoryPreset {
    const char* name;
    VibeVoicing voicing;
    float depth;
    float feedback;
    float mix;
    float rateHz;
    float drive;
    float width;
    float tone;
    float noise;
    float outputGain;
    float sweepMin;
    float sweepMax;
    float driftAmount;
    float driftRateHz;
    float preHpfHz;
    float satAsymmetry;
    float satOutTrim;
    float lampLag;
};

constexpr FactoryPreset kFactoryPresets[] = {
    // name, voicing, depth, feedback, mix, rate, drive, width, tone, noise,
    // output, sweep min/max, drift amount/rate, HPF, asymmetry, saturation trim, lamp lag
    { "Classic Uni-Vibe", VibeVoicing::ClassicChorus,
      0.76f, 0.25f, 0.50f, 0.78f, 1.55f, 0.46f, -0.10f, 0.000f,
      1.07f, 0.56f, 0.96f, 0.014f, 0.070f, 24.0f, 0.045f, 0.94f, 1.10f },
    { "Shin-ei Dark", VibeVoicing::VintageUniVibeChorus,
      0.82f, 0.34f, 0.52f, 0.88f, 1.82f, 0.42f, -0.24f, 0.006f,
      1.07f, 0.52f, 0.95f, 0.019f, 0.060f, 28.0f, 0.065f, 0.90f, 1.18f },
    { "Deja Lead", VibeVoicing::TrowerLead,
      0.76f, 0.34f, 0.48f, 1.32f, 2.15f, 0.42f, -0.08f, 0.000f,
      1.03f, 0.54f, 0.97f, 0.010f, 0.080f, 32.0f, 0.085f, 0.89f, 0.94f },
    { "Voodoo Wide", VibeVoicing::WideStereoDream,
      0.64f, 0.18f, 0.54f, 0.64f, 1.28f, 0.96f, 0.03f, 0.000f,
      1.20f, 0.58f, 0.93f, 0.008f, 0.050f, 20.0f, 0.020f, 0.98f, 0.90f },
    { "Modern Hi-Fi", VibeVoicing::ModernHiFiPhaseVibe,
      0.54f, 0.13f, 0.40f, 1.08f, 0.92f, 0.76f, 0.10f, 0.000f,
      0.90f, 0.60f, 0.90f, 0.002f, 0.080f, 26.0f, 0.000f, 1.00f, 0.82f },
    { "Classic Vibrato", VibeVoicing::ClassicVibrato,
      0.64f, 0.14f, 1.00f, 1.05f, 1.35f, 0.35f, -0.12f, 0.000f,
      1.31f, 0.58f, 0.92f, 0.012f, 0.065f, 24.0f, 0.035f, 0.95f, 1.12f },
    { "Hendrix Deep", VibeVoicing::DeepHendrixSwirl,
      0.88f, 0.48f, 0.57f, 0.72f, 2.00f, 0.52f, -0.20f, 0.004f,
      1.00f, 0.48f, 1.00f, 0.022f, 0.045f, 28.0f, 0.075f, 0.86f, 1.32f },
    { "Gentle Clean", VibeVoicing::GentleCleanVibe,
      0.40f, 0.07f, 0.28f, 0.55f, 1.00f, 0.38f, -0.02f, 0.000f,
      0.74f, 0.62f, 0.86f, 0.004f, 0.070f, 18.0f, 0.000f, 1.00f, 0.85f },
    { "Rotary Fast", VibeVoicing::FastRotaryVibe,
      0.42f, 0.10f, 0.38f, 4.60f, 1.35f, 0.60f, 0.05f, 0.000f,
      0.88f, 0.60f, 0.88f, 0.003f, 0.100f, 30.0f, 0.015f, 0.98f, 0.55f },
    { "Bass Anchor", VibeVoicing::BassSynthFriendly,
      0.46f, 0.05f, 0.28f, 0.72f, 0.95f, 0.30f, -0.05f, 0.000f,
      0.74f, 0.62f, 0.90f, 0.002f, 0.060f, 8.0f, 0.000f, 0.98f, 0.90f },
    { "Lamp Drift", VibeVoicing::LoFiLampDrift,
      0.68f, 0.25f, 0.50f, 0.58f, 1.85f, 0.48f, -0.16f, 0.012f,
      1.01f, 0.55f, 0.94f, 0.030f, 0.035f, 24.0f, 0.110f, 0.88f, 1.45f },
    { "Psychedelic Slow", VibeVoicing::PsychedelicSlowSweep,
      0.88f, 0.50f, 0.58f, 0.16f, 1.65f, 0.58f, -0.22f, 0.004f,
      1.04f, 0.46f, 0.99f, 0.016f, 0.025f, 28.0f, 0.060f, 0.86f, 1.60f }
};

} // namespace pico2vibe
