# M3 nonlinear architecture before modification

Frozen source: `1e010562bf1f23cd5887fdb6ec29691b37b5dcf2` (M2.1 merge,
also remote main when checked on 2026-10-07). No optical retuning is authorized.
The fresh full-effect capture uses 44.1 kHz, seed 1, High, all twelve factory
presets with M0/M1 gains, ReferenceOptical and SingleLampReference. A separate
LegacyOptical/Studio capture preserves the historical optical comparison.
See `regression/m3/baseline-configuration.txt`, the raw baseline metric CSVs and
`baseline-wave-hashes.csv`. Time-varying effect THD is a regression measurement;
the static analyzer is used for identifiable harmonic/alias measurements.

## Audio path, independently for left and right

```
input -> pre HPF -> add shaped previous feedback -> adaptive input BJT-like curve
      -> four grey-box phase cells -> output BJT-like curve -> makeup/clamp
      -> tone tilt -> wet smoothing -> Studio stereo/mono/wet compensation
      -> dry/wet mix + optional colored noise -> auto level/output gain/panning
      -> wrapper output conditioner -> PCM conversion on MCU
```

Feedback branches after the phase cells, before the output curve:

```
phase output -> feedback gain and raw clamp -> rational limiter
             -> clamp -> envelope gain reduction -> bounded feedback state
             -> feedback HP/LP/profile shaping -> next sample's input sum
```

The old saturation comparison engine is independent of OpticalMode: it uses
`fast_soft_clip` at the input and after each of four historical phase networks,
with emitter feedback and intermediate clamps. Selecting LegacyOptical alone
does **not** select that old saturation engine.

## Nonlinear inventory

| Operation / helper | Role/classification | Behavior before M3 |
|---|---|---|
| `bjt_shape_core`, `bjt_shape` | Circuit-inspired, musical/Studio; Legacy compatibility branch | Current: biased rational curve with zero-input bias subtraction, 0.84 input scaling, BJT gain trim and Sat Out Trim. Legacy: `x/(1+abs(x))`, drive and BJT trim only. Neither is a circuit-derived transistor equation. |
| `bjt_shape_oversampled` | Anti-alias approximation | High/nonlegacy only: current, midpoint and previous evaluations weighted 1/4, 1/2, 1/4, then one-pole coefficient 0.72. Initial sample bypasses history. Eco/Standard and Legacy saturation use direct evaluation. |
| `soft_clip_cubic` | Musical/Studio and safety intent | `x*(27+x*x)/(27+9*x*x)`. Rational, not a cubic polynomial; unbounded for large input. Used for feedback, profile grit and output conditioning. |
| `soft_clip_cubic_oversampled` | Anti-alias approximation | Same midpoint weights/one-pole as above. No interpolation reconstruction or decimation low-pass exists. |
| Input envelope, adaptive drive | Musical/Studio, circuit-inspired intent | Absolute value, attack/release branching. Base `clamp(.80+.34*InputDrive,.80,2.20)` plus 1.05 envelope, .92 feedback and .36 depth, bounded .75..3.10. Stateful, not an ADAA target. |
| Output drive and wet makeup | Musical/Studio | Output drive `clamp(.82+.18*dynamic_drive,.85,1.35)`; reciprocal BJT/trim makeup clamped 1..3.6. Final wet signal clamp +/-1.35. |
| Feedback drive | Musical/Studio | Lamp heat and feedback raise drive; high-feedback clarity reduces it. Bounded .75..2.80. |
| Feedback clamps/gain reduction | Safety | Raw +/-1.20, post-curve +/-1.15, envelope-controlled reduction with threshold/floor, final feedback +/-0.95. These are essential shipping safeguards. |
| `feedback_profile_process` | Musical/Studio, safety | HP/LP paths, Classic rational mid-grit (`f(1.6*mid)*.08`), final +/-1.25 clamp. Profile filters are stateful; isolated grit curve is memoryless. |
| Historical stage clamps | Safety, Legacy compatibility | `stage_state_limit` bounded 2..12; intermediate collector values clamped. Current grey-box audio recursion is linear; a bounded copy of its final output is stored for diagnostics, not fed into the current recursion. |
| `zap_denormal` | Numerical safety | Values smaller than 1e-20 replaced with zero in histories. |
| Dry/wet gain mapping | Musical/Studio | Square-root mix law, notch-dependent gains, power normalization and bounded focus. Control nonlinearity, not audio saturation. |
| `studio_output_gain` | Musical/Studio | Drive/feedback/mix-dependent reciprocal auto trim, smoothed multiplicatively. Can conceal level changes downstream. |
| `studio_process_wet` | Musical/Studio | Wet absolute-value envelopes, bounded reciprocal-power compensation (+/-1.5 dB), mono/stereo guards, depth/mix-dependent scaling. Stateful gain modulation; not a memoryless ADAA target. |
| `VibeOutputConditioner` | Safety/Studio | Optional 8 Hz DC blocker, linked stereo absolute peak envelope and adaptive headroom around .92, followed by `f(1.25*x)/1.25`. The rational curve alone does not enforce an output ceiling. |
| Firmware `float_to_pcm24` | Safety and conversion | Final hard bounds and quantization to signed 24-bit; optional TPDF dither. Dither is separate from nonlinear fidelity. Desktop metric WAV export also bounds stored float samples; metrics are computed before export. |
| Parameter/coefficient bounds | Numerical safety | Sanitized control ranges, finite guards, corner/pole bounds. These are not waveshapers. |
| Lamp/LFO `tanhf`, powers and clamps | Circuit-inspired optics or Studio controls | Outside the nonlinear audio-path experiment. Legacy lamp hysteresis uses tanh; LFO shaping and optical mappings remain frozen. |

## Interpretation

The current optical reference is not a transistor reference. The two current
BJT-like audio curves and the adaptive envelope are engineering approximations.
The existing High path is **lightweight midpoint antialiasing / legacy High
nonlinear smoothing**, not conventional 2x oversampling. Its low-frequency group
delay is approximately 0.5 + 0.28/0.72 = 0.889 samples per wrapper. Feedback carries
this memory, so exchanging wrappers changes loop dynamics even if DC transfers
match. The separate wet low-pass also varies with Quality.

DAFx-19 describes a Darlington-buffered phase splitter and uses biased/scaled
tanh for measured asymmetric stage clipping, with heuristic tuning. It does not
provide calibrated nonlinear transistor parameters for this project. The M3
report distinguishes that literature-based candidate from physical validation.

Source: [Darabundit, Wedelich and Bischoff, DAFx-19](https://www.dafx.de/paper-archive/2019/DAFx2019_paper_31.pdf).
