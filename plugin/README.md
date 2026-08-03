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

## GitHub Actions

The `Build VST3` workflow builds the Windows x64 Release bundle on pushes to
`main`, pull requests that affect the plugin or shared DSP core, and manual
runs. Download the `pico2vibe-vst3-windows-x64` artifact from the workflow run.

When Tempo Sync is enabled, the plugin reads BPM from the host transport without
writing over the automatable BPM parameter. The BPM control remains the fallback
for hosts that do not expose tempo, while Tempo Division selects beats per LFO
cycle. Rate remains the active control when sync is disabled.

Transport Phase Lock additionally aligns the LFO cycle to host PPQ while the
transport is playing. Disable it for a pedal-style free-running phase at the
same tempo. Hosts without valid BPM or PPQ automatically keep free-running.

With `BUILD_TESTING=ON`, CTest runs a JUCE smoke test with a simulated host
transport. It covers BPM and PPQ reads, loop/seek wrapping, stopped and missing
transport fallbacks, state restoration, mono/stereo processing and finite output.
