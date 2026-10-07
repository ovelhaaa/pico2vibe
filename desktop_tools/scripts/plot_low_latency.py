"""Create static exportable measurement figures and harmonic delta tables."""
from pathlib import Path
import csv, sys
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
root=Path(sys.argv[1]);sines=list(csv.DictReader((root/'sines.csv').open()));responses=list(csv.DictReader((root/'response-spectrum.csv').open()));multi=list(csv.DictReader((root/'multitone.csv').open()))
algorithms=['direct','midpoint','adaa1','fir4','allpass2','allpass4','allpass2_adaa','elliptic2']
fig, axes=plt.subplots(2,2,figsize=(12,8))
for algorithm in algorithms:
 rows=[r for r in responses if r['algorithm']==algorithm]
 axes[0,0].plot([float(r['frequency_over_sample_rate'])*44100/1000 for r in rows],[float(r['gain_db']) for r in rows],label=algorithm)
 rows=[r for r in sines if r['algorithm']==algorithm and r['stage']=='bjt' and r['sample_rate']=='44100' and r['drive']=='3.2' and r['input_dbfs']=='0']
 axes[0,1].plot([float(r['requested_frequency'])/1000 for r in rows],[float(r['alias_dbfs']) for r in rows],label=algorithm)
 rows=[r for r in responses if r['algorithm']==algorithm]
 f=np.array([float(r['frequency_over_sample_rate']) for r in rows]);p=np.array([float(r['phase_unwrapped_rad']) for r in rows]);axes[1,0].plot(f*44100/1000,-np.gradient(p,f)/(2*np.pi),label=algorithm)
rows=[r for r in multi if r['stage']=='bjt' and r['sample_rate']=='44100' and r['algorithm'] in algorithms]
x=np.arange(len(rows));axes[1,1].bar(x-.2,[float(r['imd_dbfs']) for r in rows],width=.4,label='legitimate IMD');axes[1,1].bar(x+.2,[float(r['alias_dbfs']) for r in rows],width=.4,label='classified alias');axes[1,1].set_xticks(x,[r['algorithm'] for r in rows],rotation=40,ha='right')
axes[0,0].set(xlim=(0,20),ylim=(-2,.1),xlabel='Frequency (kHz), host 44.1 kHz',ylabel='Linear wrapper gain (dB)')
axes[0,1].set(xlabel='Sine frequency (kHz)',ylabel='Folded harmonic alias (dBFS)')
axes[1,0].set(xlim=(0,18),ylim=(0,12),xlabel='Frequency (kHz), host 44.1 kHz',ylabel='Wrapper group delay (host samples)')
axes[1,1].set(ylabel='Multitone energy (dBFS)')
for ax in axes.flat:ax.grid(alpha=.2);ax.legend(fontsize=7)
axes[1,0].text(.97,.93,'FIR4: 64 samples (off scale)',ha='right',transform=axes[1,0].transAxes,fontsize=8)
fig.tight_layout();fig.savefig(root/'low-latency-measurements.png',dpi=160)
keys=lambda r:tuple(r[k] for k in ['stage','sample_rate','requested_frequency','input_dbfs','drive'])
direct={keys(r):r for r in sines if r['algorithm']=='direct'}
with (root/'harmonic-deltas.csv').open('w',newline='') as file:
 fields=['sample_rate','drive','input_dbfs','frequency','algorithm','H1_delta_db','H2_delta_db','H3_delta_db','H4_delta_db','H5_delta_db','THD_delta_db'];w=csv.writer(file);w.writerow(fields)
 for r in sines:
  if r['stage']!='bjt' or r['algorithm']=='direct':continue
  d=direct[keys(r)];w.writerow([r['sample_rate'],r['drive'],r['input_dbfs'],r['requested_frequency'],r['algorithm']]+[float(r[k])-float(d[k]) for k in ['H1_db','H2_db','H3_db','H4_db','H5_db','THD_db']])
