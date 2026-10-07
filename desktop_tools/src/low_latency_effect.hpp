#pragma once
#include "dsp/vibe_core.hpp"
#include "dsp/factory_presets.hpp" // Requires core types; preserve include order.
#include "nonlinear_measurement.hpp"

struct Combination {
  const char *name;
  NonlinearAA input, feedback, output;
};
const Combination combinations[] = {
    {"A_direct", NonlinearAA::Direct, NonlinearAA::Direct, NonlinearAA::Direct},
    {"B_midpoint", NonlinearAA::MidpointLegacy, NonlinearAA::MidpointLegacy,
     NonlinearAA::MidpointLegacy},
    {"C_os2_direct", NonlinearAA::Oversample2xLowLatency, NonlinearAA::Direct,
     NonlinearAA::Oversample2xLowLatency},
    {"D_os2_midpoint", NonlinearAA::Oversample2xLowLatency,
     NonlinearAA::MidpointLegacy, NonlinearAA::Oversample2xLowLatency},
    {"E_os2_all", NonlinearAA::Oversample2xLowLatency,
     NonlinearAA::Oversample2xLowLatency, NonlinearAA::Oversample2xLowLatency},
    {"F_adaa_direct", NonlinearAA::ADAA1, NonlinearAA::Direct,
     NonlinearAA::ADAA1},
    {"G_os4_direct", NonlinearAA::Oversample4xDesktop, NonlinearAA::Direct,
     NonlinearAA::Oversample4xDesktop},
    {"H_os2adaa_direct", NonlinearAA::Oversample2xADAA, NonlinearAA::Direct,
     NonlinearAA::Oversample2xADAA}};
void apply_factory_preset(Vibe &v, int preset) {
  const auto &f = pico2vibe::kFactoryPresets[preset];
  v.set_voicing(f.voicing);
  v.set_param(VibeParamId::Depth, f.depth);
  v.set_param(VibeParamId::Feedback, f.feedback);
  v.set_param(VibeParamId::Mix, f.mix);
  v.set_param(VibeParamId::LfoRateHz, f.rateHz);
  v.set_param(VibeParamId::InputDrive, f.drive);
  v.set_param(VibeParamId::StereoWidth, f.width);
  v.set_param(VibeParamId::ToneTilt, f.tone);
  v.set_param(VibeParamId::NoiseAmount, f.noise);
  // Frozen M0/M1 level trims keep Legacy comparisons reproducible.

  v.set_param(VibeParamId::OutputGain, f.outputGain);
  v.set_param(VibeParamId::SweepMin, f.sweepMin);
  v.set_param(VibeParamId::SweepMax, f.sweepMax);
  v.set_param(VibeParamId::DriftAmount, f.driftAmount);
  v.set_param(VibeParamId::DriftRateHz, f.driftRateHz);
  v.set_param(VibeParamId::PreHpfHz, f.preHpfHz);
  v.set_param(VibeParamId::SatAsymmetry, f.satAsymmetry);
  v.set_param(VibeParamId::SatOutTrim, f.satOutTrim);
  v.set_param(VibeParamId::LampLag, f.lampLag);
}
struct Capture {
  std::vector<double> left, right;
  double mean = 0, p95 = 0, p99 = 0, maximum = 0;
};
Capture effect(const Combination &c, double sr, float fb, float rate,
               float depth, int signal, int partition = 128,
               bool automate = false, int preset = 0,
               VibeQualityMode quality = VibeQualityMode::High,
               bool override = true, bool stress = true, int length = N,
               float bias = .25f) {
  std::array<float, 256> l{}, r{}, ol{}, orr{};
  VibeOutputConditioner conditioner;
  conditioner.reset(float(sr), 1);
  Vibe v(ol.data(), orr.data());
  v.prepare(float(sr));
  apply_factory_preset(v, preset);
  v.set_quality_mode(quality);
  if (preset < 0)
    preset = 0;
  if (stress) {
    v.set_param(VibeParamId::NoiseAmount, 0);
    v.set_param(VibeParamId::DriftAmount, 0);
    v.set_param(VibeParamId::Feedback, fb);
    v.set_param(VibeParamId::Depth, depth);
    v.set_param(VibeParamId::LfoRateHz, rate);
    v.set_param(VibeParamId::InputDrive, 6);
    v.set_param(VibeParamId::SatAsymmetry, bias);
    v.set_param(VibeParamId::SatOutTrim, 1.2f);
  }
  v.reseed(1);
  if (override)
    v.analysis_set_nonlinear_aa(c.input, c.feedback, c.output);
  const int bin = int(std::round(7000 * N / sr));
  Capture result;
  result.left.resize(length);
  result.right.resize(length);
  std::vector<double> timing;
  for (int pos = -length; pos < length;) {
    const int count = std::min({PERIOD, pos < 0 ? 32 : partition, length - pos,
                                256 - ((pos + length) % 256)});
    if (automate && (pos + length) % 256 == 0) {
      const float a = float(((pos + length) / 256) % 2);
      v.set_param(VibeParamId::InputDrive, .5f + 5.5f * a);
      v.set_param(VibeParamId::SatAsymmetry, -.25f + .5f * a);
      v.set_param(VibeParamId::SatOutTrim, .6f + .6f * a);
    }
    for (int i = 0; i < count; ++i) {
      const int n = pos + i;
      double x = 0;
      if (signal == 0)
        x = std::sin(2 * pi * bin * n / N);
      if (signal == 1)
        for (int k = 0; k < 3; ++k)
          x += .8 / 3 *
               std::sin(2 * pi *
                            std::round((k == 0   ? 7103
                                        : k == 1 ? 9973
                                                 : 13109) *
                                       N / sr) *
                            n / N +
                        .37 * k);
      if (signal == 2) {
        double t = double(n + length) / sr;
        x = .9 * std::exp(-5 * std::fmod(t, .4)) *
            (.5 * std::sin(2 * pi * 110 * t) + .3 * std::sin(2 * pi * 330 * t) +
             .2 * std::sin(2 * pi * 7043 * t));
      }
      if (signal == 3)
        x = n == 0 ? 1 : 0;
      if (signal == 4)
        x = 1;
      if (signal == 5)
        x = 0;
      l[i] = r[i] = float(x);
    }
    const auto start = std::chrono::steady_clock::now();
    v.out(l.data(), r.data(), count);
    for (int i = 0; i < count; ++i)
      conditioner.process_frame(ol[i], orr[i], &ol[i], &orr[i]);
    const double dt = std::chrono::duration<double, std::nano>(
                          std::chrono::steady_clock::now() - start)
                          .count() /
                      count;
    if (pos >= 0) {
      timing.push_back(dt);
      for (int i = 0; i < count; ++i) {
        if (!std::isfinite(ol[i]) || !std::isfinite(orr[i]) ||
            std::abs(ol[i]) > 16 || std::abs(orr[i]) > 16)
          throw std::runtime_error("full loop unbounded");
        result.left[pos + i] = ol[i];
        result.right[pos + i] = orr[i];
      }
    }
    pos += count;
  }
  for (double t : timing) {
    result.mean += t / timing.size();
  }
  std::sort(timing.begin(), timing.end());
  result.p95 = timing[size_t(.95 * (timing.size() - 1))];
  result.p99 = timing[size_t(.99 * (timing.size() - 1))];
  result.maximum = timing.back();
  return result;
}
