import createModule from "./vibe_wasm.js";

const TARGET_SAMPLE_RATE = 44100;
const st = {
  mod: null,
  h: 0,
  ctx: null,
  inBuf: null,
  outBuf: null,
  src: null,
  controls: [],
  paramByName: new Map(),
};
const $ = (id) => document.getElementById(id);
const status = (message) => {
  $("status").textContent = message;
};

const PARAM_COPY = {
  depth: ["Depth", "Amplitude do sweep óptico"],
  feedback: ["Feedback", "Ressonância e mastigada"],
  mix: ["Mix", "Equilíbrio dry/wet"],
  input_drive: ["Input drive", "Saturação antes do vibe"],
  output_gain: ["Output gain", "Compensação final"],
  sweep_min: ["Sweep min", "Extremo grave do LFO"],
  sweep_max: ["Sweep max", "Extremo agudo do LFO"],
  lfo_rate_hz: ["Rate", "Velocidade em Hz"],
  drift_amount: ["Drift amount", "Instabilidade orgânica"],
  drift_rate_hz: ["Drift rate", "Velocidade do drift"],
  pre_hpf_hz: ["Pre HPF", "Limpa graves antes do circuito"],
  tone_tilt: ["Tone tilt", "Inclinação claro/escuro"],
  sat_asymmetry: ["Sat asymmetry", "Assimetria tipo transistor"],
  sat_out_trim: ["Sat trim", "Apara a saturação"],
};

const GROUPS = [
  {
    id: "motion",
    title: "Movimento",
    accent: "#6ee7f9",
    params: ["lfo_rate_hz", "depth", "sweep_min", "sweep_max"],
  },
  {
    id: "blend",
    title: "Blend e ressonância",
    accent: "#a78bfa",
    params: ["mix", "feedback"],
  },
  {
    id: "tone",
    title: "Cor e ganho",
    accent: "#f7c76f",
    params: ["input_drive", "output_gain", "pre_hpf_hz", "tone_tilt"],
  },
  {
    id: "analog",
    title: "Caráter analógico",
    accent: "#fb7185",
    params: ["drift_amount", "drift_rate_hz", "sat_asymmetry", "sat_out_trim"],
  },
];

const VOICE_HINTS = {
  "Classic Chorus":
    "Vintage, mastigado e mais central: ótimo ponto de partida para guitarra limpa.",
  "Classic Vibrato":
    "100% molhado para pitch/phase wobble; use em acordes, órgãos e texturas psicodélicas.",
  "Deep Throb":
    "Pulso escuro e lento com low-mid forte, ideal para leads e riffs sustentados.",
  "Modern Wide":
    "Mais hi-fi, estéreo e brilhante para teclas, pads e produção moderna.",
};

const SCENES = {
  guitar: {
    voicing: "Classic Chorus",
    params: {
      lfo_rate_hz: 1.05,
      depth: 0.82,
      mix: 0.52,
      feedback: 0.34,
      input_drive: 1.25,
      tone_tilt: -0.08,
    },
  },
  lead: {
    voicing: "Deep Throb",
    params: {
      lfo_rate_hz: 0.72,
      depth: 0.92,
      mix: 0.62,
      feedback: 0.48,
      input_drive: 1.55,
      tone_tilt: -0.18,
    },
  },
  keys: {
    voicing: "Modern Wide",
    params: {
      lfo_rate_hz: 0.58,
      depth: 0.7,
      mix: 0.58,
      feedback: 0.22,
      input_drive: 1.05,
      tone_tilt: 0.18,
    },
  },
};

function getAudioContext() {
  if (!st.ctx) st.ctx = new AudioContext();
  return st.ctx;
}

function labelFromParamName(name) {
  if (PARAM_COPY[name]) return PARAM_COPY[name][0];
  return name
    .split("_")
    .map((part) => part.charAt(0).toUpperCase() + part.slice(1))
    .join(" ");
}

function helpFromParamName(name) {
  return PARAM_COPY[name]?.[1] ?? "Controle DSP";
}

function stepForRange(min, max) {
  return Math.max((max - min) / 400, 0.0001);
}

function clamp(value, min, max) {
  return Math.max(min, Math.min(max, value));
}

function formatTime(seconds) {
  if (!Number.isFinite(seconds)) return "—";
  const total = Math.round(seconds);
  const mins = Math.floor(total / 60);
  const secs = (total % 60).toString().padStart(2, "0");
  return `${mins}:${secs}`;
}

function setButtons() {
  $("render").disabled = !st.inBuf || !st.mod || !st.h;
  $("playIn").disabled = !st.inBuf;
  $("playOut").disabled = !st.outBuf;
  $("export").disabled = !st.outBuf;
}

function getWasmHeapF32() {
  if (st.mod?.HEAPF32 instanceof Float32Array) return st.mod.HEAPF32;
  throw new Error(
    "Memória Float32 do WASM indisponível. Recompile com HEAPF32 exportado.",
  );
}

async function play(buf) {
  const ctx = getAudioContext();
  if (ctx.state !== "running") await ctx.resume();
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
    try {
      st.src.stop();
    } catch (_error) {
      /* already stopped */
    }
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
  if (buf.sampleRate === TARGET_SAMPLE_RATE && buf.numberOfChannels === 2)
    return buf;
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

function setParamByName(name, value) {
  const ctl = st.paramByName.get(name);
  if (!ctl) return;
  const v = clamp(value, ctl.min, ctl.max);
  st.mod._vibe_set_param(st.h, ctl.id, v);
}

function selectVoicingByName(name) {
  const voicing = $("voicing");
  for (const option of voicing.options) {
    if (option.textContent === name) {
      voicing.value = option.value;
      st.mod._vibe_set_voicing(st.h, parseInt(option.value, 10));
      break;
    }
  }
  updateVoiceHint();
}

function applyScene(name) {
  const scene = SCENES[name];
  if (!scene || !st.mod || !st.h) return;
  selectVoicingByName(scene.voicing);
  for (const [param, value] of Object.entries(scene.params))
    setParamByName(param, value);
  syncControlsFromEngine();
  st.outBuf = null;
  drawScope();
  setButtons();
  status(`Cena aplicada: ${scene.voicing}. Renderize para ouvir.`);
}

function updateVoiceHint() {
  const selected = $("voicing").selectedOptions[0]?.textContent;
  $("voiceHint").textContent =
    VOICE_HINTS[selected] ?? "Ajuste a personalidade do circuito.";
}

function encodeWav(l, r, sr) {
  const n = l.length;
  const ab = new ArrayBuffer(44 + n * 4);
  const dv = new DataView(ab);
  const w = (o, s) =>
    [...s].forEach((c, i) => dv.setUint8(o + i, c.charCodeAt(0)));
  w(0, "RIFF");
  dv.setUint32(4, 36 + n * 4, true);
  w(8, "WAVE");
  w(12, "fmt ");
  dv.setUint32(16, 16, true);
  dv.setUint16(20, 1, true);
  dv.setUint16(22, 2, true);
  dv.setUint32(24, sr, true);
  dv.setUint32(28, sr * 4, true);
  dv.setUint16(32, 4, true);
  dv.setUint16(34, 16, true);
  w(36, "data");
  dv.setUint32(40, n * 4, true);
  let o = 44;
  for (let i = 0; i < n; i += 1) {
    dv.setInt16(o, Math.round(Math.max(-1, Math.min(1, l[i])) * 32767), true);
    o += 2;
    dv.setInt16(o, Math.round(Math.max(-1, Math.min(1, r[i])) * 32767), true);
    o += 2;
  }
  return new Blob([ab], { type: "audio/wav" });
}

function drawBuffer(ctx, buf, y, height, width, color, label) {
  const w = Math.max(1, Math.floor(width));
  if (!buf._peaks || buf._peaks.length !== w) {
    const channel = buf.getChannelData(0);
    const peaks = new Float32Array(w);
    const step = channel.length / w;
    for (let x = 0; x < w; x += 1) {
      const start = Math.floor(x * step);
      const end = Math.max(start + 1, Math.floor((x + 1) * step));
      let peak = 0;
      for (let i = start; i < end && i < channel.length; i += 1) {
        const val = Math.abs(channel[i]);
        if (val > peak) peak = val;
      }
      peaks[x] = peak;
    }
    buf._peaks = peaks;
  }
  const mid = y + height / 2;
  const amp = height * 0.42;
  ctx.strokeStyle = color;
  ctx.lineWidth = 2;
  ctx.beginPath();
  for (let x = 0; x < w; x += 1) {
    const peak = buf._peaks[x];
    const top = mid - peak * amp;
    const bottom = mid + peak * amp;
    ctx.moveTo(x, top);
    ctx.lineTo(x, bottom);
  }
  ctx.stroke();
  ctx.fillStyle = color;
  ctx.font = "700 13px system-ui";
  ctx.fillText(label, 16, y + 24);
}

function drawScope() {
  const canvas = $("scope");
  const ctx = canvas.getContext("2d");
  const dpr = window.devicePixelRatio || 1;
  const rect = canvas.getBoundingClientRect();
  canvas.width = Math.max(1, Math.round(rect.width * dpr));
  canvas.height = Math.max(1, Math.round(rect.height * dpr));
  ctx.scale(dpr, dpr);
  const w = rect.width;
  const h = rect.height;
  ctx.clearRect(0, 0, w, h);
  ctx.fillStyle = "rgba(255, 255, 255, .06)";
  for (let x = 0; x < w; x += w / 12) ctx.fillRect(x, 0, 1, h);
  ctx.fillStyle = "rgba(255, 255, 255, .08)";
  ctx.fillRect(0, h / 2, w, 1);
  if (!st.inBuf) {
    ctx.fillStyle = "rgba(245, 247, 251, .65)";
    ctx.font = "700 18px system-ui";
    ctx.fillText(
      "A forma de onda aparecerá após carregar um áudio.",
      22,
      h / 2,
    );
    return;
  }
  drawBuffer(ctx, st.inBuf, 0, h / 2, w, "#6ee7f9", "Original");
  if (st.outBuf)
    drawBuffer(ctx, st.outBuf, h / 2, h / 2, w, "#f7c76f", "Processado");
  else {
    ctx.fillStyle = "rgba(247, 199, 111, .7)";
    ctx.font = "700 13px system-ui";
    ctx.fillText("Renderize para visualizar o processado", 16, h / 2 + 24);
  }
}

function makeParamControl(param, group) {
  const wrap = document.createElement("div");
  wrap.className = "param";
  wrap.style.setProperty("--accent", group.accent);
  wrap.innerHTML = `
    <label>${labelFromParamName(param.name)}<small>${helpFromParamName(param.name)}</small></label>
    <input type="range" min="${param.min}" max="${param.max}" step="${stepForRange(param.min, param.max)}" value="${param.def}">
    <div class="value">${param.def.toFixed(3)}</div>`;
  const input = wrap.querySelector("input");
  const txt = wrap.querySelector(".value");
  input.oninput = () => {
    const v = parseFloat(input.value);
    txt.textContent = v.toFixed(3);
    st.mod._vibe_set_param(st.h, param.id, v);
    st.outBuf = null;
    drawScope();
    setButtons();
  };
  const ctl = {
    id: param.id,
    name: param.name,
    min: param.min,
    max: param.max,
    input,
    txt,
  };
  st.controls.push(ctl);
  st.paramByName.set(param.name, ctl);
  return wrap;
}

function renderParamGroups(paramSpecs) {
  const params = $("params");
  params.innerHTML = "";
  const byName = new Map(paramSpecs.map((param) => [param.name, param]));
  const placed = new Set();
  for (const group of GROUPS) {
    const section = document.createElement("section");
    section.className = "param-group";
    section.style.setProperty("--accent", group.accent);
    section.innerHTML = `<h3><span class="group-dot"></span>${group.title}</h3>`;
    for (const name of group.params) {
      const param = byName.get(name);
      if (!param) continue;
      section.appendChild(makeParamControl(param, group));
      placed.add(name);
    }
    params.appendChild(section);
  }
  const remaining = paramSpecs.filter((param) => !placed.has(param.name));
  if (remaining.length) {
    const section = document.createElement("section");
    section.className = "param-group";
    section.innerHTML = '<h3><span class="group-dot"></span>Extras</h3>';
    for (const param of remaining)
      section.appendChild(makeParamControl(param, { accent: "#85efac" }));
    params.appendChild(section);
  }
}

async function boot() {
  setButtons();
  drawScope();
  st.mod = await createModule();
  st.h = st.mod._vibe_create();
  const pCount = st.mod._vibe_get_param_count();
  const paramSpecs = [];
  for (let i = 0; i < pCount; i += 1) {
    paramSpecs.push({
      id: i,
      name: st.mod.UTF8ToString(st.mod._vibe_get_param_name(i)),
      min: st.mod._vibe_get_param_min(i),
      max: st.mod._vibe_get_param_max(i),
      def: st.mod._vibe_get_param_default(i),
    });
  }
  renderParamGroups(paramSpecs);
  const voicing = $("voicing");
  voicing.innerHTML = "";
  for (let i = 0; i < st.mod._vibe_get_voicing_count(); i += 1) {
    const op = document.createElement("option");
    op.value = i;
    op.textContent = st.mod.UTF8ToString(st.mod._vibe_get_voicing_name(i));
    voicing.appendChild(op);
  }
  voicing.onchange = () => {
    st.mod._vibe_set_voicing(st.h, parseInt(voicing.value, 10));
    syncControlsFromEngine();
    updateVoiceHint();
    st.outBuf = null;
    drawScope();
    setButtons();
  };
  document.querySelectorAll("[data-scene]").forEach((button) => {
    button.addEventListener("click", () => applyScene(button.dataset.scene));
  });
  updateVoiceHint();
  syncControlsFromEngine();
  setButtons();
  status("Pronto para carregar áudio.");
}

async function loadFile(f) {
  if (!f) return;
  try {
    status("Decodificando áudio…");
    const arr = await f.arrayBuffer();
    const b = await getAudioContext().decodeAudioData(arr);
    st.inBuf = await resampleToTarget(b);
    st.outBuf = null;
    $("fileName").textContent = f.name;
    $("fileDuration").textContent = formatTime(st.inBuf.duration);
    drawScope();
    setButtons();
    status(`Carregado: ${f.name}. Escolha uma cena e renderize.`);
  } catch (error) {
    status(`Erro ao decodificar: ${error.message}`);
  }
}

$("file").onchange = async (e) => loadFile(e.target.files[0]);

const dropzone = $("dropzone");
["dragenter", "dragover"].forEach((eventName) => {
  dropzone.addEventListener(eventName, (event) => {
    event.preventDefault();
    dropzone.classList.add("dragging");
  });
});
["dragleave", "drop"].forEach((eventName) => {
  dropzone.addEventListener(eventName, (event) => {
    event.preventDefault();
    dropzone.classList.remove("dragging");
  });
});
dropzone.addEventListener("drop", (event) =>
  loadFile(event.dataTransfer.files[0]),
);

$("render").onclick = () => {
  if (!st.inBuf || !st.mod || !st.h) return;
  status("Renderizando com o core WASM…");
  st.mod._vibe_reset(st.h, 1);
  const { l, r } = toStereoArrays(st.inBuf);
  const n = l.length;
  const bytes = n * Float32Array.BYTES_PER_ELEMENT;
  const ptrs = [];
  try {
    const pl = st.mod._malloc(bytes);
    ptrs.push(pl);
    const pr = st.mod._malloc(bytes);
    ptrs.push(pr);
    const ol = st.mod._malloc(bytes);
    ptrs.push(ol);
    const orr = st.mod._malloc(bytes);
    ptrs.push(orr);
    getWasmHeapF32().set(l, pl >> 2);
    getWasmHeapF32().set(r, pr >> 2);
    st.mod._vibe_process_stereo(st.h, pl, pr, ol, orr, n);
    const heapF32 = getWasmHeapF32();
    const outL = heapF32.subarray(ol >> 2, (ol >> 2) + n).slice();
    const outR = heapF32.subarray(orr >> 2, (orr >> 2) + n).slice();
    const out = getAudioContext().createBuffer(2, n, TARGET_SAMPLE_RATE);
    out.copyToChannel(outL, 0);
    out.copyToChannel(outR, 1);
    st.outBuf = out;
    drawScope();
    setButtons();
    status("Render pronto. Compare A/B ou exporte o WAV.");
  } catch (error) {
    status(`Erro no render: ${error.message}`);
  } finally {
    for (const ptr of ptrs) st.mod._free(ptr);
  }
};

$("playIn").onclick = async () => {
  if (st.inBuf) await play(st.inBuf);
};
$("playOut").onclick = async () => {
  if (st.outBuf) await play(st.outBuf);
};
$("stop").onclick = stop;
$("export").onclick = () => {
  if (!st.outBuf) return;
  const b = encodeWav(
    st.outBuf.getChannelData(0),
    st.outBuf.getChannelData(1),
    TARGET_SAMPLE_RATE,
  );
  const a = document.createElement("a");
  const url = URL.createObjectURL(b);
  a.href = url;
  a.download = "pico2vibe-processed.wav";
  a.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
};

window.addEventListener("resize", drawScope);
window.addEventListener("pagehide", (event) => {
  stop();
  if (event.persisted) return;
  if (st.h && st.mod) {
    st.mod._vibe_destroy(st.h);
    st.h = 0;
  }
});

boot().catch((e) => status(`Erro: ${e.message}`));
