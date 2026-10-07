"""Compare optical-only A/B renders without assuming identical detected notches.

Usage: python compare_optical.py LEGACY REFERENCE OUTPUT_DIR
Render Reference with --factory-levels baseline for an optical-only comparison.
"""
import csv
import math
from pathlib import Path
import sys
from compare_regression import wav_samples, normalize_hf


def read(path):
    with path.open(newline="", encoding="utf-8") as f:
        return normalize_hf(list(csv.DictReader(f)))


def write(path, columns, rows):
    with path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(columns)
        writer.writerows(rows)


def compare(legacy, reference, output):
    output.mkdir(parents=True, exist_ok=True)
    metrics, audio, tracks, frequency, thd = [], [], [], [], []
    for preset in sorted(legacy.glob("factory_*")):
        candidate = reference / preset.name
        a = {r["metric"]: float(r["value"]) for r in read(preset / "metrics/summary.csv")}
        b = {r["metric"]: float(r["value"]) for r in read(candidate / "metrics/summary.csv")}
        if a.keys() != b.keys():
            raise ValueError("Summary keys differ")
        metrics.extend([preset.name, key, a[key], b[key], b[key]-a[key]] for key in a)
        for mode, root in (("legacy", preset), ("reference", candidate)):
            for row in read(root / "metrics/notch_tracking.csv"):
                tracks.append([preset.name, mode, row["time_s"], row["freq_hz"], row["notch_depth_db"]])
            for row in read(root / "metrics/frequency_response.csv"):
                frequency.append([preset.name, mode, row["time_s"], row["freq_hz"], row["gain_db"]])
            for row in read(root / "metrics/thd_vs_drive.csv"):
                thd.append([preset.name, mode, row["sine_level_db"], row["thd_ratio"], row["thd_db"], row["broadband_hf_db"]])
        for wav in sorted((preset / "signals").glob("*_out.wav")):
            a, ah = wav_samples(wav)
            b, bh = wav_samples(candidate / "signals" / wav.name)
            if len(a) != len(b):
                raise ValueError("WAV lengths differ")
            ra = math.sqrt(sum(v*v for v in a)/len(a))
            rb = math.sqrt(sum(v*v for v in b)/len(b))
            audio.append([preset.name, wav.name, ra, rb, 20*math.log10(rb/ra),
                          max(map(abs,a)), max(map(abs,b)), max(abs(x-y) for x,y in zip(a,b)), ah, bh])
    if not audio:
        raise ValueError("No WAVs")
    write(output / "metric_comparison.csv", ["preset","metric","legacy","reference","delta"], metrics)
    write(output / "audio_comparison.csv", ["preset","signal","rms_legacy","rms_reference","rms_delta_db","peak_legacy","peak_reference","max_sample_delta","sha256_legacy","sha256_reference"], audio)
    write(output / "notch_tracking.csv", ["preset","mode","time_s","freq_hz","notch_depth_db"], tracks)
    write(output / "frequency_response.csv", ["preset","mode","time_s","freq_hz","gain_db"], frequency)
    write(output / "thd_vs_drive.csv", ["preset","mode","sine_level_db","thd_ratio","thd_db","broadband_hf_db"], thd)
    print(f"Compared {len(audio)} WAVs, {len(metrics)} summary metrics; notch events retained independently")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    compare(*(Path(p) for p in sys.argv[1:]))
