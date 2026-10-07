#include "nonlinear_measurement.hpp"
int main(int argc, char **argv) {
  std::filesystem::path out = argc > 1 ? argv[1] : "build/m3-1";
  std::ofstream csv(out / "all-wrapper-response.csv"),
      dense(out / "response-spectrum.csv");
  csv << "sample_rate,algorithm,frequency,gain_db,phase_unwrapped_rad,group_"
         "delay_samples,phase_delay_samples,impulse_peak_index\n";
  dense << "algorithm,frequency_over_sample_rate,gain_db,phase_unwrapped_rad\n";
  for (Mode mode :
       {Mode::Direct, Mode::Midpoint, Mode::ADAA, Mode::Fir2, Mode::Fir4,
        Mode::Allpass2, Mode::Allpass4, Mode::Allpass2ADAA, Mode::Elliptic2}) {
    Transfer t;
    t.bjt = false;
    Processor p(mode, t);
    for (int i = 0; i < 8192; ++i)
      p.process(0);
    std::vector<double> h(16384);
    int peak = 0;
    for (int i = 0; i < int(h.size()); ++i) {
      h[i] = p.process(i == 0 ? 1e-5 : 0) / 1e-5;
      if (std::abs(h[i]) > std::abs(h[peak]))
        peak = i;
    }
    Spectrum bins(h.begin(), h.end());
    fft(bins);
    std::vector<double> phase(h.size() / 2);
    double previous = 0, unwrapped = 0;
    for (size_t i = 0; i < phase.size(); ++i) {
      double angle = std::arg(bins[i]);
      unwrapped += std::remainder(angle - previous, 2 * pi);
      previous = angle;
      phase[i] = unwrapped;
      if (i % 8 == 0)
        dense << name(mode) << ',' << double(i) / h.size() << ','
              << db(std::norm(bins[i])) << ',' << unwrapped << '\n';
    }
    auto response = [&](double w) {
      std::complex<double> z = 0;
      for (size_t i = 0; i < h.size(); ++i)
        z += h[i] * std::polar(1., -w * i);
      return z;
    };
    for (double sr : {44100., 48000., 96000., 192000.})
      for (double f :
           {0., 80., 440., 1000., 3000., 5000., 7000., 10000., 15000.}) {
        double w = 2 * pi * f / sr, eps = 1e-5;
        auto z = response(w);
        double gd =
                   -std::arg(response(w + eps) / response(w - eps)) / (2 * eps),
               ph = std::arg(z);
        ph += 2 * pi *
              std::round((phase[size_t(std::round(f * h.size() / sr))] - ph) /
                         (2 * pi));
        csv << sr << ',' << name(mode) << ',' << f << ',' << db(std::norm(z))
            << ',' << ph << ',' << gd << ',' << (f == 0 ? gd : -ph / w) << ','
            << peak << '\n';
      }
  }
}
