#include "low_latency_effect.hpp"
int main(int argc, char **argv) {
  try {
    std::ofstream csv;
    if (argc > 1) {
      csv.open(argv[1]);
      csv << "sample_rate,combination,bias,signal,repeat_exact,peak\n";
    }
    // Every internal strategy is independently selectable in each Quality.
    const NonlinearAA strategies[] = {NonlinearAA::Direct,
                                      NonlinearAA::MidpointLegacy,
                                      NonlinearAA::ADAA1,
                                      NonlinearAA::Oversample2xLowLatency,
                                      NonlinearAA::Oversample4xDesktop,
                                      NonlinearAA::Oversample2xADAA};
    const Mode modes[] = {Mode::Direct,   Mode::Midpoint, Mode::ADAA,
                          Mode::Allpass2, Mode::Allpass4, Mode::Allpass2ADAA};
    for (auto quality : {VibeQualityMode::Eco, VibeQualityMode::Standard,
                         VibeQualityMode::High})
      for (int mode = 0; mode < 6; ++mode)
        for (bool limiter : {false, true}) {
          std::array<float, 32> l{}, r{};
          Vibe v(l.data(), r.data());
          v.set_quality_mode(quality);
          v.reseed(1);
          v.analysis_set_nonlinear_aa(strategies[mode], strategies[mode],
                                      strategies[mode]);
          Transfer t;
          t.bjt = !limiter;
          t.drive = 1.5;
          t.asym = v.smoothed_user_params().sat_asymmetry;
          t.gain = v.tuning_params().bjt_gain_trim;
          t.trim = v.smoothed_user_params().sat_out_trim;
          Processor reference(modes[mode], t);
          VibeOversampleState state;
          for (int i = 0; i < 4096; ++i) {
            float x = float(.7 * std::sin(.17 * i));
            float y = v.analysis_nonlinear_sample(x, 1.5f, limiter, state);
            if (std::abs(y - reference.process(x)) > 2e-6)
              throw std::runtime_error("independent strategy parity");
          }
        }
    for (double sr : {44100., 48000., 96000., 192000.})
      for (const auto &c : combinations)
        for (float bias : {-.25f, .25f})
          for (int signal = 0; signal < 6; ++signal) {
            auto a = effect(c, sr, .7f, 7, 1, signal, 32, false, 0,
                            VibeQualityMode::High, true, true, 8192, bias);
            auto b = effect(c, sr, .7f, 7, 1, signal, 32, false, 0,
                            VibeQualityMode::High, true, true, 8192, bias);
            if (a.left != b.left || a.right != b.right)
              throw std::runtime_error("extreme reset mismatch");
            double peak = 0;
            for (double x : a.left)
              peak = std::max(peak, std::abs(x));
            if (signal == 5 && peak != 0)
              throw std::runtime_error("extreme silence");
            if (csv)
              csv << sr << ',' << c.name << ',' << bias << ',' << signal
                  << ",1," << peak << '\n';
          }
    std::cout
        << "384 full-core extreme/reset cases passed: both biases, maximum "
           "controls, four rates, eight strategies, six excitations\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
