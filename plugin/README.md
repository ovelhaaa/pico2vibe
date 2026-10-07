# pico2vibe JUCE/VST3 wrapper

This folder contains the 0.9.0 pre-release host-plugin wrapper around the shared DSP core in `src/dsp/vibe_core.hpp`.

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
The job also validates the bundle headlessly with pluginval 1.0.4 at strictness
level 8 and publishes the validation log as `pico2vibe-pluginval-windows-x64`.

When Tempo Sync is enabled, the plugin reads BPM from the host transport without
writing over the automatable BPM parameter. The BPM control remains the fallback
for hosts that do not expose tempo, while Tempo Division selects beats per LFO
cycle. Rate remains the active control when sync is disabled.

Transport Phase Lock additionally aligns the LFO cycle to host PPQ while the
transport is playing. Disable it for a pedal-style free-running phase at the
same tempo. Hosts without valid BPM or PPQ automatically keep free-running.

With `BUILD_TESTING=ON`, CTest runs a JUCE smoke test with a simulated host
transport. It covers BPM and PPQ reads, loop/seek wrapping, stopped and missing
transport fallbacks, versioned and legacy state restoration, corrupt-state
rejection, mono/stereo processing and finite output. Missing parameters in older
sessions are initialized from their declared defaults instead of transient values.

Editing a sound parameter selects the host-visible `Custom` program. Factory
program changes reset every sound parameter deterministically, and the editor's
preset selector follows program changes made by the DAW.

The A/B buttons keep two independent sound snapshots for fast comparison. Both
slots and the active selection are stored in the project state, while bypass and Quality are
treated as global controls and remains unchanged when switching sides.

## Editor

The native JUCE controls follow `design/current-interface.svg`, at 840 x 510
logical pixels. Resize up to 1680 x 1020 using the bottom-right grip or the host;
the layout scales proportionally. Tab navigates the controls, arrow keys adjust
knobs, and their value fields accept typed input. Double-click a knob to restore
its parameter default. Tooltips explain tempo dependencies, and controls expose
names and values to accessibility clients, including output peak descriptions.

All eleven knobs, voicing, quality, sync, phase lock and bypass use APVTS
attachments. Presets and A/B retain the processor's existing state operations.
Disabled controls and their labels use the mockup's 38% opacity. Host BPM never
overwrites the fallback parameter, and bypass leaves sound controls editable.

The smoke test also exercises editor attachments in both directions, keyboard
editing, preset/A/B recall, tempo-dependent enablement, reopening and resizing.
Pass an absolute output directory to `Pico2VibePluginTests` to render PNG previews
of the default, synced, bypass and enlarged editor states for visual inspection.

The factory bank keeps the original six program indices and adds six production
voices. Classic Uni-Vibe, Shin-ei Dark and Deja Lead cover the vintage/lead range;
Voodoo Wide and Modern Hi-Fi provide controlled stereo options; Classic Vibrato,
Hendrix Deep and Psychedelic Slow emphasize pitch and deep optical movement;
Gentle Clean, Rotary Fast, Bass Anchor and Lamp Drift cover utility and texture.
Each program defines the complete sound state, including sweep range, lamp lag,
drift, saturation trim, pre-HPF and output gain.

Factory recall preserves bypass and Quality; project restore restores both.
Quality edits do not select Custom. Factory sound values are shared with desktop
analysis in `src/dsp/factory_presets.hpp`; plugin IDs and program order are stable.
