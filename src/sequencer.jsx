/* SEQUENCER — step grid + drum pads, sits below transport */

const { useState: useStateSQ } = React;

function StepGrid({ steps=16, currentStep=0, playing=false }) {
  const tracks = [
    { name:"KCK", color:"var(--accent)",     pattern:[1,0,0,0, 1,0,0,0, 1,0,0,0, 1,0,1,0] },
    { name:"SNR", color:"#e89c2b",           pattern:[0,0,0,0, 1,0,0,0, 0,0,0,0, 1,0,0,1] },
    { name:"HHC", color:"#7ab87a",           pattern:[1,0,1,0, 1,0,1,0, 1,0,1,0, 1,0,1,0] },
    { name:"CLP", color:"#5fa8d8",           pattern:[0,0,0,0, 1,0,0,0, 0,0,0,1, 1,0,0,0] },
    { name:"PRC", color:"#b87ad6",           pattern:[0,0,1,0, 0,0,0,1, 0,1,0,0, 0,0,1,0] },
  ];

  // local toggle state, seeded from pattern
  const [grid, setGrid] = useStateSQ(() => tracks.map(t => [...t.pattern]));

  const toggle = (r,c) => {
    setGrid(g => g.map((row,ri) => ri === r ? row.map((v,ci) => ci === c ? (v?0:1) : v) : row));
  };

  return (
    <div style={{ display:"flex", flexDirection:"column", gap:5 }}>
      {/* step ruler */}
      <div style={{ display:"grid", gridTemplateColumns:"40px repeat(16, 1fr)", gap:4 }}>
        <div></div>
        {Array.from({length:16}).map((_,i) => (
          <div key={i} style={{
            textAlign:"center",
            fontSize:8, fontWeight:600, letterSpacing:"0.05em",
            color: i === currentStep && playing ? "var(--accent)" : (i % 4 === 0 ? "var(--ink)" : "var(--ink-faint)"),
          }}>
            {i+1}
          </div>
        ))}
      </div>
      {tracks.map((t, ri) => (
        <div key={t.name} style={{ display:"grid", gridTemplateColumns:"40px repeat(16, 1fr)", gap:4, alignItems:"center" }}>
          <div style={{ display:"flex", alignItems:"center", gap:4 }}>
            <div style={{ width:6, height:6, borderRadius:1, background:t.color, boxShadow:`0 0 4px ${t.color}88` }} />
            <span style={{ fontSize:9, fontWeight:700, letterSpacing:"0.08em", color:"var(--ink)" }}>{t.name}</span>
          </div>
          {grid[ri].map((on,ci) => {
            const isCurrent = ci === currentStep && playing;
            const isQuarter = ci % 4 === 0;
            return (
              <button key={ci} onClick={() => toggle(ri,ci)}
                style={{
                  height:24, padding:0, borderRadius:3, border:"1px solid rgba(0,0,0,0.35)",
                  background: on
                    ? `linear-gradient(180deg, ${t.color}, ${t.color}cc)`
                    : isQuarter ? "rgba(0,0,0,0.18)" : "rgba(0,0,0,0.08)",
                  boxShadow: on
                    ? `0 0 6px ${t.color}88, inset 0 1px 0 rgba(255,255,255,0.4)`
                    : "inset 0 1px 1px rgba(0,0,0,0.3)",
                  cursor:"pointer",
                  outline: isCurrent ? "2px solid var(--accent)" : "none",
                  outlineOffset: isCurrent ? "1px" : 0,
                  transition: "outline 80ms",
                }}
              />
            );
          })}
        </div>
      ))}
    </div>
  );
}

function DrumPads() {
  const [active, setActive] = useStateSQ(null);
  const pads = [
    { label:"KCK", c:"var(--accent)" },
    { label:"SNR", c:"#e89c2b" },
    { label:"HHC", c:"#7ab87a" },
    { label:"HHO", c:"#7ab87a" },
    { label:"CLP", c:"#5fa8d8" },
    { label:"RIM", c:"#5fa8d8" },
    { label:"TOM", c:"#b87ad6" },
    { label:"CYM", c:"#b87ad6" },
  ];
  return (
    <div style={{ display:"grid", gridTemplateColumns:"repeat(4, 1fr)", gap:6 }}>
      {pads.map((p,i) => (
        <Pad key={i}
          color={p.c}
          label={p.label}
          lit
          active={active === i}
          size={42}
          onClick={() => { setActive(i); setTimeout(() => setActive(null), 200); }}
        />
      ))}
    </div>
  );
}

function SequencerBar({ playing, currentStep }) {
  return (
    <Panel title="STEP SEQUENCER" sub="PATTERN A · 16 · 1/16" accent
      style={{ flex:1 }}
      contentStyle={{ display:"grid", gridTemplateColumns:"1fr auto auto", gap:14, alignItems:"flex-start" }}
    >
      <StepGrid currentStep={currentStep} playing={playing} />
      <div style={{ display:"flex", flexDirection:"column", gap:6, paddingLeft:10, borderLeft:"1px dashed rgba(0,0,0,0.15)" }}>
        <div style={{ fontSize:9, fontWeight:700, letterSpacing:"0.15em", color:"var(--ink-soft)" }}>PADS</div>
        <DrumPads />
        <div style={{ display:"flex", gap:4, marginTop:4 }}>
          <Tag>BANK A</Tag>
          <Tag>KIT 09</Tag>
        </div>
      </div>
      <div style={{ display:"flex", flexDirection:"column", gap:6, paddingLeft:10, borderLeft:"1px dashed rgba(0,0,0,0.15)", alignItems:"center" }}>
        <div style={{ fontSize:9, fontWeight:700, letterSpacing:"0.15em", color:"var(--ink-soft)" }}>PATTERN</div>
        <div style={{ display:"flex", gap:6 }}>
          <Knob defaultValue={0.5} size={36} label="SWING" marks={false} />
          <Knob defaultValue={0.7} size={36} label="VEL" marks={false} accent />
        </div>
        <div style={{ display:"flex", gap:3 }}>
          {["A","B","C","D"].map((p,i) => (
            <button key={p} style={{
              width:22, height:18, padding:0, borderRadius:3,
              fontSize:9, fontWeight:700, fontFamily:'"JetBrains Mono", monospace',
              background: i === 0 ? "var(--accent)" : "rgba(0,0,0,0.06)",
              color: i === 0 ? "#fff" : "var(--ink-soft)",
              border:"1px solid rgba(0,0,0,0.2)",
              cursor:"pointer",
              boxShadow: i === 0 ? "0 0 4px var(--accent)" : "none",
            }}>{p}</button>
          ))}
        </div>
        <div style={{ display:"flex", gap:4, marginTop:2 }}>
          <ToggleSwitch value={true} labels={["EXT","INT"]} />
        </div>
      </div>
    </Panel>
  );
}

Object.assign(window, { SequencerBar });
