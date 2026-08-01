# pico2vibe (RP2350 + WebAssembly preview)

This repository now uses a **single shared DSP core** in `src/dsp/vibe_core.hpp` for:
- RP2350 firmware build
- desktop tooling
- browser WASM preview

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
- Emscripten (`emcc` available in PATH)

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

## VST3 plugin scaffold

The repository now includes an optional JUCE wrapper in `plugin/juce`. Firmware remains the default build; the plugin can be configured without Pico SDK:

```powershell
cmake -S . -B build/vst -G Ninja ^
  -DPICO2VIBE_BUILD_FIRMWARE=OFF ^
  -DPICO2VIBE_BUILD_JUCE_PLUGIN=ON ^
  -DPICO2VIBE_JUCE_DIR=C:/path/to/JUCE
cmake --build build/vst -j
```

If JUCE is installed as a CMake package, omit `PICO2VIBE_JUCE_DIR`. The first wrapper exposes the shared DSP parameters, voicing, quality mode, factory presets, smoothed bypass and output metering for host-readiness testing.

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
