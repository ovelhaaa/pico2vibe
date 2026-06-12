import createModule from './vibe_wasm.js';

const TARGET_SAMPLE_RATE = 44100;
const st = { mod: null, h: 0, ctx: null, inBuf: null, outBuf: null, src: null, controls: [] };
const $ = (id) => document.getElementById(id);
const status = (message) => { $('status').textContent = message; };

function getAudioContext() {
  if (!st.ctx) st.ctx = new AudioContext();
  return st.ctx;
}

function labelFromParamName(name) {
  return name
    .split('_')
    .map((part) => part.charAt(0).toUpperCase() + part.slice(1))
    .join(' ');
}

function stepForRange(min, max) {
  return Math.max((max - min) / 400, 0.0001);
}

async function play(buf) {
  const ctx = getAudioContext();
  if (ctx.state !== 'running') await ctx.resume();
  stop();
  const src = ctx.createBufferSource();
  src.buffer = buf;
  src.connect(ctx.destination);
  src.onended = () => {
    if (st.src === src) st.src = null;
  };
  src.start();
  st.src = src;
}

function stop() {
  if (st.src) {
    try { st.src.stop(); } catch (_error) { /* already stopped */ }
    st.src.disconnect();
    st.src = null;
  }
}

function toStereoArrays(buf) {
  const n = buf.length;
  const l = new Float32Array(n);
  const r = new Float32Array(n);
  l.set(buf.getChannelData(0));
  if (buf.numberOfChannels > 1) r.set(buf.getChannelData(1));
  else r.set(l);
  return { l, r };
}

async function resampleToTarget(buf) {
  if (buf.sampleRate === TARGET_SAMPLE_RATE && buf.numberOfChannels === 2) return buf;
  const frameCount = Math.ceil(buf.duration * TARGET_SAMPLE_RATE);
  const off = new OfflineAudioContext(2, frameCount, TARGET_SAMPLE_RATE);
  const src = off.createBufferSource();
  src.buffer = buf;
  src.connect(off.destination);
  src.start();
  return off.startRendering();
}

function syncControlsFromEngine() {
  for (const ctl of st.controls) {
    const v = st.mod._vibe_get_param(st.h, ctl.id);
    ctl.input.value = String(v);
    ctl.txt.textContent = v.toFixed(3);
  }
}

function encodeWav(l, r, sr) {
  const n = l.length;
  const ab = new ArrayBuffer(44 + n * 4);
  const dv = new DataView(ab);
  const w = (o, s) => [...s].forEach((c, i) => dv.setUint8(o + i, c.charCodeAt(0)));
  w(0, 'RIFF');
  dv.setUint32(4, 36 + n * 4, true);
  w(8, 'WAVE');
  w(12, 'fmt ');
  dv.setUint32(16, 16, true);
  dv.setUint16(20, 1, true);
  dv.setUint16(22, 2, true);
  dv.setUint32(24, sr, true);
  dv.setUint32(28, sr * 4, true);
  dv.setUint16(32, 4, true);
  dv.setUint16(34, 16, true);
  w(36, 'data');
  dv.setUint32(40, n * 4, true);
  let o = 44;
  for (let i = 0; i < n; i += 1) {
    dv.setInt16(o, Math.round(Math.max(-1, Math.min(1, l[i])) * 32767), true);
    o += 2;
    dv.setInt16(o, Math.round(Math.max(-1, Math.min(1, r[i])) * 32767), true);
    o += 2;
  }
  return new Blob([ab], { type: 'audio/wav' });
}

async function boot() {
  st.mod = await createModule();
  st.h = st.mod._vibe_create();
  const pCount = st.mod._vibe_get_param_count();
  const params = $('params');
  params.innerHTML = '';
  for (let i = 0; i < pCount; i += 1) {
    const name = st.mod.UTF8ToString(st.mod._vibe_get_param_name(i));
    const min = st.mod._vibe_get_param_min(i);
    const max = st.mod._vibe_get_param_max(i);
    const def = st.mod._vibe_get_param_default(i);
    const wrap = document.createElement('div');
    wrap.className = 'param';
    wrap.innerHTML = `<label>${labelFromParamName(name)}</label><input type="range" min="${min}" max="${max}" step="${stepForRange(min, max)}" value="${def}"><div>${def.toFixed(3)}</div>`;
    const input = wrap.querySelector('input');
    const txt = wrap.querySelector('div');
    input.oninput = () => {
      const v = parseFloat(input.value);
      txt.textContent = v.toFixed(3);
      st.mod._vibe_set_param(st.h, i, v);
    };
    st.controls.push({ id: i, input, txt });
    params.appendChild(wrap);
  }
  const voicing = $('voicing');
  voicing.innerHTML = '';
  for (let i = 0; i < st.mod._vibe_get_voicing_count(); i += 1) {
    const op = document.createElement('option');
    op.value = i;
    op.textContent = st.mod.UTF8ToString(st.mod._vibe_get_voicing_name(i));
    voicing.appendChild(op);
  }
  voicing.onchange = () => {
    st.mod._vibe_set_voicing(st.h, parseInt(voicing.value, 10));
    syncControlsFromEngine();
  };
  syncControlsFromEngine();
  status('Ready');
}

$('file').onchange = async (e) => {
  const f = e.target.files[0];
  if (!f) return;
  try {
    status('Decoding…');
    const arr = await f.arrayBuffer();
    const b = await getAudioContext().decodeAudioData(arr);
    st.inBuf = await resampleToTarget(b);
    st.outBuf = null;
    status(`Loaded ${f.name}`);
  } catch (error) {
    status(`Decode error: ${error.message}`);
  }
};

$('render').onclick = () => {
  if (!st.inBuf || !st.mod || !st.h) return;
  status('Rendering…');
  st.mod._vibe_reset(st.h, 1);
  const { l, r } = toStereoArrays(st.inBuf);
  const n = l.length;
  const bytes = n * Float32Array.BYTES_PER_ELEMENT;
  const ptrs = [];
  try {
    const pl = st.mod._malloc(bytes); ptrs.push(pl);
    const pr = st.mod._malloc(bytes); ptrs.push(pr);
    const ol = st.mod._malloc(bytes); ptrs.push(ol);
    const orr = st.mod._malloc(bytes); ptrs.push(orr);
    st.mod.HEAPF32.set(l, pl >> 2);
    st.mod.HEAPF32.set(r, pr >> 2);
    st.mod._vibe_process_stereo(st.h, pl, pr, ol, orr, n);
    const outL = new Float32Array(st.mod.HEAPF32.buffer, ol, n).slice();
    const outR = new Float32Array(st.mod.HEAPF32.buffer, orr, n).slice();
    const out = getAudioContext().createBuffer(2, n, TARGET_SAMPLE_RATE);
    out.copyToChannel(outL, 0);
    out.copyToChannel(outR, 1);
    st.outBuf = out;
    status('Done');
  } catch (error) {
    status(`Render error: ${error.message}`);
  } finally {
    for (const ptr of ptrs) st.mod._free(ptr);
  }
};

$('playIn').onclick = async () => { if (st.inBuf) await play(st.inBuf); };
$('playOut').onclick = async () => { if (st.outBuf) await play(st.outBuf); };
$('stop').onclick = stop;
$('export').onclick = () => {
  if (!st.outBuf) return;
  const b = encodeWav(st.outBuf.getChannelData(0), st.outBuf.getChannelData(1), TARGET_SAMPLE_RATE);
  const a = document.createElement('a');
  const url = URL.createObjectURL(b);
  a.href = url;
  a.download = 'processed.wav';
  a.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
};

window.addEventListener('pagehide', () => {
  stop();
  if (st.h && st.mod) st.mod._vibe_destroy(st.h);
});

boot().catch((e) => status(`Error: ${e.message}`));
