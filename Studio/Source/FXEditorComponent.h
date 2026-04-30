/*
  ==============================================================================
    FXEditorComponent.h  — M14 FX chain editor (Phase-6 WebView)
    Floating window per mixer track: Compressor, Delay, Reverb
    UI served as inline HTML via WebBrowserComponent + ResourceProvider.
  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>
#include <functional>
#include "ProjectModel.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
namespace FXEditorHelper
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
body{
  font-family:'JBMono',Monaco,'Courier New',monospace;
  background:linear-gradient(180deg,var(--chassis),var(--chassis2));
  color:var(--ink);padding:14px;min-height:100vh;
}
.section{
  background:var(--panel);border:1px solid var(--rim);
  border-radius:7px;margin-bottom:12px;overflow:hidden;
}
.sec-hdr{
  display:flex;align-items:center;gap:8px;padding:9px 14px;
  background:linear-gradient(180deg,var(--chassis),var(--chassis2));
  border-bottom:1px solid var(--rim);
}
.sec-label{
  font-size:11px;font-weight:700;letter-spacing:.18em;
  color:var(--ink-soft);text-transform:uppercase;flex:1;
}
.toggle{
  font-family:inherit;font-size:10px;font-weight:700;letter-spacing:.13em;
  padding:4px 13px;border-radius:4px;border:1px solid var(--rim);
  cursor:pointer;background:var(--chassis2);color:var(--ink-faint);
  transition:all .12s;
}
.toggle.on{
  background:var(--accent);border-color:var(--accent);
  color:#fff;box-shadow:0 0 7px rgba(216,65,42,.4);
}
.params{padding:12px 14px;display:flex;flex-direction:column;gap:10px;}
.row{
  display:grid;grid-template-columns:118px 1fr 64px;
  align-items:center;gap:10px;
}
.plabel{
  font-size:10px;font-weight:600;letter-spacing:.09em;
  color:var(--ink-faint);text-transform:uppercase;
}
input[type=range]{
  -webkit-appearance:none;appearance:none;
  width:100%;height:5px;background:var(--chassis2);
  border:1px solid var(--rim);border-radius:3px;outline:none;
}
input[type=range]::-webkit-slider-thumb{
  -webkit-appearance:none;width:15px;height:15px;border-radius:50%;
  background:var(--accent);cursor:pointer;
  box-shadow:0 0 5px rgba(216,65,42,.55);border:1px solid rgba(0,0,0,.3);
}
.val{
  font-size:10px;font-weight:600;color:var(--display-fg);
  background:var(--display-bg);padding:3px 6px;border-radius:3px;
  text-align:right;letter-spacing:.04em;white-space:nowrap;
}
</style></head>
<body>

<div class="section">
  <div class="sec-hdr">
    <span class="sec-label">COMPRESSOR</span>
    <button class="toggle" id="cmp-en" onclick="tog('compEnabled','cmp-en')">OFF</button>
  </div>
  <div class="params">
    <div class="row"><span class="plabel">THRESHOLD</span>
      <input type="range" id="compThreshDB" min="-60" max="0" step="0.5" value="-12"
             oninput="emit('compThreshDB',+this.value)">
      <span class="val" id="v-compThreshDB">-12 dB</span></div>
    <div class="row"><span class="plabel">RATIO</span>
      <input type="range" id="compRatio" min="1" max="20" step="0.5" value="4"
             oninput="emit('compRatio',+this.value)">
      <span class="val" id="v-compRatio">4.0x</span></div>
    <div class="row"><span class="plabel">ATTACK</span>
      <input type="range" id="compAttackMs" min="1" max="500" step="1" value="10"
             oninput="emit('compAttackMs',+this.value)">
      <span class="val" id="v-compAttackMs">10 ms</span></div>
    <div class="row"><span class="plabel">RELEASE</span>
      <input type="range" id="compReleaseMs" min="5" max="2000" step="5" value="100"
             oninput="emit('compReleaseMs',+this.value)">
      <span class="val" id="v-compReleaseMs">100 ms</span></div>
  </div>
</div>

<div class="section">
  <div class="sec-hdr">
    <span class="sec-label">DELAY</span>
    <button class="toggle" id="dly-en" onclick="tog('delayEnabled','dly-en')">OFF</button>
  </div>
  <div class="params">
    <div class="row"><span class="plabel">TIME</span>
      <input type="range" id="delayBeats" min="0.125" max="2" step="0.125" value="0.5"
             oninput="emit('delayBeats',+this.value)">
      <span class="val" id="v-delayBeats">1/2</span></div>
    <div class="row"><span class="plabel">FEEDBACK</span>
      <input type="range" id="delayFeedback" min="0" max="0.95" step="0.01" value="0.3"
             oninput="emit('delayFeedback',+this.value)">
      <span class="val" id="v-delayFeedback">0.30</span></div>
    <div class="row"><span class="plabel">MIX</span>
      <input type="range" id="delayMix" min="0" max="1" step="0.01" value="0.25"
             oninput="emit('delayMix',+this.value)">
      <span class="val" id="v-delayMix">0.25</span></div>
  </div>
</div>

<div class="section">
  <div class="sec-hdr">
    <span class="sec-label">REVERB</span>
    <button class="toggle" id="rvb-en" onclick="tog('reverbEnabled','rvb-en')">OFF</button>
  </div>
  <div class="params">
    <div class="row"><span class="plabel">ROOM SIZE</span>
      <input type="range" id="reverbRoom" min="0" max="1" step="0.01" value="0.5"
             oninput="emit('reverbRoom',+this.value)">
      <span class="val" id="v-reverbRoom">0.50</span></div>
    <div class="row"><span class="plabel">DAMPING</span>
      <input type="range" id="reverbDamp" min="0" max="1" step="0.01" value="0.5"
             oninput="emit('reverbDamp',+this.value)">
      <span class="val" id="v-reverbDamp">0.50</span></div>
    <div class="row"><span class="plabel">WET MIX</span>
      <input type="range" id="reverbWet" min="0" max="1" step="0.01" value="0.25"
             oninput="emit('reverbWet',+this.value)">
      <span class="val" id="v-reverbWet">0.25</span></div>
    <div class="row"><span class="plabel">WIDTH</span>
      <input type="range" id="reverbWidth" min="0" max="1" step="0.01" value="1"
             oninput="emit('reverbWidth',+this.value)">
      <span class="val" id="v-reverbWidth">1.00</span></div>
  </div>
</div>

<script>
const toggleState={compEnabled:false,delayEnabled:false,reverbEnabled:false};

function tog(key,btnId){
  toggleState[key]=!toggleState[key];
  const btn=document.getElementById(btnId);
  if(toggleState[key]){btn.classList.add('on');btn.textContent='ON';}
  else{btn.classList.remove('on');btn.textContent='OFF';}
  window.__JUCE__.backend.emitEvent("fxChange",{key:key,value:toggleState[key]?1:0});
}

const fmt={
  compThreshDB: v=>v.toFixed(1)+' dB',
  compRatio:    v=>v.toFixed(1)+'x',
  compAttackMs: v=>v.toFixed(0)+' ms',
  compReleaseMs:v=>v.toFixed(0)+' ms',
  delayBeats:   v=>{const m={0.125:'1/8',0.25:'1/4',0.5:'1/2',1:'1',2:'2'};return m[v]||v.toFixed(3)+' b';},
  delayFeedback:v=>v.toFixed(2),
  delayMix:     v=>v.toFixed(2),
  reverbRoom:   v=>v.toFixed(2),
  reverbDamp:   v=>v.toFixed(2),
  reverbWet:    v=>v.toFixed(2),
  reverbWidth:  v=>v.toFixed(2),
};

function emit(key,value){
  const el=document.getElementById('v-'+key);
  if(el&&fmt[key])el.textContent=fmt[key](value);
  window.__JUCE__.backend.emitEvent("fxChange",{key:key,value:value});
}

function setToggleBtn(key,btnId,val){
  toggleState[key]=Boolean(val);
  const btn=document.getElementById(btnId);
  if(!btn)return;
  if(toggleState[key]){btn.classList.add('on');btn.textContent='ON';}
  else{btn.classList.remove('on');btn.textContent='OFF';}
}

window.__JUCE__.backend.addEventListener("fxLoad",function(p){
  const sliders=['compThreshDB','compRatio','compAttackMs','compReleaseMs',
                 'delayBeats','delayFeedback','delayMix',
                 'reverbRoom','reverbDamp','reverbWet','reverbWidth'];
  for(const k of sliders){
    const el=document.getElementById(k);
    if(el&&p[k]!==undefined){el.value=p[k];const ve=document.getElementById('v-'+k);if(ve&&fmt[k])ve.textContent=fmt[k](p[k]);}
  }
  setToggleBtn('compEnabled','cmp-en',p.compEnabled);
  setToggleBtn('delayEnabled','dly-en',p.delayEnabled);
  setToggleBtn('reverbEnabled','rvb-en',p.reverbEnabled);
});
</script>
</body></html>)HTML";
    }
} // namespace FXEditorHelper

// ---------------------------------------------------------------------------
// Content panel — WebBrowserComponent-based, same public API as before
// ---------------------------------------------------------------------------
class FXEditorPanel : public juce::Component
{
public:
    std::function<void()> onParamsChanged;

    FXEditorPanel() : browser_(buildOptions(this))
    {
        addAndMakeVisible(browser_);
        browser_.goToURL(juce::WebBrowserComponent::getResourceProviderRoot());
    }

    void loadParams(const FXParams& p)
    {
        cachedParams_ = p;
        pushToJs();
    }

    void applyToParams(FXParams& p) const { p = cachedParams_; }

    void resized() override { browser_.setBounds(getLocalBounds()); }

private:
    FXParams                    cachedParams_;
    juce::WebBrowserComponent   browser_;

    void pushToJs()
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty("compEnabled",   cachedParams_.compEnabled   ? 1 : 0);
        obj->setProperty("compThreshDB",  (double)cachedParams_.compThreshDB);
        obj->setProperty("compRatio",     (double)cachedParams_.compRatio);
        obj->setProperty("compAttackMs",  (double)cachedParams_.compAttackMs);
        obj->setProperty("compReleaseMs", (double)cachedParams_.compReleaseMs);
        obj->setProperty("delayEnabled",  cachedParams_.delayEnabled  ? 1 : 0);
        obj->setProperty("delayBeats",    (double)cachedParams_.delayBeats);
        obj->setProperty("delayFeedback", (double)cachedParams_.delayFeedback);
        obj->setProperty("delayMix",      (double)cachedParams_.delayMix);
        obj->setProperty("reverbEnabled", cachedParams_.reverbEnabled ? 1 : 0);
        obj->setProperty("reverbRoom",    (double)cachedParams_.reverbRoom);
        obj->setProperty("reverbDamp",    (double)cachedParams_.reverbDamp);
        obj->setProperty("reverbWet",     (double)cachedParams_.reverbWet);
        obj->setProperty("reverbWidth",   (double)cachedParams_.reverbWidth);
        browser_.emitEventIfBrowserIsVisible("fxLoad", juce::var(obj));
    }

    static juce::WebBrowserComponent::Options buildOptions(FXEditorPanel* self)
    {
        using WBC = juce::WebBrowserComponent;
        return WBC::Options{}
            .withNativeIntegrationEnabled()
            .withKeepPageLoadedWhenBrowserIsHidden()
            .withResourceProvider([](const juce::String& url) -> std::optional<WBC::Resource> {
                if (url == "/" || url == "/index.html")
                    return WBC::Resource{ FXEditorHelper::toBytes(FXEditorHelper::buildHtml()),
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
            .withEventListener("fxChange", [self](const juce::var& d) {
                const auto key = d["key"].toString();
                const auto val = (float)(double)d["value"];
                auto& p = self->cachedParams_;
                if      (key == "compEnabled")   p.compEnabled   = val > 0.5f;
                else if (key == "compThreshDB")  p.compThreshDB  = val;
                else if (key == "compRatio")     p.compRatio     = val;
                else if (key == "compAttackMs")  p.compAttackMs  = val;
                else if (key == "compReleaseMs") p.compReleaseMs = val;
                else if (key == "delayEnabled")  p.delayEnabled  = val > 0.5f;
                else if (key == "delayBeats")    p.delayBeats    = val;
                else if (key == "delayFeedback") p.delayFeedback = val;
                else if (key == "delayMix")      p.delayMix      = val;
                else if (key == "reverbEnabled") p.reverbEnabled = val > 0.5f;
                else if (key == "reverbRoom")    p.reverbRoom    = val;
                else if (key == "reverbDamp")    p.reverbDamp    = val;
                else if (key == "reverbWet")     p.reverbWet     = val;
                else if (key == "reverbWidth")   p.reverbWidth   = val;
                if (self->onParamsChanged) self->onParamsChanged();
            });
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FXEditorPanel)
};

// ---------------------------------------------------------------------------
// Floating window — unchanged API
// ---------------------------------------------------------------------------
class FXEditorWindow : public juce::DocumentWindow
{
public:
    FXEditorPanel panel;

    FXEditorWindow()
        : juce::DocumentWindow("FX Editor",
                               juce::Colour(0xff2a2218),
                               juce::DocumentWindow::closeButton)
    {
        setContentNonOwned(&panel, false);
        setResizable(true, false);
        setSize(460, 440);
    }

    void setTrackName(const juce::String& name) { setName(juce::String::fromUTF8("FX  \xe2\x80\x94  ") + name); }
    void closeButtonPressed() override { setVisible(false); }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FXEditorWindow)
};
