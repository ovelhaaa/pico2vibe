#include <cmath>
#include <cstdint>
#define private public
#include "dsp/vibe_core.hpp"
#undef private
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace fs = std::filesystem;
struct Point { double time; OpticalFrame f; int cycle; };
double value(const OpticalFrame& f, int cell) { return cell < 0 ? f.brightness : f.resistance[cell]; }

void summarize(std::ostream& out, const std::string& mode, float rate, float depth,
               const std::vector<Point>& points, int cell, float sampleRate) {
    double lo=1e30, hi=-1e30, sum=0, logsum=0, cs=0, sn=0, ds=0, dc=0;
    std::array<double,4> cycleSum{}; std::array<int,4> cycleCount{};
    for (const auto& p:points) {
        double v=value(p.f,cell);
        lo=std::min(lo,v); hi=std::max(hi,v); sum+=v;
        if(cell>=0) logsum+=std::log(v);
        double phase=2*kPi*p.f.phase;
        // Resistance decreases with illumination: use -R for optical lag sign.
        cs+=(cell<0?v:-v)*std::cos(phase); sn+=(cell<0?v:-v)*std::sin(phase);
        dc+=p.f.drive*std::cos(phase); ds+=p.f.drive*std::sin(phase);
        cycleSum[p.cycle]+=v; ++cycleCount[p.cycle];
    }
    double mean=sum/points.size(), stability=0;
    for(int c=0;c<4;++c) stability=std::max(stability,std::abs(cycleSum[c]/cycleCount[c]/mean-1));
    // Last complete cycle: 10-90% rising/falling trajectory crossing intervals.
    std::vector<double> x;
    for(const auto& p:points) if(p.cycle==3) x.push_back(value(p.f,cell));
    auto minIt=std::min_element(x.begin(),x.end()), maxIt=std::max_element(x.begin(),x.end());
    size_t imin=minIt-x.begin(), imax=maxIt-x.begin();
    double v10=*minIt+0.1*(*maxIt-*minIt), v90=*minIt+0.9*(*maxIt-*minIt);
    double dt=32.0/sampleRate;
    auto interval=[&](size_t start, bool rise) {
        double t10=-1,t90=-1;
        for(size_t j=1;j<=x.size();++j) {
            double a=x[(start+j-1)%x.size()],b=x[(start+j)%x.size()];
            auto crosses=[&](double level) { return rise?(a<level&&b>=level):(a>level&&b<=level); };
            if(t10<0&&crosses(v10)) t10=(j-1+(v10-a)/(b-a))*dt;
            if(t90<0&&crosses(v90)) t90=(j-1+(v90-a)/(b-a))*dt;
        }
        return std::abs(t90-t10);
    };
    double lag=std::remainder(std::atan2(sn,cs)-std::atan2(ds,dc),2*kPi)/(2*kPi);
    double riseFraction=double((imax+x.size()-imin)%x.size())/x.size();
    out<<mode<<','<<rate<<','<<depth<<','<<cell<<','<<lo<<','<<hi<<','<<mean<<','
       <<(cell<0?0:std::exp(logsum/points.size()))<<','<<(cell<0?0:hi/lo)<<','
       <<lag<<','<<riseFraction<<','<<interval(imin,true)<<','<<interval(imax,false)<<','<<stability<<'\n';
}

void trajectory(const fs::path& dir, OpticalMode mode, float rate,float depth,float fsample,
                std::ostream& summary) {
    std::array<float,PERIOD> zero{},outL{},outR{};
    Vibe vibe(outL.data(),outR.data()); vibe.prepare(fsample);
    vibe.set_quality_mode(VibeQualityMode::High);
    vibe.set_param(VibeParamId::LfoRateHz,rate); vibe.set_param(VibeParamId::Depth,depth);
    vibe.set_param(VibeParamId::DriftAmount,0); vibe.set_param(VibeParamId::StereoWidth,0);
    vibe.reseed(1); vibe.set_optical_mode(mode);
    std::string name=mode==OpticalMode::LegacyOptical?"legacy":"reference";
    std::string stem=name+"_"+std::to_string(rate)+"_"+std::to_string(depth);
    std::ofstream csv(dir/(stem+".csv")); csv<<std::setprecision(9);
    csv<<"time_s,cycle,phase,lamp_drive,lamp_brightness,r1_ohms,r2_ohms,r3_ohms,r4_ohms\n";
    std::vector<Point> points; float prev=0; int cycle=-1; uint64_t samples=0;
    const int settleCycles=std::max(5,int(std::ceil(3*rate)));
    while(cycle<settleCycles+4) {
        vibe.out(zero.data(),zero.data(),PERIOD); samples+=PERIOD;
        auto f=vibe.optical_frame();
        if(f.phase<prev) ++cycle;
        prev=f.phase;
        if(cycle>=settleCycles&&cycle<settleCycles+4) {
            Point p{double(samples)/fsample,f,cycle-settleCycles}; points.push_back(p);
            csv<<p.time<<','<<p.cycle<<','<<f.phase<<','<<f.drive<<','<<f.brightness;
            for(float r:f.resistance) csv<<','<<r;
            csv<<'\n';
        }
    }
    for(int cell=-1;cell<4;++cell) summarize(summary,name,rate,depth,points,cell,fsample);
    // Phase-binned averages of multiple measured simulated periods, never hardware data.
    std::array<std::array<double,6>,256> bins{}; std::array<int,256> counts{};
    for(const auto& p:points) {
        int bin=std::min(255,int(p.f.phase*256)); ++counts[bin];
        bins[bin][0]+=p.f.drive; bins[bin][1]+=p.f.brightness;
        for(int c=0;c<4;++c) bins[bin][c+2]+=p.f.resistance[c];
    }
    std::ofstream averaged(dir/(stem+"_averaged.csv"));
    averaged<<"phase,samples,lamp_drive,lamp_brightness,r1_ohms,r2_ohms,r3_ohms,r4_ohms\n";
    for(int b=0;b<256;++b) {
        averaged<<(b+0.5)/256<<','<<counts[b];
        for(double v:bins[b]) averaged<<','<<(counts[b]?v/counts[b]:0);
        averaged<<'\n';
    }
}

void benchmark(const fs::path& dir) {
    using Clock=std::chrono::steady_clock;
    std::ofstream csv(dir/"cpu.csv");
    csv<<"path,mode,quality,mean_ns_per_stereo_frame,max_block_us,blocks,checksum\n";
    volatile float sink=0;
    for(auto mode:{OpticalMode::LegacyOptical,OpticalMode::ReferenceOptical}) {
        std::string name=mode==OpticalMode::LegacyOptical?"legacy":"reference";
        std::array<float,PERIOD> l{},r{},ol{},orr{};
        Vibe v(ol.data(),orr.data()); v.prepare(44100); v.set_optical_mode(mode); v.reseed(1);
        for(int i=0;i<PERIOD;++i) l[i]=r[i]=0.1f*std::sin(2*kPi*i/PERIOD);
        for(auto quality:{VibeQualityMode::Eco,VibeQualityMode::Standard,VibeQualityMode::High}) {
            v.set_quality_mode(quality);
            for(int b=0;b<1000;++b) v.out(l.data(),r.data(),PERIOD);
            double total=0,maximum=0; constexpr int blocks=10000;
            for(int b=0;b<blocks;++b) {
                auto start=Clock::now(); v.out(l.data(),r.data(),PERIOD); auto end=Clock::now();
                double ns=std::chrono::duration<double,std::nano>(end-start).count();
                total+=ns; maximum=std::max(maximum,ns); sink=sink+ol[b%PERIOD];
            }
            csv<<"full_core,"<<name<<','<<int(quality)<<','<<total/(blocks*PERIOD)<<','<<maximum/1000<<','<<blocks<<','<<sink<<'\n';
        }
        double total=0,maximum=0; constexpr int blocks=20000;
        // Fixed identical precomputed excitation; two lanes, no audio/oscillator cost.
        for(int b=0;b<blocks;++b) {
            auto start=Clock::now();
            for(int i=0;i<PERIOD;++i) {
                float e=0.5f+0.45f*l[i]/0.1f;
                if(mode==OpticalMode::LegacyOptical) {
                    v.process_legacy_optical(e,e,0.85f,0.03f,0.97f,1,0.015f);
                    sink=sink+v.mod_res_l*1e-6f;
                } else {
                    auto& a=v.reference_optical[0].process_sample(0,e,0.85f,0.03f,0.97f);
                    auto& z=v.reference_optical[1].process_sample(0,e,0.85f,0.03f,0.97f);
                    sink=sink+(a.resistance[0]+z.resistance[0])*1e-6f;
                }
            }
            double ns=std::chrono::duration<double,std::nano>(Clock::now()-start).count();
            total+=ns; maximum=std::max(maximum,ns);
        }
        csv<<"optical_step,"<<name<<",-1,"<<total/(blocks*PERIOD)<<','<<maximum/1000<<','<<blocks<<','<<sink<<'\n';
    }
}

int main(int argc,char** argv) {
    try {
        fs::path dir="optical_out"; float sampleRate=44100; bool cpuOnly=false;
        for(int i=1;i<argc;++i) {
            std::string arg=argv[i];
            if(arg=="--cpu-only") cpuOnly=true;
            else if(arg=="--out-dir"&&i+1<argc) dir=argv[++i];
            else if(arg=="--sample-rate"&&i+1<argc) sampleRate=std::stof(argv[++i]);
            else throw std::runtime_error("Usage: optical_analyze --out-dir DIR [--sample-rate 44100] [--cpu-only]");
        }
        if(!std::isfinite(sampleRate)||sampleRate<8000||sampleRate>384000) throw std::runtime_error("Invalid sample rate");
        fs::create_directories(dir);
        std::ofstream metadata(dir/"configuration.txt");
        metadata<<"trajectory_sample_rate="<<sampleRate<<"\ntrajectory_stride=32\ncycles=4\nseed=1\ndrift=0\nquality=High\nvoicing=ClassicChorus\nbenchmark_sample_rate=44100\nsizeof_OpticalModel="<<sizeof(OpticalModel)<<"\nsizeof_Vibe="<<sizeof(Vibe)<<'\n';
        const auto base = make_vibe_preset(VibeVoicing::ClassicChorus);
        metadata<<"sweep_min="<<base.user.sweep_min<<"\nsweep_max="<<base.user.sweep_max<<"\nlamp_lag="<<base.user.lamp_lag
                <<"\nreference_lamp_attack_s="<<base.tuning.lamp_attack_sec*2.4f
                <<"\nreference_lamp_release_s="<<base.tuning.lamp_release_sec*1.75f<<'\n';
        if(!cpuOnly) {
            std::ofstream summary(dir/"summary.csv"); summary<<std::setprecision(9);
            summary<<"mode,speed_hz,intensity,cell,min,max,mean,geometric_mean,modulation_ratio,lag_cycles,rise_fraction,rise_10_90_s,fall_90_10_s,cycle_mean_relative_spread\n";
            for(auto mode:{OpticalMode::LegacyOptical,OpticalMode::ReferenceOptical})
                for(float rate:{0.2f,0.5f,1.f,2.f,4.f,7.f})
                    for(float depth:{0.15f,0.35f,0.60f,0.85f,1.f}) trajectory(dir,mode,rate,depth,sampleRate,summary);
        }
        benchmark(dir);
        std::cout<<"Optical CSVs and CPU comparison: "<<dir<<'\n';
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
