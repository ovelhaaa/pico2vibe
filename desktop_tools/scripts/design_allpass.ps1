# Generate allpass coefficients with the original HIIR designer at a pinned revision.
# Downloads build-time tooling only; no dependency enters the audio runtime.
param([string]$OutputDirectory = 'build/m3-1/design')
$ErrorActionPreference = 'Stop'
$revision = '4589fedb4d08b899514cb605ccd7418bf262ab18'
New-Item -ItemType Directory -Force "$OutputDirectory/hiir" | Out-Null
foreach ($file in @('PolyphaseIir2Designer.h','def.h','fnc.h','fnc.hpp')) {
    Invoke-WebRequest "https://raw.githubusercontent.com/unevens/hiir/$revision/$file" -OutFile "$OutputDirectory/hiir/$file"
}
@"
#include "hiir/PolyphaseIir2Designer.h"
#include <iostream>
#include <iomanip>
int main(){double c[32];int n=hiir::PolyphaseIir2Designer::compute_coefs(c,80,.08);std::cout<<n<<'\n'<<std::setprecision(17);for(int i=0;i<n;++i)std::cout<<c[i]<<",\n";}
"@ | Set-Content "$OutputDirectory/design.cpp"
& g++ -O2 "-I$OutputDirectory" "$OutputDirectory/design.cpp" -o "$OutputDirectory/design.exe"
if ($LASTEXITCODE -ne 0) { throw 'Designer build failed' }
& "$OutputDirectory/design.exe"
