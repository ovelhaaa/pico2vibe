# M2 optical architecture (before refactoring)

Baseline: main 780399294a52554771196f2196179c4d187f39cd. Remote main verified equal.
Captured all 12 factory presets using seed 1, High, 44.1 kHz and existing
`dsp_validate` defaults in `build/m2/before`, before editing the core.

EffectLFO advances tempo/free-running phase with seeded filtered random plus
sinusoidal creative Drift. Right phase includes voicing/profile/width offset.
Shapes are sine, smooth triangle, or BulbAsym (smoothstep rise ending at phase
0.42 and longer fall). A voicing-dependent low-pass smooths the shaped oscillator.
Depth currently scales this unipolar result before adding SweepMin; SweepMax
sets its span. Thus Depth also changes the operating point.

The target passes a direction-dependent memory accumulator, whose coefficients
are per sample rather than seconds (hysteresis/profile heuristic). A tanh centered
at 0.5 compresses it. The second state uses heating/cooling coefficients computed
from seconds, scaled by LampLag and fixed seeded channel tolerances. Coefficients
have a 0.0001 floor, which weakens sample-rate equivalence at high rates.
Brightness is state^1.5. A 256-entry exponential resistance LUT approximates
Rdark*exp(-7.6009*brightness*curve_scale), with linear interpolation and tuning
bounds. A 1.5 ms resistance slew precedes coefficient updates. Four stage means
use Table 1 scales {405/290,233/290,1,240/290}, reduced for Modern profiles,
with subtle seeded tolerance; minimums also include the existing 470 pF guard.

Circuit inspired: incandescent thermal inertia, asymmetric response, inverse
light/resistance relationship, four dissimilar photocells, Table 1 relative means.
Empirical: attack/release, LDR exponential curve, optical bounds, preset tuning.
Heuristics: imposed 42/58 phase asymmetry, memory coefficient/hysteresis formula,
tanh compression, coefficient floors and stacked smoothing; useful character is
preserved as LegacyOptical rather than judged incorrect without measurements.
Studio: Drift, second lamp lane/stereo phase, profile mixing of measured scales,
non-circuit sweep range and creative shape choice. Tone/feedback/output remain
outside this milestone. Quality only sets existing coefficient/audio processing
cadence; it must not select different optical algorithms.
