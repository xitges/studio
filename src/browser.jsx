/* SAMPLE BROWSER — left side rail. Folder tree, sample list w/ waveform previews, search */

const { useState: useStateB, useMemo: useMemoB } = React;

function miniWave(seed, n=60) {
  let s = seed;
  const r = () => { s = (s * 9301 + 49297) % 233280; return s / 233280; };
  return Array.from({length:n}, (_, i) => {
    const env = Math.sin((i/n) * Math.PI) * 0.7 + 0.3;
    return ((r() - 0.5) * 1.6 + Math.sin(i*0.3) * 0.4) * env;
  });
}

function MiniWave({ seed, color="var(--accent)", w=80, h=18 }) {
  const data = useMemoB(() => miniWave(seed), [seed]);
  return (
    <svg width={w} height={h} viewBox={`0 0 ${data.length} ${h}`} preserveAspectRatio="none" style={{ display:"block" }}>
      {data.map((v,i) => {
        const bh = Math.abs(v) * (h/2 - 1);
        return <rect key={i} x={i} y={h/2 - bh} width={0.7} height={bh*2} fill={color} opacity={0.85} />;
      })}
    </svg>
  );
}

function FolderRow({ icon, label, count, depth=0, active=false, open=false, onClick, color }) {
  return (
    <div onClick={onClick} style={{
      display:"flex", alignItems:"center", gap:6,
      padding: "4px 8px",
      paddingLeft: 8 + depth * 12,
      cursor:"pointer", borderRadius:3,
      background: active ? "rgba(216,65,42,0.12)" : "transparent",
      color: active ? "var(--ink)" : "var(--ink-soft)",
      fontSize:10, fontWeight: active ? 700 : 500,
      letterSpacing:"0.04em",
      borderLeft: active ? "2px solid var(--accent)" : "2px solid transparent",
    }}>
      <span style={{ fontFamily:'"VT323", monospace', fontSize:13, color: color || "var(--ink-faint)", width:10 }}>
        {icon}
      </span>
      <span style={{ flex:1, textTransform:"uppercase", overflow:"hidden", textOverflow:"ellipsis", whiteSpace:"nowrap" }}>
        {label}
      </span>
      {count != null && (
        <span style={{ fontSize:8, fontWeight:600, color:"var(--ink-faint)", letterSpacing:"0.1em" }}>
          {count}
        </span>
      )}
    </div>
  );
}

function SampleRow({ name, dur, note, bpm, seed, color, active, onClick }) {
  return (
    <div onClick={onClick} style={{
      display:"grid", gridTemplateColumns:"14px 1fr 80px auto", gap:8, alignItems:"center",
      padding:"5px 8px", borderRadius:3, cursor:"pointer",
      background: active ? "rgba(216,65,42,0.14)" : "transparent",
      borderLeft: active ? "2px solid var(--accent)" : "2px solid transparent",
    }}>
      <div style={{ display:"flex", alignItems:"center", justifyContent:"center" }}>
        <div style={{
          width:0, height:0,
          borderTop:"4px solid transparent",
          borderBottom:"4px solid transparent",
          borderLeft: `6px solid ${active ? "var(--accent)" : "var(--ink-faint)"}`,
        }} />
      </div>
      <div style={{ minWidth:0 }}>
        <div style={{
          fontSize:10, fontWeight:600, color:"var(--ink)", letterSpacing:"0.02em",
          overflow:"hidden", textOverflow:"ellipsis", whiteSpace:"nowrap",
        }}>{name}</div>
        <div style={{
          fontSize:8, fontWeight:600, letterSpacing:"0.12em", color:"var(--ink-faint)", textTransform:"uppercase",
          marginTop:1,
        }}>
          {dur} · {note} · {bpm}bpm
        </div>
      </div>
      <MiniWave seed={seed} color={color} />
      <div style={{ display:"flex", flexDirection:"column", alignItems:"center", gap:2 }}>
        <div style={{
          width:5, height:5, borderRadius:"50%", background: color,
          boxShadow:`0 0 4px ${color}`,
        }} />
      </div>
    </div>
  );
}

function SampleBrowser() {
  const [tab, setTab] = useStateB("samples");
  const [folder, setFolder] = useStateB("drums-909");
  const [active, setActive] = useStateB("kick_909_punch.wav");
  const [query, setQuery] = useStateB("");

  const folders = [
    { id:"all",        icon:"◆", label:"All sounds",    count:1284, color:"var(--ink)" },
    { id:"recent",     icon:"◷", label:"Recent",        count:24, depth:0 },
    { id:"favs",       icon:"★", label:"Favorites",     count:18, color:"var(--accent)" },
    { id:"_h1",        header:"FACTORY", },
    { id:"drums",      icon:"▸", label:"Drums",         count:412, depth:0 },
    { id:"drums-909",  icon:"·", label:"909 kit",       count:64, depth:1 },
    { id:"drums-808",  icon:"·", label:"808 kit",       count:48, depth:1 },
    { id:"drums-tape", icon:"·", label:"Tape drums",    count:96, depth:1 },
    { id:"bass",       icon:"▸", label:"Bass",          count:218, depth:0 },
    { id:"keys",       icon:"▸", label:"Keys & pads",   count:174, depth:0 },
    { id:"fx",         icon:"▸", label:"FX & textures", count:142, depth:0 },
    { id:"_h2",        header:"USER", },
    { id:"field",      icon:"▸", label:"Field rec",     count:46, depth:0 },
    { id:"vox",        icon:"▸", label:"Voice memos",   count:31, depth:0 },
    { id:"loops",      icon:"▸", label:"Loops",         count:88, depth:0 },
  ];

  const samples = [
    { name:"kick_909_punch.wav",   dur:"0:01", note:"C1",  bpm:124, seed:11, color:"var(--accent)" },
    { name:"kick_909_subby.wav",   dur:"0:01", note:"C1",  bpm:124, seed:13, color:"var(--accent)" },
    { name:"kick_909_distort.wav", dur:"0:01", note:"C1",  bpm:124, seed:17, color:"var(--accent)" },
    { name:"snare_909_tight.wav",  dur:"0:00", note:"D2",  bpm:124, seed:19, color:"#e89c2b" },
    { name:"snare_909_open.wav",   dur:"0:01", note:"D2",  bpm:124, seed:23, color:"#e89c2b" },
    { name:"hat_closed_top.wav",   dur:"0:00", note:"F#3", bpm:124, seed:29, color:"#7ab87a" },
    { name:"hat_open_long.wav",    dur:"0:02", note:"A#3", bpm:124, seed:31, color:"#7ab87a" },
    { name:"clap_909_layered.wav", dur:"0:01", note:"D#2", bpm:124, seed:37, color:"#5fa8d8" },
    { name:"rim_909_short.wav",    dur:"0:00", note:"E2",  bpm:124, seed:41, color:"#5fa8d8" },
    { name:"tom_low_warm.wav",     dur:"0:01", note:"A1",  bpm:124, seed:43, color:"#b87ad6" },
    { name:"tom_mid_dry.wav",      dur:"0:01", note:"D2",  bpm:124, seed:47, color:"#b87ad6" },
    { name:"crash_909_long.wav",   dur:"0:04", note:"C#4", bpm:124, seed:53, color:"#b87ad6" },
    { name:"ride_909_bell.wav",    dur:"0:03", note:"D#4", bpm:124, seed:59, color:"#b87ad6" },
  ].filter(s => !query || s.name.includes(query.toLowerCase()));

  return (
    <div style={{
      background:"var(--panel)",
      border:"1px solid var(--panel-rim)",
      borderRadius:8,
      boxShadow:"inset 0 1px 0 rgba(255,255,255,0.5), 0 1px 0 rgba(255,255,255,0.4)",
      display:"flex", flexDirection:"column",
      flex:1, minHeight:0, overflow:"hidden",
    }}>
      {/* header */}
      <div style={{
        padding:"10px 12px",
        background:"linear-gradient(180deg, var(--chassis), var(--chassis-2))",
        borderBottom:"1px solid var(--panel-rim)",
      }}>
        <div style={{ display:"flex", alignItems:"center", justifyContent:"space-between", marginBottom:8 }}>
          <div style={{ display:"flex", alignItems:"center", gap:8 }}>
            <div style={{ width:8, height:8, borderRadius:2, background:"var(--accent)" }} />
            <div style={{ fontSize:10, fontWeight:700, letterSpacing:"0.18em", color:"var(--ink)", textTransform:"uppercase" }}>
              BROWSER
            </div>
          </div>
          <Tag>1,284 SOUNDS</Tag>
        </div>

        {/* tabs */}
        <div style={{ display:"flex", gap:2, marginBottom:8 }}>
          {[
            { id:"samples", label:"SAMPLES" },
            { id:"patches", label:"PATCHES" },
            { id:"loops", label:"LOOPS" },
          ].map(t => (
            <button key={t.id} onClick={() => setTab(t.id)} style={{
              flex:1, padding:"4px 8px", borderRadius:3, cursor:"pointer",
              fontSize:9, fontWeight:700, letterSpacing:"0.12em",
              fontFamily:'"JetBrains Mono", monospace',
              background: tab === t.id ? "var(--accent)" : "rgba(0,0,0,0.05)",
              color: tab === t.id ? "#fff" : "var(--ink-soft)",
              border:"1px solid " + (tab === t.id ? "var(--accent)" : "rgba(0,0,0,0.15)"),
              boxShadow: tab === t.id ? "0 0 6px var(--accent)" : "none",
            }}>{t.label}</button>
          ))}
        </div>

        {/* search */}
        <div style={{
          display:"flex", alignItems:"center", gap:6,
          background:"var(--display-bg)",
          borderRadius:3, padding:"4px 8px",
          boxShadow:"inset 0 1px 3px rgba(0,0,0,0.7)",
          border:"1px solid rgba(0,0,0,0.5)",
        }}>
          <span style={{ fontFamily:'"VT323",monospace', color:"var(--display-fg)", fontSize:14, textShadow:"0 0 3px rgba(185,255,102,0.6)" }}>›</span>
          <input
            value={query}
            onChange={e => setQuery(e.target.value)}
            placeholder="search sounds..."
            style={{
              flex:1, background:"transparent", border:"none", outline:"none",
              fontFamily:'"VT323",monospace', fontSize:14, color:"var(--display-fg)",
              letterSpacing:"0.05em",
            }}
          />
          <span style={{ fontFamily:'"JetBrains Mono",monospace', fontSize:8, color:"var(--display-fg)", opacity:0.5 }}>⌘F</span>
        </div>
      </div>

      {/* body: folder tree + sample list */}
      <div style={{ display:"grid", gridTemplateRows:"auto 1fr", flex:1, minHeight:0, overflow:"hidden" }}>
        {/* folders */}
        <div className="scroll" style={{
          maxHeight:170, overflowY:"auto",
          padding:"6px 4px",
          borderBottom:"1px solid var(--rule)",
          background:"linear-gradient(180deg, transparent, rgba(0,0,0,0.02))",
        }}>
          {folders.map(f => f.header ? (
            <div key={f.id} style={{
              fontSize:8, fontWeight:700, letterSpacing:"0.18em",
              color:"var(--ink-faint)", textTransform:"uppercase",
              padding:"6px 10px 2px",
            }}>{f.header}</div>
          ) : (
            <FolderRow key={f.id} {...f}
              active={folder === f.id}
              onClick={() => setFolder(f.id)}
            />
          ))}
        </div>

        {/* samples */}
        <div className="scroll" style={{ overflowY:"auto", padding:"4px" }}>
          {samples.map(s => (
            <SampleRow key={s.name} {...s}
              active={active === s.name}
              onClick={() => setActive(s.name)}
            />
          ))}
        </div>
      </div>

      {/* preview footer */}
      <div style={{
        padding:"8px 10px",
        borderTop:"1px solid var(--panel-rim)",
        background:"linear-gradient(180deg, var(--chassis-2), var(--chassis))",
        display:"flex", flexDirection:"column", gap:6,
      }}>
        <div style={{ display:"flex", alignItems:"center", justifyContent:"space-between" }}>
          <div style={{ minWidth:0 }}>
            <div style={{ fontSize:9, fontWeight:600, letterSpacing:"0.12em", color:"var(--ink-faint)", textTransform:"uppercase" }}>
              NOW PREVIEWING
            </div>
            <div style={{ fontSize:11, fontWeight:700, color:"var(--ink)", overflow:"hidden", textOverflow:"ellipsis", whiteSpace:"nowrap" }}>
              {active}
            </div>
          </div>
          <LED on color="green" size={7} />
        </div>
        <div style={{
          background:"var(--display-bg)", borderRadius:3,
          padding:"6px 8px",
          boxShadow:"inset 0 2px 4px rgba(0,0,0,0.7)",
          border:"1px solid rgba(0,0,0,0.5)",
        }}>
          <MiniWave seed={11} w={250} h={28} color="var(--display-fg)" />
        </div>
        <div style={{ display:"flex", gap:4 }}>
          <button style={{
            flex:1, padding:"5px 8px", borderRadius:3, cursor:"pointer",
            fontSize:9, fontWeight:700, letterSpacing:"0.12em",
            fontFamily:'"JetBrains Mono", monospace',
            background: "var(--accent)", color:"#fff",
            border:"1px solid var(--accent)",
            boxShadow:"0 0 6px var(--accent)66",
          }}>＋ DROP TO TRACK</button>
          <button style={{
            padding:"5px 8px", borderRadius:3, cursor:"pointer",
            fontSize:9, fontWeight:700, letterSpacing:"0.12em",
            fontFamily:'"JetBrains Mono", monospace',
            background: "transparent", color:"var(--ink-soft)",
            border:"1px solid rgba(0,0,0,0.2)",
          }}>★</button>
          <button style={{
            padding:"5px 8px", borderRadius:3, cursor:"pointer",
            fontSize:9, fontWeight:700, letterSpacing:"0.12em",
            fontFamily:'"JetBrains Mono", monospace',
            background: "transparent", color:"var(--ink-soft)",
            border:"1px solid rgba(0,0,0,0.2)",
          }}>EDIT</button>
        </div>
      </div>
    </div>
  );
}

Object.assign(window, { SampleBrowser });
