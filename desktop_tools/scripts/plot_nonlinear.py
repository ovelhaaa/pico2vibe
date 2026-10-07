"""Plot the measured M3 transfers, harmonics, alias energy and fundamental gain.

Usage: python plot_nonlinear.py docs/regression/m3
Requires numpy and matplotlib only for plotting; the analyzer has no dependencies.
"""
import csv
from pathlib import Path
import sys
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt


def read(path):
    with path.open(newline="") as f:
        return list(csv.DictReader(f))


def main(root):
    curves = read(root / "transfer.csv")
    sines = read(root / "sines.csv")
    fig, axes = plt.subplots(2, 2, figsize=(12, 8), constrained_layout=True)
    for drive in (.8, 1.5, 3.2):
        rows = [r for r in curves if float(r["drive"]) == drive and float(r["asymmetry"]) == .08]
        axes[0, 0].plot([float(r["input"]) for r in rows], [float(r["output"]) for r in rows], label=f"drive {drive}")
    axes[0, 0].set(xlabel="Input", ylabel="Output", title="Rational BJT-like transfer, asymmetry 0.08")
    for h in (2, 3, 4, 5):
        rows = [r for r in sines if r["stage"] == "bjt" and r["algorithm"] == "direct" and float(r["sample_rate"]) == 44100 and float(r["drive"]) == 3.2 and float(r["requested_frequency"]) == 440]
        axes[0, 1].plot([float(r["input_dbfs"]) for r in rows], [float(r[f"H{h}_db"]) - float(r["H1_db"]) for r in rows], marker=".", label=f"H{h}")
    axes[0, 1].set(xlabel="Input dBFS", ylabel="Harmonic dBc", ylim=(-150, 0), title="Individual harmonics, 440 Hz, drive 3.2")
    for algorithm in ("direct", "midpoint", "adaa1", "fir2", "fir4"):
        rows = [r for r in sines if r["stage"] == "bjt" and r["algorithm"] == algorithm and float(r["sample_rate"]) == 44100 and float(r["drive"]) == 3.2 and float(r["input_dbfs"]) == 0]
        frequency = [float(r["frequency"]) for r in rows]
        axes[1, 0].plot(frequency, [max(-160, float(r["alias_dbc"])) for r in rows], marker=".", label=algorithm)
        axes[1, 1].plot(frequency, [float(r["H1_db"]) for r in rows], marker=".", label=algorithm)
    axes[1, 0].set(xlabel="Frequency Hz", ylabel="Classified alias dBc", title="Alias-bin energy (collisions excluded)")
    axes[1, 1].set(xlabel="Frequency Hz", ylabel="Fundamental peak dBFS", title="Fundamental gain reveals filtering tradeoffs")
    for ax in axes.flat:
        ax.grid(alpha=.25)
        ax.legend(fontsize=8)
    fig.savefig(root / "nonlinear-characterization.png", dpi=160)
    plt.close(fig)


if __name__ == "__main__":
    main(Path(sys.argv[1]))
