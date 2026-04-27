/* SYNTH — left column instrument panel: oscillators, envelope, filter, FX, modular patch */

const { useState: useStateS } = React;

function OscWave({ type="saw" }) {
  const paths = {
    saw:    "M0 12 L8 0 L8 24 L16 0 L16 24 L24 0 L24 24",
    square: "M0 0 L4 0 L4 24 L12 24 L12 0 L20 0 L20 24 L24 24",
    sine:   "M0 12 Q3 0 6 12 T12 12 T18 12 T24 12",
    tri:    "M0 12 L6 0 L12 24 L18 0 L24 12",
    noise:  "M0 12 L2 4 L4 18 L6 8 L8 22 L10 6 L12 16 L14 4 L16 20 L18 10 L20 22 L22 6 L24 14",
  };
  return (
    <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.4" strokeLinejoin="round">
      <path d={paths[type]} />
    </svg>
  );
}

function WaveSelect({ value, onChange }) {
  const opts = ["sine","tri","saw","square","noise"];
  return (
    <div style={{
      display:"flex", padding:2, borderRadius:4,
      background:"var(--display-bg)",
      boxShadow:"inset 0 1px 3px rgba(0,0,0,0.7)",
      border:"1px solid rgba(0,0,0,0.5)",
    }}>
      {opts.map(o => (
        <button key={o} onClick={() => onChange && onChange(o)}
          style={{
            width:24, height:22, padding:0, border:"none",
            background: value === o ? "var(--accent)" : "transparent",
            color: value === o ? "#fff" : "var(--display-fg)",
            borderRadius:3, cursor:"pointer",
            display:"flex", alignItems:"center", justifyContent:"center",
            boxShadow: value === o ? `0 0 6px var(--accent)` : "none",
          }}>
          <OscWave type={o} />
        </button>
      ))}
    </div>
  );
}

function ADSRGraph({ a=0.2, d=0.3, s=0.6, r=0.4 }) {
  const W = 160, H = 56;
  const ax = a * 30;
  const dx = ax + d * 30;
  const sy = H - s * (H - 6) - 3;
  const sx = W - r * 40;
  const rx = W;
  return (
    <svg width={W} height={H} style={{
      background:"var(--display-bg)",
      borderRadius:4,
      boxShadow:"inset 0 2px 4px rgba(0,0,0,0.7)",
      border:"1px solid rgba(0,0,0,0.5)",
    }}>
      {/* grid */}
      {[0,1,2,3].map(i => (
        <line key={i} x1={i*W/4} y1="0" x2={i*W/4} y2={H} stroke="rgba(185,255,102,0.1)" strokeWidth="0.5" />
      ))}
      <polyline
        points={`0,${H-3} ${ax},3 ${dx},${sy} ${sx},${sy} ${rx},${H-3}`}
        fill="none" stroke="var(--display-fg)" strokeWidth="1.5"
        style={{ filter:"drop-shadow(0 0 2px rgba(185,255,102,0.6))" }}
      />
      <polyline
        points={`0,${H-3} ${ax},3 ${dx},${sy} ${sx},${sy} ${rx},${H-3}`}
        fill="rgba(185,255,102,0.12)" stroke="none"
      />
    </svg>
  );
}

function PatchPort({ label, color="var(--accent)", connected=false, top=false }) {
  return (
    <div style={{ display:"flex", flexDirection:"column", alignItems:"center", gap:3 }}>
      <div style={{
        width:14, height:14, borderRadius:"50%",
        background:"radial-gradient(circle at 30% 25%, #4a4338, #1a1612 70%, #0a0805)",
        border:"1px solid rgba(0,0,0,0.6)",
        boxShadow:"inset 0 1px 2px rgba(0,0,0,0.8), 0 1px 0 rgba(255,255,255,0.2)",
        position:"relative",
      }}>
        <div style={{
          position:"absolute", inset:4, borderRadius:"50%",
          background: connected ? color : "#0a0805",
          boxShadow: connected ? `0 0 4px ${color}` : "inset 0 1px 1px rgba(0,0,0,0.8)",
        }} />
      </div>
      <div style={{ fontSize:7, fontWeight:600, letterSpacing:"0.08em", color:"var(--ink-faint)", textTransform:"uppercase" }}>
        {label}
      </div>
    </div>
  );
}

function SynthPanel() {
  const [osc1, setOsc1] = useStateS("saw");
  const [osc2, setOsc2] = useStateS("square");
  const [filterMode, setFilterMode] = useStateS("LP");
  const [adsr, setAdsr] = useStateS({ a:0.18, d:0.4, s:0.55, r:0.35 });

  return (
    <div style={{ display:"flex", flexDirection:"column", gap:10 }}>

      {/* Patch header */}
      <Panel title="INSTRUMENT" sub="A · ANALOG POLY" accent>
        <div style={{ display:"flex", alignItems:"center", justifyContent:"space-between", gap:8 }}>
          <div>
            <div style={{ fontFamily:'"Space Grotesk", sans-serif', fontSize:18, fontWeight:600, color:"var(--ink)" }}>
              POLY-6 mk2
            </div>
            <div style={{ fontSize:9, letterSpacing:"0.12em", color:"var(--ink-faint)" }}>
              PATCH 014 · "deep brass pad"
            </div>
          </div>
          <SegmentDisplay height={28} padding="2px 8px">A4 · 64v</SegmentDisplay>
        </div>
      </Panel>

      {/* OSC */}
      <Panel title="OSCILLATORS" sub="DUAL VCO">
        <div style={{ display:"flex", flexDirection:"column", gap:10 }}>
          {[
            { id:1, val:osc1, set:setOsc1 },
            { id:2, val:osc2, set:setOsc2 },
          ].map(o => (
            <div key={o.id} style={{ display:"grid", gridTemplateColumns:"auto 1fr auto auto auto", gap:10, alignItems:"center" }}>
              <Tag color="var(--ink)">OSC {o.id}</Tag>
              <WaveSelect value={o.val} onChange={o.set} />
              <Knob defaultValue={0.5} size={36} label="TUNE" marks={false} />
              <Knob defaultValue={0.3} size={36} label="DET" marks={false} />
              <Knob defaultValue={0.7} size={36} label="LVL" marks={false} accent={o.id===1} />
            </div>
          ))}
        </div>
      </Panel>

      {/* FILTER */}
      <Panel title="FILTER" sub="STATE-VARIABLE 24dB">
        <div style={{ display:"grid", gridTemplateColumns:"auto 1fr", gap:12, alignItems:"center" }}>
          <div style={{ display:"flex", flexDirection:"column", gap:4 }}>
            {["LP","BP","HP"].map(m => (
              <button key={m} onClick={() => setFilterMode(m)}
                style={{
                  fontSize:9, fontWeight:700, letterSpacing:"0.12em",
                  padding:"3px 8px", borderRadius:3, cursor:"pointer",
                  background: filterMode === m ? "var(--accent)" : "transparent",
                  color: filterMode === m ? "#fff" : "var(--ink-soft)",
                  border:"1px solid " + (filterMode === m ? "var(--accent)" : "rgba(0,0,0,0.2)"),
                  boxShadow: filterMode === m ? `0 0 6px var(--accent)` : "none",
                  fontFamily:'"JetBrains Mono", monospace',
                }}>{m}</button>
            ))}
          </div>
          <div style={{ display:"flex", justifyContent:"space-around" }}>
            <Knob defaultValue={0.62} size={48} label="CUT" />
            <Knob defaultValue={0.28} size={48} label="RES" accent />
            <Knob defaultValue={0.4} size={48} label="ENV" />
            <Knob defaultValue={0.5} size={48} label="KEY" />
          </div>
        </div>
      </Panel>

      {/* ENV */}
      <Panel title="ENVELOPE" sub="ADSR · AMP">
        <div style={{ display:"flex", gap:10, alignItems:"center" }}>
          <ADSRGraph {...adsr} />
          <div style={{ display:"flex", gap:4 }}>
            <Knob value={adsr.a} onChange={v => setAdsr(s => ({...s,a:v}))} size={34} label="A" marks={false} />
            <Knob value={adsr.d} onChange={v => setAdsr(s => ({...s,d:v}))} size={34} label="D" marks={false} />
            <Knob value={adsr.s} onChange={v => setAdsr(s => ({...s,s:v}))} size={34} label="S" marks={false} />
            <Knob value={adsr.r} onChange={v => setAdsr(s => ({...s,r:v}))} size={34} label="R" marks={false} />
          </div>
        </div>
      </Panel>

      {/* PATCH BAY */}
      <Panel title="PATCH BAY" sub="CV / GATE">
        <div style={{ display:"flex", justifyContent:"space-between" }}>
          <PatchPort label="PITCH" color="var(--led-amber)" connected />
          <PatchPort label="GATE" color="var(--led-green)" connected />
          <PatchPort label="CUT" color="var(--accent)" />
          <PatchPort label="RES" color="var(--accent)" />
          <PatchPort label="LFO" color="var(--led-amber)" connected />
          <PatchPort label="EXT" color="var(--led-green)" />
        </div>
      </Panel>

    </div>
  );
}

Object.assign(window, { SynthPanel });
