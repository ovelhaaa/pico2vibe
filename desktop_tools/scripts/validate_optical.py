"""Behavioral regression targets for optical_analyze's full Speed x Intensity grid."""
import csv
import math
import sys

with open(sys.argv[1], newline="", encoding="utf-8") as f:
    rows = list(csv.DictReader(f))
assert len(rows) == 300, "Expected 60 points x lamp/four cells"
index = {}
for row in rows:
    for key, val in row.items():
        if key != "mode":
            assert math.isfinite(float(val)), f"Non-finite {key}"
    key = (row["mode"], round(float(row["speed_hz"]),2), round(float(row["intensity"]),2), int(row["cell"]))
    assert key not in index
    index[key] = {k:float(v) for k,v in row.items() if k != "mode"}
    assert float(row["cycle_mean_relative_spread"]) < 0.01, "Cycle stability exceeds 1%"
    if int(row["cell"]) >= 0:
        assert 3900 <= float(row["min"]) <= float(row["max"]) <= 1e6
        assert float(row["min"]) <= float(row["geometric_mean"]) <= float(row["max"])
    else:
        assert 0 <= float(row["min"]) < float(row["max"]) <= 1
for rate in (0.2,0.5,1,2,4,7):
    previous = 0
    for depth in (0.15,0.35,0.6,0.85,1):
        lamp = index[("reference",rate,depth,-1)]
        excursion = lamp["max"]-lamp["min"]
        assert excursion > previous, "Intensity response failed"
        previous = excursion
        cells = [index[("reference",rate,depth,c)]["mean"] for c in range(4)]
        assert len(set(cells)) == 4, "Cells collapsed"
        assert 0 < lamp["lag_cycles"] < 0.4, "Lamp phase lag failed"
for depth in (0.15,0.35,0.6,0.85,1):
    slow = index[("reference",0.2,depth,-1)]
    fast = index[("reference",7,depth,-1)]
    assert fast["max"]-fast["min"] < 0.8*(slow["max"]-slow["min"]), "Speed inertia failed"
print("60 optical operating points passed: bounds, phase lag, cycle stability, intensity, speed inertia, individual cells")
