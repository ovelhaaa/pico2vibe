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
