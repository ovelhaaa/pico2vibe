# pico2vibe 0.9.0 - pre-release optical vibe

This repository now uses a **single shared DSP core** in `src/dsp/vibe_core.hpp` for:
- RP2350 firmware build
- desktop tooling
- browser WASM preview
- JUCE VST3 plugin

## Embedded (RP2350) build

Prerequisites:
- Pico SDK 2.x
- `cmake` 3.13+
- `ninja`
- ARM GCC toolchain
- `PICO_SDK_PATH` set

Build:
```bash
cmake --preset rp2350-zero
cmake --build --preset rp2350-zero -j
```

UF2 output:
`build/rp2350-zero/univibe_rp2350_dma.uf2`

## Web preview (WASM)

Prerequisites:
- Emscripten (`em++` available in PATH)

Build static web assets:
```bash
web/wasm/build_web.sh
```

Output is generated in `web/dist/`:
- `index.html`
- `app.js`
- `styles.css`
- `vibe_wasm.js`
- `vibe_wasm.wasm`
- `.nojekyll`

Serve locally with any static server, e.g.:
```bash
python3 -m http.server --directory web/dist 8080
```

## GitHub Pages deployment

Workflow: `.github/workflows/web-pages.yml`

- builds on PRs and pushes touching `src/**` or `web/**`
- compiles WASM with Emscripten
- uploads `web/dist` as Pages artifact
- deploys on pushes to `main`

The browser app loads the same C++ DSP core used by firmware via the exported C ABI in `web/wasm/vibe_wasm.cpp`.

## VST3 pre-release

The repository now includes an optional JUCE wrapper in `plugin/juce`. Firmware remains the default build; the plugin can be configured without Pico SDK:

```powershell
cmake -S . -B build/vst -G Ninja ^
  -DPICO2VIBE_BUILD_FIRMWARE=OFF ^
  -DPICO2VIBE_BUILD_JUCE_PLUGIN=ON ^
  -DPICO2VIBE_JUCE_DIR=C:/path/to/JUCE
cmake --build build/vst -j
```

If JUCE is installed as a CMake package, omit `PICO2VIBE_JUCE_DIR`. The plugin exposes the shared DSP parameters, voicing, quality mode, factory presets, smoothed bypass and output metering for host-readiness testing.

The `.github/workflows/vst.yml` workflow builds the Windows x64 VST3 on relevant pushes and pull requests, or on demand. It runs the JUCE smoke tests and pluginval at strictness level 8 before publishing the complete plugin bundle as the `pico2vibe-vst3-windows-x64` artifact.

## Sound design

pico2vibe is a digital optical vibe inspired by classic Uni-Vibe circuits. It uses a four-stage phase network per channel, lamp/LDR-style inertia, component mismatch, nonlinear transistor-style drive and chorus/vibrato modes.

The Classic voicings now favor a more mono-compatible, vintage center image with less automatic wet-level correction, so the optical pulse can breathe. Modern Wide keeps a cleaner, wider and more controlled stereo presentation for hi-fi preview and production use.

The `BulbAsym` LFO shape models an asymmetric bulb-like sweep: the rise is slightly quicker, the decay relaxes more slowly, and both edges remain smooth to avoid audible phase discontinuities during slow modulation.

### Voicings

- Classic Chorus: vintage mono-ish chew, moderate feedback.
- Classic Vibrato: 100% wet pitch/phase wobble.
- Deep Throb: slower, darker, stronger low-mid pulse.
- Modern Wide: cleaner, wider stereo image with brighter feedback.
- Vintage Uni-Vibe Chorus: warm liquid chorus with softened highs and classic feedback.
- Deep Hendrix Swirl: deep, vocal swirl with slower lamp inertia and safe chew.
- Trower Lead: mid-forward lead voice that keeps attack with drive/fuzz.
- Gentle Clean Vibe: slow, subtle clean chord movement.
- Wide Stereo Dream: wide complementary L/R sweep for pads and production.
- Vintage Vibrato: wet vintage wobble without dominant dry.
- Shallow Always-On: low-mix movement for an always-on signal lift.
- Psychedelic Slow Sweep: very slow deep sweep for sustained textures.
- Fast Rotary-ish Vibe: fast vibe shimmer with moderate depth.
- Bass/Synth Friendly Vibe: low-feedback voicing that preserves fundamentals.
- Lo-Fi Lamp Drift: organic asymmetry and lamp drift with mild saturation.
- Modern Hi-Fi Phase Vibe: clean, stable, controlled modern phase-vibe.

### Recommended settings

Guitar clean: Classic Chorus, speed 0.8–1.3 Hz, depth 0.7–0.9.
Lead guitar: Deep Throb, feedback 0.4–0.5.
Keys/pads: Modern Wide, mix 0.55–0.65.

## Model, musical presets and runtime preferences

`VibeVoicing` is the current model/character selector: it chooses Classic/Modern
behavior, optical tuning, feedback profile, LFO shape, chorus/vibrato topology
and circuit-oriented tuning. `set_voicing()` installs that profile and useful
standalone defaults. The plugin then reapplies every APVTS sound parameter, so
APVTS remains the authoritative project state. `currentProgram` is a host/UI
label: musical edits select Custom; it is not a second source of DSP settings.
The public parameter ID `voicing` and all existing program indices are unchanged.

Factory presets are musician-facing parameter sets based on one voicing. The
shared bank in `src/dsp/factory_presets.hpp` supplies both the plugin and desktop
factory analysis. Recalling a preset preserves bypass and Quality. Eco, Standard
and High keep their existing processing behavior; Quality and bypass are global
across A/B slots and stored/restored in the top-level DAW project state. Old A/B
snapshots containing these controls are accepted but cannot override the globals.

## Reference / Studio preparation

The four grey-box cells, input conditioning, lamp/LDR motion, transistor shaping
and chorus/vibrato topology remain the circuit-oriented Reference foundation.
`Vibe::studio_process_wet()` owns wet mid/side width, stereo focus/mono guard,
wet energy compensation and vibrato makeup. `studio_output_gain()` owns adaptive
output trim/auto level. Studio tone/clarity shaping, creative feedback shaping
and optional colored noise/drift are identified in the current processing path;
they remain interleaved where extraction would disturb lane/state ordering.
There is no Reference/Studio mode switch or DSP retuning in this release.

`VibeOutputConditioner` is the final Studio safety chain, ordered DC blocker,
auto headroom, soft limiter. `VibeOutputConditionerConfig` permits independent
runtime selection at prepare/reset boundaries; compile-time flags supply only
defaults. All three remain enabled by default. `ENABLE_TPDF_DITHER` is actually
used by the firmware's float-to-PCM24 conversion before quantization. It is never
applied by the conditioner or to floating-point VST/WASM output.

See [M0/M1 validation and regression report](docs/m0-m1-readiness.md) for the test
matrix, measured before/after results, reproduction commands and remaining risks.
