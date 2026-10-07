#include "dsp/vibe_core.hpp"
#include <array>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>

void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
struct Stats { double low = 1, high = 0, mean = 0, rmean = 0; };
Stats measure(float rate, float depth, float fs) {
    OpticalModel model;
    model.prepare(fs);
    EffectLFO oscillator;
    VibeUserParams user; user.lfo_rate_hz = rate; user.drift_amount = 0; user.stereo_width = 0;
    VibeTuningParams tuning;
    Stats result;
    const int n = int(fs * (4 / rate + 2));
    int count = 0;
    for (int i = 0; i < n; ++i) {
        float excitation = 0, unused = 0;
        oscillator.processSample(&excitation, &unused, user, tuning, LfoShape::Sine,
                                 VibeProfile::Classic, 0, 0, 1/fs, true);
        const auto& f = model.process_sample(oscillator.phase_left(), excitation, depth, 0.04f, 0.96f);
        for (float r : f.resistance) require(std::isfinite(r) && r >= 6000 && r <= 4.3e6f, "unbounded resistance");
        require(std::isfinite(f.brightness) && f.brightness >= 0 && f.brightness <= 1, "unbounded lamp");
        if (i >= n - int(fs / rate)) {
            result.low = std::min(result.low, double(f.brightness));
            result.high = std::max(result.high, double(f.brightness));
            result.mean += f.brightness;
            result.rmean += f.resistance[0];
            ++count;
        }
    }
    result.mean /= count; result.rmean /= count;
    return result;
}

// Observe actual integrated Vibe frames, including sample-clock optical automation.
std::vector<OpticalFrame> render(int block, bool automate, OpticalMode mode) {
    std::array<float, PERIOD> zero{}, outL{}, outR{};
    Vibe v(outL.data(), outR.data());
    v.prepare(48000); v.set_optical_mode(mode);
    v.set_param(VibeParamId::DriftAmount, 0); v.reseed(42);
    std::vector<OpticalFrame> frames;
    for (int pos = 0; pos < 48000;) {
        if (automate && pos == 24000) {
            v.set_param(VibeParamId::Depth, 1);
            v.set_param(VibeParamId::LampLag, 2.5f);
            v.set_param(VibeParamId::LfoRateHz, 7);
            v.set_param(VibeParamId::SweepMin, 0.2f);
        }
        int n = std::min(block, 48000-pos);
        n = std::min(n, 32-pos%32); // Align observations, while retaining irregular callbacks.
        // Automation event occurs at the same absolute sample for each partition.
        if (pos < 24000) n = std::min(n, 24000-pos);
        v.out(zero.data(), zero.data(), n);
        pos += n;
        if (pos % 32 == 0) frames.push_back(v.optical_frame());
    }
    return frames;
}

int main() {
    try {
        OpticalModel model;
        for (int cell = 0; cell < 4; ++cell) {
            float previous = model.resistance_for_brightness(0, cell);
            for (int i = 1; i <= 4096; ++i) {
                float r = model.resistance_for_brightness(float(i)/4096, cell);
                require(r <= previous, "brightness must monotonically lower resistance");
                previous = r;
            }
        }
        for (int c=0; c<4; ++c) {
            require(model.resistance_for_brightness(0,c)>1e6f, "per-cell dark range lost");
            require(std::abs(model.resistance_for_brightness(0,c)/kDafxCells[c].measuredMaxResistance-1)<0.025f, "dark bound constraint");
            require(std::abs(model.resistance_for_brightness(1,c)/kDafxCells[c].measuredMinResistance-1)<0.025f, "bright bound constraint");
            require(std::abs(model.resistance_for_brightness(0.5f,c)/kDafxCells[c].measuredMeanResistance-1)<0.025f, "mean pivot constraint");
        }
        require(model.drive_for_region(0.2f)<model.drive_for_region(0.4f), "sweep mapping has a dead region");
        require(model.drive_for_region(0.58f)!=0.58f, "sweep directly interpreted as voltage");
        // Physical reference shares exactly one optical state, regardless of width.
        std::array<float,32> z{},ol{},orr{};
        Vibe physical(ol.data(),orr.data()); physical.prepare(48000);
        physical.set_optical_topology(OpticalTopology::SingleLampReference);
        physical.set_param(VibeParamId::StereoWidth,1);
        physical.set_param(VibeParamId::DriftAmount,0.1f);
        for(int i=0;i<4000;++i) {
            physical.out(z.data(),z.data(),32);
            auto a=physical.optical_frame(0), b=physical.optical_frame(1);
            require(a.phase==b.phase && a.drive==b.drive && a.brightness==b.brightness,"reference lamp split");
            for(int c=0;c<4;++c) require(a.resistance[c]==b.resistance[c],"reference cells split");
        }
        physical.set_optical_topology(OpticalTopology::StudioStereo);
        for(int i=0;i<4000;++i) physical.out(z.data(),z.data(),32);
        require(physical.optical_frame(0).brightness!=physical.optical_frame(1).brightness,"studio width lost");
        auto slow = measure(0.2f, 0.85f, 48000);
        auto fast = measure(7, 0.85f, 48000);
        require(fast.high-fast.low < 0.8*(slow.high-slow.low), "speed inertia missing");
        double previous = 0;
        for (float depth : {0.15f, 0.35f, 0.60f, 0.85f, 1.0f}) {
            auto s = measure(1, depth, 48000);
            require(s.high-s.low > previous, "intensity excursion not increasing");
            previous = s.high-s.low;
        }
        for (float rate : {0.2f, 1.0f, 7.0f}) {
            auto ref = measure(rate, 0.85f, 48000);
            for (float fs : {44100.f, 96000.f, 192000.f}) {
                auto s = measure(rate, 0.85f, fs);
                require(std::abs(s.mean-ref.mean) < 0.001, "sample-rate lamp mean diverged");
                require(std::abs(s.high-ref.high) < 0.001, "sample-rate lamp extrema diverged");
                require(std::abs(s.rmean/ref.rmean-1) < 0.01, "sample-rate resistance diverged");
            }
        }
        for (bool automate : {false, true}) {
            auto a = render(32, automate, OpticalMode::ReferenceOptical);
            for (int block : {1, 5, 7, 16, 31}) {
                auto b = render(block, automate, OpticalMode::ReferenceOptical);
                require(a.size() == b.size(), "partition observation mismatch");
                for (size_t i = 0; i < a.size(); ++i) {
                    require(a[i].phase == b[i].phase && a[i].brightness == b[i].brightness, "optics depends on block partition");
                    for (int cell = 0; cell < 4; ++cell)
                        require(a[i].resistance[cell] == b[i].resistance[cell], "LDR partition mismatch");
                }
            }
        }
        auto a = render(32, false, OpticalMode::ReferenceOptical);
        auto b = render(32, false, OpticalMode::ReferenceOptical);
        for (size_t i = 0; i < a.size(); ++i) for (int c = 0; c < 4; ++c)
            require(a[i].resistance[c] == b[i].resistance[c], "fixed-seed repeatability failed");
        require(a.back().resistance[0] != a.back().resistance[1], "cells collapsed");
        // Fixed component tolerances are independent of creative Drift and subtle.
        OpticalCalibration calibrated;
        OpticalModel cellA, cellB;
        cellA.configure(calibrated,41); cellB.configure(calibrated,42);
        for(int c=0;c<4;++c)
            require(std::abs(cellA.resistance_for_brightness(0.3f,c)/cellB.resistance_for_brightness(0.3f,c)-1)<0.026f,
                    "component tolerance too large");
        // Heating/cooling step response, tested in the thermal domain rather than
        // conflating trajectory crossing time with an exponential time constant.
        OpticalModel step;
        step.prepare(48000);
        int rise10=-1,rise90=-1,fall90=-1,fall10=-1;
        // At full region/depth the calibrated bias/drive yields 0..0.96.
        const float lowPower=0, highPower=0.96f*0.96f;
        for(int i=0;i<24000;++i) {
            step.process_sample(0,1,1,0,1);
            float t=step.temperature();
            if(rise10<0&&t>=0.1f*highPower) rise10=i;
            if(rise90<0&&t>=0.9f*highPower) rise90=i;
        }
        for(int i=0;i<48000;++i) {
            step.process_sample(0,0,1,0,1);
            float normalized=(step.temperature()-lowPower)/(highPower-lowPower);
            if(fall90<0&&normalized<=0.9f) fall90=i;
            if(fall10<0&&normalized<=0.1f) fall10=i;
        }
        require(rise90>rise10&&fall10>fall90&&(fall10-fall90)>2*(rise90-rise10), "asymmetric step response failed");
        // Supported boundaries, reversed regions, malformed internal calibration.
        OpticalCalibration bad; bad.lampAttack = NAN; bad.cells[0].measuredMinResistance = -1; bad.cells[0].measuredMaxResistance = INFINITY;
        model.configure(bad, 42);
        for (float depth : {0.f, 1.f}) for (float lag : {0.35f, 2.5f}) {
            model.set_lag(lag);
            for (int i=0; i<10000; ++i) {
                auto f=model.process_sample(float(i%100)/100, float(i%100)/100, depth, 1, 0);
                for(float r:f.resistance) require(std::isfinite(r) && r>0 && r<=5e6f,"boundary safety");
            }
        }
        std::cout << "optical_model_test passed: monotonicity, bounds, inertia, intensity, sample rates, automation/block invariance, seed repeatability\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
