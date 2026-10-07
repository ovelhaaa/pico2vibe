"""Compare seeded dsp_validate outputs and retain compact numerical evidence.

Usage: python compare_regression.py BEFORE AFTER OUTPUT_DIR
WAVs are the harness's float32 exports (its writer bounds samples to +/-1).
Summary metrics are computed by the harness before export.
"""
import array
import csv
import hashlib
import math
from pathlib import Path
import struct
import sys


def wav_samples(path):
    raw = path.read_bytes()
    if raw[:4] != b"RIFF" or raw[8:12] != b"WAVE":
        raise ValueError(f"Not RIFF WAVE: {path}")
    offset = 12
    fmt = None
    data = None
    while offset + 8 <= len(raw):
        tag, size = struct.unpack_from("<4sI", raw, offset)
        payload = raw[offset + 8:offset + 8 + size]
        if tag == b"fmt ":
            fmt = struct.unpack_from("<HHIIHH", payload)
        if tag == b"data":
            data = payload
        offset += 8 + size + (size & 1)
    if fmt is None or fmt[0] != 3 or fmt[1] != 2 or fmt[5] != 32 or data is None:
        raise ValueError(f"Expected stereo float32 WAV: {path}")
    samples = array.array("f")
    samples.frombytes(data)
    if sys.byteorder != "little":
        samples.byteswap()
    if not samples or not all(math.isfinite(v) for v in samples):
        raise ValueError(f"Empty/non-finite WAV: {path}")
    return samples, hashlib.sha256(raw).hexdigest()


def compare(before, after, output):
    output.mkdir(parents=True, exist_ok=True)
    rows = []
    wave_rows = []
    changed_metrics = 0
    for preset in sorted(before.glob("factory_*")):
        candidate = after / preset.name
        for name in ("summary.csv", "frequency_response.csv", "notch_tracking.csv", "thd_vs_drive.csv"):
            with (preset / "metrics" / name).open(newline="") as f:
                old = list(csv.DictReader(f))
            with (candidate / "metrics" / name).open(newline="") as f:
                new = list(csv.DictReader(f))
            if len(old) != len(new):
                raise ValueError(f"Row count changed: {preset.name}/{name}")
            for index, (a, b) in enumerate(zip(old, new)):
                if a.keys() != b.keys():
                    raise ValueError(f"Columns changed: {preset.name}/{name}")
                for key in a:
                    if key == "metric":
                        if a[key] != b[key]:
                            raise ValueError("Metric order changed")
                        continue
                    av, bv = float(a[key]), float(b[key])
                    if not math.isfinite(av) or not math.isfinite(bv):
                        raise ValueError("Non-finite metric")
                    delta = bv - av
                    changed_metrics += delta != 0
                    rows.append([preset.name, name, a.get("metric", str(index)), key, av, bv, delta])
        for wav in sorted((preset / "signals").glob("*_out.wav")):
            a, ah = wav_samples(wav)
            b, bh = wav_samples(candidate / "signals" / wav.name)
            if len(a) != len(b):
                raise ValueError(f"Sample count changed: {wav}")
            rms_a = math.sqrt(sum(v * v for v in a) / len(a))
            rms_b = math.sqrt(sum(v * v for v in b) / len(b))
            peak_a, peak_b = max(map(abs, a)), max(map(abs, b))
            wave_rows.append([preset.name, wav.name, rms_a, rms_b, peak_a, peak_b,
                              max(abs(x - y) for x, y in zip(a, b)), ah, bh])
    if not wave_rows:
        raise ValueError("No factory output WAVs found")
    for name, headers, values in (
        ("metric_comparison.csv", ["preset", "file", "row", "column", "before", "after", "delta"], rows),
        ("waveform_comparison.csv", ["preset", "signal", "rms_before", "rms_after", "peak_before", "peak_after",
                                     "max_sample_delta", "sha256_before", "sha256_after"], wave_rows),
    ):
        with (output / name).open("w", newline="", encoding="utf-8") as f:
            writer = csv.writer(f)
            writer.writerow(headers)
            writer.writerows(values)
    changed_waves = sum(r[-2] != r[-1] for r in wave_rows)
    print(f"{len(rows)} metric values, {len(wave_rows)} output WAVs; "
          f"{changed_metrics} changed metric values, {changed_waves} changed WAVs")
    return changed_metrics != 0 or changed_waves != 0


if __name__ == "__main__":
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    raise SystemExit(compare(*(Path(p) for p in sys.argv[1:])))
