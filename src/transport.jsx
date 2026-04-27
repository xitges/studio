/* TRANSPORT — top bar with tape reels, time display, transport buttons, master meter */

const { useEffect: useEffectT, useState: useStateT, useRef: useRefT } = React;

function TapeReel({ spinning=false, size=64, side="L" }) {
  const [angle, setAngle] = useStateT(0);
  const rafRef = useRefT();
  useEffectT(() => {
    if (!spinning) return;
    let last = performance.now();
    const tick = (now) => {
      const dt = now - last; last = now;
      setAngle(a => (a + dt * 0.18) % 360);
      rafRef.current = requestAnimationFrame(tick);
    };
    rafRef.current = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(rafRef.current);
  }, [spinning]);

  return (
    <div style={{ width:size, height:size, position:"relative" }}>
      {/* outer rim */}
      <div style={{
        position:"absolute", inset:0, borderRadius:"50%",
        background:"radial-gradient(circle at 30% 25%, #4a4338, #1a1612 70%, #0a0805)",
        boxShadow:"0 2px 4px rgba(0,0,0,0.6), inset 0 1px 0 rgba(255,255,255,0.1)",
        border:"1px solid rgba(0,0,0,0.6)",
      }} />
      {/* tape — visible inner ring */}
      <div style={{
        position:"absolute", inset:size*0.12, borderRadius:"50%",
        background:"radial-gradient(circle, #2a1f15 0%, #1a1208 60%, #0d0805 100%)",
        boxShadow:"inset 0 0 8px rgba(0,0,0,0.8)",
      }} />
      {/* spokes */}
      <div style={{
        position:"absolute", inset:0,
        transform:`rotate(${angle}deg)`,
        transition: spinning ? "none" : "transform 600ms ease-out",
      }}>
        {[0,60,120].map(a => (
          <div key={a} style={{
            position:"absolute", left:"50%", top:"50%",
            width: size*0.7, height:3,
            background:"linear-gradient(90deg, #c7bb9a, #888070)",
            transform:`translate(-50%,-50%) rotate(${a}deg)`,
            borderRadius:1,
            boxShadow:"0 1px 1px rgba(0,0,0,0.6)",
          }} />
        ))}
        {/* hub */}
        <div style={{
          position:"absolute", left:"50%", top:"50%",
          width:size*0.28, height:size*0.28,
          transform:"translate(-50%,-50%)", borderRadius:"50%",
          background:"radial-gradient(circle at 30% 25%, #f3ecda, #c7bb9a 60%, #8d8268)",
          boxShadow:"0 1px 2px rgba(0,0,0,0.5), inset 0 1px 0 rgba(255,255,255,0.6)",
          border:"1px solid rgba(0,0,0,0.5)",
        }}>
          <div style={{
            position:"absolute", left:"50%", top:"50%",
            width:3, height:3, borderRadius:"50%",
            background:"var(--accent)",
            transform:"translate(-50%,-50%)",
          }} />
        </div>
      </div>
      <div style={{
        position:"absolute", left:0, right:0, bottom:-12, textAlign:"center",
        fontSize:8, fontWeight:600, letterSpacing:"0.18em", color:"var(--ink-faint)",
      }}>{side}</div>
    </div>
  );
}

function TransportBtn({ icon, active=false, onClick, color, size=44 }) {
  return (
    <button
      onClick={onClick}
      style={{
        width:size, height:size, borderRadius:6, padding:0,
        background: active && color
          ? `radial-gradient(circle at 30% 25%, ${color}cc, ${color} 60%, ${color}88)`
          : "linear-gradient(180deg, #f3ecda 0%, #c7bb9a 60%, #8d8268 100%)",
        border:"1px solid rgba(0,0,0,0.5)",
        boxShadow: active
          ? `0 0 12px ${color}, inset 0 1px 0 rgba(255,255,255,0.4), inset 0 -1px 2px rgba(0,0,0,0.3)`
          : "0 2px 0 rgba(0,0,0,0.5), inset 0 1px 0 rgba(255,255,255,0.6), inset 0 -2px 4px rgba(0,0,0,0.2)",
        cursor:"pointer", display:"flex", alignItems:"center", justifyContent:"center",
        color: active ? "#fff" : "var(--ink)",
        transition:"box-shadow 80ms",
      }}
    >
      {icon}
    </button>
  );
}

function TransportIcons() {
  return null;
}

const ICON = {
  play:    <svg width="14" height="14" viewBox="0 0 14 14"><polygon points="3,2 12,7 3,12" fill="currentColor"/></svg>,
  pause:   <svg width="14" height="14" viewBox="0 0 14 14"><rect x="3" y="2" width="3" height="10" fill="currentColor"/><rect x="8" y="2" width="3" height="10" fill="currentColor"/></svg>,
  stop:    <svg width="14" height="14" viewBox="0 0 14 14"><rect x="3" y="3" width="8" height="8" fill="currentColor"/></svg>,
  rec:     <svg width="14" height="14" viewBox="0 0 14 14"><circle cx="7" cy="7" r="4.5" fill="currentColor"/></svg>,
  rew:     <svg width="14" height="14" viewBox="0 0 14 14"><polygon points="11,2 5,7 11,12" fill="currentColor"/><rect x="3" y="2" width="2" height="10" fill="currentColor"/></svg>,
  ff:      <svg width="14" height="14" viewBox="0 0 14 14"><polygon points="3,2 9,7 3,12" fill="currentColor"/><rect x="9" y="2" width="2" height="10" fill="currentColor"/></svg>,
  loop:    <svg width="14" height="14" viewBox="0 0 14 14" fill="none" stroke="currentColor" strokeWidth="1.4"><path d="M2 7 a5 5 0 1 1 5 5"/><polyline points="5,11 7,12 8,10" fill="currentColor" stroke="none"/></svg>,
};

function MasterMeter({ playing=false, level=0.7 }) {
  const [bars, setBars] = useStateT([0.6,0.7]);
  useEffectT(() => {
    if (!playing) { setBars([0.05,0.05]); return; }
    const id = setInterval(() => {
      setBars([
        Math.min(1, level * (0.5 + Math.random() * 0.7)),
        Math.min(1, level * (0.5 + Math.random() * 0.7)),
      ]);
    }, 90);
    return () => clearInterval(id);
  }, [playing, level]);

  const SEGMENTS = 18;
  const renderBar = (v) => (
    <div style={{ display:"flex", flexDirection:"column-reverse", gap:1, height:46 }}>
      {Array.from({length: SEGMENTS}).map((_,i) => {
        const t = i / (SEGMENTS-1);
        const active = t < v;
        const c = t < 0.7 ? "var(--led-green)" : t < 0.88 ? "var(--led-amber)" : "var(--led-red)";
        return (
          <div key={i} style={{
            width:8, height:1.6,
            background: active ? c : "rgba(0,0,0,0.35)",
            boxShadow: active ? `0 0 3px ${c}` : "none",
            borderRadius:0.5,
          }} />
        );
      })}
    </div>
  );

  return (
    <div style={{
      display:"flex", gap:6, padding:"6px 8px",
      background:"var(--display-bg)", borderRadius:4,
      boxShadow:"inset 0 2px 4px rgba(0,0,0,0.8)",
      border:"1px solid rgba(0,0,0,0.6)",
    }}>
      {renderBar(bars[0])}
      {renderBar(bars[1])}
    </div>
  );
}

function Transport({ playing, recording, looping, bpm, position, onPlay, onStop, onRec, onLoop, onBpm, onScrub }) {
  return (
    <div style={{
      display:"grid",
      gridTemplateColumns:"auto 1fr auto",
      gap:14,
      padding:"14px 16px",
      background: "linear-gradient(180deg, var(--chassis) 0%, var(--chassis-2) 100%)",
      borderRadius:12,
      border:"1px solid var(--panel-rim)",
      boxShadow:"inset 0 1px 0 rgba(255,255,255,0.5), 0 1px 0 rgba(255,255,255,0.4), 0 -1px 0 rgba(0,0,0,0.1)",
      alignItems:"center",
      position:"relative",
    }}>
      {/* LEFT: brand + reels */}
      <div style={{ display:"flex", alignItems:"center", gap:18 }}>
        <div style={{ display:"flex", flexDirection:"column", gap:2, paddingRight:14, borderRight:"1px dashed rgba(0,0,0,0.15)" }}>
          <div style={{ display:"flex", alignItems:"baseline", gap:6 }}>
            <div style={{ fontFamily:'"Space Grotesk", sans-serif', fontSize:22, fontWeight:700, letterSpacing:"-0.02em", color:"var(--ink)" }}>
              fieldlab<span style={{ color:"var(--accent)" }}>.</span>
            </div>
          </div>
          <div style={{ fontSize:9, letterSpacing:"0.22em", color:"var(--ink-faint)", fontWeight:600, textTransform:"uppercase" }}>
            TR-1 — TABLETOP DAW
          </div>
          <div style={{ display:"flex", gap:4, marginTop:4 }}>
            <Tag>Studio · 04</Tag>
            <Tag color="var(--accent)">● REC ARM</Tag>
          </div>
        </div>

        <div style={{ display:"flex", alignItems:"center", gap:18, paddingRight:18, borderRight:"1px dashed rgba(0,0,0,0.15)" }}>
          <TapeReel spinning={playing} side="L" />
          {/* tape strip between reels */}
          <div style={{
            width:40, height:3, borderRadius:1,
            background:"linear-gradient(90deg, #2a1f15, #4a3a25, #2a1f15)",
            boxShadow:"0 1px 1px rgba(0,0,0,0.4)",
          }} />
          <TapeReel spinning={playing} side="R" />
        </div>
      </div>

      {/* CENTER: time display + transport */}
      <div style={{ display:"flex", alignItems:"center", gap:14, justifyContent:"center" }}>
        <div style={{ display:"flex", flexDirection:"column", gap:4 }}>
          <SegmentDisplay height={42}>
            {position.bar.toString().padStart(3,"0")}.{position.beat.toString().padStart(2,"0")}.{position.tick.toString().padStart(3,"0")}
          </SegmentDisplay>
          <div style={{ display:"flex", gap:6, justifyContent:"space-between", padding:"0 4px" }}>
            <span style={{ fontSize:8, fontWeight:600, letterSpacing:"0.15em", color:"var(--ink-faint)" }}>BAR</span>
            <span style={{ fontSize:8, fontWeight:600, letterSpacing:"0.15em", color:"var(--ink-faint)" }}>BEAT</span>
            <span style={{ fontSize:8, fontWeight:600, letterSpacing:"0.15em", color:"var(--ink-faint)" }}>TICK</span>
          </div>
        </div>

        <div style={{ display:"flex", alignItems:"center", gap:6 }}>
          <TransportBtn icon={ICON.rew} onClick={() => onScrub && onScrub(-1)} />
          <TransportBtn icon={playing ? ICON.pause : ICON.play} onClick={onPlay} active={playing} color="var(--led-green)" size={50} />
          <TransportBtn icon={ICON.stop} onClick={onStop} />
          <TransportBtn icon={ICON.rec} onClick={onRec} active={recording} color="var(--led-red)" />
          <TransportBtn icon={ICON.ff} onClick={() => onScrub && onScrub(1)} />
          <TransportBtn icon={ICON.loop} onClick={onLoop} active={looping} color="var(--led-amber)" />
        </div>

        <div style={{ display:"flex", flexDirection:"column", alignItems:"center", gap:2 }}>
          <SegmentDisplay height={28} padding="2px 10px">
            {bpm.toFixed(1)}
          </SegmentDisplay>
          <div style={{ fontSize:8, fontWeight:600, letterSpacing:"0.18em", color:"var(--ink-faint)" }}>BPM · 4/4</div>
        </div>
      </div>

      {/* RIGHT: meters + status */}
      <div style={{ display:"flex", alignItems:"center", gap:14 }}>
        <div style={{ display:"flex", flexDirection:"column", gap:5, alignItems:"flex-end" }}>
          <div style={{ display:"flex", alignItems:"center", gap:6 }}>
            <LED on={playing} color="green" size={7} />
            <span style={{ fontSize:9, letterSpacing:"0.15em", color:"var(--ink-soft)", fontWeight:600 }}>PLAY</span>
            <LED on={recording} color="red" size={7} blink={recording} />
            <span style={{ fontSize:9, letterSpacing:"0.15em", color:"var(--ink-soft)", fontWeight:600 }}>REC</span>
            <LED on={looping} color="amber" size={7} />
            <span style={{ fontSize:9, letterSpacing:"0.15em", color:"var(--ink-soft)", fontWeight:600 }}>LOOP</span>
          </div>
          <div style={{ fontSize:9, letterSpacing:"0.12em", color:"var(--ink-faint)" }}>
            44.1kHz · 24bit · BUF 128
          </div>
          <div style={{ fontSize:9, letterSpacing:"0.12em", color:"var(--ink-faint)" }}>
            CPU <span style={{ color:"var(--ink)" }}>21%</span> · MEM 1.4G
          </div>
        </div>

        <div style={{ display:"flex", flexDirection:"column", alignItems:"center", gap:4 }}>
          <MasterMeter playing={playing} level={0.78} />
          <div style={{ fontSize:8, fontWeight:600, letterSpacing:"0.18em", color:"var(--ink-faint)" }}>MASTER L · R</div>
        </div>

        <Knob defaultValue={0.82} label="MASTER" accent size={48} />
      </div>
    </div>
  );
}

Object.assign(window, { Transport, TapeReel });
