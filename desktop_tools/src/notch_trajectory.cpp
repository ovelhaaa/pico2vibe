#include <cmath>
#include <cstdint>
#define private public
#include "dsp/vibe_core.hpp"
#undef private
#include "dsp/factory_presets.hpp"
#include <array>
#include <complex>
#include <fstream>
#include <iostream>
#include <vector>

// Frozen linear four-stage + equal dry/wet probe. Excludes feedback,
// saturation, output conditioning, and Studio mix/energy compensation.
// Tracks frequency-ordered minima, not the deepest event in a log sweep.
int main(int argc, char** argv) {
    if(argc!=2) return 1;
    std::ofstream csv(argv[1]);
    csv<<"preset,time_s,phase,branch,frequency_hz,depth_db,corner1_hz,corner2_hz,corner3_hz,corner4_hz\n";
    constexpr int bins=768;
    std::array<double,bins> freq{};
    std::array<std::complex<double>,bins> delay{};
    for(int j=0;j<bins;++j) {
        freq[j]=20*std::pow(1000.,double(j)/(bins-1));
        delay[j]=std::polar(1.,-2*kPi*freq[j]/44100);
    }
    for(int preset=0;preset<12;++preset) {
        const auto& f=pico2vibe::kFactoryPresets[preset];
        std::array<float,32> z{},ol{},orr{}; Vibe v(ol.data(),orr.data());
        v.prepare(44100); v.set_voicing(f.voicing); v.set_quality_mode(VibeQualityMode::High);
        v.set_param(VibeParamId::Depth,f.depth); v.set_param(VibeParamId::LfoRateHz,f.rateHz);
        v.set_param(VibeParamId::SweepMin,f.sweepMin); v.set_param(VibeParamId::SweepMax,f.sweepMax);
        v.set_param(VibeParamId::LampLag,f.lampLag);
        v.set_param(VibeParamId::StereoWidth,0); v.set_param(VibeParamId::DriftAmount,0); v.reseed(1);
        v.set_optical_mode(OpticalMode::ReferenceOptical);
#ifndef M2_BASELINE
        v.set_optical_topology(OpticalTopology::SingleLampReference);
#endif
        int settle=int(44100*std::max(3.f,5/f.rateHz));
        for(int i=0;i<settle;i+=32) v.out(z.data(),z.data(),32);
        int duration=int(44100*2/f.rateHz), stride=std::max(1,duration/32/256);
        for(int i=0;i<duration;i+=32) {
            v.out(z.data(),z.data(),32);
            if(i/32%stride) continue;
            std::array<double,bins> gain{};
            for(int j=0;j<bins;++j) {
                std::complex<double> h=1;
                for(int c=0;c<4;++c) {
                    const auto& a=v.greybox_targets[c];
                    h*=(double(a.b0)+double(a.b1)*delay[j])/(1.+double(a.a1)*delay[j]);
                }
                gain[j]=20*std::log10(std::max(1e-12,std::abs(0.5+0.5*h)));
            }
            int branch=0;
            for(int j=1;j<bins-1;++j) if(gain[j]<gain[j-1]&&gain[j]<=gain[j+1]) {
                csv<<preset<<','<<double(i)/44100<<','<<v.optical_frame().phase<<','<<++branch<<','<<freq[j]<<','<<gain[j];
                for(int c=0;c<4;++c) {
                    const auto& a=v.greybox_targets[c];
                    double t=(1+double(a.a1))/(1-double(a.a1));
                    csv<<','<<std::atan(t)*44100/kPi;
                }
                csv<<'\n';
            }
        }
    }
    std::cout<<"Frozen linear notch trajectories: "<<argv[1]<<'\n';
}
