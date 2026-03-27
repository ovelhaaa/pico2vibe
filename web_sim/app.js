const SAMPLE_RATE = 44100;
const PERIOD = 32;
const CHANNELS = 2;
const DENORMAL_GUARD = 1e-18;
const INV_SAMPLE_RATE = 1 / SAMPLE_RATE;
const PI = Math.PI;

const userDefs = [
  { key: "depth", label: "Depth", min: 0.0, max: 1.0, defaultValue: 0.85 },
  { key: "feedback", label: "Feedback", min: 0.0, max: 0.65, defaultValue: 0.42 },
  { key: "mix", label: "Mix", min: 0.0, max: 1.0, defaultValue: 0.5 },
  { key: "input_drive", label: "Input Drive", min: 0.5, max: 6.0, defaultValue: 3.5 },
  { key: "output_gain", label: "Output Gain", min: 0.25, max: 2.0, defaultValue: 1.0 },
  { key: "sweep_min", label: "Sweep Min", min: 0.0, max: 1.0, defaultValue: 0.58 },
  { key: "sweep_max", label: "Sweep Max", min: 0.0, max: 1.0, defaultValue: 0.98 },
  { key: "lfo_rate_hz", label: "LFO Rate", min: 0.02, max: 12.0, defaultValue: 1.2 },
  { key: "drift_amount", label: "Drift Amount", min: 0.0, max: 0.05, defaultValue: 0.018 },
  { key: "drift_rate_hz", label: "Drift Rate", min: 0.005, max: 0.5, defaultValue: 0.08 },
];

const tuningDefs = [
  { key: "lamp_attack_sec", label: "Lamp Attack", min: 0.001, max: 0.25, defaultValue: 0.01 },
  { key: "lamp_release_sec", label: "Lamp Release", min: 0.001, max: 0.5, defaultValue: 0.04 },
  { key: "ldr_dark_ohms", label: "LDR Dark Ohms", min: 10000, max: 1000000, defaultValue: 1000000, logScale: true },
  { key: "ldr_curve", label: "LDR Curve", min: 1.0, max: 12.0, defaultValue: 7.6009 },
  { key: "ldr_min_ohms", label: "LDR Min Ohms", min: 1000, max: 20000, defaultValue: 3900, logScale: true },
  { key: "ldr_max_ohms", label: "LDR Max Ohms", min: 50000, max: 1000000, defaultValue: 1000000, logScale: true },
  { key: "emitter_fb_scale", label: "Emitter FB Scale", min: 1000, max: 30000, defaultValue: 12500, logScale: true },
  { key: "emitter_fb_min", label: "Emitter FB Min", min: 0.001, max: 0.5, defaultValue: 0.01, logScale: true },
  { key: "emitter_fb_max", label: "Emitter FB Max", min: 0.2, max: 4.0, defaultValue: 2.5 },
  { key: "stage_state_limit", label: "Stage Limit", min: 2.0, max: 12.0, defaultValue: 6.0 },
  { key: "bjt_gain_trim", label: "BJT Gain Trim", min: 0.1, max: 0.8, defaultValue: 0.35 },
  { key: "lfo_shape_smoothing", label: "LFO Shape Smooth", min: 0.01, max: 1.0, defaultValue: 0.2 },
  { key: "stereo_phase_offset", label: "Stereo Offset", min: 0.0, max: 0.5, defaultValue: 0.25 },
  { key: "control_smoothing_hz", label: "Control Smooth Hz", min: 1.0, max: 80.0, defaultValue: 18.0 },
];

const defaultUser = Object.fromEntries(userDefs.map((def) => [def.key, def.defaultValue]));
const defaultTuning = Object.fromEntries(tuningDefs.map((def) => [def.key, def.defaultValue]));

const dom = {
  fileInput: document.getElementById("file-input"),
  fileName: document.getElementById("file-name"),
  fileMeta: document.getElementById("file-meta"),
  renderButton: document.getElementById("render-button"),
  playInputButton: document.getElementById("play-input-button"),
  playOutputButton: document.getElementById("play-output-button"),
  stopButton: document.getElementById("stop-button"),
  exportButton: document.getElementById("export-button"),
  inputWaveform: document.getElementById("input-waveform"),
  outputWaveform: document.getElementById("output-waveform"),
  inputRms: document.getElementById("input-rms"),
  outputRms: document.getElementById("output-rms"),
  engineStatus: document.getElementById("engine-status"),
  controlsGrid: document.getElementById("controls-grid"),
  tuningGrid: document.getElementById("tuning-grid"),
  eventLog: document.getElementById("event-log"),
  ledParam: document.getElementById("led-param"),
  ledMode: document.getElementById("led-mode"),
  ledParamLabel: document.getElementById("led-param-label"),
  ledModeLabel: document.getElementById("led-mode-label"),
  encoderTarget: document.getElementById("encoder-target"),
  modeStatus: document.getElementById("mode-status"),
  clockStatus: document.getElementById("clock-status"),
  encoderLeft: document.getElementById("encoder-left"),
  encoderRight: document.getElementById("encoder-right"),
  encoderButton: document.getElementById("encoder-button"),
};

function clamp(value, min, max) {
  return Math.min(max, Math.max(min, value));
}

function fastSoftClip(x) {
  return x / (1 + Math.abs(x));
}

function noiseBipolar(state) {
  state.value = (Math.imul(state.value, 1664525) + 1013904223) >>> 0;
  const uni = ((state.value >>> 8) & 0x00ffffff) / 16777215;
  return uni * 2 - 1;
}

function makeFilterState() {
  return { x1: 0, y1: 0, n0: 0, n1: 0, d1: 0 };
}

function makeStage() {
  return {
    vc: makeFilterState(),
    vcvo: makeFilterState(),
    ecvc: makeFilterState(),
    vevo: makeFilterState(),
    oldcvolt: 0,
    ldr_mismatch: 1,
  };
}

function sanitizeUser(user) {
  user.depth = clamp(user.depth, 0, 1);
  user.feedback = clamp(user.feedback, 0, 0.65);
  user.mix = clamp(user.mix, 0, 1);
  user.input_drive = clamp(user.input_drive, 0.5, 6);
  user.output_gain = clamp(user.output_gain, 0.25, 2);
  user.sweep_min = clamp(user.sweep_min, 0, 1);
  user.sweep_max = clamp(user.sweep_max, user.sweep_min, 1);
  user.lfo_rate_hz = clamp(user.lfo_rate_hz, 0.02, 12);
  user.drift_amount = clamp(user.drift_amount, 0, 0.05);
  user.drift_rate_hz = clamp(user.drift_rate_hz, 0.005, 0.5);
}

function sanitizeTuning(tuning) {
  tuning.lamp_attack_sec = clamp(tuning.lamp_attack_sec, 0.001, 0.25);
  tuning.lamp_release_sec = clamp(tuning.lamp_release_sec, 0.001, 0.5);
  tuning.ldr_dark_ohms = clamp(tuning.ldr_dark_ohms, 10000, 1000000);
  tuning.ldr_curve = clamp(tuning.ldr_curve, 1, 12);
  tuning.ldr_min_ohms = clamp(tuning.ldr_min_ohms, 1000, 20000);
  tuning.ldr_max_ohms = clamp(tuning.ldr_max_ohms, tuning.ldr_min_ohms, 1000000);
  tuning.emitter_fb_scale = clamp(tuning.emitter_fb_scale, 1000, 30000);
  tuning.emitter_fb_min = clamp(tuning.emitter_fb_min, 0.001, 0.5);
  tuning.emitter_fb_max = clamp(tuning.emitter_fb_max, tuning.emitter_fb_min, 4);
  tuning.stage_state_limit = clamp(tuning.stage_state_limit, 2, 12);
  tuning.bjt_gain_trim = clamp(tuning.bjt_gain_trim, 0.1, 0.8);
  tuning.lfo_shape_smoothing = clamp(tuning.lfo_shape_smoothing, 0.01, 1);
  tuning.stereo_phase_offset = clamp(tuning.stereo_phase_offset, 0, 0.5);
  tuning.control_smoothing_hz = clamp(tuning.control_smoothing_hz, 1, 80);
}

class EffectLFO {
  constructor() {
    this.phase = 0;
    this.stateL = 0;
    this.stateR = 0;
    this.driftState = 0;
    this.driftRng = { value: 0xa341316c };
  }

  reseed(seed) {
    this.phase = 0;
    this.stateL = 0;
    this.stateR = 0;
    this.driftState = 0;
    this.driftRng.value = seed || 0xa341316c;
  }

  processBlock(user, tuning) {
    const blockTime = PERIOD / SAMPLE_RATE;
    const driftAlpha = 1 - Math.exp(-2 * PI * clamp(user.drift_rate_hz, 0.005, 0.5) * blockTime);
    this.driftState += driftAlpha * (noiseBipolar(this.driftRng) - this.driftState);
    this.driftState = clamp(this.driftState, -1, 1);

    const drift = 1 + clamp(user.drift_amount, 0, 0.05) * this.driftState;
    this.phase += clamp(user.lfo_rate_hz, 0.02, 12) * drift * INV_SAMPLE_RATE * PERIOD;
    if (this.phase >= 1) this.phase -= 1;

    const triL = this.phase < 0.4 ? this.phase * 2.5 : 1 - (this.phase - 0.4) * 1.6666;
    let pR = this.phase + clamp(tuning.stereo_phase_offset, 0, 0.5);
    if (pR >= 1) pR -= 1;
    const triR = pR < 0.4 ? pR * 2.5 : 1 - (pR - 0.4) * 1.6666;

    const smoothing = clamp(tuning.lfo_shape_smoothing, 0.01, 1);
    this.stateL += smoothing * (triL - this.stateL);
    this.stateR += smoothing * (triR - this.stateR);

    return { left: clamp(this.stateL, 0, 1), right: clamp(this.stateR, 0, 1) };
  }
}

class VibeEngine {
  constructor() {
    this.params = {
      user: structuredClone(defaultUser),
      tuning: structuredClone(defaultTuning),
    };
    this.smoothedUser = structuredClone(defaultUser);
    this.modeChorus = true;
    this.lpanning = 1;
    this.rpanning = 1;
    this.stage = Array.from({ length: 8 }, () => makeStage());
    this.C1 = new Float32Array(8);
    this.en0 = new Float32Array(8);
    this.en1 = new Float32Array(8);
    this.fbl = 0;
    this.fbr = 0;
    this.gainBjt = 0;
    this.k = 0;
    this.R1 = 0;
    this.C2 = 0;
    this.beta = 150;
    this.rngSeed = 0x13579bdf;
    this.lampStateL = 0;
    this.lampStateR = 0;
    this.lampAttack = 0;
    this.lampRelease = 0;
    this.lfo = new EffectLFO();

    sanitizeUser(this.params.user);
    sanitizeTuning(this.params.tuning);
    this.smoothedUser = structuredClone(this.params.user);
    this.lfo.reseed(this.rngSeed ^ 0xa511e9b3);
    this.updateTimeConstants();
    this.initVibes();
  }

  reseed(seed) {
    this.rngSeed = seed >>> 0 || 0x13579bdf;
    this.lampStateL = 0;
    this.lampStateR = 0;
    this.fbl = 0;
    this.fbr = 0;
    this.smoothedUser = structuredClone(this.params.user);
    sanitizeUser(this.smoothedUser);
    this.lfo.reseed(this.rngSeed ^ 0xa511e9b3);
    this.updateTimeConstants();
    this.initVibes();
  }

  setUserParam(key, value) {
    this.params.user[key] = value;
    sanitizeUser(this.params.user);
  }

  setTuningParam(key, value) {
    this.params.tuning[key] = value;
    sanitizeTuning(this.params.tuning);
  }

  updateTimeConstants() {
    const blockTime = PERIOD / SAMPLE_RATE;
    this.lampAttack = 1 - Math.exp(-blockTime / clamp(this.params.tuning.lamp_attack_sec, 0.001, 0.25));
    this.lampRelease = 1 - Math.exp(-blockTime / clamp(this.params.tuning.lamp_release_sec, 0.001, 0.5));
  }

  updateSmoothedUserParams() {
    sanitizeUser(this.params.user);
    const alpha = 1 - Math.exp(-2 * PI * clamp(this.params.tuning.control_smoothing_hz, 1, 80) * (PERIOD / SAMPLE_RATE));
    for (const def of userDefs) {
      const key = def.key;
      this.smoothedUser[key] += alpha * (this.params.user[key] - this.smoothedUser[key]);
    }
    sanitizeUser(this.smoothedUser);
  }

  vibefilter(data, f) {
    const y0 = data * f.n0 + f.x1 * f.n1 - f.y1 * f.d1;
    f.y1 = y0 + DENORMAL_GUARD;
    f.x1 = data;
    return y0;
  }

  bjtShape(data, drive) {
    return fastSoftClip(data * drive) * this.params.tuning.bjt_gain_trim;
  }

  initVibes() {
    this.k = 2 * SAMPLE_RATE;
    this.R1 = 4700;
    this.C2 = 1e-6;
    this.gainBjt = -this.beta / (this.beta + 1);
    const baseC1 = [0.015e-6, 0.22e-6, 470e-12, 0.0047e-6, 0.015e-6, 0.22e-6, 470e-12, 0.0047e-6];
    const componentRng = { value: this.rngSeed ^ 0x51f15eed };

    for (let i = 0; i < 8; i += 1) {
      this.C1[i] = baseC1[i] * (1 + 0.1 * noiseBipolar(componentRng));
      this.stage[i] = makeStage();
      this.stage[i].ldr_mismatch = 1 + 0.05 * noiseBipolar(componentRng);
      this.en1[i] = this.k * this.R1 * this.C1[i];
      this.en0[i] = 1;
    }
  }

  modulate(resL, resR) {
    for (let i = 0; i < 8; i += 1) {
      const baseRes = i < 4 ? resL : resR;
      const stageRes = clamp(baseRes * this.stage[i].ldr_mismatch, this.params.tuning.ldr_min_ohms, this.params.tuning.ldr_max_ohms);
      const currentRv = 4700 + stageRes;
      const R1pRv = this.R1 + currentRv;
      const C2pC1 = this.C2 + this.C1[i];

      const cd1 = this.k * R1pRv * this.C1[i];
      const cn1 = this.k * this.gainBjt * currentRv * this.C1[i];
      const ecd1 = this.k * cd1 * this.C2 / C2pC1;
      const ecn1 = this.k * this.gainBjt * this.R1 * cd1 * this.C2 / (currentRv * C2pC1);
      const on1 = this.k * currentRv * this.C2;

      const cd0 = 1 + this.C1[i] / this.C2;
      const ecd0 = 1;
      const od0 = 1 + this.C2 / this.C1[i];
      const ed0 = 1 + this.C1[i] / this.C2;
      const cn0 = this.gainBjt * (1 + this.C1[i] / this.C2);
      const on0 = 1;
      const ecn0 = 0;

      let tmp = 1 / (cd1 + cd0);
      this.stage[i].vc.n1 = tmp * (cn0 - cn1);
      this.stage[i].vc.n0 = tmp * (cn1 + cn0);
      this.stage[i].vc.d1 = tmp * (cd0 - cd1);

      tmp = 1 / (ecd1 + ecd0);
      this.stage[i].ecvc.n1 = tmp * (ecn0 - ecn1);
      this.stage[i].ecvc.n0 = tmp * (ecn1 + ecn0);
      this.stage[i].ecvc.d1 = tmp * (ecd0 - ecd1);

      tmp = 1 / (on1 + od0);
      this.stage[i].vcvo.n1 = tmp * (on0 - on1);
      this.stage[i].vcvo.n0 = tmp * (on1 + on0);
      this.stage[i].vcvo.d1 = tmp * (od0 - on1);

      const ed1 = this.k * R1pRv * this.C1[i];
      tmp = 1 / (ed1 + ed0);
      this.stage[i].vevo.n1 = tmp * (this.en0[i] - this.en1[i]);
      this.stage[i].vevo.n0 = tmp * (this.en1[i] + this.en0[i]);
      this.stage[i].vevo.d1 = tmp * (ed0 - ed1);
    }
  }

  processStereo(leftIn, rightIn) {
    const leftOut = new Float32Array(leftIn.length);
    const rightOut = new Float32Array(rightIn.length);

    for (let offset = 0; offset < leftIn.length; offset += PERIOD) {
      this.updateTimeConstants();
      this.updateSmoothedUserParams();
      const lfo = this.lfo.processBlock(this.smoothedUser, this.params.tuning);
      const targetL = this.smoothedUser.sweep_min + this.smoothedUser.depth * lfo.left * (this.smoothedUser.sweep_max - this.smoothedUser.sweep_min);
      const targetR = this.smoothedUser.sweep_min + this.smoothedUser.depth * lfo.right * (this.smoothedUser.sweep_max - this.smoothedUser.sweep_min);

      this.lampStateL += (targetL > this.lampStateL ? this.lampAttack : this.lampRelease) * (targetL - this.lampStateL);
      this.lampStateR += (targetR > this.lampStateR ? this.lampAttack : this.lampRelease) * (targetR - this.lampStateR);
      this.lampStateL = clamp(this.lampStateL, 0, 1);
      this.lampStateR = clamp(this.lampStateR, 0, 1);

      let resL = this.params.tuning.ldr_dark_ohms * Math.exp(-this.params.tuning.ldr_curve * (this.lampStateL * Math.sqrt(this.lampStateL)));
      let resR = this.params.tuning.ldr_dark_ohms * Math.exp(-this.params.tuning.ldr_curve * (this.lampStateR * Math.sqrt(this.lampStateR)));
      resL = clamp(resL, this.params.tuning.ldr_min_ohms, this.params.tuning.ldr_max_ohms);
      resR = clamp(resR, this.params.tuning.ldr_min_ohms, this.params.tuning.ldr_max_ohms);
      this.modulate(resL, resR);

      const emitterFbL = clamp(this.params.tuning.emitter_fb_scale / resL, this.params.tuning.emitter_fb_min, this.params.tuning.emitter_fb_max);
      const emitterFbR = clamp(this.params.tuning.emitter_fb_scale / resR, this.params.tuning.emitter_fb_min, this.params.tuning.emitter_fb_max);
      const stageLimit = clamp(this.params.tuning.stage_state_limit, 2, 12);
      const frames = Math.min(PERIOD, leftIn.length - offset);

      for (let i = 0; i < frames; i += 1) {
        const dryL = leftIn[offset + i];
        let input = this.bjtShape(this.fbl + dryL, this.smoothedUser.input_drive);
        for (let j = 0; j < 4; j += 1) {
          let cvolt = this.vibefilter(input, this.stage[j].ecvc) + this.vibefilter(input + emitterFbL * this.stage[j].oldcvolt, this.stage[j].vc);
          cvolt = clamp(cvolt, -stageLimit, stageLimit);
          const ocvolt = clamp(this.vibefilter(cvolt, this.stage[j].vcvo), -stageLimit, stageLimit);
          this.stage[j].oldcvolt = ocvolt;
          input = this.bjtShape(ocvolt + this.vibefilter(input, this.stage[j].vevo), this.smoothedUser.input_drive);
        }
        this.fbl = fastSoftClip(this.stage[3].oldcvolt * this.smoothedUser.feedback);
        leftOut[offset + i] = this.smoothedUser.output_gain * (
          this.modeChorus
            ? this.lpanning * (dryL * (1 - this.smoothedUser.mix) + input * this.smoothedUser.mix)
            : this.lpanning * input
        );

        const dryR = rightIn[offset + i];
        input = this.bjtShape(this.fbr + dryR, this.smoothedUser.input_drive);
        for (let j = 4; j < 8; j += 1) {
          let cvolt = this.vibefilter(input, this.stage[j].ecvc) + this.vibefilter(input + emitterFbR * this.stage[j].oldcvolt, this.stage[j].vc);
          cvolt = clamp(cvolt, -stageLimit, stageLimit);
          const ocvolt = clamp(this.vibefilter(cvolt, this.stage[j].vcvo), -stageLimit, stageLimit);
          this.stage[j].oldcvolt = ocvolt;
          input = this.bjtShape(ocvolt + this.vibefilter(input, this.stage[j].vevo), this.smoothedUser.input_drive);
        }
        this.fbr = fastSoftClip(this.stage[7].oldcvolt * this.smoothedUser.feedback);
        rightOut[offset + i] = this.smoothedUser.output_gain * (
          this.modeChorus
            ? this.rpanning * (dryR * (1 - this.smoothedUser.mix) + input * this.smoothedUser.mix)
            : this.rpanning * input
        );
      }
    }

    return { left: leftOut, right: rightOut };
  }
}

const state = {
  audioContext: null,
  engine: new VibeEngine(),
  modeIsDepth: false,
  inputStereo: null,
  outputStereo: null,
  inputBuffer: null,
  outputBuffer: null,
  currentSource: null,
};

function getAudioContext() {
  if (!state.audioContext) state.audioContext = new AudioContext();
  return state.audioContext;
}

function logLine(message) {
  const timestamp = new Date().toLocaleTimeString("pt-BR");
  dom.eventLog.textContent = `[${timestamp}] ${message}\n${dom.eventLog.textContent}`.trimEnd();
}

function formatValue(def, value) {
  if (Math.abs(value) >= 1000) return value.toFixed(0);
  if (Math.abs(value) >= 10) return value.toFixed(2);
  return value.toFixed(3);
}

function valueToSlider(def, value) {
  const t = def.logScale
    ? (Math.log(value) - Math.log(def.min)) / (Math.log(def.max) - Math.log(def.min))
    : (value - def.min) / (def.max - def.min);
  return Math.round(clamp(t, 0, 1) * 1000);
}

function sliderToValue(def, sliderValue) {
  const t = clamp(sliderValue / 1000, 0, 1);
  return def.logScale
    ? Math.exp(Math.log(def.min) + t * (Math.log(def.max) - Math.log(def.min)))
    : def.min + t * (def.max - def.min);
}

function updateLedView() {
  dom.ledParam.classList.toggle("is-on", state.modeIsDepth);
  dom.ledMode.classList.toggle("is-warm", !state.engine.modeChorus);
  dom.ledParamLabel.textContent = state.modeIsDepth ? "Depth" : "Speed";
  dom.ledModeLabel.textContent = state.engine.modeChorus ? "Chorus" : "Vibrato";
  dom.encoderTarget.textContent = state.modeIsDepth ? "Depth" : "Speed";
  dom.modeStatus.textContent = state.engine.modeChorus ? "Chorus" : "Vibrato";
}

function updateButtons() {
  dom.renderButton.disabled = !state.inputStereo;
  dom.playInputButton.disabled = !state.inputBuffer;
  dom.playOutputButton.disabled = !state.outputBuffer;
  dom.exportButton.disabled = !state.outputStereo;
  dom.stopButton.disabled = !state.currentSource;
}

function rmsStereo(stereo) {
  if (!stereo) return 0;
  let acc = 0;
  for (let i = 0; i < stereo.left.length; i += 1) {
    const m = 0.5 * (stereo.left[i] + stereo.right[i]);
    acc += m * m;
  }
  return Math.sqrt(acc / stereo.left.length);
}

function drawWave(canvas, stereo, color) {
  const ctx = canvas.getContext("2d");
  const { width, height } = canvas;
  ctx.clearRect(0, 0, width, height);
  ctx.strokeStyle = "rgba(255,255,255,0.08)";
  for (let i = 0; i <= 4; i += 1) {
    const y = (height / 4) * i;
    ctx.beginPath();
    ctx.moveTo(0, y);
    ctx.lineTo(width, y);
    ctx.stroke();
  }
  if (!stereo) return;

  const mono = stereo.left;
  const step = Math.max(1, Math.floor(mono.length / width));
  ctx.strokeStyle = color;
  ctx.lineWidth = 2;
  ctx.beginPath();
  for (let x = 0; x < width; x += 1) {
    const start = x * step;
    const end = Math.min(mono.length, start + step);
    let min = 1;
    let max = -1;
    for (let i = start; i < end; i += 1) {
      const v = mono[i];
      if (v < min) min = v;
      if (v > max) max = v;
    }
    const y1 = ((1 - max) * 0.5) * height;
    const y2 = ((1 - min) * 0.5) * height;
    ctx.moveTo(x, y1);
    ctx.lineTo(x, y2);
  }
  ctx.stroke();
}

function refreshScopes() {
  drawWave(dom.inputWaveform, state.inputStereo, "#88f1cd");
  drawWave(dom.outputWaveform, state.outputStereo, "#ffba72");
  dom.inputRms.textContent = state.inputStereo ? `RMS ${rmsStereo(state.inputStereo).toFixed(4)}` : "RMS --";
  dom.outputRms.textContent = state.outputStereo ? `RMS ${rmsStereo(state.outputStereo).toFixed(4)}` : "RMS --";
}

function renderControlCards(defs, grid, getter, setter, liveGetter) {
  grid.innerHTML = "";
  for (const def of defs) {
    const card = document.createElement("div");
    card.className = "control-card";
    card.innerHTML = `
      <div class="control-top">
        <h3 class="control-name">${def.label}</h3>
        <span class="control-live" id="live-${def.key}">Live ${formatValue(def, liveGetter(def.key))}</span>
      </div>
      <input id="slider-${def.key}" type="range" min="0" max="1000" step="1" value="${valueToSlider(def, getter(def.key))}">
      <div class="control-meta">
        <span id="value-${def.key}">${formatValue(def, getter(def.key))}</span>
        <span>${formatValue(def, def.min)} - ${formatValue(def, def.max)}</span>
      </div>
    `;
    grid.appendChild(card);
    const slider = card.querySelector("input");
    slider.addEventListener("input", () => {
      setter(def.key, sliderToValue(def, Number(slider.value)));
      syncControlValues();
    });
  }
}

function syncControlValues() {
  for (const def of userDefs) {
    document.getElementById(`value-${def.key}`).textContent = formatValue(def, state.engine.params.user[def.key]);
    document.getElementById(`live-${def.key}`).textContent = `Live ${formatValue(def, state.engine.smoothedUser[def.key])}`;
    document.getElementById(`slider-${def.key}`).value = `${valueToSlider(def, state.engine.params.user[def.key])}`;
  }
  for (const def of tuningDefs) {
    document.getElementById(`value-${def.key}`).textContent = formatValue(def, state.engine.params.tuning[def.key]);
    document.getElementById(`live-${def.key}`).textContent = `Live ${formatValue(def, state.engine.params.tuning[def.key])}`;
    document.getElementById(`slider-${def.key}`).value = `${valueToSlider(def, state.engine.params.tuning[def.key])}`;
  }
}

async function decodeFileToStereo(file) {
  const arrayBuffer = await file.arrayBuffer();
  const ctx = getAudioContext();
  const decoded = await ctx.decodeAudioData(arrayBuffer.slice(0));
  const offline = new OfflineAudioContext(CHANNELS, Math.ceil(decoded.duration * SAMPLE_RATE), SAMPLE_RATE);
  const source = offline.createBufferSource();
  const buffer = offline.createBuffer(CHANNELS, decoded.length, decoded.sampleRate);
  if (decoded.numberOfChannels === 1) {
    const mono = decoded.getChannelData(0);
    buffer.copyToChannel(mono, 0);
    buffer.copyToChannel(mono, 1);
  } else {
    buffer.copyToChannel(decoded.getChannelData(0), 0);
    buffer.copyToChannel(decoded.getChannelData(1), 1);
  }
  source.buffer = buffer;
  source.connect(offline.destination);
  source.start();
  const rendered = await offline.startRendering();
  return {
    left: new Float32Array(rendered.getChannelData(0)),
    right: new Float32Array(rendered.getChannelData(1)),
    duration: rendered.length / SAMPLE_RATE,
  };
}

function stereoToBuffer(stereo) {
  const ctx = getAudioContext();
  const buffer = ctx.createBuffer(CHANNELS, stereo.left.length, SAMPLE_RATE);
  buffer.copyToChannel(stereo.left, 0);
  buffer.copyToChannel(stereo.right, 1);
  return buffer;
}

function stopPlayback() {
  if (!state.currentSource) return;
  try {
    state.currentSource.stop();
  } catch (_error) {
    // noop
  }
  state.currentSource.disconnect();
  state.currentSource = null;
  dom.clockStatus.textContent = "idle";
  updateButtons();
}

function playBuffer(buffer, label) {
  stopPlayback();
  const source = getAudioContext().createBufferSource();
  source.buffer = buffer;
  source.connect(getAudioContext().destination);
  source.onended = () => {
    if (state.currentSource === source) {
      state.currentSource = null;
      dom.clockStatus.textContent = "idle";
      updateButtons();
    }
  };
  source.start();
  state.currentSource = source;
  dom.clockStatus.textContent = `playing ${label}`;
  updateButtons();
}

function writeWav16(stereo) {
  const frames = stereo.left.length;
  const dataSize = frames * 4;
  const buffer = new ArrayBuffer(44 + dataSize);
  const view = new DataView(buffer);
  const writeString = (offset, text) => {
    for (let i = 0; i < text.length; i += 1) view.setUint8(offset + i, text.charCodeAt(i));
  };

  writeString(0, "RIFF");
  view.setUint32(4, 36 + dataSize, true);
  writeString(8, "WAVE");
  writeString(12, "fmt ");
  view.setUint32(16, 16, true);
  view.setUint16(20, 1, true);
  view.setUint16(22, CHANNELS, true);
  view.setUint32(24, SAMPLE_RATE, true);
  view.setUint32(28, SAMPLE_RATE * 4, true);
  view.setUint16(32, 4, true);
  view.setUint16(34, 16, true);
  writeString(36, "data");
  view.setUint32(40, dataSize, true);

  let offset = 44;
  for (let i = 0; i < frames; i += 1) {
    view.setInt16(offset, Math.round(clamp(stereo.left[i], -1, 1) * 32767), true);
    view.setInt16(offset + 2, Math.round(clamp(stereo.right[i], -1, 1) * 32767), true);
    offset += 4;
  }
  return new Blob([buffer], { type: "audio/wav" });
}

function renderOffline() {
  if (!state.inputStereo) return;
  state.engine.reseed(0x13579bdf);
  const started = performance.now();
  state.outputStereo = state.engine.processStereo(state.inputStereo.left, state.inputStereo.right);
  state.outputBuffer = stereoToBuffer(state.outputStereo);
  const elapsed = performance.now() - started;
  dom.engineStatus.textContent = `${state.engine.modeChorus ? "chorus" : "vibrato"} rendered`;
  refreshScopes();
  syncControlValues();
  updateButtons();
  logLine(`Render offline concluído em ${elapsed.toFixed(1)} ms.`);
}

function bindHardwareUi() {
  const applyEncoderDelta = (delta) => {
    const now = performance.now();
    const last = bindHardwareUi.lastEncoderEventMs || 0;
    const dtUs = last === 0 ? 0 : (now - last) * 1000;
    bindHardwareUi.lastEncoderEventMs = now;
    const accel = dtUs === 0 ? 1 : dtUs < 12000 ? 4 : dtUs < 22000 ? 3 : dtUs < 45000 ? 2 : dtUs < 80000 ? 1.4 : 1;

    if (state.modeIsDepth) {
      const normalized = clamp(state.engine.params.user.depth, 0, 1);
      const baseStep = 0.01 + 0.01 * (normalized * normalized * (3 - 2 * normalized));
      state.engine.setUserParam("depth", state.engine.params.user.depth + delta * baseStep * (1 + 0.35 * (accel - 1)));
    } else {
      const normalized = clamp((state.engine.params.user.lfo_rate_hz - 0.02) / (12 - 0.02), 0, 1);
      const curved = normalized * normalized * (3 - 2 * normalized);
      const baseStep = 0.025 + 0.05 * curved + 0.145 * curved * curved;
      state.engine.setUserParam("lfo_rate_hz", state.engine.params.user.lfo_rate_hz + delta * baseStep * accel);
    }
    syncControlValues();
    updateLedView();
    logLine(`Encoder ${delta > 0 ? "+" : "-"} em ${state.modeIsDepth ? "depth" : "speed"}.`);
  };

  dom.encoderLeft.addEventListener("click", () => applyEncoderDelta(-1));
  dom.encoderRight.addEventListener("click", () => applyEncoderDelta(1));

  let holdTimer = null;
  let longPressHandled = false;
  dom.encoderButton.addEventListener("pointerdown", () => {
    longPressHandled = false;
    holdTimer = setTimeout(() => {
      longPressHandled = true;
      state.engine.modeChorus = !state.engine.modeChorus;
      updateLedView();
      logLine(`Modo alterado para ${state.engine.modeChorus ? "chorus" : "vibrato"}.`);
    }, 700);
  });

  const release = () => {
    clearTimeout(holdTimer);
    if (!longPressHandled) {
      state.modeIsDepth = !state.modeIsDepth;
      updateLedView();
      logLine(`Encoder target em ${state.modeIsDepth ? "depth" : "speed"}.`);
    }
  };

  dom.encoderButton.addEventListener("pointerup", release);
  dom.encoderButton.addEventListener("pointerleave", release);
}

async function handleFileChange(event) {
  const file = event.target.files?.[0];
  if (!file) return;

  dom.engineStatus.textContent = "decoding...";
  logLine(`Carregando ${file.name}...`);
  state.inputStereo = await decodeFileToStereo(file);
  state.outputStereo = null;
  state.inputBuffer = stereoToBuffer(state.inputStereo);
  state.outputBuffer = null;
  dom.fileName.textContent = file.name;
  dom.fileMeta.textContent = `${state.inputStereo.duration.toFixed(2)} s, stereo 44.1 kHz`;
  dom.engineStatus.textContent = "ready to render";
  refreshScopes();
  updateButtons();
  logLine("Arquivo convertido para stereo 44.1 kHz.");
}

function exportWav() {
  if (!state.outputStereo) return;
  const blob = writeWav16(state.outputStereo);
  const url = URL.createObjectURL(blob);
  const anchor = document.createElement("a");
  anchor.href = url;
  anchor.download = "pico2vibe-web-render.wav";
  anchor.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
  logLine("Exportação WAV concluída.");
}

function init() {
  renderControlCards(
    userDefs,
    dom.controlsGrid,
    (key) => state.engine.params.user[key],
    (key, value) => state.engine.setUserParam(key, value),
    (key) => state.engine.smoothedUser[key]
  );
  renderControlCards(
    tuningDefs,
    dom.tuningGrid,
    (key) => state.engine.params.tuning[key],
    (key, value) => state.engine.setTuningParam(key, value),
    (key) => state.engine.params.tuning[key]
  );
  bindHardwareUi();
  updateLedView();
  syncControlValues();
  updateButtons();
  refreshScopes();

  dom.fileInput.addEventListener("change", (event) => {
    handleFileChange(event).catch((error) => {
      dom.engineStatus.textContent = "decode failed";
      logLine(`Erro ao carregar arquivo: ${error.message}`);
      console.error(error);
    });
  });

  dom.renderButton.addEventListener("click", () => {
    try {
      renderOffline();
    } catch (error) {
      logLine(`Erro no render: ${error.message}`);
      console.error(error);
    }
  });
  dom.playInputButton.addEventListener("click", () => {
    if (state.inputBuffer) playBuffer(state.inputBuffer, "input");
  });
  dom.playOutputButton.addEventListener("click", () => {
    if (state.outputBuffer) playBuffer(state.outputBuffer, "output");
  });
  dom.stopButton.addEventListener("click", stopPlayback);
  dom.exportButton.addEventListener("click", exportWav);

  logLine("Web bench iniciado.");
}

init();
