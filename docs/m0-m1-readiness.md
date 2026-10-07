# Pico2Vibe M0/M1 readiness - 0.9.0 pre-release

Validated locally on 2026-10-06 against baseline `bf3d0a0` (main, grey-box phase
network). The existing sound was retained; no phase, optical, BJT, oversampling,
Quality-mode tuning or UI redesign was performed.

## Release and CI fixes

The VST job previously passed pluginval but failed reading a file pluginval never
created at the expected path. The job now initializes its own log before build,
captures both output streams through `Tee-Object`, saves `$LASTEXITCODE` immediately,
and requires exit code 0 plus a standalone SUCCESS line. Strictness remains 8,
seed remains `0x7f78bbf`, timeout remains 120000 ms and GUI tests stay skipped as
before. Log upload uses `always()`. VST3 upload is explicitly conditional on the
validation step succeeding, independently of a log-upload failure. No optional
pluginval-generated file is read. The same PowerShell capture/check logic passed
locally against pluginval 1.0.4 with SUCCESS and exit code 0.

The WASM source is C++ but the previous script used the C driver `emcc` to link,
leaving new/delete unresolved. `web/wasm/build_web.sh` now compiles and links with
`em++ -std=c++17`; the normal C++ runtime resolves allocation. It still includes
`src/dsp/vibe_core.hpp` through the same C ABI. No custom allocation replacements
or DSP copies were added. The complete static preview compiled locally with the
installed Emscripten SDK; Git Bash used a shell adapter invoking `em++.py` because
that Windows SDK provides .bat/.ps1/.py launchers rather than a Unix `em++` binary.

Desktop CI now triggers on shared DSP changes and runs CTest before publishing.
GitHub-hosted Actions and artifact upload were not dispatched from this checkout;
local execution verifies the commands, not GitHub's service availability.

## Preset, Quality and state behavior

Factory presets now contain only voicing and musical values in the shared
`src/dsp/factory_presets.hpp` bank. Recall preserves current bypass and Quality.
A/B snapshots omit both globals; A/B recall injects the current global values
before APVTS replacement, including for old v1 snapshots containing stale globals.
Project state still saves/restores both. Quality edits no longer label the sound
Custom. Eco/Standard/High DSP behavior, parameter IDs, program indices and project
schema version 1 remain compatible. Existing plugin/manufacturer codes `P2vc` /
`P2vb`, product name and generated bundle identity are unchanged.

APVTS remains authoritative: `set_voicing()` installs circuit/character tuning
and standalone defaults, then the plugin reapplies APVTS musical parameters.
Current-program tracking is a label, not another DSP parameter source. State
migration fills missing controls, clamps out-of-range values and replaces
non-finite numeric values with parameter defaults.

The project/plugin version is 0.9.0. Manufacturer remains pico2vibe, product remains
pico2vibe, and the description is "Optical vibe with chorus and vibrato". README
wording describes a pre-release product instead of a scaffold.

## DSP responsibility boundary

`studio_process_wet()` owns mid/side width, mono guard/stereo focus, wet energy
compensation and vibrato makeup. `studio_output_gain()` owns adaptive trim and
auto level. Existing expressions, sample cadence and order are preserved. The
Reference foundation retains input conditioning, optical motion, the four
grey-box cells per lane, transistor shaping and chorus/vibrato topology.
Creative feedback mapping, non-circuit tone/clarity shaping and optional noise/
drift are named/documented as Studio responsibilities, retained in lane/LFO
order pending a future mode implementation. No mode switch was added.

`VibeOutputConditionerConfig` supplies independent runtime flags for DC blocking,
auto headroom and soft limiting, applied in that order. Configure/reset at prepare
boundaries; compile-time flags are defaults only. Default processing is preserved.
`ENABLE_TPDF_DITHER` is live in embedded float-to-PCM24 conversion and deliberately
retained/documented. The conditioner's RNG accessor supports that conversion;
its float audio process never adds dither. VST/WASM output remains undithered.

## Product-readiness matrix

| Coverage | Automated check | Local result |
| --- | --- | --- |
| Silence, feedback off, frozen modulation | Three existing desktop tests | Pass |
| Notch behavior / four grey-box stages | Existing phase_notch_test, greybox_stage_test | Pass |
| Core block-size invariance | Existing seeded block_size_invariance_test | Pass |
| Conditioner stages / reset | New test: all 8 flag combinations, 44.1/48/96/192 kHz | Pass |
| Mono/stereo; all three Qualities; all 12 presets | New JUCE processing matrix, 44.1/48/96/192 kHz | Pass |
| Arbitrary blocks; empty callback | Matrix blocks 1, 7, 31, 32, 33, 127, 513, 2049, plus 0 | Pass |
| Automation; finite/safe output | Matrix automates depth/mix and bounds peaks below 1.25 | Pass |
| Preset bypass/Quality preservation | All 12 presets x 3 Qualities x 2 bypass states | Pass |
| Project save/restore | Globals, repeated restoration, Custom program, A/B | Pass |
| A/B globals / legacy slot migration | Old full slot values cannot override globals | Pass |
| Transport | Existing BPM/PPQ, loop/seek, stopped/missing host, phase lock tests | Pass |
| Invalid/malformed state | Corrupt binary, missing control, numeric extremes, bad slot | Pass |
| Preset loudness/mono compatibility | Existing 12-preset seeded audio test, High explicitly selected | Pass |
| Editor attachments / presets / A/B / reopening | Existing editor smoke test | Pass |
| VST3 build | JUCE 7.0.12, Windows x64 Release, MinGW GCC 14.2 | Pass |
| pluginval | 1.0.4 strictness 8, automation/fuzz/thread safety/state tests | SUCCESS, exit 0 |
| Web preview build | Installed Emscripten C++ driver | Pass |
| RP2350 firmware build attempt | Existing build/rp2350-zero configuration | Unavailable: cached Pico SDK installation absent |

CTest: 7/7 desktop tests and 1/1 JUCE smoke executable pass. The JUCE executable
contains the full matrix and state/editor checks above. Visual/layout changes
were not made. MinGW emits upstream JUCE DirectWrite/time-zone warnings; no
project compilation errors remain. MSVC was unavailable locally; Windows Actions
still uses VS2022 and requires a hosted run to confirm that toolchain.

## Objective baseline and after comparison

Before modifying `vibe_core.hpp`, the existing desktop analysis tooling was given
factory-bank adapters that read the extracted, unchanged main preset values.
Both runs use seed 1, High, 44.1 kHz, default signal durations (impulse 1s,
sweep 8s, synthetic guitar 4s, 440Hz sine 2s at -24/-18/-12/-6/0 dB), identical
parameters and final output conditioning. This avoids measuring old CLI aliases,
whose musical values differ from the actual VST bank. No harness-side DSP is
introduced. Factory parameters override the standalone voicing defaults exactly.

All 3,415 compared numerical values (summary, frequency response, notch tracking,
THD/alias proxy by input level) are unchanged at CSV precision. All 64 exported
output WAV files have identical SHA256 hashes and zero maximum sample delta.
RMS/peak below describe both runs on the synthetic guitar output; other columns
are the existing harness summary. WAV exports bound samples to +/-1, so the
waveform comparison is supplemented by pre-export DSP metric comparisons and
JUCE finite/level tests. These are seeded objective regressions, not a listening
assessment or a circuit measurement.

| Preset | RMS before = after | Peak before = after | Deepest notch dB | Side/mid dB | Mono fold dB | L/R corr | Worst THD dB | Alias proxy dB |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Classic Uni-Vibe | 0.058793 | 0.704327 | -9.235 | -21.637 | -0.030 | 0.987251 | -21.846 | -79.540 |
| Shin-ei Dark | 0.060932 | 0.696095 | -10.446 | -20.331 | -0.040 | 0.983252 | -22.846 | -79.250 |
| Deja Lead | 0.057360 | 0.683082 | -9.143 | -22.977 | -0.022 | 0.990390 | -21.899 | -79.802 |
| Voodoo Wide | 0.058889 | 0.720928 | -9.660 | -6.593 | -0.861 | 0.640929 | -22.316 | -79.527 |
| Modern Hi-Fi | 0.054963 | 0.645029 | -7.945 | -18.170 | -0.066 | 0.970025 | -22.733 | -79.829 |
| Classic Vibrato | 0.058000 | 0.572358 | -11.121 | -25.209 | -0.013 | 0.994022 | -15.574 | -82.595 |
| Hendrix Deep | 0.059656 | 0.640797 | -13.556 | -13.032 | -0.211 | 0.915028 | -21.625 | -78.737 |
| Gentle Clean | 0.054755 | 0.593652 | -7.155 | -37.948 | -0.001 | 0.999694 | -24.865 | -79.883 |

The measurable DSP delta for this matrix is zero. Selecting a preset while bypass
is active now leaves it active; selecting a preset under Eco/Standard now retains
that Quality instead of forcing High. Those intentional behavior differences
must not be interpreted as changes to any Quality algorithm or factory tuning.

Evidence saved as compact CSVs under [regression/m0-m1](regression/m0-m1):
`before_summary.csv`, `after_summary.csv`, `metric_comparison.csv` and
`waveform_comparison.csv`, plus `pluginval.log`, `juce-smoke.log` and
`core-tests.log`. Full generated WAVs remain in `build/m0m1` (ignored build output).

### Reproduce

```powershell
cmake -S desktop_tools -B build/desktop_tools -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/desktop_tools --parallel 4
ctest --test-dir build/desktop_tools --output-on-failure
build/desktop_tools/dsp_validate.exe --out-dir build/m0m1/after --compare-to build/m0m1/before --quality high --preset factory_0 --preset factory_1 --preset factory_2 --preset factory_3 --preset factory_4 --preset factory_5 --preset factory_6 --preset factory_7
python desktop_tools/scripts/compare_regression.py build/m0m1/before build/m0m1/after docs/regression/m0-m1
cmake -S . -B build/m0m1/vst -G Ninja -DCMAKE_BUILD_TYPE=Release -DPICO2VIBE_BUILD_FIRMWARE=OFF -DPICO2VIBE_BUILD_JUCE_PLUGIN=ON -DBUILD_TESTING=ON -DPICO2VIBE_JUCE_DIR=C:/path/to/JUCE-7.0.12
cmake --build build/m0m1/vst --parallel 4
ctest --test-dir build/m0m1/vst --output-on-failure
```

Use the validation command in `.github/workflows/vst.yml` for pluginval. Run
`web/wasm/build_web.sh` with the Emscripten SDK activated. For a fresh before run,
use baseline main `bf3d0a0` plus the factory-bank analysis adapter only, retaining
its unmodified DSP core; capture before any structural edits.

## Files changed

- `.github/workflows/vst.yml`, `.github/workflows/main.yml`: logging/artifacts and core test CI.
- `CMakeLists.txt`, `plugin/juce/CMakeLists.txt`: version/description.
- `web/wasm/build_web.sh`, `.gitignore`: C++ WASM linkage and generated-output exclusion.
- `plugin/juce/PluginProcessor.cpp`, `.h`: preset globals, A/B state, defensive state migration.
- `src/dsp/factory_presets.hpp`: shared musical bank, unchanged values, no Quality member.
- `src/dsp/vibe_core.hpp`: Studio function boundaries, conditioner configuration and comments.
- `plugin/juce/PluginSmokeTest.cpp`: global/state/processing matrix tests.
- `desktop_tools/CMakeLists.txt`, `src/output_conditioner_test.cpp`: conditioner coverage.
- `desktop_tools/src/processor.hpp`, `.cpp`, `src/analysis_main.cpp`: actual factory-bank analysis.
- `desktop_tools/scripts/compare_regression.py`: seeded metrics/WAV comparison and CSV evidence.
- `README.md`, `plugin/README.md`, `desktop_tools/README.md`: product and architecture documentation.
- `docs/m0-m1-readiness.md`, `docs/regression/m0-m1/*`: this report, before/after evidence and validation logs.

## Remaining risks before the next milestone

A GitHub-hosted Windows/MSVC run and its artifact upload still need observation;
local MinGW/pluginval validation cannot stand in for a hosted Actions run. Firmware
cannot be rebuilt with the removed cached Pico SDK until that dependency is
restored. Physical hardware CPU budget and listening/real-DAW compatibility tests
remain outside the local automated evidence. Reference/Studio is deliberately a
prepared boundary: creative feedback/tone/drift are still interleaved, with no
new public mode. Objective regression is limited to the seeded High/44.1 kHz
analysis suite; other rates/Qualities have smoke/stability coverage, not spectral
characterization. Existing preset recall/state synchronization threading design
is retained and should receive a dedicated real-time audit before production.
