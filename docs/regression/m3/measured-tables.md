### Harmonic results

BJT probe: asymmetry 0.08, gain trim 0.70, output trim 0.95, coherent frequency near 440 Hz at 44.1 kHz. Harmonics are peak dBFS; THD is dBc. These are representative isolated settings, not complete factory renders.

| Drive | Input dBFS | H1 | H2 | H3 | H4 | H5 | THD |
|---|---|---|---|---|---|---|---|
| 1.5 | -24 | -25.66 | -73.16 | -92.57 | -134.45 | -158.50 | -47.45 |
| 1.5 | -12 | -13.83 | -49.60 | -56.89 | -87.00 | -99.06 | -35.02 |
| 1.5 | -6 | -8.35 | -38.91 | -39.85 | -64.65 | -70.38 | -27.99 |
| 1.5 | 0 | -4.04 | -31.26 | -25.12 | -46.19 | -44.92 | -20.07 |
| 3.2 | -24 | -19.50 | -54.00 | -74.43 | -102.70 | -129.35 | -34.46 |
| 3.2 | -12 | -8.16 | -31.79 | -39.55 | -56.89 | -70.67 | -22.94 |
| 3.2 | -6 | -3.90 | -24.43 | -24.73 | -38.72 | -44.68 | -17.56 |
| 3.2 | 0 | -1.95 | -23.54 | -15.29 | -29.20 | -26.39 | -12.26 |

### High-frequency sine results

BJT probe: 44.1 kHz, drive 3.2, input 0 dBFS. Alias energy is relative to the fundamental; aliases colliding with legitimate harmonics are excluded.

| Requested Hz | Algorithm | H1 dBFS | Alias dBc | Worst alias RMS dBFS |
|---|---|---|---|---|
| 7000 | direct | -1.95 | -22.11 | -29.40 |
| 7000 | midpoint | -4.34 | -40.53 | -49.05 |
| 7000 | adaa1 | -2.46 | -34.27 | -40.65 |
| 7000 | fir2 | -1.95 | -51.27 | -57.46 |
| 7000 | fir4 | -1.95 | -121.48 | -129.59 |
| 15000 | direct | -1.95 | -12.26 | -18.30 |
| 15000 | midpoint | -10.19 | -14.12 | -27.60 |
| 15000 | adaa1 | -5.63 | -19.07 | -28.04 |
| 15000 | fir2 | -1.95 | -23.77 | -29.40 |
| 15000 | fir4 | -1.95 | -57.52 | -64.61 |

### Multitone results

BJT probe: 44.1 kHz, drive 3.2, three upper-band coherent tones, composite peak bounded by 0.8. Values are RMS dBFS; no collision bins at this rate.

| Algorithm | In-band IMD | Classified alias | Unclassified residual | 16x magnitude residual |
|---|---|---|---|---|
| direct | -22.75 | -26.24 | -58.83 | -26.24 |
| midpoint | -27.74 | -44.49 | -68.23 | -15.07 |
| adaa1 | -26.21 | -38.99 | -71.60 | -20.34 |
| fir2 | -23.00 | -60.29 | -61.72 | -39.97 |
| fir4 | -23.00 | -106.28 | -64.90 | -40.02 |
| tanh_reference | -22.79 | -26.24 | -56.31 | -26.13 |

### Candidate cost and low-frequency null

Desktop single-stage timing includes candidate dispatch; it is not RP2350 timing. Fixed analysis objects reserve FIR storage for every mode. Active storage is listed separately; FIR storage includes coefficients.

| Algorithm | ns/sample | Active bytes | Reserved object bytes | Delay samples | 79.4 Hz null dBc |
|---|---|---|---|---|---|
| direct | 8.5 | 0 | 4728 | 0 | -300.00 |
| midpoint | 20.7 | 25 | 4728 | 0.888889 | -94.75 |
| adaa1 | 76.1 | 25 | 4728 | 0.5 | -110.03 |
| fir2 | 399.2 | 4648 | 4728 | 64 | -141.39 |
| fir4 | 795.7 | 4648 | 4728 | 64 | -141.96 |
| tanh_reference | 15.5 | 0 | 4728 | 0 | -46.43 |
