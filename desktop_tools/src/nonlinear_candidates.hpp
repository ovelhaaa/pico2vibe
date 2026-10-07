#pragma once
#include "dsp/nonlinear_transfer.hpp"
#include <array>
#include <cmath>
#include <algorithm>

namespace nonlinear {
constexpr double pi = 3.14159265358979323846;
enum class Mode { Direct, Midpoint, ADAA, Fir2, Fir4, TanhReference };
inline const char* name(Mode m) {
    constexpr const char* names[] = {"direct", "midpoint", "adaa1", "fir2", "fir4", "tanh_reference"};
    return names[static_cast<int>(m)];
}
struct Transfer {
    bool bjt = true;
    double drive = 1.5, asym = .08, gain = .70, trim = .95;
    double operator()(double x) const {
        // Invoke the shipping float arithmetic so baseline measurements match it.
        return bjt ? vibe_bjt_transfer(float(x), float(drive), float(asym), float(gain), float(trim))
                   : soft_clip_cubic(float(x));
    }
    static double rational(double x) { return x * (27 + x*x) / (27 + 9*x*x); }
    // f(x)=x/9+(8/3)x/(x*x+3); integration constant chosen for F(0)=0.
    static double primitive(double x) { return x*x/18 + (4.0/3)*std::log1p(x*x/3); }
    double integral(double x) const {
        if (!bjt) return primitive(x);
        const double a = .84*drive, b = asym*drive;
        return gain*trim*((primitive(a*x+b)-primitive(b))/a-rational(b)*x);
    }
    double tanh_reference(double x) const {
        return bjt ? gain*trim*(std::tanh((.84*x+asym)*drive)-std::tanh(asym*drive)) : std::tanh(x);
    }
};

// Desktop experiment only. Fixed storage, no audio-loop allocations.
// Blackman-windowed sinc interpolation (polyphase) and decimation low-pass.
// 64*factor+1 taps; each filter delays 32 base samples, total delay 64.
struct Fir {
    std::array<double,257> h{}, history{};
    std::array<double,65> input{};
    int factor = 2, taps = 129, pos = 0, input_pos = 0;
    explicit Fir(int f = 2) : factor(f), taps(64*f+1) {
        const double fc = .46/f;
        double sum = 0;
        for (int i=0; i<taps; ++i) {
            const double t = i-(taps-1)*.5;
            const double w = .42-.5*std::cos(2*pi*i/(taps-1))+.08*std::cos(4*pi*i/(taps-1));
            h[i] = (t==0 ? 2*fc : std::sin(2*pi*fc*t)/(pi*t))*w;
            sum += h[i];
        }
        for (int i=0; i<taps; ++i) h[i] /= sum;
    }
    void reset() { history.fill(0); input.fill(0); pos=input_pos=0; }
    template<class Function> double process(double x, Function fn) {
        input[input_pos] = x;
        double y = 0;
        for (int phase=0; phase<factor; ++phase) {
            double up = 0;
            for (int k=phase, j=0; k<taps; k+=factor, ++j)
                up += factor*h[k]*input[(input_pos-j+65)%65];
            history[pos] = fn(up);
            if (phase==0) {
                for (int k=0; k<taps; ++k) y += h[k]*history[(pos-k+taps)%taps];
            }
            pos=(pos+1)%taps;
        }
        input_pos=(input_pos+1)%65;
        return y;
    }
};

struct Processor {
    Mode mode;
    Transfer transfer;
    Fir fir;
    double previous = 0, y1 = 0, lp = 0;
    bool primed = false;
    Processor(Mode m, Transfer t) : mode(m), transfer(t), fir(m==Mode::Fir4 ? 4 : 2) {}
    void reset() { previous=y1=lp=0; primed=false; fir.reset(); }
    double process(double x) {
        if (mode==Mode::Direct) return transfer(x);
        if (mode==Mode::TanhReference) return transfer.tanh_reference(x);
        if (mode==Mode::Fir2 || mode==Mode::Fir4) return fir.process(x, transfer);
        const double cur = transfer(x);
        if (!primed) { previous=x; y1=lp=cur; primed=true; return cur; }
        double y;
        if (mode==Mode::Midpoint) {
            // Match the legacy High float evaluation and one-pole smoothing.
            const float mid = .5f*(float(previous)+float(x));
            const float weighted = .25f*float(y1)+.5f*float(transfer(mid))+.25f*float(cur);
            lp = float(lp)+.72f*(weighted-float(lp));
            y=lp;
        } else {
            const double dx=x-previous;
            // Current parameters applied to BOTH endpoints: automation does not
            // subtract primitives belonging to different functions.
            y = std::abs(dx)<1e-5*(1+std::max(std::abs(x),std::abs(previous)))
                ? transfer(.5*(x+previous))
                : (transfer.integral(x)-transfer.integral(previous))/dx;
        }
        previous=x; y1=cur;
        return y;
    }
};
} // namespace nonlinear
