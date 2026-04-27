/* MIXER — right column channel strips with EQ, sends, faders, master */

const { useState: useStateM } = React;

function ChannelStrip({ name, color, defaultVol=0.7, defaultPan=0.5, eq=[0.5,0.55,0.5], muted=false, soloed=false, sends=[0.3,0.1] }) {
  const [vol, setVol] = useStateM(defaultVol);
  const [m, setM] = useStateM(muted);
  const [s, setS] = useStateM(soloed);

  return (
    <div style={{
      width:64, padding:"8px 6px",
      background: "linear-gradient(180deg, #ebe3cf 0%, #e0d6bd 100%)",
      borderRadius:5,
      border:"1px solid var(--panel-rim)",
      boxShadow:"inset 0 1px 0 rgba(255,255,255,0.5), 0 1px 0 rgba(255,255,255,0.4)",
      display:"flex", flexDirection:"column", alignItems:"center", gap:6,
    }}>
      {/* color tab */}
      <div style={{
        width:"100%", height:5, borderRadius:1,
        background: color,
        boxShadow:`0 0 4px ${color}66`,
      }} />
      {/* name */}
      <div style={{
        width:"100%", textAlign:"center", fontSize:9, fontWeight:700, letterSpacing:"0.06em",
        color:"var(--ink)", textTransform:"uppercase",
        overflow:"hidden", textOverflow:"ellipsis", whiteSpace:"nowrap",
      }}>{name}</div>

      {/* PAN */}
      <div style={{ display:"flex", flexDirection:"column", alignItems:"center", gap:2 }}>
        <Knob defaultValue={defaultPan} size={28} marks={false} min={0} max={1} />
        <div style={{ fontSize:7, fontWeight:600, letterSpacing:"0.1em", color:"var(--ink-faint)" }}>PAN</div>
      </div>

      {/* EQ 3-band */}
      <div style={{
        display:"flex", flexDirection:"column", gap:2, alignItems:"center",
        padding:"4px 4px", borderRadius:3,
        background:"rgba(0,0,0,0.04)",
      }}>
        {["HI","MD","LO"].map((b,i) => (
          <div key={b} style={{ display:"flex", alignItems:"center", gap:4 }}>
            <span style={{ fontSize:7, fontWeight:600, color:"var(--ink-faint)", width:10 }}>{b}</span>
            <Knob defaultValue={eq[i]} size={20} marks={false} />
          </div>
        ))}
      </div>

      {/* SENDS */}
      <div style={{ display:"flex", flexDirection:"column", gap:3, alignItems:"center" }}>
        {sends.map((v,i) => (
          <div key={i} style={{ display:"flex", alignItems:"center", gap:3 }}>
            <span style={{ fontSize:7, fontWeight:600, color:"var(--accent)", width:10 }}>{["RV","DL"][i]}</span>
            <Knob defaultValue={v} size={18} marks={false} accent />
          </div>
        ))}
      </div>

      {/* M/S buttons */}
      <div style={{ display:"flex", gap:3 }}>
        <button onClick={() => setM(!m)} style={{
          width:18, height:14, padding:0, borderRadius:2,
          fontSize:8, fontWeight:700, fontFamily:'"JetBrains Mono", monospace',
          background: m ? "var(--led-amber)" : "rgba(0,0,0,0.06)",
          color: m ? "rgba(0,0,0,0.85)" : "var(--ink-soft)",
          border:"1px solid rgba(0,0,0,0.2)",
          cursor:"pointer",
          boxShadow: m ? "0 0 4px var(--led-amber)" : "none",
        }}>M</button>
        <button onClick={() => setS(!s)} style={{
          width:18, height:14, padding:0, borderRadius:2,
          fontSize:8, fontWeight:700, fontFamily:'"JetBrains Mono", monospace',
          background: s ? "var(--led-green)" : "rgba(0,0,0,0.06)",
          color: s ? "rgba(0,0,0,0.85)" : "var(--ink-soft)",
          border:"1px solid rgba(0,0,0,0.2)",
          cursor:"pointer",
          boxShadow: s ? "0 0 4px var(--led-green)" : "none",
        }}>S</button>
      </div>

      {/* Fader + meter */}
      <div style={{ display:"flex", gap:4, alignItems:"flex-end" }}>
        <Fader value={vol} onChange={setVol} height={100} />
        <ChannelMeter level={m ? 0 : vol * (0.6 + Math.sin(parseInt(name.charCodeAt(0))) * 0.3 + 0.3)} height={100} />
      </div>

      {/* dB readout */}
      <div style={{
        width:"100%", padding:"2px 4px", textAlign:"center",
        background:"var(--display-bg)",
        borderRadius:2,
        boxShadow:"inset 0 1px 2px rgba(0,0,0,0.7)",
        fontFamily:'"VT323", monospace', fontSize:11,
        color:"var(--display-fg)", letterSpacing:"0.05em",
        textShadow:"0 0 3px rgba(185,255,102,0.6)",
      }}>
        {m ? "-INF" : `${(20 * Math.log10(Math.max(0.001, vol))).toFixed(1)}`}
      </div>
    </div>
  );
}

function ChannelMeter({ level=0.5, height=100 }) {
  const SEG = 22;
  return (
    <div style={{
      display:"flex", flexDirection:"column-reverse", gap:1,
      padding:"3px 2px", borderRadius:2, height,
      background:"var(--display-bg)",
      boxShadow:"inset 0 1px 3px rgba(0,0,0,0.7)",
      border:"1px solid rgba(0,0,0,0.5)",
    }}>
      {Array.from({length:SEG}).map((_,i) => {
        const t = i / (SEG-1);
        const active = t < level;
        const c = t < 0.7 ? "var(--led-green)" : t < 0.88 ? "var(--led-amber)" : "var(--led-red)";
        return (
          <div key={i} style={{
            width:4, height: (height-8) / SEG - 1,
            background: active ? c : "rgba(0,0,0,0.4)",
            boxShadow: active ? `0 0 2px ${c}` : "none",
            borderRadius:0.5,
          }} />
        );
      })}
    </div>
  );
}

function Mixer() {
  const channels = [
    { name:"DRUMS", color:"#d8412a" },
    { name:"BASS",  color:"#e89c2b" },
    { name:"POLY",  color:"#7ab87a", defaultVol:0.62 },
    { name:"FIELD", color:"#5fa8d8", defaultVol:0.4, muted:true },
    { name:"VOX",   color:"#b87ad6", defaultVol:0.74 },
    { name:"TAPE",  color:"#8d7a5a", defaultVol:0.55, soloed:true },
  ];

  return (
    <div style={{ display:"flex", flexDirection:"column", gap:10, height:"100%" }}>
      <Panel title="MIXER" sub="6 CH · 2 BUS" accent style={{ flex:1, display:"flex", flexDirection:"column" }} contentStyle={{ display:"flex", flexDirection:"column", gap:8, flex:1 }}>
        <div className="scroll" style={{ display:"flex", gap:6, overflowX:"auto", paddingBottom:6 }}>
          {channels.map(c => <ChannelStrip key={c.name} {...c} />)}
        </div>
      </Panel>

      <Panel title="MASTER BUS" sub="STEREO OUT">
        <div style={{ display:"flex", gap:14, alignItems:"flex-end", justifyContent:"space-between" }}>
          <div style={{ display:"flex", flexDirection:"column", gap:6 }}>
            <div style={{ display:"flex", gap:6 }}>
              <Knob defaultValue={0.85} size={36} label="OUT" accent marks={false} />
              <Knob defaultValue={0.4} size={36} label="COMP" marks={false} />
              <Knob defaultValue={0.3} size={36} label="DRIVE" marks={false} />
            </div>
            <div style={{ display:"flex", gap:6 }}>
              <Knob defaultValue={0.55} size={32} label="LO" marks={false} />
              <Knob defaultValue={0.5} size={32} label="MID" marks={false} />
              <Knob defaultValue={0.6} size={32} label="HI" marks={false} />
            </div>
          </div>
          <div style={{ display:"flex", gap:6, alignItems:"flex-end" }}>
            <Fader defaultValue={0.85} height={88} label="L" />
            <Fader defaultValue={0.85} height={88} label="R" />
            <ChannelMeter level={0.78} height={92} />
            <ChannelMeter level={0.81} height={92} />
          </div>
        </div>
      </Panel>
    </div>
  );
}

Object.assign(window, { Mixer, ChannelStrip });
