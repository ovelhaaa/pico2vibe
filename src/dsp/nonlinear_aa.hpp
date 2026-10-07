#pragma once
#include "nonlinear_transfer.hpp"
#include <cmath>

// Internal only; no public parameter or preset contract.
enum class NonlinearAA {
  Direct,
  MidpointLegacy,
  ADAA1,
  Oversample2xLowLatency,
  Oversample2xADAA,
  Oversample4xDesktop
};

// Laurent de Soras HIIR PolyphaseIir2Designer::compute_coefs(c,80,.08).
// Six first-order allpasses per filter, split into two three-section branches.
// Base-rate section A(z)=(a+z^-1)/(1+a*z^-1).
struct VibeAllpass2x {
  static constexpr float coefficients[6] = {
      .045362164348961023f, .16808748123450207f, .33714968797907374f,
      .52237785430835371f,  .70806413636353838f, .89744559117277378f};
  float up[6]{}, down[6]{}, odd_delay = 0;
  void reset() {
    for (auto &s : up)
      s = 0;
    for (auto &s : down)
      s = 0;
    odd_delay = 0;
  }
  static float branch(float x, float *s, int parity) {
    for (int k = parity; k < 6; k += 2) {
      const float y = coefficients[k] * x + s[k];
      const float next = x - coefficients[k] * y;
      s[k] = std::abs(next) < 1e-20f ? 0.f : next;
      x = y;
    }
    return x;
  }
  template <class Fn> float process(float x, Fn fn) {
    const float even = fn(branch(x, up, 0));
    const float odd = fn(branch(x, up, 1));
    const float y = .5f * (branch(even, down, 0) + odd_delay);
    odd_delay = branch(odd, down, 1);
    return y;
  }
};
struct VibeAAState {
  VibeAllpass2x first, second;
  double previous = 0;
  bool primed = false;
  void reset() {
    first.reset();
    second.reset();
    previous = 0;
    primed = false;
  }
  static double primitive(double x) {
    return x * x / 18. + (4. / 3.) * std::log1p(x * x / 3.);
  }
  template <class Fn, class Integral>
  float adaa(float x, Fn fn, Integral integral) {
    const double dx = double(x) - previous;
    const float y =
        !primed ? fn(x)
        : std::abs(dx) < 1e-5 * (1 + std::fmax(std::abs(x), std::abs(previous)))
            ? fn(float(.5 * (x + previous)))
            : float((integral(x) - integral(previous)) / dx);
    previous = x;
    primed = true;
    return y;
  }
  template <class Fn, class Integral>
  float process(float x, NonlinearAA mode, Fn fn, Integral integral) {
    switch (mode) {
    case NonlinearAA::ADAA1:
      return adaa(x, fn, integral);
    case NonlinearAA::Oversample2xLowLatency:
      return first.process(x, fn);
    case NonlinearAA::Oversample2xADAA:
      return first.process(x, [&](float v) { return adaa(v, fn, integral); });
    case NonlinearAA::Oversample4xDesktop:
      return first.process(x, [&](float v) { return second.process(v, fn); });
    default:
      return fn(x);
    }
  }
};
