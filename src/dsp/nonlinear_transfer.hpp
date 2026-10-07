#pragma once
#include <cmath>

// Historical name retained: this is a rational curve, not a cubic polynomial.
static inline float soft_clip_cubic(float x) {
    const float xx = x * x;
    return x * (27.0f + xx) / (27.0f + 9.0f * xx);
}

static inline float vibe_bjt_transfer(float data, float drive, float asym,
                                      float gain_trim, float out_trim) {
    const float x = (data * 0.84f + asym) * drive;
    const float x2 = x * x;
    const float sat = x * (27.0f + x2) / (27.0f + 9.0f * x2);
    const float xa = asym * drive;
    const float xa2 = xa * xa;
    const float sat_bias = xa * (27.0f + xa2) / (27.0f + 9.0f * xa2);
    return (sat - sat_bias) * gain_trim * out_trim;
}
