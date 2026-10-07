"""Validate complete M3 measurements and the candidate's intended improvements.

Usage: python validate_nonlinear.py docs/regression/m3
"""
import csv
import math
from pathlib import Path
import sys


def read(path):
    with path.open(newline="") as f:
        return list(csv.DictReader(f))


def main(root):
    sines = read(root / "sines.csv")
    assert len(sines) == 5832, "incomplete sine matrix"
    assert len(read(root / "transfer.csv")) == 9612, "incomplete transfers"
    assert len(read(root / "multitone.csv")) == 48, "incomplete multitone matrix"
    for r in sines:
        for key in ("H1_db", "THD_db", "DC", "rms", "peak", "alias_dbfs", "alias_dbc", "worst_alias_dbfs", "noise_residual_dbfs"):
            assert math.isfinite(float(r[key])), (key, r)
        for h in range(2, 6):
            value = float(r[f"H{h}_db"])
            assert math.isnan(value) == (h * float(r["frequency"]) >= float(r["sample_rate"]) / 2)
    selected = {r["algorithm"]: r for r in sines if r["stage"] == "bjt" and float(r["sample_rate"]) == 44100 and float(r["requested_frequency"]) == 15000 and float(r["input_dbfs"]) == 0 and float(r["drive"]) == 3.2}
    assert float(selected["fir4"]["alias_dbc"]) < float(selected["direct"]["alias_dbc"]) - 35
    assert abs(float(selected["fir4"]["H1_db"]) - float(selected["direct"]["H1_db"])) < .05
    nulls = {r["algorithm"]: float(r["residual_dbc"]) for r in read(root / "null.csv")}
    assert nulls["fir4"] < -100 and nulls["adaa1"] < -70
    for mode in ("reference", "legacy"):
        rows = read(root / f"{mode}-regression" / "waveform_comparison.csv")
        assert len(rows) == 96
        assert all(float(r["max_sample_delta"]) == 0 and r["sha256_before"] == r["sha256_after"] for r in rows)
    assert all(r["before_sha256"] == r["after_sha256"] for r in read(root / "optical-reproducibility.csv"))
    print("PASS: 5832 sine captures, 9612 transfers, 48 multitone captures; finite measurements, missing-harmonic classification, 4x alias improvement/gain, low-frequency nulls, exact Reference/Legacy/optical regression")


if __name__ == "__main__":
    main(Path(sys.argv[1]))
