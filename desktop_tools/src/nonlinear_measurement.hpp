#pragma once
#include "nonlinear_candidates.hpp"
#include <chrono>
#include <complex>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

using namespace nonlinear;
constexpr int N = 32768;
using Spectrum = std::vector<std::complex<double>>;
void fft(Spectrum &a) {
  const int n = int(a.size());
  for (int i = 1, j = 0; i < n; ++i) {
    int b = n >> 1;
    for (; j & b; b >>= 1)
      j ^= b;
    j ^= b;
    if (i < j)
      std::swap(a[i], a[j]);
  }
  for (int len = 2; len <= n; len *= 2) {
    const auto wlen = std::polar(1.0, -2 * pi / len);
    for (int i = 0; i < n; i += len) {
      std::complex<double> w = 1;
      for (int j = 0; j < len / 2; ++j) {
        auto u = a[i + j], v = a[i + j + len / 2] * w;
        a[i + j] = u + v;
        a[i + j + len / 2] = u - v;
        w *= wlen;
      }
    }
  }
}
Spectrum spectrum(const std::vector<double> &y) {
  Spectrum a(y.size());
  for (size_t i = 0; i < y.size(); ++i)
    a[i] = y[i] * (.5 - .5 * std::cos(2 * pi * i / y.size()));
  fft(a);
  return a;
}
double db(double energy) { return 10 * std::log10(std::max(1e-30, energy)); }
double bin_energy(const Spectrum &a, int bin) {
  double e = 0;
  for (int k = std::max(1, bin - 2);
       k <= std::min(int(a.size() / 2) - 1, bin + 2); ++k)
    e += std::norm(a[k]);
  return e;
}
void mark(std::vector<bool> &mask, int bin) {
  for (int k = std::max(1, bin - 2); k <= std::min(N / 2 - 1, bin + 2); ++k)
    mask[k] = true;
}
int fold(int k) {
  k = std::abs(k) % N;
  return k > N / 2 ? N - k : k;
}
std::vector<double> render(Processor &p, double sr, double amp,
                           const std::vector<int> &bins) {
  std::vector<double> y(N);
  for (int i = -N / 4; i < N; ++i) {
    double x = 0;
    for (size_t j = 0; j < bins.size(); ++j)
      x += amp / bins.size() * std::sin(2 * pi * bins[j] * i / N + .37 * j);
    const double v = p.process(x);
    if (!std::isfinite(v))
      throw std::runtime_error("nonfinite candidate");
    if (i >= 0)
      y[i] = v;
  }
  (void)sr;
  return y;
}
