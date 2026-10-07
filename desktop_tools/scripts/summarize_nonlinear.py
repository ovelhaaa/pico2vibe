"""Create compact measured tables for docs/m3-nonlinear-fidelity.md."""
import csv
from pathlib import Path
import sys


def read(path):
    with path.open(newline="") as f:
        return list(csv.DictReader(f))


def table(headers, rows):
    return "| " + " | ".join(headers) + " |\n|" + "|".join(["---"] * len(headers)) + "|\n" + "".join("| " + " | ".join(row) + " |\n" for row in rows)


def main(root):
    sines = read(root / "sines.csv")
    selected = [r for r in sines if r["stage"] == "bjt" and r["algorithm"] == "direct" and float(r["sample_rate"]) == 44100 and float(r["requested_frequency"]) == 440 and float(r["input_dbfs"]) in (-24, -12, -6, 0)]
    text = "### Harmonic results\n\nBJT probe: asymmetry 0.08, gain trim 0.70, output trim 0.95, coherent frequency near 440 Hz at 44.1 kHz. Harmonics are peak dBFS; THD is dBc. These are representative isolated settings, not complete factory renders.\n\n"
    text += table(["Drive", "Input dBFS", "H1", "H2", "H3", "H4", "H5", "THD"], [[r["drive"], r["input_dbfs"]] + [f"{float(r[k]):.2f}" for k in ("H1_db", "H2_db", "H3_db", "H4_db", "H5_db", "THD_db")] for r in selected])
    text += "\n### High-frequency sine results\n\nBJT probe: 44.1 kHz, drive 3.2, input 0 dBFS. Alias energy is relative to the fundamental; aliases colliding with legitimate harmonics are excluded.\n\n"
    selected = [r for r in sines if r["stage"] == "bjt" and float(r["sample_rate"]) == 44100 and float(r["drive"]) == 3.2 and float(r["input_dbfs"]) == 0 and float(r["requested_frequency"]) in (7000, 15000) and r["algorithm"] != "tanh_reference"]
    text += table(["Requested Hz", "Algorithm", "H1 dBFS", "Alias dBc", "Worst alias RMS dBFS"], [[r["requested_frequency"], r["algorithm"]] + [f"{float(r[k]):.2f}" for k in ("H1_db", "alias_dbc", "worst_alias_dbfs")] for r in selected])
    text += "\n### Multitone results\n\nBJT probe: 44.1 kHz, drive 3.2, three upper-band coherent tones, composite peak bounded by 0.8. Values are RMS dBFS; no collision bins at this rate.\n\n"
    selected = [r for r in read(root / "multitone.csv") if r["stage"] == "bjt" and float(r["sample_rate"]) == 44100]
    text += table(["Algorithm", "In-band IMD", "Classified alias", "Unclassified residual", "16x magnitude residual"], [[r["algorithm"]] + [f"{float(r[k]):.2f}" for k in ("imd_dbfs", "alias_dbfs", "residual_dbfs", "reference_magnitude_residual_dbfs")] for r in selected])
    text += "\n### Candidate cost and low-frequency null\n\nDesktop single-stage timing includes candidate dispatch; it is not RP2350 timing. Fixed analysis objects reserve FIR storage for every mode. Active payload storage is listed separately (ADAA/midpoint counts exclude padding); FIR storage includes coefficients.\n\n"
    nulls = {r["algorithm"]: r for r in read(root / "null.csv")}
    text += table(["Algorithm", "ns/sample", "Active bytes", "Reserved object bytes", "Delay samples", "79.4 Hz null dBc"], [[r["algorithm"], f"{float(r['ns_per_sample']):.1f}", r["state_bytes"], r["total_object_bytes"], r["latency_samples"], f"{float(nulls[r['algorithm']]['residual_dbc']):.2f}"] for r in read(root / "cpu.csv")])
    (root / "measured-tables.md").write_text(text, encoding="utf-8")
    print(text)


if __name__ == "__main__":
    main(Path(sys.argv[1]))
