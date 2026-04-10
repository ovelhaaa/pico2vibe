import createModule from './vibe_wasm.js';
const ctx = new AudioContext();
const st = {mod:null,h:0,inBuf:null,outBuf:null,src:null,params:[]};
const $ = (id)=>document.getElementById(id);
const status=(s)=>$('status').textContent=s;

function play(buf){ stop(); const src=ctx.createBufferSource(); src.buffer=buf; src.connect(ctx.destination); src.start(); st.src=src; }
function stop(){ if(st.src){ try{st.src.stop();}catch{} st.src=null; } }

function toStereoArrays(buf){
  const n=buf.length; const l=new Float32Array(n); const r=new Float32Array(n);
  l.set(buf.getChannelData(0));
  if(buf.numberOfChannels>1) r.set(buf.getChannelData(1)); else r.set(l);
  return {l,r};
}
async function resampleTo44100(buf){
  if(buf.sampleRate===44100) return buf;
  const off = new OfflineAudioContext(2, Math.ceil(buf.duration*44100), 44100);
  const src = off.createBufferSource(); src.buffer = buf; src.connect(off.destination); src.start();
  return await off.startRendering();
}

function encodeWav(l,r,sr){ const n=l.length; const ab=new ArrayBuffer(44+n*4); const dv=new DataView(ab); const w=(o,s)=>[...s].forEach((c,i)=>dv.setUint8(o+i,c.charCodeAt(0))); w(0,'RIFF'); dv.setUint32(4,36+n*4,true); w(8,'WAVEfmt '); dv.setUint32(16,16,true); dv.setUint16(20,1,true); dv.setUint16(22,2,true); dv.setUint32(24,sr,true); dv.setUint32(28,sr*4,true); dv.setUint16(32,4,true); dv.setUint16(34,16,true); w(36,'data'); dv.setUint32(40,n*4,true); let o=44; for(let i=0;i<n;i++){ dv.setInt16(o,Math.max(-1,Math.min(1,l[i]))*32767,true); o+=2; dv.setInt16(o,Math.max(-1,Math.min(1,r[i]))*32767,true); o+=2;} return new Blob([ab],{type:'audio/wav'}); }

async function boot(){
  st.mod = await createModule();
  st.h = st.mod._vibe_create();
  const pCount=st.mod._vibe_get_param_count();
  const params=$('params');
  for(let i=0;i<pCount;i++){
    const name=st.mod.UTF8ToString(st.mod._vibe_get_param_name(i));
    const min=st.mod._vibe_get_param_min(i), max=st.mod._vibe_get_param_max(i), def=st.mod._vibe_get_param_default(i);
    const wrap=document.createElement('div'); wrap.className='param';
    wrap.innerHTML=`<label>${name}</label><input type="range" min="${min}" max="${max}" step="${(max-min)/400}" value="${def}"><div>${def.toFixed(3)}</div>`;
    const input=wrap.querySelector('input'),txt=wrap.querySelector('div');
    input.oninput=()=>{const v=parseFloat(input.value); txt.textContent=v.toFixed(3); st.mod._vibe_set_param(st.h,i,v);};
    params.appendChild(wrap);
  }
  const voicing=$('voicing');
  for(let i=0;i<st.mod._vibe_get_voicing_count();i++){ const op=document.createElement('option'); op.value=i; op.textContent=st.mod.UTF8ToString(st.mod._vibe_get_voicing_name(i)); voicing.appendChild(op);} 
  voicing.onchange=()=>st.mod._vibe_set_voicing(st.h,parseInt(voicing.value));
  status('Ready');
}

$('file').onchange=async(e)=>{ const f=e.target.files[0]; if(!f)return; status('Decoding…'); const arr=await f.arrayBuffer(); const b=await ctx.decodeAudioData(arr); st.inBuf=await resampleTo44100(b); status(`Loaded ${f.name}`); };
$('render').onclick=()=>{
  if(!st.inBuf) return; status('Rendering…');
  st.mod._vibe_reset(st.h,1);
  const {l,r}=toStereoArrays(st.inBuf); const n=l.length;
  const bytes=n*4; const pl=st.mod._malloc(bytes), pr=st.mod._malloc(bytes), ol=st.mod._malloc(bytes), orr=st.mod._malloc(bytes);
  st.mod.HEAPF32.set(l,pl>>2); st.mod.HEAPF32.set(r,pr>>2);
  st.mod._vibe_process_stereo(st.h,pl,pr,ol,orr,n);
  const outL=new Float32Array(st.mod.HEAPF32.buffer,ol,n).slice(); const outR=new Float32Array(st.mod.HEAPF32.buffer,orr,n).slice();
  st.mod._free(pl);st.mod._free(pr);st.mod._free(ol);st.mod._free(orr);
  const out=ctx.createBuffer(2,n,44100); out.copyToChannel(outL,0); out.copyToChannel(outR,1); st.outBuf=out; status('Done');
};
$('playIn').onclick=()=>st.inBuf&&play(st.inBuf);
$('playOut').onclick=()=>st.outBuf&&play(st.outBuf);
$('stop').onclick=stop;
$('export').onclick=()=>{ if(!st.outBuf)return; const b=encodeWav(st.outBuf.getChannelData(0),st.outBuf.getChannelData(1),44100); const a=document.createElement('a'); a.href=URL.createObjectURL(b); a.download='processed.wav'; a.click();};

boot().catch(e=>status(`Error: ${e.message}`));
