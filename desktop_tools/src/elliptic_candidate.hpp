#pragma once
#include <array>
// scipy.signal.ellipord(.42,.58,.02,80), ellip(...,output="sos").
// Conventional zero insertion / 2x interpolation, nonlinear evaluation,
// anti-alias SOS filtering then even-phase decimation. Desktop-only reference.
struct Elliptic2x {
  static constexpr double coefficients[4][5] = {
      {0.0073661101527224705, 0.013699799730345913, 0.0073661101527224696,
       -0.82974004986958427, 0.21585664515443176},
      {1, 1.1445077654681739, 1.0000000000000002, -0.65710158499868077,
       0.42265485963281102},
      {1, 0.58435495423555495, 1, -0.47012935220002333, 0.67692718730525403},
      {1, 0.34006555781864894, 1.0000000000000002, -0.37649310986145895,
       0.89568336607465548}};
  std::array<std::array<double, 2>, 4> up{}, down{};
  void reset() {
    up = {};
    down = {};
  }
  template <class State> static double filter(double x, State &state) {
    for (size_t k = 0; k < state.size(); ++k) {
      const auto &c = coefficients[k];
      double y = c[0] * x + state[k][0];
      state[k][0] = c[1] * x - c[3] * y + state[k][1];
      state[k][1] = c[2] * x - c[4] * y;
      x = y;
    }
    return x;
  }
  template <class Fn> double process(double x, Fn fn) {
    double y = filter(fn(filter(2 * x, up)), down);
    filter(fn(filter(0, up)), down);
    return y;
  }
};
