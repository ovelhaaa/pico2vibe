"""Optional matplotlib figure from optical_analyze's averaged CSVs.

Usage: python plot_optical.py OPTICAL_DIR OUTPUT_PNG
"""
import csv
from pathlib import Path
import sys
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

root, output = Path(sys.argv[1]), Path(sys.argv[2])
fig, axes = plt.subplots(2,3,figsize=(12,6),layout="constrained")
colors = ["#1976a3", "#da6f2a", "#33845e", "#854ea2"]
for col, rate in enumerate((0.2,1,7)):
    for j, depth in enumerate((0.15,0.6,1)):
        for mode, style in (("reference","-"),("legacy","--")):
            with (root/f"{mode}_{rate:.6f}_{depth:.6f}_averaged.csv").open() as f:
                rows = list(csv.DictReader(f))
            axes[0,col].plot([float(r["phase"]) for r in rows], [float(r["lamp_brightness"]) for r in rows],
                             style,color=colors[j],label=f"Depth {depth:.2f} {mode}",linewidth=1.5)
    for mode, style in (("reference","-"),("legacy","--")):
        with (root/f"{mode}_{rate:.6f}_0.850000_averaged.csv").open() as f:
            rows = list(csv.DictReader(f))
        for c in range(4):
            axes[1,col].plot([float(r["phase"]) for r in rows],[float(r[f"r{c+1}_ohms"])/1000 for r in rows],
                             style,color=colors[c],label=f"Cell {c+1} {mode}",linewidth=1.5)
    axes[0,col].set(title=f"{rate:g} Hz",ylim=(0,1),xlabel="Oscillator phase")
    axes[1,col].set(xlabel="Oscillator phase",yscale="log")
    for row in range(2):
        axes[row,col].grid(alpha=0.2)
        axes[row,col].set_xlim(0,1)
axes[0,0].set_ylabel("Lamp brightness")
axes[1,0].set_ylabel("LDR resistance (kΩ), Depth 0.85")
axes[0,2].legend(fontsize=7,loc="lower right")
axes[1,2].legend(fontsize=7,loc="upper right",ncol=2)
fig.suptitle("Pico2Vibe simulated optical trajectories • solid Reference / dashed Legacy",fontsize=13)
fig.savefig(output,dpi=160)
print(output)
