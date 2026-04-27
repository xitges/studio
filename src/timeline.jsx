/* TIMELINE — multitrack with waveforms, playhead, ruler */

const { useMemo: useMemoTL } = React;

function generateWaveform(seed, length=400, amp=1) {
  // deterministic pseudo-random based on seed
  let s = seed;
  const rand = () => { s = (s * 9301 + 49297) % 233280; return s / 233280; };
  const out = [];
  for (let i = 0; i < length; i++) {
    const env = Math.sin((i/length) * Math.PI) * 0.6 + 0.4;
    const noise = (rand() - 0.5) * 2;
    const tone = Math.sin(i * 0.18) * 0.6 + Math.sin(i * 0.07) * 0.3;
    out.push(Math.max(-1, Math.min(1, (noise * 0.5 + tone) * env * amp)));
  }
  return out;
}

function Waveform({ data, color="var(--accent)", height=42, width="100%" }) {
  const bars = data.length;
  return (
    <svg width={width} height={height} viewBox={`0 0 ${bars} ${height}`} preserveAspectRatio="none" style={{ display:"block" }}>
      {data.map((v, i) => {
        const h = Math.abs(v) * (height/2 - 1);
        return (
          <rect key={i} x={i} y={height/2 - h} width={0.8} height={h*2} fill={color} opacity={0.85} />
        );
      })}
    </svg>
  );
}

function Clip({ x, w, color, label, waveSeed, muted=false }) {
  const data = useMemoTL(() => generateWaveform(waveSeed, Math.max(40, Math.floor(w*0.6))), [waveSeed, w]);
  return (
    <div style={{
      position:"absolute", left:x, top:4, width:w, bottom:4,
      borderRadius:3, overflow:"hidden",
      background: `linear-gradient(180deg, ${color}38, ${color}18)`,
      border:`1px solid ${color}`,
      boxShadow:"0 1px 0 rgba(255,255,255,0.4), inset 0 1px 0 rgba(255,255,255,0.15)",
      opacity: muted ? 0.4 : 1,
      cursor:"grab",
    }}>
      <div style={{
        position:"absolute", top:0, left:0, right:0, padding:"2px 6px",
        fontSize:9, fontWeight:600, letterSpacing:"0.08em", color:"var(--ink)",
        background: `linear-gradient(180deg, ${color}66, ${color}33)`,
        borderBottom: `1px solid ${color}`,
        display:"flex", justifyContent:"space-between", alignItems:"center",
      }}>
        <span style={{ textTransform:"uppercase", overflow:"hidden", textOverflow:"ellipsis", whiteSpace:"nowrap" }}>{label}</span>
        {muted && <span style={{ color:"var(--ink-faint)" }}>M</span>}
      </div>
      <div style={{ position:"absolute", top:18, left:4, right:4, bottom:4, display:"flex", alignItems:"center" }}>
        <Waveform data={data} color={color} height={36} />
      </div>
    </div>
  );
}

function TrackHead({ idx, name, type, color, muted, soloed, armed, onMute, onSolo, onArm }) {
  return (
    <div style={{
      height:62, padding:"6px 8px",
      background: "linear-gradient(180deg, var(--panel) 0%, #ebe3cf 100%)",
      borderBottom:"1px solid var(--rule)",
      display:"grid", gridTemplateColumns:"auto 1fr auto", gap:6, alignItems:"center",
    }}>
      <div style={{ display:"flex", flexDirection:"column", alignItems:"center", gap:4 }}>
        <div style={{
          width:18, height:18, borderRadius:3,
          background: color,
          boxShadow:`0 0 6px ${color}66, inset 0 1px 0 rgba(255,255,255,0.4)`,
          border:"1px solid rgba(0,0,0,0.4)",
          display:"flex", alignItems:"center", justifyContent:"center",
          fontSize:9, fontWeight:700, color:"#fff",
        }}>{idx}</div>
      </div>
      <div style={{ minWidth:0 }}>
        <div style={{ fontSize:11, fontWeight:600, color:"var(--ink)", letterSpacing:"0.02em", textTransform:"uppercase", overflow:"hidden", textOverflow:"ellipsis", whiteSpace:"nowrap" }}>
          {name}
        </div>
        <div style={{ fontSize:8, fontWeight:600, letterSpacing:"0.18em", color:"var(--ink-faint)", textTransform:"uppercase" }}>
          {type}
        </div>
        <div style={{ display:"flex", gap:3, marginTop:3 }}>
          {[
            { k:"M", on:muted, c:"var(--led-amber)", click:onMute },
            { k:"S", on:soloed, c:"var(--led-green)", click:onSolo },
            { k:"R", on:armed, c:"var(--led-red)", click:onArm },
          ].map(b => (
            <button key={b.k} onClick={b.click}
              style={{
                width:18, height:14, padding:0, borderRadius:2,
                fontSize:8, fontWeight:700, fontFamily:'"JetBrains Mono", monospace',
                background: b.on ? b.c : "rgba(0,0,0,0.05)",
                color: b.on ? "rgba(0,0,0,0.85)" : "var(--ink-soft)",
                border:"1px solid rgba(0,0,0,0.2)",
                cursor:"pointer",
                boxShadow: b.on ? `0 0 6px ${b.c}` : "none",
              }}>{b.k}</button>
          ))}
        </div>
      </div>
      <div style={{ display:"flex", flexDirection:"column", alignItems:"center", gap:3 }}>
        <Knob defaultValue={0.7} size={26} marks={false} />
        <div style={{ fontSize:7, fontWeight:600, letterSpacing:"0.1em", color:"var(--ink-faint)" }}>VOL</div>
      </div>
    </div>
  );
}

function Ruler({ bars=32, pxPerBar, playhead }) {
  return (
    <div style={{
      height:22,
      background:"var(--display-bg)",
      position:"relative",
      borderBottom:"1px solid rgba(0,0,0,0.6)",
      boxShadow:"inset 0 -2px 4px rgba(0,0,0,0.5)",
      overflow:"hidden",
    }}>
      {Array.from({length: bars+1}).map((_,i) => {
        const isMajor = i % 4 === 0;
        return (
          <div key={i} style={{
            position:"absolute", left: i * pxPerBar, top:0, bottom:0,
            width:1, background: isMajor ? "rgba(185,255,102,0.5)" : "rgba(185,255,102,0.15)",
          }}>
            {isMajor && (
              <span style={{
                position:"absolute", left:4, top:3,
                fontFamily:'"VT323", monospace', fontSize:13,
                color:"var(--display-fg)", letterSpacing:"0.05em",
                textShadow:"0 0 4px rgba(185,255,102,0.6)",
              }}>{(i+1).toString().padStart(2,"0")}</span>
            )}
          </div>
        );
      })}
      {/* playhead */}
      <div style={{
        position:"absolute", left: playhead, top:0, bottom:0, width:2,
        background:"var(--accent)",
        boxShadow:"0 0 6px var(--accent)",
      }}>
        <div style={{
          position:"absolute", left:-5, top:0, width:12, height:8,
          background:"var(--accent)",
          clipPath:"polygon(0 0, 100% 0, 50% 100%)",
        }} />
      </div>
    </div>
  );
}

function Timeline({ playing, playhead, onScrub }) {
  const PX_PER_BAR = 44;
  const BARS = 32;

  const tracks = [
    { idx:1, name:"DRUM KIT 09", type:"SAMPLER",  color:"#d8412a", clips:[ {bar:0,len:8,seed:11}, {bar:8,len:8,seed:13}, {bar:16,len:6,seed:17}, {bar:24,len:6,seed:19} ] },
    { idx:2, name:"SUB BASS",   type:"SYNTH",    color:"#e89c2b", clips:[ {bar:4,len:12,seed:23}, {bar:18,len:10,seed:29} ] },
    { idx:3, name:"POLY-6",     type:"SYNTH",    color:"#7ab87a", clips:[ {bar:8,len:16,seed:31, label:"chord stack"} ] },
    { idx:4, name:"FIELD REC",  type:"AUDIO",    color:"#5fa8d8", clips:[ {bar:0,len:32,seed:37, muted:true, label:"birds.wav"} ] },
    { idx:5, name:"VOX",        type:"AUDIO",    color:"#b87ad6", armed:true, clips:[ {bar:12,len:6,seed:41}, {bar:20,len:8,seed:43} ] },
    { idx:6, name:"TAPE LOOP",  type:"AUDIO",    color:"#8d7a5a", soloed:true, clips:[ {bar:0,len:4,seed:47}, {bar:4,len:4,seed:47}, {bar:8,len:4,seed:47}, {bar:12,len:4,seed:47}, {bar:16,len:4,seed:47}, {bar:20,len:4,seed:47}, {bar:24,len:4,seed:47}, {bar:28,len:4,seed:47} ] },
    { idx:7, name:"SEND · REVERB", type:"BUS",   color:"#a8a098", clips:[] },
  ];

  return (
    <div style={{
      background:"var(--panel)",
      border:"1px solid var(--panel-rim)",
      borderRadius:8,
      overflow:"hidden",
      boxShadow:"inset 0 1px 0 rgba(255,255,255,0.5), 0 1px 0 rgba(255,255,255,0.4)",
      display:"flex", flexDirection:"column",
      flex:1, minHeight:0,
    }}>
      {/* header strip */}
      <div style={{
        padding:"8px 12px",
        background:"linear-gradient(180deg, var(--chassis), var(--chassis-2))",
        borderBottom:"1px solid var(--panel-rim)",
        display:"flex", alignItems:"center", justifyContent:"space-between",
      }}>
        <div style={{ display:"flex", alignItems:"center", gap:10 }}>
          <div style={{ width:8, height:8, borderRadius:2, background:"var(--accent)" }} />
          <div style={{ fontSize:10, fontWeight:700, letterSpacing:"0.18em", color:"var(--ink)", textTransform:"uppercase" }}>
            ARRANGEMENT
          </div>
          <Tag>32 bars · 4/4 · 124.0 bpm</Tag>
          <Tag color="var(--accent)">●  region 02 — "verse"</Tag>
        </div>
        <div style={{ display:"flex", alignItems:"center", gap:6 }}>
          <Tag>SNAP 1/16</Tag>
          <Tag>QUANT 50%</Tag>
          <Tag>FOLLOW</Tag>
        </div>
      </div>

      {/* body grid */}
      <div style={{ display:"grid", gridTemplateColumns:"160px 1fr", flex:1, minHeight:0, overflow:"hidden" }}>
        {/* LEFT: track heads */}
        <div style={{ borderRight:"1px solid var(--panel-rim)", overflow:"hidden", display:"flex", flexDirection:"column" }}>
          {/* spacer to align with ruler */}
          <div style={{ height:22, background:"var(--display-bg)", borderBottom:"1px solid rgba(0,0,0,0.6)", display:"flex", alignItems:"center", padding:"0 8px" }}>
            <span style={{ fontFamily:'"VT323", monospace', fontSize:12, color:"var(--display-fg)", letterSpacing:"0.1em" }}>
              TRK / 07
            </span>
          </div>
          {tracks.map(t => (
            <TrackHead key={t.idx} {...t} />
          ))}
        </div>

        {/* RIGHT: ruler + clips, scrollable */}
        <div className="scroll" style={{ overflow:"auto", position:"relative" }}
          onClick={(e) => {
            const rect = e.currentTarget.getBoundingClientRect();
            const x = e.clientX - rect.left + e.currentTarget.scrollLeft;
            onScrub && onScrub(x / PX_PER_BAR);
          }}
        >
          <div style={{ width: BARS * PX_PER_BAR, position:"relative" }}>
            <Ruler bars={BARS} pxPerBar={PX_PER_BAR} playhead={playhead * PX_PER_BAR} />

            {tracks.map(t => (
              <div key={t.idx} style={{
                height:62, position:"relative",
                borderBottom:"1px solid var(--rule)",
                background: t.idx % 2 === 0
                  ? "linear-gradient(180deg, #ede5d1, #e6dec9)"
                  : "linear-gradient(180deg, #f3ecda, #ede5d1)",
              }}>
                {/* bar gridlines */}
                {Array.from({length: BARS+1}).map((_,i) => (
                  <div key={i} style={{
                    position:"absolute", left:i*PX_PER_BAR, top:0, bottom:0, width:1,
                    background: i % 4 === 0 ? "rgba(0,0,0,0.12)" : "rgba(0,0,0,0.05)",
                  }} />
                ))}
                {t.clips.map((c, i) => (
                  <Clip key={i}
                    x={c.bar * PX_PER_BAR + 2}
                    w={c.len * PX_PER_BAR - 4}
                    color={t.color}
                    label={c.label || `${t.name} · ${i+1}`}
                    waveSeed={c.seed}
                    muted={c.muted}
                  />
                ))}
              </div>
            ))}

            {/* playhead overlay */}
            <div style={{
              position:"absolute", left: playhead * PX_PER_BAR, top:22, bottom:0, width:2,
              background:"var(--accent)",
              boxShadow:"0 0 6px var(--accent)",
              pointerEvents:"none",
              opacity: 0.85,
            }} />
          </div>
        </div>
      </div>
    </div>
  );
}

Object.assign(window, { Timeline });
