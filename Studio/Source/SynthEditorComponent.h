/*
  ==============================================================================
    SynthEditorComponent.h  — M13 Synth parameter editor (Phase-6 WebView)
    Floating window per channel: waveform, ADSR, filter, LFO
    UI served as inline HTML via WebBrowserComponent + ResourceProvider.
  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>
#include <functional>
#include "ProjectModel.h"
#include "SynthEngine.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
namespace SynthEditorHelper
{
    inline std::vector<std::byte> toBytes(const juce::String& s)
    {
        const auto u = s.toStdString();
        return { reinterpret_cast<const std::byte*>(u.data()),
                 reinterpret_cast<const std::byte*>(u.data() + u.size()) };
    }

    inline juce::String buildHtml()
    {
        return R"HTML(<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8">
<style>
@font-face{font-family:'JBMono';font-weight:400;src:url('/font-regular.ttf') format('truetype');}
@font-face{font-family:'JBMono';font-weight:700;src:url('/font-bold.ttf') format('truetype');}
:root{
  --chassis:#e6dec9;--chassis2:#ddd3ba;--panel:#f3ecda;--rim:#c7bb9a;
  --accent:#d8412a;--ink:#1a1612;--ink-soft:#4a4338;--ink-faint:#8c8170;
  --display-bg:#0d1410;--display-fg:#b9ff66;
}
*{box-sizing:border-box;margin:0;padding:0;}
body{font-family:'JBMono',Monaco,'Courier New',monospace;background:linear-gradient(180deg,var(--chassis),var(--chassis2));color:var(--ink);padding:12px;}
.topbar{display:flex;align-items:center;gap:6px;margin-bottom:10px;flex-wrap:wrap;}
.section{background:var(--panel);border:1px solid var(--rim);border-radius:7px;margin-bottom:10px;overflow:hidden;}
.sec-hdr{display:flex;align-items:center;gap:8px;padding:8px 12px;background:linear-gradient(180deg,var(--chassis),var(--chassis2));border-bottom:1px solid var(--rim);}
.sec-label{font-size:10px;font-weight:700;letter-spacing:.18em;color:var(--ink-soft);text-transform:uppercase;flex:1;}
.params{padding:10px 12px;display:flex;flex-direction:column;gap:9px;}
.row{display:grid;grid-template-columns:114px 1fr 62px;align-items:center;gap:8px;}
.plabel{font-size:10px;font-weight:600;letter-spacing:.09em;color:var(--ink-faint);text-transform:uppercase;}
input[type=range]{-webkit-appearance:none;appearance:none;width:100%;height:5px;background:var(--chassis2);border:1px solid var(--rim);border-radius:3px;outline:none;}
input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:15px;height:15px;border-radius:50%;background:var(--accent);cursor:pointer;box-shadow:0 0 5px rgba(216,65,42,.55);border:1px solid rgba(0,0,0,.3);}
.val{font-size:10px;font-weight:600;color:var(--display-fg);background:var(--display-bg);padding:3px 6px;border-radius:3px;text-align:right;letter-spacing:.04em;white-space:nowrap;}
.btn{font-family:inherit;font-size:10px;font-weight:700;letter-spacing:.09em;padding:4px 9px;border-radius:4px;border:1px solid var(--rim);cursor:pointer;background:var(--chassis2);color:var(--ink-faint);transition:all .1s;text-transform:uppercase;}
.btn.on{background:var(--accent);border-color:var(--accent);color:#fff;box-shadow:0 0 6px rgba(216,65,42,.4);}
.btn.src-on{background:var(--accent);border-color:var(--accent);color:#fff;}
.toggle{font-family:inherit;font-size:10px;font-weight:700;letter-spacing:.11em;padding:4px 12px;border-radius:4px;border:1px solid var(--rim);cursor:pointer;background:var(--chassis2);color:var(--ink-faint);transition:all .12s;}
.toggle.on{background:var(--accent);border-color:var(--accent);color:#fff;box-shadow:0 0 6px rgba(216,65,42,.4);}
select{font-family:inherit;font-size:10px;font-weight:600;background:var(--display-bg);color:var(--display-fg);border:1px solid var(--rim);border-radius:4px;padding:4px 7px;outline:none;cursor:pointer;}
.btn-row{display:flex;gap:5px;flex-wrap:wrap;align-items:center;}
.file-row{display:flex;align-items:center;gap:8px;}
.fname{font-size:10px;color:var(--ink-faint);flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;}
.incr{display:inline-flex;align-items:center;}
.incr button{font-family:inherit;font-size:12px;font-weight:700;width:22px;height:22px;border:1px solid var(--rim);background:var(--chassis2);color:var(--ink-soft);cursor:pointer;line-height:1;}
.incr button:first-child{border-radius:4px 0 0 4px;border-right:none;}
.incr button:last-child{border-radius:0 4px 4px 0;border-left:none;}
.incr span{font-size:10px;font-weight:600;color:var(--display-fg);background:var(--display-bg);padding:2px 7px;border-top:1px solid var(--rim);border-bottom:1px solid var(--rim);min-width:34px;text-align:center;line-height:18px;}
canvas{display:block;width:100%;background:var(--display-bg);border-radius:4px;}
.hidden{display:none!important;}
</style></head><body>

<!-- SOURCE + PRESET BAR -->
<div class="topbar">
  <button class="btn src-on" id="src-synth" onclick="setSource('synth')">SYNTH</button>
  <button class="btn" id="src-sampler" onclick="setSource('sampler')">SAMPLER</button>
  <select id="preset-sel" onchange="onPresetChange(this.value)" style="flex:1;min-width:0;"></select>
  <button class="btn" onclick="doAction('savePreset')">SAVE</button>
  <button class="btn" id="btn-ren" onclick="doAction('renamePreset')" style="opacity:.4;">REN</button>
  <button class="btn" id="btn-del" onclick="doAction('deletePreset')" style="opacity:.4;">DEL</button>
</div>
<div class="topbar" style="margin-top:-2px;margin-bottom:8px;">
  <button class="toggle" id="btn-enable" onclick="togParam('enabled','btn-enable')">OFF</button>
  <select id="test-note" style="width:56px;">
    <option value="36">C2</option><option value="48">C3</option>
    <option value="60" selected>C4</option><option value="72">C5</option>
  </select>
  <button class="btn" id="btn-test" onclick="doTest()">▶ PLAY</button>
</div>

<!-- OSCILLATOR -->
<div class="section" id="sec-osc">
  <div class="sec-hdr"><span class="sec-label">OSCILLATOR</span></div>
  <div class="params">
    <div class="btn-row" id="wave-btns">
      <button class="btn" id="wv0" onclick="setWave(0)">SINE</button>
      <button class="btn on" id="wv1" onclick="setWave(1)">SAW</button>
      <button class="btn" id="wv2" onclick="setWave(2)">SQR</button>
      <button class="btn" id="wv3" onclick="setWave(3)">TRI</button>
      <button class="btn" id="wv4" onclick="setWave(4)">PULSE</button>
      <button class="btn" id="wv5" onclick="setWave(5)">NOISE</button>
      <button class="btn" id="wv6" onclick="setWave(6)">SSPW</button>
      <button class="btn" id="wv7" onclick="setWave(7)">PLUCK</button>
      <button class="btn" id="wv8" onclick="setWave(8)">WIND</button>
    </div>
    <div class="row hidden" id="row-pw">
      <span class="plabel">PULSE WIDTH</span>
      <input type="range" id="pulseWidth" min="0.05" max="0.95" step="0.01" value="0.5" oninput="emit('pulseWidth',+this.value)">
      <span class="val" id="v-pulseWidth">0.50</span>
    </div>
  </div>
</div>

<!-- SAMPLER -->
<div class="section hidden" id="sec-sampler">
  <div class="sec-hdr"><span class="sec-label">SAMPLER</span></div>
  <div class="params">
    <div class="file-row">
      <span class="plabel" style="white-space:nowrap;">SAMPLE</span>
      <span class="fname" id="sampler-file">(none)</span>
      <button class="btn" onclick="doAction('browseFile')">BROWSE</button>
    </div>
    <div class="row">
      <span class="plabel">ROOT NOTE</span>
      <div class="incr"><button onclick="adjRoot(-1)">-</button><span id="v-rootNote">C4</span><button onclick="adjRoot(1)">+</button></div>
      <span></span>
    </div>
    <div class="row">
      <span class="plabel">FINE TUNE</span>
      <input type="range" id="samplerFineTune" min="-100" max="100" step="0.1" value="0" oninput="emit('samplerFineTune',+this.value)">
      <span class="val" id="v-samplerFineTune">0 ct</span>
    </div>
    <div class="row">
      <span class="plabel">GAIN</span>
      <input type="range" id="samplerGain" min="0" max="2" step="0.01" value="1" oninput="emit('samplerGain',+this.value)">
      <span class="val" id="v-samplerGain">1.00</span>
    </div>
    <div class="btn-row">
      <button class="toggle" id="btn-loop" onclick="togParam('samplerLoop','btn-loop')">LOOP</button>
      <button class="toggle" id="btn-oneshot" onclick="togParam('samplerOneShot','btn-oneshot')">ONE-SHOT</button>
    </div>
  </div>
</div>

<!-- WAVE PREVIEW -->
<div class="section">
  <div class="sec-hdr"><span class="sec-label" id="canvas-lbl">WAVE PREVIEW</span></div>
  <div class="params" style="padding:8px;">
    <canvas id="wf-canvas" height="72"></canvas>
  </div>
</div>

<!-- AUTO PATCH -->
<div class="section">
  <div class="sec-hdr">
    <span class="sec-label">AUTO PATCH</span>
    <button class="toggle" id="btn-ddsp" onclick="togParam('ddspEnabled','btn-ddsp')">OFF</button>
  </div>
  <div class="params">
    <div class="row"><span class="plabel">BLEND</span><input type="range" id="ddspAmount" min="0" max="1" step="0.01" value="0" oninput="emit('ddspAmount',+this.value)"><span class="val" id="v-ddspAmount">0.00</span></div>
    <div class="row"><span class="plabel">BRIGHTNESS</span><input type="range" id="ddspBrightness" min="0" max="1" step="0.01" value="0.5" oninput="emit('ddspBrightness',+this.value)"><span class="val" id="v-ddspBrightness">0.50</span></div>
    <div class="row"><span class="plabel">MOTION</span><input type="range" id="ddspMotion" min="0" max="1" step="0.01" value="0.25" oninput="emit('ddspMotion',+this.value)"><span class="val" id="v-ddspMotion">0.25</span></div>
  </div>
</div>

<!-- UNISON -->
<div class="section">
  <div class="sec-hdr"><span class="sec-label">UNISON</span></div>
  <div class="params">
    <div class="row">
      <span class="plabel">VOICES</span>
      <div class="incr"><button onclick="adjVoices(-1)">-</button><span id="v-unisonVoices">1</span><button onclick="adjVoices(1)">+</button></div>
      <span></span>
    </div>
    <div class="row"><span class="plabel">DETUNE</span><input type="range" id="unisonDetune" min="0" max="100" step="0.1" value="0" oninput="emit('unisonDetune',+this.value)"><span class="val" id="v-unisonDetune">0.0 ct</span></div>
    <div class="row"><span class="plabel">SPREAD</span><input type="range" id="unisonSpread" min="0" max="1" step="0.01" value="0.5" oninput="emit('unisonSpread',+this.value)"><span class="val" id="v-unisonSpread">0.50</span></div>
    <div class="row"><span class="plabel">DRIFT</span><input type="range" id="driftDepth" min="0" max="1" step="0.01" value="0.3" oninput="emit('driftDepth',+this.value)"><span class="val" id="v-driftDepth">0.30</span></div>
  </div>
</div>

<!-- ADSR -->
<div class="section">
  <div class="sec-hdr"><span class="sec-label">ADSR ENVELOPE</span></div>
  <div class="params">
    <canvas id="adsr-canvas" height="52"></canvas>
    <div class="row"><span class="plabel">ATTACK</span><input type="range" id="attack" min="1" max="2000" step="1" value="5" oninput="emit('attack',+this.value)"><span class="val" id="v-attack">5 ms</span></div>
    <div class="row"><span class="plabel">DECAY</span><input type="range" id="decay" min="1" max="2000" step="1" value="80" oninput="emit('decay',+this.value)"><span class="val" id="v-decay">80 ms</span></div>
    <div class="row"><span class="plabel">SUSTAIN</span><input type="range" id="sustain" min="0" max="1" step="0.01" value="0.6" oninput="emit('sustain',+this.value)"><span class="val" id="v-sustain">0.60</span></div>
    <div class="row"><span class="plabel">RELEASE</span><input type="range" id="release" min="1" max="4000" step="1" value="200" oninput="emit('release',+this.value)"><span class="val" id="v-release">200 ms</span></div>
  </div>
</div>

<!-- FILTER -->
<div class="section">
  <div class="sec-hdr"><span class="sec-label">FILTER</span></div>
  <div class="params">
    <div class="btn-row">
      <button class="btn on" id="fm0" onclick="setFiltMode(0)">LADDER</button>
      <button class="btn" id="fm1" onclick="setFiltMode(1)">SVF</button>
      <span style="flex:1;"></span>
      <button class="btn on" id="ft0" onclick="setFiltType(0)">LP</button>
      <button class="btn" id="ft1" onclick="setFiltType(1)">HP</button>
      <button class="btn" id="ft2" onclick="setFiltType(2)">BP</button>
      <button class="btn hidden" id="ft3" onclick="setFiltType(3)">NOTCH</button>
    </div>
    <div class="row"><span class="plabel">CUTOFF</span><input type="range" id="cutoff" min="20" max="20000" step="1" value="4000" oninput="emit('cutoff',+this.value)"><span class="val" id="v-cutoff">4.0k Hz</span></div>
    <div class="row"><span class="plabel">RESONANCE</span><input type="range" id="resonance" min="0" max="1" step="0.01" value="0.3" oninput="emit('resonance',+this.value)"><span class="val" id="v-resonance">0.30</span></div>
    <div class="row"><span class="plabel">ENV DEPTH</span><input type="range" id="filterEnvAmount" min="-1" max="1" step="0.01" value="0.5" oninput="emit('filterEnvAmount',+this.value)"><span class="val" id="v-filterEnvAmount">0.50</span></div>
    <div class="row"><span class="plabel">DRIVE</span><input type="range" id="filterDrive" min="0" max="1" step="0.01" value="0" oninput="emit('filterDrive',+this.value)"><span class="val" id="v-filterDrive">0.00</span></div>
  </div>
</div>

<!-- LFO -->
<div class="section">
  <div class="sec-hdr"><span class="sec-label">LFO</span></div>
  <div class="params">
    <div class="btn-row">
      <button class="btn on" id="lt0" onclick="setLfoTarget(0)">CUT</button>
      <button class="btn" id="lt1" onclick="setLfoTarget(1)">PITCH</button>
      <button class="btn" id="lt2" onclick="setLfoTarget(2)">AMP</button>
      <button class="btn" id="lt3" onclick="setLfoTarget(3)">PW</button>
    </div>
    <div class="btn-row">
      <button class="btn on" id="lw0" onclick="setLfoWave(0)">SINE</button>
      <button class="btn" id="lw1" onclick="setLfoWave(1)">TRI</button>
      <button class="btn" id="lw2" onclick="setLfoWave(2)">SAW</button>
      <button class="btn" id="lw3" onclick="setLfoWave(3)">SQR</button>
      <button class="btn" id="lw4" onclick="setLfoWave(4)">S&H</button>
      <button class="toggle" id="btn-lfoFree" onclick="togParam('lfoFreeRun','btn-lfoFree')" style="margin-left:auto;">SYNC</button>
    </div>
    <div class="row"><span class="plabel">RATE</span><input type="range" id="lfoRate" min="0.1" max="20" step="0.1" value="2" oninput="emit('lfoRate',+this.value)"><span class="val" id="v-lfoRate">2.0 Hz</span></div>
    <div class="row"><span class="plabel">DEPTH</span><input type="range" id="lfoDepth" min="0" max="1" step="0.01" value="0" oninput="emit('lfoDepth',+this.value)"><span class="val" id="v-lfoDepth">0.00</span></div>
    <div class="row"><span class="plabel">FADE IN</span><input type="range" id="lfoFadeIn" min="0" max="2000" step="1" value="0" oninput="emit('lfoFadeIn',+this.value)"><span class="val" id="v-lfoFadeIn">0 ms</span></div>
  </div>
</div>

<!-- PHYSICAL MODEL — PLUCKED -->
<div class="section hidden" id="sec-pluck">
  <div class="sec-hdr"><span class="sec-label">PHYSICAL MODEL — PLUCKED</span></div>
  <div class="params">
    <div class="row"><span class="plabel">DAMPING</span><input type="range" id="ksDamping" min="0" max="0.8" step="0.01" value="0.15" oninput="emit('ksDamping',+this.value)"><span class="val" id="v-ksDamping">0.15</span></div>
    <div class="row"><span class="plabel">DECAY</span><input type="range" id="ksDecay" min="0.99" max="1" step="0.0001" value="0.9985" oninput="emit('ksDecay',+this.value)"><span class="val" id="v-ksDecay">0.9985</span></div>
    <div class="row"><span class="plabel">STIFFNESS</span><input type="range" id="ksStiffness" min="0" max="0.8" step="0.01" value="0.3" oninput="emit('ksStiffness',+this.value)"><span class="val" id="v-ksStiffness">0.30</span></div>
    <div class="row"><span class="plabel">BRIGHTNESS</span><input type="range" id="ksBrightness" min="1000" max="16000" step="100" value="8000" oninput="emit('ksBrightness',+this.value)"><span class="val" id="v-ksBrightness">8000</span></div>
    <div class="row"><span class="plabel">PLUCK POS</span><input type="range" id="ksPluckPos" min="0.02" max="0.5" step="0.01" value="0.1" oninput="emit('ksPluckPos',+this.value)"><span class="val" id="v-ksPluckPos">0.10</span></div>
    <div class="row"><span class="plabel">BODY FREQ</span><input type="range" id="ksBodyFreq" min="60" max="400" step="1" value="180" oninput="emit('ksBodyFreq',+this.value)"><span class="val" id="v-ksBodyFreq">180</span></div>
    <div class="row"><span class="plabel">BODY AMT</span><input type="range" id="ksBodyAmount" min="0" max="0.6" step="0.01" value="0.25" oninput="emit('ksBodyAmount',+this.value)"><span class="val" id="v-ksBodyAmount">0.25</span></div>
  </div>
</div>

<!-- PHYSICAL MODEL — WIND -->
<div class="section hidden" id="sec-wind">
  <div class="sec-hdr"><span class="sec-label">PHYSICAL MODEL — WIND</span></div>
  <div class="params">
    <div class="row"><span class="plabel">BREATH</span><input type="range" id="windBreathPressure" min="0.1" max="1" step="0.01" value="0.5" oninput="emit('windBreathPressure',+this.value)"><span class="val" id="v-windBreathPressure">0.50</span></div>
    <div class="row"><span class="plabel">REED STIFF</span><input type="range" id="windReedStiffness" min="0.1" max="1" step="0.01" value="0.7" oninput="emit('windReedStiffness',+this.value)"><span class="val" id="v-windReedStiffness">0.70</span></div>
    <div class="row"><span class="plabel">BORE LOSS</span><input type="range" id="windBoreLoss" min="0.01" max="0.5" step="0.01" value="0.15" oninput="emit('windBoreLoss',+this.value)"><span class="val" id="v-windBoreLoss">0.15</span></div>
    <div class="row"><span class="plabel">BREATH NOISE</span><input type="range" id="windNoiseAmount" min="0" max="0.2" step="0.005" value="0.03" oninput="emit('windNoiseAmount',+this.value)"><span class="val" id="v-windNoiseAmount">0.030</span></div>
  </div>
</div>

<script>
const S={
  enabled:false,waveform:1,pulseWidth:0.5,
  unisonVoices:1,unisonDetune:0,unisonSpread:0.5,driftDepth:0.3,
  attack:5,decay:80,sustain:0.6,release:200,
  cutoff:4000,resonance:0.3,filterMode:0,filterType:0,filterDrive:0,filterEnvAmount:0.5,
  lfoRate:2,lfoDepth:0,lfoTarget:0,lfoWaveform:0,lfoFreeRun:false,lfoFadeIn:0,
  ddspEnabled:false,ddspAmount:0,ddspBrightness:0.5,ddspMotion:0.25,
  ksDamping:0.15,ksDecay:0.9985,ksStiffness:0.3,ksBrightness:8000,
  ksPluckPos:0.10,ksBodyFreq:180,ksBodyAmount:0.25,
  windBreathPressure:0.5,windReedStiffness:0.7,windBoreLoss:0.15,windNoiseAmount:0.03,
  samplerRootNote:60,samplerFineTune:0,samplerGain:1,samplerLoop:false,samplerOneShot:false,
  sourceMode:'synth',previewPlaying:false,
  presets:[],factoryCount:0,selectedPreset:'',
  wfMin:[],wfMax:[],wfAtkFrac:0,wfDecFrac:0,wfRelFrac:0
};

const NOTES=['C','C#','D','D#','E','F','F#','G','G#','A','A#','B'];
const midiName=n=>NOTES[n%12]+String(Math.floor(n/12)-1);

const fmt={
  pulseWidth:v=>v.toFixed(2),
  unisonDetune:v=>v.toFixed(1)+' ct',
  unisonSpread:v=>v.toFixed(2),
  driftDepth:v=>v.toFixed(2),
  attack:v=>v.toFixed(0)+' ms',
  decay:v=>v.toFixed(0)+' ms',
  sustain:v=>v.toFixed(2),
  release:v=>v.toFixed(0)+' ms',
  cutoff:v=>v>=1000?(v/1000).toFixed(1)+'k Hz':v.toFixed(0)+' Hz',
  resonance:v=>v.toFixed(2),
  filterEnvAmount:v=>v.toFixed(2),
  filterDrive:v=>v.toFixed(2),
  lfoRate:v=>v.toFixed(1)+' Hz',
  lfoDepth:v=>v.toFixed(2),
  lfoFadeIn:v=>v.toFixed(0)+' ms',
  ddspAmount:v=>v.toFixed(2),
  ddspBrightness:v=>v.toFixed(2),
  ddspMotion:v=>v.toFixed(2),
  samplerFineTune:v=>v.toFixed(1)+' ct',
  samplerGain:v=>v.toFixed(2),
  ksDamping:v=>v.toFixed(2),
  ksDecay:v=>v.toFixed(4),
  ksStiffness:v=>v.toFixed(2),
  ksBrightness:v=>v.toFixed(0),
  ksPluckPos:v=>v.toFixed(2),
  ksBodyFreq:v=>v.toFixed(0),
  ksBodyAmount:v=>v.toFixed(2),
  windBreathPressure:v=>v.toFixed(2),
  windReedStiffness:v=>v.toFixed(2),
  windBoreLoss:v=>v.toFixed(2),
  windNoiseAmount:v=>v.toFixed(3)
};

function emit(key,value){
  S[key]=value;
  const ve=document.getElementById('v-'+key);
  if(ve&&fmt[key])ve.textContent=fmt[key](value);
  window.__JUCE__.backend.emitEvent('synthChange',{key,value});
  if(key==='attack'||key==='decay'||key==='sustain'||key==='release')drawAdsr();
}

function togParam(key,btnId){
  S[key]=!S[key];
  syncToggle(key,btnId);
  window.__JUCE__.backend.emitEvent('synthChange',{key,value:S[key]?1:0});
}

function syncToggle(key,btnId){
  const btn=document.getElementById(btnId);if(!btn)return;
  const on=S[key];
  if(on)btn.classList.add('on');else btn.classList.remove('on');
  if(btnId==='btn-enable')btn.textContent=on?'ENABLED':'OFF';
  else if(btnId==='btn-lfoFree')btn.textContent=on?'FREE':'SYNC';
  else if(btnId==='btn-ddsp')btn.textContent=on?'ON':'OFF';
  else if(btnId==='btn-loop')btn.textContent='LOOP';
  else if(btnId==='btn-oneshot')btn.textContent='ONE-SHOT';
  else btn.textContent=on?'ON':'OFF';
}

function setWave(w){
  for(let i=0;i<9;i++){const b=document.getElementById('wv'+i);if(b)b.classList.remove('on');}
  const bw=document.getElementById('wv'+w);if(bw)bw.classList.add('on');
  S.waveform=w;
  document.getElementById('row-pw').classList.toggle('hidden',w!==4);
  document.getElementById('sec-pluck').classList.toggle('hidden',w!==7);
  document.getElementById('sec-wind').classList.toggle('hidden',w!==8);
  window.__JUCE__.backend.emitEvent('synthChange',{key:'waveform',value:w});
}

function setFiltMode(m){
  [0,1].forEach(i=>document.getElementById('fm'+i).classList.remove('on'));
  document.getElementById('fm'+m).classList.add('on');
  S.filterMode=m;
  document.getElementById('ft3').classList.toggle('hidden',m!==1);
  if(m===0&&S.filterType===3){setFiltType(0);return;}
  window.__JUCE__.backend.emitEvent('synthChange',{key:'filterMode',value:m});
}
function setFiltType(t){
  [0,1,2,3].forEach(i=>{const e=document.getElementById('ft'+i);if(e)e.classList.remove('on');});
  const fb=document.getElementById('ft'+t);if(fb)fb.classList.add('on');
  S.filterType=t;
  window.__JUCE__.backend.emitEvent('synthChange',{key:'filterType',value:t});
}

function setLfoTarget(t){
  [0,1,2,3].forEach(i=>document.getElementById('lt'+i).classList.remove('on'));
  document.getElementById('lt'+t).classList.add('on');
  S.lfoTarget=t;
  window.__JUCE__.backend.emitEvent('synthChange',{key:'lfoTarget',value:t});
}
function setLfoWave(w){
  [0,1,2,3,4].forEach(i=>document.getElementById('lw'+i).classList.remove('on'));
  document.getElementById('lw'+w).classList.add('on');
  S.lfoWaveform=w;
  window.__JUCE__.backend.emitEvent('synthChange',{key:'lfoWaveform',value:w});
}

function adjVoices(d){
  S.unisonVoices=Math.max(1,Math.min(8,S.unisonVoices+d));
  document.getElementById('v-unisonVoices').textContent=S.unisonVoices;
  window.__JUCE__.backend.emitEvent('synthChange',{key:'unisonVoices',value:S.unisonVoices});
}
function adjRoot(d){
  S.samplerRootNote=Math.max(0,Math.min(127,S.samplerRootNote+d));
  document.getElementById('v-rootNote').textContent=midiName(S.samplerRootNote);
  window.__JUCE__.backend.emitEvent('synthChange',{key:'samplerRootNote',value:S.samplerRootNote});
}

function setSource(src){
  S.sourceMode=src;
  document.getElementById('src-synth').classList.toggle('src-on',src==='synth');
  document.getElementById('src-sampler').classList.toggle('src-on',src==='sampler');
  document.getElementById('sec-osc').classList.toggle('hidden',src==='sampler');
  document.getElementById('sec-sampler').classList.toggle('hidden',src==='synth');
  document.getElementById('canvas-lbl').textContent=src==='synth'?'WAVE PREVIEW':'SAMPLE PREVIEW';
  window.__JUCE__.backend.emitEvent('synthAction',{action:'setSource',value:src});
}

function onPresetChange(name){
  S.selectedPreset=name;
  updatePresetBtns();
  if(!name)return;
  window.__JUCE__.backend.emitEvent('synthAction',{action:'loadPreset',value:name});
}
function doAction(action){
  window.__JUCE__.backend.emitEvent('synthAction',{action,value:S.selectedPreset});
}
function updatePresetBtns(){
  const idx=S.presets.findIndex(p=>p.name===S.selectedPreset);
  const isCustom=idx>=S.factoryCount&&idx>=0;
  document.getElementById('btn-ren').style.opacity=isCustom?'1':'0.4';
  document.getElementById('btn-del').style.opacity=isCustom?'1':'0.4';
}

function doTest(){
  if(S.previewPlaying){
    S.previewPlaying=false;
    const b=document.getElementById('btn-test');
    b.textContent='▶ PLAY';b.classList.remove('on');
    window.__JUCE__.backend.emitEvent('synthAction',{action:'stopPreview',value:0});
  } else {
    S.previewPlaying=true;
    const b=document.getElementById('btn-test');
    b.textContent='■ STOP';b.classList.add('on');
    const note=parseInt(document.getElementById('test-note').value)||60;
    window.__JUCE__.backend.emitEvent('synthAction',{action:'preview',value:note});
  }
}

function drawAdsr(){
  const c=document.getElementById('adsr-canvas');
  c.width=c.clientWidth||300;c.height=52;
  const ctx=c.getContext('2d');
  const W=c.width,H=c.height,pad=6;
  ctx.fillStyle='#0d1410';ctx.fillRect(0,0,W,H);
  ctx.strokeStyle='rgba(185,255,102,0.12)';ctx.lineWidth=0.5;
  ctx.beginPath();ctx.moveTo(0,H/2);ctx.lineTo(W,H/2);ctx.stroke();
  const a=S.attack,d=S.decay,s=S.sustain,r=S.release;
  const total=a+d+(r*0.6)+50;
  const sc=(W-2*pad)/total;
  const x0=pad,xA=x0+a*sc,xD=xA+d*sc,xS=xD+50*sc,xR=xS+r*0.6*sc;
  const top=pad,bot=H-pad,sY=top+(1-s)*(bot-top);
  ctx.beginPath();ctx.moveTo(x0,bot);ctx.lineTo(xA,top);ctx.lineTo(xD,sY);ctx.lineTo(xS,sY);ctx.lineTo(xR,bot);
  ctx.strokeStyle='#b9ff66';ctx.lineWidth=1.5;ctx.stroke();
  ctx.fillStyle='rgba(185,255,102,0.35)';ctx.font='7px monospace';
  ctx.fillText('A',x0+2,H-3);ctx.fillText('D',xA+2,H-3);
  ctx.fillText('S',xD+2,H-3);ctx.fillText('R',xS+2,H-3);
}

function drawWaveform(){
  const c=document.getElementById('wf-canvas');
  c.width=c.clientWidth||300;c.height=72;
  const ctx=c.getContext('2d');
  const W=c.width,H=c.height;
  ctx.fillStyle='#0d1410';ctx.fillRect(0,0,W,H);
  ctx.strokeStyle='rgba(185,255,102,0.12)';ctx.lineWidth=0.5;
  ctx.beginPath();ctx.moveTo(0,H/2);ctx.lineTo(W,H/2);ctx.stroke();
  const n=S.wfMin.length;if(!n)return;
  const mid=H/2,sc=H*0.44;
  for(let i=0;i<n;i++){
    const x=i/n*W;
    const frac=i/n;
    ctx.strokeStyle=frac<S.wfAtkFrac?'#44dd88':frac<S.wfDecFrac?'#ddaa33':frac<S.wfRelFrac?'#3498db':'#dd4444';
    ctx.lineWidth=1;
    ctx.beginPath();ctx.moveTo(x,mid-S.wfMax[i]*sc);ctx.lineTo(x,mid-S.wfMin[i]*sc);ctx.stroke();
  }
}

// C++ → JS events
window.__JUCE__.backend.addEventListener('synthLoad',function(p){
  // Booleans via toggle sync
  if(p.enabled!==undefined){S.enabled=Boolean(p.enabled);syncToggle('enabled','btn-enable');}
  if(p.ddspEnabled!==undefined){S.ddspEnabled=Boolean(p.ddspEnabled);syncToggle('ddspEnabled','btn-ddsp');}
  if(p.lfoFreeRun!==undefined){S.lfoFreeRun=Boolean(p.lfoFreeRun);syncToggle('lfoFreeRun','btn-lfoFree');}
  if(p.samplerLoop!==undefined){S.samplerLoop=Boolean(p.samplerLoop);syncToggle('samplerLoop','btn-loop');}
  if(p.samplerOneShot!==undefined){S.samplerOneShot=Boolean(p.samplerOneShot);syncToggle('samplerOneShot','btn-oneshot');}
  // Enum buttons
  if(p.waveform!==undefined)setWave(p.waveform);
  if(p.filterMode!==undefined)setFiltMode(p.filterMode);
  if(p.filterType!==undefined)setFiltType(p.filterType);
  if(p.lfoTarget!==undefined)setLfoTarget(p.lfoTarget);
  if(p.lfoWaveform!==undefined)setLfoWave(p.lfoWaveform);
  // Inc/dec
  if(p.unisonVoices!==undefined){S.unisonVoices=p.unisonVoices;document.getElementById('v-unisonVoices').textContent=p.unisonVoices;}
  if(p.samplerRootNote!==undefined){S.samplerRootNote=p.samplerRootNote;document.getElementById('v-rootNote').textContent=midiName(p.samplerRootNote);}
  // Source mode
  if(p.sourceMode)setSource(p.sourceMode);
  // Regular sliders
  const sliders=['pulseWidth','unisonDetune','unisonSpread','driftDepth',
                 'attack','decay','sustain','release',
                 'cutoff','resonance','filterEnvAmount','filterDrive',
                 'lfoRate','lfoDepth','lfoFadeIn',
                 'ddspAmount','ddspBrightness','ddspMotion',
                 'ksDamping','ksDecay','ksStiffness','ksBrightness','ksPluckPos','ksBodyFreq','ksBodyAmount',
                 'windBreathPressure','windReedStiffness','windBoreLoss','windNoiseAmount',
                 'samplerFineTune','samplerGain'];
  for(const k of sliders){
    if(p[k]===undefined)continue;
    S[k]=p[k];
    const el=document.getElementById(k);if(el)el.value=p[k];
    const ve=document.getElementById('v-'+k);if(ve&&fmt[k])ve.textContent=fmt[k](p[k]);
  }
  drawAdsr();
});

window.__JUCE__.backend.addEventListener('presetsLoaded',function(p){
  S.presets=p.presets||[];S.factoryCount=p.factoryCount||0;
  const sel=document.getElementById('preset-sel');
  sel.innerHTML='<option value="">-- Select Preset --</option>';
  for(const pr of S.presets){
    const opt=document.createElement('option');
    opt.value=pr.name;opt.textContent=pr.name;
    if(pr.name===p.selected)opt.selected=true;
    sel.appendChild(opt);
  }
  S.selectedPreset=p.selected||'';
  updatePresetBtns();
});

window.__JUCE__.backend.addEventListener('waveformLoaded',function(p){
  S.wfMin=p.min||[];S.wfMax=p.max||[];
  S.wfAtkFrac=p.attackFrac||0;S.wfDecFrac=p.decayFrac||0;S.wfRelFrac=p.releaseFrac||0;
  drawWaveform();
});

window.__JUCE__.backend.addEventListener('previewStopped',function(){
  S.previewPlaying=false;
  const b=document.getElementById('btn-test');
  b.textContent='▶ PLAY';b.classList.remove('on');
});

window.__JUCE__.backend.addEventListener('samplerInfo',function(p){
  if(p.fileName)document.getElementById('sampler-file').textContent=p.fileName;
  if(p.sourceMode)setSource(p.sourceMode);
});

// Initial draw
drawAdsr();
drawWaveform();
window.addEventListener('resize',()=>{drawAdsr();drawWaveform();});
</script>
</body></html>)HTML";
    }
} // namespace SynthEditorHelper

// ---------------------------------------------------------------------------
// Content panel — WebBrowserComponent-based
// ---------------------------------------------------------------------------
class SynthEditorContent : public juce::Component,
                           private juce::Timer
{
public:
    std::function<void()>                            onParamsChanged;
    std::function<void(const SynthParams&, int)>     onPreviewRequested;
    std::function<void()>                            onStopPreviewRequested;
    std::function<bool()>                            onIsPreviewActive;
    std::function<void(const SynthParams&)>          onSavePresetRequested;
    std::function<void(const juce::String&)>         onRenamePresetRequested;
    std::function<void(const juce::String&)>         onDeletePresetRequested;

    SynthEditorContent() : browser_(buildOptions(this))
    {
        addAndMakeVisible(browser_);
        browser_.goToURL(juce::WebBrowserComponent::getResourceProviderRoot());
    }

    ~SynthEditorContent() override { stopTimer(); }

    void loadParams(const SynthParams& p)
    {
        cachedParams_ = p;
        pushParamsToJs();
        pushWaveformToJs();
    }

    void applyToParams(SynthParams& p) const { p = cachedParams_; }

    void loadSamplerData(ChannelSourceType srcType, const SamplerParams& sp)
    {
        currentSourceType_    = srcType;
        currentSamplerParams_ = sp;
        pushParamsToJs();
        pushWaveformToJs();
    }

    void setSamplerPreviewBuffer(std::shared_ptr<const juce::AudioBuffer<float>> buf)
    {
        samplerPreviewBuffer_ = std::move(buf);
        pushWaveformToJs();
    }

    SamplerParams     getSamplerParams() const { return currentSamplerParams_; }
    ChannelSourceType getSourceType()    const { return currentSourceType_; }

    void setAvailablePresets(const std::vector<SynthPresets::Preset>& presets,
                             const juce::String& selectName = {})
    {
        availablePresets_   = presets;
        factoryPresetCount_ = (int)SynthPresets::getAll().size();
        selectedPresetName_ = selectName;

        auto* obj = new juce::DynamicObject();
        juce::Array<juce::var> arr;
        for (const auto& pr : availablePresets_)
        {
            auto* po = new juce::DynamicObject();
            po->setProperty("name", pr.name);
            arr.add(juce::var(po));
        }
        obj->setProperty("presets",      arr);
        obj->setProperty("factoryCount", factoryPresetCount_);
        obj->setProperty("selected",     selectName);
        browser_.emitEventIfBrowserIsVisible("presetsLoaded", juce::var(obj));
    }

    void resized() override { browser_.setBounds(getLocalBounds()); }

private:
    SynthParams       cachedParams_;
    SamplerParams     currentSamplerParams_ = {};
    ChannelSourceType currentSourceType_    = ChannelSourceType::Synth;

    std::vector<SynthPresets::Preset> availablePresets_;
    int               factoryPresetCount_ = 0;
    juce::String      selectedPresetName_;

    juce::WebBrowserComponent                       browser_;
    std::shared_ptr<const juce::AudioBuffer<float>> samplerPreviewBuffer_;
    std::shared_ptr<juce::FileChooser>              fileChooser_;
    bool previewPlaying_ = false;

    void timerCallback() override
    {
        if (!previewPlaying_) { stopTimer(); return; }
        const bool still = onIsPreviewActive ? onIsPreviewActive() : false;
        if (!still)
        {
            previewPlaying_ = false;
            stopTimer();
            browser_.emitEventIfBrowserIsVisible("previewStopped", juce::var());
        }
    }

    void pushParamsToJs()
    {
        auto& p  = cachedParams_;
        auto& sp = currentSamplerParams_;
        auto* obj = new juce::DynamicObject();
        obj->setProperty("enabled",           p.enabled           ? 1 : 0);
        obj->setProperty("waveform",          p.waveform);
        obj->setProperty("pulseWidth",        (double)p.pulseWidth);
        obj->setProperty("unisonVoices",      p.unisonVoices);
        obj->setProperty("unisonDetune",      (double)p.unisonDetune);
        obj->setProperty("unisonSpread",      (double)p.unisonSpread);
        obj->setProperty("driftDepth",        (double)p.driftDepth);
        obj->setProperty("attack",            (double)p.attack);
        obj->setProperty("decay",             (double)p.decay);
        obj->setProperty("sustain",           (double)p.sustain);
        obj->setProperty("release",           (double)p.release);
        obj->setProperty("cutoff",            (double)p.cutoff);
        obj->setProperty("resonance",         (double)p.resonance);
        obj->setProperty("filterMode",        p.filterMode);
        obj->setProperty("filterType",        p.filterType);
        obj->setProperty("filterDrive",       (double)p.filterDrive);
        obj->setProperty("filterEnvAmount",   (double)p.filterEnvAmount);
        obj->setProperty("lfoRate",           (double)p.lfoRate);
        obj->setProperty("lfoDepth",          (double)p.lfoDepth);
        obj->setProperty("lfoTarget",         p.lfoTarget);
        obj->setProperty("lfoWaveform",       p.lfoWaveform);
        obj->setProperty("lfoFreeRun",        p.lfoFreeRun       ? 1 : 0);
        obj->setProperty("lfoFadeIn",         (double)p.lfoFadeIn);
        obj->setProperty("ddspEnabled",       p.ddspAuto.enabled ? 1 : 0);
        obj->setProperty("ddspAmount",        (double)p.ddspAuto.amount);
        obj->setProperty("ddspBrightness",    (double)p.ddspAuto.brightness);
        obj->setProperty("ddspMotion",        (double)p.ddspAuto.motion);
        obj->setProperty("ksDamping",         (double)p.ksDamping);
        obj->setProperty("ksDecay",           (double)p.ksDecay);
        obj->setProperty("ksStiffness",       (double)p.ksStiffness);
        obj->setProperty("ksBrightness",      (double)p.ksBrightness);
        obj->setProperty("ksPluckPos",        (double)p.ksPluckPos);
        obj->setProperty("ksBodyFreq",        (double)p.ksBodyFreq);
        obj->setProperty("ksBodyAmount",      (double)p.ksBodyAmount);
        obj->setProperty("windBreathPressure",(double)p.windBreathPressure);
        obj->setProperty("windReedStiffness", (double)p.windReedStiffness);
        obj->setProperty("windBoreLoss",      (double)p.windBoreLoss);
        obj->setProperty("windNoiseAmount",   (double)p.windNoiseAmount);
        obj->setProperty("samplerRootNote",   sp.rootNote);
        obj->setProperty("samplerFineTune",   (double)sp.fineTuneCents);
        obj->setProperty("samplerGain",       (double)sp.gain);
        obj->setProperty("samplerLoop",       sp.loopEnabled     ? 1 : 0);
        obj->setProperty("samplerOneShot",    sp.oneShot         ? 1 : 0);
        obj->setProperty("sourceMode",        currentSourceType_ == ChannelSourceType::Synth
                                              ? juce::String("synth") : juce::String("sampler"));
        browser_.emitEventIfBrowserIsVisible("synthLoad", juce::var(obj));
    }

    void pushWaveformToJs()
    {
        SynthPreview::WaveformData data;
        if (currentSourceType_ == ChannelSourceType::Sampler && samplerPreviewBuffer_)
            data = SynthPreview::renderWaveformDataFromSampler(
                cachedParams_, currentSamplerParams_, samplerPreviewBuffer_, 256);
        else
            data = SynthPreview::renderWaveformData(cachedParams_, 256);

        auto* obj = new juce::DynamicObject();
        juce::Array<juce::var> minArr, maxArr;
        for (size_t i = 0; i < data.minVals.size(); ++i)
        {
            minArr.add((double)data.minVals[i]);
            maxArr.add((double)data.maxVals[i]);
        }
        obj->setProperty("min",         minArr);
        obj->setProperty("max",         maxArr);
        obj->setProperty("attackFrac",  (double)data.attackFrac);
        obj->setProperty("decayFrac",   (double)data.decayFrac);
        obj->setProperty("releaseFrac", (double)data.releaseFrac);
        browser_.emitEventIfBrowserIsVisible("waveformLoaded", juce::var(obj));
    }

    static juce::WebBrowserComponent::Options buildOptions(SynthEditorContent* self)
    {
        using WBC = juce::WebBrowserComponent;
        return WBC::Options{}
            .withNativeIntegrationEnabled()
            .withKeepPageLoadedWhenBrowserIsHidden()
            .withResourceProvider([](const juce::String& url) -> std::optional<WBC::Resource> {
                if (url == "/" || url == "/index.html")
                    return WBC::Resource{ SynthEditorHelper::toBytes(SynthEditorHelper::buildHtml()),
                                         "text/html" };
                if (url == "/font-regular.ttf")
                    return WBC::Resource{ { reinterpret_cast<const std::byte*>(BinaryData::JetBrainsMonoRegular_ttf),
                                           reinterpret_cast<const std::byte*>(BinaryData::JetBrainsMonoRegular_ttf + BinaryData::JetBrainsMonoRegular_ttfSize) },
                                         "font/truetype" };
                if (url == "/font-bold.ttf")
                    return WBC::Resource{ { reinterpret_cast<const std::byte*>(BinaryData::JetBrainsMonoBold_ttf),
                                           reinterpret_cast<const std::byte*>(BinaryData::JetBrainsMonoBold_ttf + BinaryData::JetBrainsMonoBold_ttfSize) },
                                         "font/truetype" };
                return std::nullopt;
            })
            .withEventListener("synthChange", [self](const juce::var& d) {
                const auto key = d["key"].toString();
                const float val = (float)(double)d["value"];
                auto& p  = self->cachedParams_;
                auto& sp = self->currentSamplerParams_;
                if      (key == "enabled")            p.enabled              = val > 0.5f;
                else if (key == "waveform")           p.waveform             = (int)val;
                else if (key == "pulseWidth")         p.pulseWidth           = val;
                else if (key == "unisonVoices")       p.unisonVoices         = (int)val;
                else if (key == "unisonDetune")       p.unisonDetune         = val;
                else if (key == "unisonSpread")       p.unisonSpread         = val;
                else if (key == "driftDepth")         p.driftDepth           = val;
                else if (key == "attack")             p.attack               = val;
                else if (key == "decay")              p.decay                = val;
                else if (key == "sustain")            p.sustain              = val;
                else if (key == "release")            p.release              = val;
                else if (key == "cutoff")             p.cutoff               = val;
                else if (key == "resonance")          p.resonance            = val;
                else if (key == "filterMode")         p.filterMode           = (int)val;
                else if (key == "filterType")         p.filterType           = (int)val;
                else if (key == "filterDrive")        p.filterDrive          = val;
                else if (key == "filterEnvAmount")    p.filterEnvAmount      = val;
                else if (key == "lfoRate")            p.lfoRate              = val;
                else if (key == "lfoDepth")           p.lfoDepth             = val;
                else if (key == "lfoTarget")          p.lfoTarget            = (int)val;
                else if (key == "lfoWaveform")        p.lfoWaveform          = (int)val;
                else if (key == "lfoFreeRun")         p.lfoFreeRun           = val > 0.5f;
                else if (key == "lfoFadeIn")          p.lfoFadeIn            = val;
                else if (key == "ddspEnabled")        p.ddspAuto.enabled     = val > 0.5f;
                else if (key == "ddspAmount")         p.ddspAuto.amount      = val;
                else if (key == "ddspBrightness")     p.ddspAuto.brightness  = val;
                else if (key == "ddspMotion")         p.ddspAuto.motion      = val;
                else if (key == "ksDamping")          p.ksDamping            = val;
                else if (key == "ksDecay")            p.ksDecay              = val;
                else if (key == "ksStiffness")        p.ksStiffness          = val;
                else if (key == "ksBrightness")       p.ksBrightness         = val;
                else if (key == "ksPluckPos")         p.ksPluckPos           = val;
                else if (key == "ksBodyFreq")         p.ksBodyFreq           = val;
                else if (key == "ksBodyAmount")       p.ksBodyAmount         = val;
                else if (key == "windBreathPressure") p.windBreathPressure   = val;
                else if (key == "windReedStiffness")  p.windReedStiffness    = val;
                else if (key == "windBoreLoss")       p.windBoreLoss         = val;
                else if (key == "windNoiseAmount")    p.windNoiseAmount      = val;
                else if (key == "samplerRootNote")    sp.rootNote            = (int)val;
                else if (key == "samplerFineTune")    sp.fineTuneCents       = val;
                else if (key == "samplerGain")        sp.gain                = val;
                else if (key == "samplerLoop")        sp.loopEnabled         = val > 0.5f;
                else if (key == "samplerOneShot")     sp.oneShot             = val > 0.5f;
                if (self->onParamsChanged) self->onParamsChanged();
                if (key == "waveform" || key == "attack" || key == "decay" ||
                    key == "sustain"  || key == "release")
                    self->pushWaveformToJs();
            })
            .withEventListener("synthAction", [self](const juce::var& d) {
                const auto action = d["action"].toString();
                if (action == "preview")
                {
                    if (self->onPreviewRequested)
                        self->onPreviewRequested(self->cachedParams_, (int)(double)d["value"]);
                    self->previewPlaying_ = true;
                    self->startTimer(80);
                }
                else if (action == "stopPreview")
                {
                    if (self->onStopPreviewRequested) self->onStopPreviewRequested();
                    self->previewPlaying_ = false;
                    self->stopTimer();
                }
                else if (action == "savePreset")
                {
                    if (self->onSavePresetRequested)
                        self->onSavePresetRequested(self->cachedParams_);
                }
                else if (action == "renamePreset")
                {
                    const auto name = d["value"].toString();
                    if (self->onRenamePresetRequested && name.isNotEmpty())
                        self->onRenamePresetRequested(name);
                }
                else if (action == "deletePreset")
                {
                    const auto name = d["value"].toString();
                    if (self->onDeletePresetRequested && name.isNotEmpty())
                        self->onDeletePresetRequested(name);
                }
                else if (action == "loadPreset")
                {
                    const auto name = d["value"].toString();
                    for (const auto& pr : self->availablePresets_)
                    {
                        if (pr.name == name)
                        {
                            self->cachedParams_      = pr.params;
                            self->selectedPresetName_ = name;
                            self->pushParamsToJs();
                            self->pushWaveformToJs();
                            if (self->onParamsChanged) self->onParamsChanged();
                            break;
                        }
                    }
                }
                else if (action == "setSource")
                {
                    const auto src = d["value"].toString();
                    self->currentSourceType_ = (src == "sampler")
                        ? ChannelSourceType::Sampler : ChannelSourceType::Synth;
                    if (self->onParamsChanged) self->onParamsChanged();
                    self->pushWaveformToJs();
                }
                else if (action == "browseFile")
                {
                    self->fileChooser_ = std::make_shared<juce::FileChooser>(
                        "Select sample...",
                        juce::File::getSpecialLocation(juce::File::userHomeDirectory),
                        "*.wav;*.aiff;*.aif;*.flac;*.mp3");
                    self->fileChooser_->launchAsync(
                        juce::FileBrowserComponent::openMode |
                        juce::FileBrowserComponent::canSelectFiles,
                        [self](const juce::FileChooser& fc)
                        {
                            auto results = fc.getResults();
                            if (results.isEmpty()) return;
                            const auto file = results[0];
                            self->currentSamplerParams_.samplePath = file.getFullPathName();
                            if (self->onParamsChanged) self->onParamsChanged();
                            auto* info = new juce::DynamicObject();
                            info->setProperty("fileName", file.getFileName());
                            self->browser_.emitEventIfBrowserIsVisible("samplerInfo", juce::var(info));
                        });
                }
            });
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SynthEditorContent)
};

// ---------------------------------------------------------------------------
// SynthEditorPanel — thin wrapper, same public API as before
// ---------------------------------------------------------------------------
class SynthEditorPanel : public juce::Component
{
public:
    std::function<void()>                            onParamsChanged;
    std::function<void(const SynthParams&, int)>     onPreviewRequested;
    std::function<void()>                            onStopPreviewRequested;
    std::function<bool()>                            onIsPreviewActive;
    std::function<void(const SynthParams&)>          onSavePresetRequested;
    std::function<void(const juce::String&)>         onRenamePresetRequested;
    std::function<void(const juce::String&)>         onDeletePresetRequested;

    SynthEditorPanel()
    {
        content_.onParamsChanged         = [this] { if (onParamsChanged)         onParamsChanged(); };
        content_.onPreviewRequested      = [this](const SynthParams& p, int n)   { if (onPreviewRequested)      onPreviewRequested(p, n); };
        content_.onStopPreviewRequested  = [this]                                { if (onStopPreviewRequested)  onStopPreviewRequested(); };
        content_.onIsPreviewActive       = [this]() -> bool                      { return onIsPreviewActive ? onIsPreviewActive() : false; };
        content_.onSavePresetRequested   = [this](const SynthParams& p)          { if (onSavePresetRequested)   onSavePresetRequested(p); };
        content_.onRenamePresetRequested = [this](const juce::String& s)         { if (onRenamePresetRequested) onRenamePresetRequested(s); };
        content_.onDeletePresetRequested = [this](const juce::String& s)         { if (onDeletePresetRequested) onDeletePresetRequested(s); };
        addAndMakeVisible(content_);
    }

    void resized() override { content_.setBounds(getLocalBounds()); }

    void loadParams(const SynthParams& p)                  { content_.loadParams(p); }
    void applyToParams(SynthParams& p)              const  { content_.applyToParams(p); }
    void loadSamplerData(ChannelSourceType t, const SamplerParams& sp) { content_.loadSamplerData(t, sp); }
    void setSamplerPreviewBuffer(std::shared_ptr<const juce::AudioBuffer<float>> buf)
                                                           { content_.setSamplerPreviewBuffer(std::move(buf)); }
    SamplerParams     getSamplerParams() const              { return content_.getSamplerParams(); }
    ChannelSourceType getSourceType()    const              { return content_.getSourceType(); }
    void setAvailablePresets(const std::vector<SynthPresets::Preset>& presets,
                             const juce::String& selectName = {})
    {
        content_.setAvailablePresets(presets, selectName);
    }

private:
    SynthEditorContent content_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SynthEditorPanel)
};

// ---------------------------------------------------------------------------
// Floating window — unchanged API
// ---------------------------------------------------------------------------
class SynthEditorWindow : public juce::DocumentWindow
{
public:
    SynthEditorPanel panel;

    SynthEditorWindow()
        : juce::DocumentWindow("Synth Editor",
                               juce::Colour(0xff2a2218),
                               juce::DocumentWindow::closeButton)
    {
        setContentNonOwned(&panel, false);
        setResizable(true, false);
        setSize(470, 660);
    }

    void setChannelName(const juce::String& name) { setName("Synth  \xe2\x80\x94  " + name); }
    void closeButtonPressed() override { setVisible(false); }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SynthEditorWindow)
};
