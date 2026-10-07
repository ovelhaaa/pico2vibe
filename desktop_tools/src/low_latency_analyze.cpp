#include "low_latency_effect.hpp"
int main(int argc, char **argv) {
  try {
    const std::filesystem::path out = argc > 1 ? argv[1] : "build/m3-1";
    std::filesystem::create_directories(out);
    std::ofstream aliases(out / "full-stationary-alias.csv");
    aliases << "sample_rate,combination,feedback,signal,alias_lower_bound_dbfs,"
               "unclassified_residual_dbfs,collision_bins\n";
    std::ofstream full(out / "full-effect.csv"), cpu(out / "full-cpu.csv"),
        states(out / "state-tests.csv");
    full << "sample_rate,combination,feedback,rate,depth,signal,rms,peak,"
            "stereo_difference_rms,mono_rms,hf_energy_dbfs,difference_rms,"
            "decay_tail_rms\n";
    if (argc < 3 || std::string(argv[2]) != "--state-only")
      for (double sr : {44100., 48000., 96000., 192000.})
        for (float fb : {0.f, .25f, .45f, .60f, .70f})
          for (auto setting : {std::pair<float, float>{.18f, 1.f},
                               {1.2f, .6f},
                               {7.f, 1.f},
                               {1.2f, 0.f}})
            for (int signal = 0; signal < 6; ++signal) {
              auto reference = effect(combinations[0], sr, fb, setting.first,
                                      setting.second, signal);
              for (const auto &c : combinations) {
                auto y = c.name == combinations[0].name
                             ? reference
                             : effect(c, sr, fb, setting.first, setting.second,
                                      signal);
                double rms = 0, peak = 0, stereo = 0, mono = 0, diff = 0,
                       tail = 0;
                for (int i = 0; i < N; ++i) {
                  rms += y.left[i] * y.left[i] / N;
                  peak = std::max(peak, std::abs(y.left[i]));
                  stereo += std::pow(y.left[i] - y.right[i], 2) / N;
                  mono += std::pow(.5 * (y.left[i] + y.right[i]), 2) / N;
                  diff += std::pow(y.left[i] - reference.left[i], 2) / N;
                  if (i > 3 * N / 4)
                    tail += y.left[i] * y.left[i] / (N / 4);
                }
                auto a = spectrum(y.left);
                double hf = 0;
                for (int k = int(12000 * N / sr); k < N / 2; ++k)
                  hf += std::norm(a[k]) * 16. / (3. * N * N);
                if (setting.second == 0 && (signal == 0 || signal == 1)) {
                  std::vector<bool> valid(N / 2), folded(N / 2);
                  if (signal == 0) {
                    int bin = int(std::round(7000 * N / sr));
                    for (int order = 1; order <= 128; ++order) {
                      if (order * bin < N / 2)
                        mark(valid, order * bin);
                      else
                        mark(folded, fold(order * bin));
                    }
                  } else {
                    int bins[3] = {int(std::round(7103 * N / sr)),
                                   int(std::round(9973 * N / sr)),
                                   int(std::round(13109 * N / sr))};
                    for (int i = -7; i <= 7; ++i)
                      for (int j = -7; j <= 7; ++j)
                        for (int k = -7; k <= 7; ++k) {
                          int order = std::abs(i) + std::abs(j) + std::abs(k);
                          if (order == 0 || order > 7)
                            continue;
                          int b =
                              std::abs(i * bins[0] + j * bins[1] + k * bins[2]);
                          if (b < N / 2)
                            mark(valid, b);
                          else
                            mark(folded, fold(b));
                        }
                  }
                  double ae = 0, res = 0;
                  int collisions = 0;
                  for (int k = 3; k < N / 2; ++k) {
                    if (folded[k] && !valid[k])
                      ae += std::norm(a[k]);
                    if (!folded[k] && !valid[k])
                      res += std::norm(a[k]);
                    if (folded[k] && valid[k])
                      ++collisions;
                  }
                  aliases << sr << ',' << c.name << ',' << fb << ',' << signal
                          << ',' << db(ae * 16. / (3. * N * N)) << ','
                          << db(res * 16. / (3. * N * N)) << ',' << collisions
                          << '\n';
                }
                full << sr << ',' << c.name << ',' << fb << ',' << setting.first
                     << ',' << setting.second << ',' << signal << ','
                     << std::sqrt(rms) << ',' << peak << ','
                     << std::sqrt(stereo) << ',' << std::sqrt(mono) << ','
                     << db(hf) << ',' << std::sqrt(diff) << ','
                     << std::sqrt(tail) << '\n';
              }
            }
    cpu << "sample_rate,preset,mode,mean_ns,p95_ns,p99_ns,max_ns\n";
    for (double sr : {44100., 48000., 96000., 192000.})
      for (int preset : {0, 6, 3, 4, 5}) {
        for (auto q : {VibeQualityMode::Eco, VibeQualityMode::Standard,
                       VibeQualityMode::High}) {
          auto y = effect(combinations[0], sr, .45f, 1.2f, .6f, 2, 128, false,
                          preset, q, false, false);
          cpu << sr << ',' << preset << ',' << int(q) << ',' << y.mean << ','
              << y.p95 << ',' << y.p99 << ',' << y.maximum << '\n';
        }
        for (const auto &c : combinations) {
          auto y = effect(c, sr, .45f, 1.2f, .6f, 2, 128, false, preset,
                          VibeQualityMode::High, true, false);
          cpu << sr << ',' << preset << ',' << c.name << ',' << y.mean << ','
              << y.p95 << ',' << y.p99 << ',' << y.maximum << '\n';
        }
      }
    std::ofstream longrun(out / "long-stress.csv");
    longrun << "sample_rate,combination,rate,depth,bias,duration_seconds,peak,"
               "rms,late_rms\n";
    if (argc < 3)
      for (double sr : {44100., 48000., 96000., 192000.})
        for (const auto &c : combinations)
          for (float bias : {-.25f, .25f}) {
            int length = int(sr * 2 / .18);
            auto y = effect(c, sr, .7f, .18f, 1.f, 2, 32, false, 0,
                            VibeQualityMode::High, true, true, length, bias);
            double peak = 0, rms = 0, late = 0;
            for (int i = 0; i < length; ++i) {
              peak = std::max(peak, std::abs(y.left[i]));
              rms += y.left[i] * y.left[i] / length;
              if (i >= 3 * length / 4)
                late += y.left[i] * y.left[i] / (length / 4);
            }
            longrun << sr << ',' << c.name << ",.18,1," << bias << ','
                    << double(length) / sr << ',' << peak << ','
                    << std::sqrt(rms) << ',' << std::sqrt(late) << '\n';
          }
    states << "sample_rate,combination,repeat_exact,partition_max_error\n";
    for (double sr : {44100., 48000., 96000., 192000.})
      for (const auto &c : combinations) {
        auto a = effect(c, sr, .7f, 1.2f, .6f, 2, 128, true);
        auto b = effect(c, sr, .7f, 1.2f, .6f, 2, 128, true);
        auto d = effect(c, sr, .7f, 1.2f, .6f, 2, 17, true);
        double e = 0;
        for (int i = 0; i < N; ++i)
          e = std::max(e, std::abs(a.left[i] - d.left[i]));
        states << sr << ',' << c.name << ",1," << e << '\n';
        if (a.left != b.left || e > 2e-4)
          throw std::runtime_error(
              "reset/partition invariance: " + std::string(c.name) +
              " error=" + std::to_string(e));
      }
    std::cout << "Full loop, responses, CPU and state tests passed; Vibe="
              << sizeof(Vibe) << " AAState=" << sizeof(VibeAAState)
              << " OS2=" << sizeof(VibeAllpass2x) << '\n';
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
