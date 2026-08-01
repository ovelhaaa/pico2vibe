# pico2vibe JUCE/VST3 wrapper

This folder contains the first host-plugin wrapper around the shared DSP core in `src/dsp/vibe_core.hpp`.

The wrapper is intentionally optional so firmware, desktop tools, and WASM builds do not require JUCE.

```powershell
cmake -S . -B build/vst -G Ninja ^
  -DPICO2VIBE_BUILD_FIRMWARE=OFF ^
  -DPICO2VIBE_BUILD_JUCE_PLUGIN=ON ^
  -DPICO2VIBE_JUCE_DIR=C:/path/to/JUCE
cmake --build build/vst -j
```

If JUCE is installed as a CMake package, omit `PICO2VIBE_JUCE_DIR`.