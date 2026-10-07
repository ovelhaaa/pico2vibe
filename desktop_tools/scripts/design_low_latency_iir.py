from scipy import signal
import numpy as np
n,wn=signal.ellipord(.42,.58,.02,80)
sos=signal.ellip(n,.02,80,wn,output='sos')
print(n); print(sos)
from pathlib import Path
coeff=',\n'.join('{'+','.join(format(float(x),'.17g') for x in row[[0,1,2,4,5]])+'}' for row in sos)
Path('desktop_tools/src/elliptic_candidate.hpp').write_text('''#pragma once
#include <array>
// scipy.signal.ellipord(.42,.58,.02,80), ellip(...,output="sos").
// Conventional zero insertion / 2x interpolation, nonlinear evaluation,
// anti-alias SOS filtering then even-phase decimation. Desktop-only reference.
struct Elliptic2x {
 static constexpr double coefficients['''+str(len(sos))+'''][5]={
'''+coeff+'''};
 std::array<std::array<double,2>,'''+str(len(sos))+'''> up{},down{};
 void reset(){up={};down={};}
 template<class State> static double filter(double x,State& state){
  for(size_t k=0;k<state.size();++k){const auto& c=coefficients[k];double y=c[0]*x+state[k][0];state[k][0]=c[1]*x-c[3]*y+state[k][1];state[k][1]=c[2]*x-c[4]*y;x=y;}return x;
 }
 template<class Fn> double process(double x,Fn fn){double y=filter(fn(filter(2*x,up)),down);filter(fn(filter(0,up)),down);return y;}
};
''')
