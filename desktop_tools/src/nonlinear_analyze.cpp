#include "nonlinear_measurement.hpp"
int main(int argc,char** argv) {
 try {
    // Calibration with known harmonic, folded harmonic and unrelated residual.
    {
        std::vector<double> y(N);const int b=1777, alias=fold(11*b);
        for(int i=0;i<N;++i) y[i]=.5*std::sin(2*pi*b*i/N)+.05*std::sin(2*pi*2*b*i/N)+.01*std::sin(2*pi*alias*i/N);
        auto a=spectrum(y);const double norm=16.0/(3.0*N*N);
        if(std::abs(bin_energy(a,b)*norm-.125)>1e-10 ||
           std::abs(bin_energy(a,2*b)*norm-.00125)>1e-10 ||
           std::abs(bin_energy(a,alias)*norm-.00005)>1e-10)
            throw std::runtime_error("FFT amplitude/energy calibration");
        std::vector<bool> valid(N/2), folded(N/2);
        for(int h=1;h<=128;++h) {if(h*b<N/2) mark(valid,h*b);else mark(folded,fold(h*b));}
        if(valid[alias] || !folded[alias] || !valid[2*b]) throw std::runtime_error("harmonic/alias classification");
        if(argc>1 && std::string(argv[1])=="--self-test") {
            std::cout<<"FFT amplitude and harmonic/alias classification passed\n";return 0;
        }
    }
    std::filesystem::path out=argc>1 ? argv[1]:"build/m3/nonlinear";
    std::filesystem::create_directories(out);
    std::ofstream csv(out/"sines.csv"), curves(out/"transfer.csv"), multi(out/"multitone.csv"), cpu(out/"cpu.csv"), nulls(out/"null.csv"), filters(out/"filters.csv");
    filters<<"factor,frequency_over_base_sample_rate,one_filter_gain_db,total_linear_gain_db\n";
    for(int factor:{2,4}) {
        Fir f(factor);
        for(double freq:{0.,.1,.3,.4,.42,.46,.48,.5,.55,.75,1.}) {
            std::complex<double> response=0;
            for(int i=0;i<f.taps;++i) response+=f.h[i]*std::polar(1.,-2*pi*freq*i/factor);
            filters<<factor<<','<<freq<<','<<db(std::norm(response))<<','<<2*db(std::norm(response))<<'\n';
        }
    }
    csv<<std::setprecision(12); multi<<std::setprecision(12);
    csv<<"stage,sample_rate,requested_frequency,frequency,input_dbfs,drive,asymmetry,algorithm,H1_db,H2_db,H3_db,H4_db,H5_db,THD_db,DC,rms,peak,alias_dbfs,alias_dbc,worst_alias_dbfs,noise_residual_dbfs,collision_bins\n";
    curves<<"drive,asymmetry,input,output,derivative,local_gain,tanh_output\n";
    for(double d:{.8,1.5,3.2}) for(double a:{-.25,0.,.08,.25}) for(int i=-400;i<=400;++i) {
        Transfer t; t.drive=d;t.asym=a; const double x=i*.01, delta=1e-3;
        curves<<d<<','<<a<<','<<x<<','<<t(x)<<','<<(t(x+delta)-t(x-delta))/(2*delta)<<','<<(x==0 ? (t(delta)-t(-delta))/(2*delta):t(x)/x)<<','<<t.tanh_reference(x)<<'\n';
    }
    const std::array<Mode,10> modes={Mode::Direct,Mode::Midpoint,Mode::ADAA,Mode::Fir2,Mode::Fir4,Mode::TanhReference,Mode::Allpass2,Mode::Allpass4,Mode::Allpass2ADAA,Mode::Elliptic2};
    for(bool bjt:{true,false}) for(double sr:{44100.,48000.,96000.,192000.})
      for(double d: {.8,1.5,3.2}) for(double requested:{80.,440.,1000.,3000.,7000.,9000.,10000.,12000.,15000.}) {
        if(!bjt && d!=1.5) continue;
        const int bin=int(std::round(requested*N/sr));
        const double freq=bin*sr/N;
        for(double level:{-60.,-36.,-24.,-18.,-12.,-9.,-6.,-3.,0.}) for(Mode mode:modes) {
            Transfer t;t.bjt=bjt;t.drive=bjt?d:1.;t.asym=bjt?.08:0.;
            Processor p(mode,t); const auto y=render(p,sr,std::pow(10,level/20),{bin}); const auto a=spectrum(y);
            std::vector<bool> valid(N/2), aliased(N/2),collision(N/2);
            for(int h=1;h<=128;++h) {
                if(h*bin<N/2) mark(valid,h*bin); else mark(aliased,fold(h*bin));
            }
            double ae=0,ne=0,worst=0,he=0,dc=0,rms=0,peak=0;int collisions=0;
            for(int k=1;k<N/2;++k) {
                if(valid[k]&&aliased[k]) { collision[k]=true; ++collisions; }
                if(aliased[k]&&!valid[k]) {
                    ae+=std::norm(a[k]);
                    double tone=0;
                    for(int j=std::max(1,k-2);j<=std::min(N/2-1,k+2);++j)
                        if(!valid[j] && aliased[j]) tone+=std::norm(a[j]);
                    worst=std::max(worst,tone);
                }
                if(!valid[k]&&!aliased[k]&&k>2) ne+=std::norm(a[k]);
            }
            for(int h=2;h*bin<N/2;++h) he+=bin_energy(a,h*bin);
            for(double v:y) {dc+=v;rms+=v*v;peak=std::max(peak,std::abs(v));}
            const double h1=bin_energy(a,bin), normalization=16.0/(3.0*N*N);
            csv<<(bjt ? "bjt":"rational_limiter")<<','<<sr<<','<<requested<<','<<freq<<','<<level<<','<<t.drive<<','<<t.asym<<','<<name(mode);
            for(int h=1;h<=5;++h) {
                if(h*bin>=N/2) csv<<",nan";
                else csv<<','<<db(bin_energy(a,h*bin)*normalization*2); // peak amplitude dBFS
            }
            csv<<','<<db(he/h1)<<','<<dc/N<<','<<std::sqrt(rms/N)<<','<<peak<<','<<db(ae*normalization)<<','<<db(ae/h1)<<','<<db(worst*normalization)<<','<<db(ne*normalization)<<','<<collisions<<'\n';
        }
      }
    // Enumerate signed intermodulation products through total order 7.
    multi<<"stage,sample_rate,algorithm,imd_dbfs,alias_dbfs,residual_dbfs,reference_magnitude_residual_dbfs,collision_bins\n";
    for(bool bjt:{true,false}) for(double sr:{44100.,48000.,96000.,192000.}) {
        const std::vector<int> bins={int(std::round(7103*N/sr)),int(std::round(9973*N/sr)),int(std::round(13109*N/sr))};
        std::vector<bool> legit(N/2),alias(N/2),fund(N/2);
        for(int b:bins) mark(fund,b);
        for(int i=-7;i<=7;++i) for(int j=-7;j<=7;++j) for(int k=-7;k<=7;++k) {
            const int order=std::abs(i)+std::abs(j)+std::abs(k); if(order==0 || order>7) continue;
            const int b=std::abs(i*bins[0]+j*bins[1]+k*bins[2]);
            if(b<N/2) mark(legit,b); else mark(alias,fold(b));
        }
        // Analytic sine excitation at 16x, same transfer. Low spectral bins are
        // an offline bandlimited reference; no circuit-ground-truth claim.
        Transfer t;t.bjt=bjt;t.drive=3.2;
        std::vector<double> ref(N*16);
        for(int i=0;i<N*16;++i) {
            double x=0;for(size_t j=0;j<bins.size();++j) x+=.8/bins.size()*std::sin(2*pi*bins[j]*i/(N*16)+.37*j);
            ref[i]=t(x);
        }
        const auto ra=spectrum(ref);
        for(Mode m:modes) {
            Processor p(m,t);auto y=render(p,sr,.8,bins);auto a=spectrum(y);
            double imd=0,ae=0,res=0,diff=0;int collisions=0;
            for(int k=3;k<N/2;++k) {
                const double e=std::norm(a[k]);
                if(legit[k]&&!fund[k]) imd+=e;
                if(alias[k]&&!legit[k]) ae+=e;
                if(alias[k]&&legit[k]) ++collisions;
                if(!alias[k]&&!legit[k]) res+=e;
                const double delta=std::abs(a[k])-std::abs(ra[k])/16;diff+=delta*delta;
            }
            const double norm=16.0/(3.0*N*N);
            multi<<(bjt?"bjt":"rational_limiter")<<','<<sr<<','<<name(m)<<','<<db(imd*norm)<<','<<db(ae*norm)<<','<<db(res*norm)<<','<<db(diff*norm)<<','<<collisions<<'\n';
        }
    }
    cpu<<"algorithm,ns_per_sample,state_bytes,total_object_bytes,legacy_nominal_delay_samples\n";
    nulls<<"algorithm,frequency,gain_alignment,residual_dbc,phase_only_residual_dbc\n";
    volatile double sink=0;
    for(Mode m:modes) {
        Processor p(m,{}); auto input=render(p,44100,.7,{59}); p.reset();
        auto start=std::chrono::steady_clock::now();
        for(int run=0;run<8;++run) for(double v:input) sink=p.process(v);
        auto elapsed=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count();
        cpu<<name(m)<<','<<elapsed/(8*N)<<','<<(m==Mode::Fir2||m==Mode::Fir4 ? sizeof(Fir):m==Mode::Direct||m==Mode::TanhReference?0:m==Mode::Elliptic2?sizeof(Elliptic2x):m==Mode::Allpass2||m==Mode::Allpass4||m==Mode::Allpass2ADAA?sizeof(VibeAAState):sizeof(double)*3+sizeof(bool))<<','<<sizeof(p)<<','<<(m==Mode::Fir2||m==Mode::Fir4 ? 64:m==Mode::ADAA?.5:m==Mode::Midpoint?.5+(.28/.72):m==Mode::Direct||m==Mode::TanhReference?0:std::numeric_limits<double>::quiet_NaN())<<'\n';
        Processor q(m,{}), direct(Mode::Direct,{});auto y=render(q,44100,.7,{59}), target=render(direct,44100,.7,{59});
        // FIR delay is integer; ADAA/midpoint require fractional phase alignment.
        // Coherent periodic null: align before windowing; a delayed Hann window
        // would create a false difference in the neighboring leakage bins.
        Spectrum a(y.begin(),y.end()), b(target.begin(),target.end());fft(a);fft(b);
        const double phase=std::arg(a[59]/b[59]);const double delay=-phase*N/(2*pi*59);
        double yy=0,yb=0,bb=0;
        for(int k=1;k<N/2;++k) {
            a[k]*=std::polar(1.,2*pi*k*delay/N);
            yy+=std::norm(a[k]);yb+=(a[k]*std::conj(b[k])).real();bb+=std::norm(b[k]);
        }
        double g=yb/yy,error=0;for(int k=1;k<N/2;++k) error+=std::norm(g*a[k]-b[k]);
        double phase_only=0;for(int k=1;k<N/2;++k)phase_only+=std::norm(a[k]-b[k]);
        nulls<<name(m)<<','<<59*44100./N<<','<<g<<','<<db(error/bb)<<','<<db(phase_only/bb)<<'\n';
    }
    std::cout<<"Nonlinear analysis written to "<<out<<"; N="<<N<<"; sink="<<sink<<'\n';
    return 0;
 } catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
