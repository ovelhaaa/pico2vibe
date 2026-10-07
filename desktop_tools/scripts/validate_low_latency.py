"""Validate measured AA properties without treating shipping selection as a filter test."""
import csv, sys, math
from pathlib import Path
root = Path(sys.argv[1])
response = list(csv.DictReader((root / 'all-wrapper-response.csv').open()))
for row in response:
    if row['algorithm'] not in ('allpass2', 'allpass4'): continue
    frequency = float(row['frequency'])
    target = .1 if frequency <= 5000 else .25 if frequency <= 10000 else .5
    assert abs(float(row['gain_db'])) <= target, row
    assert 0 < float(row['group_delay_samples']) < 20, row
sines = list(csv.DictReader((root / 'sines.csv').open()))
case = {row['algorithm']: row for row in sines if row['stage']=='bjt' and row['sample_rate']=='44100' and row['drive']=='3.2' and row['input_dbfs']=='0' and row['requested_frequency']=='7000'}
assert float(case['direct']['alias_dbfs'])-float(case['allpass2']['alias_dbfs']) >= 15
states = list(csv.DictReader((root / 'state-tests.csv').open()))
assert len(states)==32
assert all(float(row['partition_max_error']) < 2e-6 and row['repeat_exact']=='1' for row in states)
full = list(csv.DictReader((root/'full-effect.csv').open()))
assert len(full)==3840, len(full)
assert all(math.isfinite(float(row['rms'])) and float(row['peak']) < 16 for row in full)
longrun = list(csv.DictReader((root/'long-stress.csv').open()))
assert len(longrun)==64 and all(float(row['duration_seconds']) > 11 for row in longrun)
print('Passband, group delay, 7 kHz alias target, 3840 full-effect captures, 64 long stress runs, and 32 deterministic automation/reset combinations pass.')
