#include "nonlinear_candidates.hpp"
#include "dsp/vibe_core.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>
#include <fstream>

using namespace nonlinear;
void check(bool good,const char* message) {if(!good) throw std::runtime_error(message);}

// Sample-addressed automation: identical event times under callback partitions.
std::vector<float> automated(VibeQualityMode quality,int partition,bool ramp) {
    std::array<float,PERIOD> l{},r{},ol{},orr{};
    Vibe v(ol.data(),orr.data());v.prepare(44100);v.reseed(1);v.set_quality_mode(quality);
    v.set_param(VibeParamId::NoiseAmount,0);
    v.set_param(VibeParamId::InputDrive,.5f);v.set_param(VibeParamId::SatAsymmetry,-.25f);
    v.set_param(VibeParamId::SatOutTrim,.6f);v.set_param(VibeParamId::Feedback,0);
    v.reseed(1); // prepare/reset controls before the sample-addressed events
    std::vector<float> result;
    for(int pos=0;pos<16384;) {
        // Control events occur every 256 samples and partitions split at them.
        const float n=ramp ? float(pos/256)/63:float((pos/256)&1);
        v.set_param(VibeParamId::InputDrive,.5f+5.5f*n);
        v.set_param(VibeParamId::SatAsymmetry,-.25f+.5f*n);
        v.set_param(VibeParamId::SatOutTrim,.6f+.6f*n);
        v.set_param(VibeParamId::Feedback,.65f*n);
        const int end=std::min(16384,(pos/256+1)*256);
        while(pos<end) {
            int count=std::min(partition,end-pos);
            for(int i=0;i<count;++i) l[i]=r[i]=.3f*std::sin(2*pi*440*(pos+i)/44100);
            v.out(l.data(),r.data(),count);
            for(int i=0;i<count;++i) {check(std::isfinite(ol[i]),"automation nonfinite");result.push_back(ol[i]);}
            pos+=count;
        }
    }
    return result;
}
int main(int argc,char** argv) {
 try {
    std::ofstream report;if(argc>1) {report.open(argv[1]);report<<"test,algorithm,metric,value\n";}
    std::ofstream transients;
    if(argc>1) {
        auto path=std::string(argv[1]);auto slash=path.find_last_of("/\\");
        transients.open((slash==std::string::npos ? "":path.substr(0,slash+1))+"transients.csv");
        transients<<"stage,drive,asymmetry,algorithm,peak,maximum_static_endpoint,overshoot,max_sample_delta\n";
    }
    // Tie the static harness to the real Eco/Standard/High implementation.
    for(auto quality:{VibeQualityMode::Eco,VibeQualityMode::Standard,VibeQualityMode::High}) for(bool limiter:{false,true}) {
        std::array<float,PERIOD> l{},r{};Vibe v(l.data(),r.data());v.reseed(1);v.set_quality_mode(quality);
        Transfer t;t.bjt=!limiter;t.drive=1.5;t.asym=v.smoothed_user_params().sat_asymmetry;
        t.gain=v.tuning_params().bjt_gain_trim;t.trim=v.smoothed_user_params().sat_out_trim;
        Processor p(quality==VibeQualityMode::High ? Mode::Midpoint:Mode::Direct,t);
        VibeOversampleState state;
        for(int i=0;i<4096;++i) {
            const float x=float(.7*std::sin(i*.17));
            check(std::abs(v.analysis_nonlinear_sample(x,1.5f,limiter,state)-p.process(x))<2e-7,"shipping Quality probe parity");
        }
    }
    for(bool bjt:{true,false}) for(double d:{.8,1.5,3.2}) for(double bias:{-.25,0.,.25}) {
        Transfer t;t.bjt=bjt;t.drive=d;t.asym=bias;
        for(double x:{-4.,-1.,-.01,0.,.01,1.,4.}) {
            const double e=1e-4;
            check(std::abs((t.integral(x+e)-t.integral(x-e))/(2*e)-t(x))<2e-6,"primitive derivative");
        }
        for(Mode m:{Mode::Direct,Mode::Midpoint,Mode::ADAA,Mode::Fir2,Mode::Fir4}) {
            Processor p(m,t);double maxstep=0,previous=0,peak=0;
            for(int i=0;i<2048;++i) {
                double x=i<256?0:i<1024?.15:i<1536?-.15:1e-12;
                const double y=p.process(x);check(std::isfinite(y),"step nonfinite");
                if(i<256) check(std::abs(y)<1e-12,"silence output");
                maxstep=std::max(maxstep,std::abs(y-previous));previous=y;
                peak=std::max(peak,std::abs(y));
            }
            p.reset();Processor q(m,t);
            for(int i=0;i<1024;++i) {
                const double x=.1*std::sin(i*.01);
                check(p.process(x)==q.process(x),"reset determinism");
            }
            p.reset();double settled=0;
            for(int i=0;i<4096;++i) settled=p.process(.1);
            check(std::abs(settled-t(.1))<2e-6,"DC transfer consistency across Quality/candidates");
            if(report) report<<"step,"<<name(m)<<",maximum_sample_delta,"<<maxstep<<'\n';
            if(transients) {
                const double endpoints=std::max(std::abs(t(.15)),std::abs(t(-.15)));
                transients<<(bjt?"bjt":"rational_limiter")<<','<<d<<','<<bias<<','<<name(m)<<','<<peak<<','<<endpoints<<','<<std::max(0.,peak-endpoints)<<','<<maxstep<<'\n';
            }
        }
    }
    // Bias subtraction cancels DC at zero, not the mean of asymmetric AC.
    for(double bias:{-.25,0.,.25}) {
        Transfer t;t.asym=bias;Processor p(Mode::ADAA,t);
        VibeOutputConditioner c;VibeOutputConditionerConfig config;
        config.auto_headroom=false;config.soft_limiter=false;c.configure(config);c.reset(44100,1);
        double before=0,after=0;
        for(int i=0;i<44100*3;++i) {
            double y=p.process(.3+.7*std::sin(2*pi*100*i/44100));float l,r;
            c.process_frame(float(y),float(y),&l,&r);
            if(i>=44100*2) {before+=y;after+=l;}
        }
        check(std::abs(after/44100)<1e-6,"DC blocker mean");
        if(report) report<<"dc,adaa1,before,"<<before/44100<<"\ndc,adaa1,after,"<<after/44100<<'\n';
    }
    for(auto quality:{VibeQualityMode::Eco,VibeQualityMode::Standard,VibeQualityMode::High}) for(bool ramp:{false,true}) {
        auto a=automated(quality,32,ramp),b=automated(quality,7,ramp);
        double difference=0,delta=0;
        for(size_t i=0;i<a.size();++i) {difference=std::max(difference,double(std::abs(a[i]-b[i])));if(i)delta=std::max(delta,double(std::abs(a[i]-a[i-1])));}
        check(difference<2e-6,"automation block invariance");
        // A generous numerical discontinuity bound, not a subjective click test.
        check(delta<.5,"automation discontinuity");
        if(report) report<<"automation,"<<int(quality)<<",block_error,"<<difference<<"\nautomation,"<<int(quality)<<",max_delta,"<<delta<<'\n';
    }
    for(Mode m:{Mode::Direct,Mode::Midpoint,Mode::ADAA,Mode::Fir2,Mode::Fir4}) for(bool ramp:{false,true}) {
        Processor p(m,{});double last=0,delta=0;
        for(int i=0;i<8192;++i) {
            const double control=ramp ? double(i)/8191:double((i/256)&1);
            p.transfer.drive=.8+2.4*control;p.transfer.asym=-.25+.5*control;p.transfer.trim=.6+.6*control;
            const double y=p.process(.7*std::sin(2*pi*7000*i/44100));
            check(std::isfinite(y),"candidate automation nonfinite");delta=std::max(delta,std::abs(y-last));last=y;
        }
        p.reset();for(int i=0;i<8192;++i) {
            p.transfer.asym=(i&1)?-.25:.25;
            check(std::abs(p.process(0))<1e-12,"candidate automation silence");
        }
        if(report) report<<"candidate_automation,"<<name(m)<<",max_delta,"<<delta<<'\n';
    }
    std::cout<<"nonlinear_test passed: primitives, silence, DC, steps, reset, automation, partitioning\n";
 } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
