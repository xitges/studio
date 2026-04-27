/* Reusable hardware atoms: Knob, Fader, LED, SegmentDisplay, Pad, Screw, MetalRim */

const { useState, useRef, useEffect, useCallback, useMemo } = React;

/* ---------- KNOB ---------- */
function Knob({ value: controlledValue, defaultValue=0.5, min=0, max=1, size=56, label, unit="", color="var(--ink)", onChange, marks=true, accent=false }) {
  const [internal, setInternal] = useState(defaultValue);
  const value = controlledValue ?? internal;
  const set = (v) => {
    const clamped = Math.max(min, Math.min(max, v));
    if (controlledValue === undefined) setInternal(clamped);
    onChange && onChange(clamped);
  };
  const ref = useRef(null);
  const dragRef = useRef({ y:0, v:0 });

  const onPointerDown = (e) => {
    e.preventDefault();
    dragRef.current = { y: e.clientY, v: value };
    const move = (ev) => {
      const dy = dragRef.current.y - ev.clientY;
      const range = max - min;
      const next = dragRef.current.v + (dy / 140) * range;
      set(next);
    };
    const up = () => {
      window.removeEventListener("pointermove", move);
      window.removeEventListener("pointerup", up);
    };
    window.addEventListener("pointermove", move);
    window.addEventListener("pointerup", up);
  };

  // -135deg to +135deg
  const norm = (value - min) / (max - min);
  const angle = -135 + norm * 270;

  return (
    <div style={{ display:"flex", flexDirection:"column", alignItems:"center", gap:6, userSelect:"none" }}>
      <div
        ref={ref}
        onPointerDown={onPointerDown}
        style={{
          width: size, height: size, position:"relative", cursor:"ns-resize",
          borderRadius:"50%",
          background: accent
            ? "radial-gradient(circle at 30% 25%, var(--accent-2), var(--accent) 60%, #8a2616)"
            : "radial-gradient(circle at 30% 25%, #4a4338, #1a1612 70%, #0d0a07)",
          boxShadow: "0 2px 0 rgba(0,0,0,0.5), inset 0 1px 0 rgba(255,255,255,0.18), inset 0 -2px 4px rgba(0,0,0,0.4)",
          border: "1px solid rgba(0,0,0,0.6)",
        }}
      >
        {/* knurling ring */}
        <div style={{
          position:"absolute", inset:3, borderRadius:"50%",
          background: `repeating-conic-gradient(from 0deg, rgba(255,255,255,0.06) 0deg 4deg, transparent 4deg 8deg)`,
          maskImage: "radial-gradient(circle, transparent 50%, black 52%, black 100%)",
          WebkitMaskImage: "radial-gradient(circle, transparent 50%, black 52%, black 100%)",
        }} />
        {/* indicator */}
        <div style={{
          position:"absolute", left:"50%", top:"50%",
          width:2, height: size*0.42,
          background: accent ? "#fff" : "var(--accent)",
          transform:`translate(-50%, -100%) rotate(${angle}deg)`,
          transformOrigin:"50% 100%",
          borderRadius:1,
          boxShadow:"0 0 4px rgba(255,255,255,0.2)",
        }} />
        {/* center cap */}
        <div style={{
          position:"absolute", left:"50%", top:"50%", width: size*0.18, height: size*0.18,
          transform:"translate(-50%,-50%)", borderRadius:"50%",
          background:"radial-gradient(circle at 30% 25%, #2a251e, #0a0806)",
          boxShadow:"inset 0 1px 1px rgba(255,255,255,0.2)",
        }} />
      </div>
      {marks && (
        <div style={{ position:"relative", width:size+12, height:0 }}>
          {[-135, -90, -45, 0, 45, 90, 135].map((a,i) => {
            const rad = a * Math.PI/180;
            const r = size/2 + 4;
            const x = Math.cos(rad - Math.PI/2) * r;
            const y = Math.sin(rad - Math.PI/2) * r;
            return (
              <div key={i} style={{
                position:"absolute",
                left: `calc(50% + ${x}px)`,
                top: y - size/2 - 4,
                width:2, height:4, background:"rgba(0,0,0,0.35)",
                transform:`translate(-50%,0) rotate(${a}deg)`, transformOrigin:"50% 0",
              }} />
            );
          })}
        </div>
      )}
      {label && (
        <div style={{ fontSize:9, fontWeight:600, letterSpacing:"0.12em", color:"var(--ink-soft)", textTransform:"uppercase", marginTop:-2 }}>
          {label}
        </div>
      )}
    </div>
  );
}

/* ---------- FADER ---------- */
function Fader({ value: controlledValue, defaultValue=0.7, min=0, max=1, height=140, label, onChange, color="var(--accent)" }) {
  const [internal, setInternal] = useState(defaultValue);
  const value = controlledValue ?? internal;
  const set = (v) => {
    const c = Math.max(min, Math.min(max, v));
    if (controlledValue === undefined) setInternal(c);
    onChange && onChange(c);
  };
  const trackRef = useRef(null);
  const dragRef = useRef({ y:0, v:0 });

  const onPointerDown = (e) => {
    e.preventDefault();
    dragRef.current = { y: e.clientY, v: value };
    const move = (ev) => {
      const dy = dragRef.current.y - ev.clientY;
      const next = dragRef.current.v + (dy / height) * (max - min);
      set(next);
    };
    const up = () => {
      window.removeEventListener("pointermove", move);
      window.removeEventListener("pointerup", up);
    };
    window.addEventListener("pointermove", move);
    window.addEventListener("pointerup", up);
  };

  const norm = (value - min) / (max - min);

  return (
    <div style={{ display:"flex", flexDirection:"column", alignItems:"center", gap:6, userSelect:"none" }}>
      <div
        ref={trackRef}
        style={{
          width:18, height, position:"relative", borderRadius:3,
          background:"linear-gradient(180deg,#1a1410 0%,#0a0805 100%)",
          boxShadow:"inset 0 2px 4px rgba(0,0,0,0.7), inset 0 -1px 0 rgba(255,255,255,0.05)",
          border:"1px solid rgba(0,0,0,0.5)",
        }}
      >
        {/* tick marks */}
        {[0,0.25,0.5,0.75,1].map(t => (
          <div key={t} style={{
            position:"absolute", left:-6, right:-6, top:`${(1-t)*100}%`,
            height:1, background:"rgba(0,0,0,0.5)",
          }} />
        ))}
        {/* slot line */}
        <div style={{
          position:"absolute", left:"50%", top:8, bottom:8, width:2,
          background:"rgba(255,255,255,0.08)", transform:"translateX(-50%)", borderRadius:1,
        }} />
        {/* cap */}
        <div
          onPointerDown={onPointerDown}
          style={{
            position:"absolute", left:"50%", top:`${(1-norm)*100}%`,
            transform:"translate(-50%,-50%)",
            width:30, height:18, borderRadius:3,
            background:"linear-gradient(180deg, #f3ecda 0%, #c7bb9a 50%, #8d8268 100%)",
            boxShadow:"0 2px 4px rgba(0,0,0,0.6), inset 0 1px 0 rgba(255,255,255,0.5), inset 0 -1px 0 rgba(0,0,0,0.3)",
            cursor:"ns-resize",
            border:"1px solid rgba(0,0,0,0.5)",
          }}
        >
          <div style={{ position:"absolute", left:3, right:3, top:"50%", height:2, background:color, borderRadius:1, transform:"translateY(-50%)" }} />
        </div>
      </div>
      {label && (
        <div style={{ fontSize:9, fontWeight:600, letterSpacing:"0.1em", color:"var(--ink-soft)", textTransform:"uppercase" }}>
          {label}
        </div>
      )}
    </div>
  );
}

/* ---------- LED ---------- */
function LED({ on=false, color="red", size=8, blink=false }) {
  const colorMap = { red:"var(--led-red)", amber:"var(--led-amber)", green:"var(--led-green)" };
  const c = colorMap[color] || color;
  return (
    <div style={{
      width:size, height:size, borderRadius:"50%",
      background: on ? c : "var(--led-off)",
      boxShadow: on ? `0 0 ${size*0.8}px ${c}, inset 0 0 2px rgba(255,255,255,0.6)` : "inset 0 1px 1px rgba(0,0,0,0.5)",
      border:"1px solid rgba(0,0,0,0.5)",
      animation: on && blink ? "led-blink 1s steps(2) infinite" : "none",
    }} />
  );
}

/* ---------- SEGMENT DISPLAY ---------- */
function SegmentDisplay({ children, height=44, mono=true, glow=true, padding="6px 14px" }) {
  return (
    <div style={{
      background: "var(--display-bg)",
      color: "var(--display-fg)",
      fontFamily: '"VT323", "JetBrains Mono", monospace',
      fontSize: height * 0.65,
      lineHeight: `${height}px`,
      height,
      padding,
      borderRadius: 4,
      boxShadow: "inset 0 2px 8px rgba(0,0,0,0.9), inset 0 0 0 1px rgba(0,0,0,0.6), 0 1px 0 rgba(255,255,255,0.4)",
      textShadow: glow ? "0 0 6px rgba(185,255,102,0.6)" : "none",
      letterSpacing: "0.08em",
      display:"inline-flex", alignItems:"center", whiteSpace:"nowrap",
      position:"relative",
    }}>
      {/* scanlines */}
      <div style={{
        position:"absolute", inset:0, pointerEvents:"none", borderRadius:4,
        background:"repeating-linear-gradient(0deg, rgba(0,0,0,0.15) 0px, rgba(0,0,0,0.15) 1px, transparent 1px, transparent 3px)",
      }} />
      <span style={{ position:"relative" }}>{children}</span>
    </div>
  );
}

/* ---------- PAD ---------- */
function Pad({ active=false, lit=false, color="var(--accent)", onClick, size=46, label }) {
  return (
    <button
      onClick={onClick}
      style={{
        width:size, height:size, borderRadius:6, padding:0,
        background: active ? color : "linear-gradient(180deg,#3a342a 0%, #1a1612 100%)",
        border: "1px solid rgba(0,0,0,0.6)",
        boxShadow: active
          ? `0 0 14px ${color}, inset 0 1px 0 rgba(255,255,255,0.4), inset 0 -2px 4px rgba(0,0,0,0.3)`
          : "inset 0 1px 0 rgba(255,255,255,0.08), inset 0 -2px 4px rgba(0,0,0,0.5), 0 1px 0 rgba(255,255,255,0.3)",
        cursor:"pointer",
        position:"relative",
        transition:"background 80ms, box-shadow 80ms",
      }}
    >
      {lit && !active && (
        <div style={{
          position:"absolute", inset:8, borderRadius:3,
          background: `radial-gradient(circle, ${color}aa 0%, ${color}33 60%, transparent 100%)`,
        }} />
      )}
      {label && (
        <div style={{
          position:"absolute", left:0,right:0, bottom:4, fontSize:8, fontWeight:600,
          letterSpacing:"0.08em", color: active ? "rgba(0,0,0,0.7)" : "rgba(255,255,255,0.5)",
        }}>{label}</div>
      )}
    </button>
  );
}

/* ---------- SCREW ---------- */
function Screw({ size=10, angle=Math.random()*180 }) {
  return (
    <div style={{
      width:size, height:size, borderRadius:"50%",
      background:"radial-gradient(circle at 30% 25%, #d8d0bb 0%, #888070 60%, #4a4338 100%)",
      boxShadow:"inset 0 -1px 1px rgba(0,0,0,0.5), 0 1px 1px rgba(0,0,0,0.4)",
      position:"relative",
      border:"1px solid rgba(0,0,0,0.4)",
    }}>
      <div style={{
        position:"absolute", left:"50%", top:"15%", bottom:"15%", width:1,
        background:"rgba(0,0,0,0.6)",
        transform:`translateX(-50%) rotate(${angle}deg)`, transformOrigin:"50% 50%",
      }} />
    </div>
  );
}

/* ---------- PANEL (the labeled, recessed plate) ---------- */
function Panel({ title, sub, children, accent=false, style={}, contentStyle={} }) {
  return (
    <div style={{
      background: "var(--panel)",
      border: "1px solid var(--panel-rim)",
      borderRadius: 8,
      padding: "10px 12px 12px",
      boxShadow:"inset 0 1px 0 rgba(255,255,255,0.6), inset 0 -1px 0 rgba(0,0,0,0.08), 0 1px 0 rgba(255,255,255,0.5)",
      position:"relative",
      ...style,
    }}>
      {title && (
        <div style={{
          display:"flex", alignItems:"center", justifyContent:"space-between",
          marginBottom:8, paddingBottom:6,
          borderBottom: "1px dashed rgba(0,0,0,0.18)",
        }}>
          <div style={{ display:"flex", alignItems:"center", gap:8 }}>
            {accent && <div style={{ width:8, height:8, borderRadius:2, background:"var(--accent)" }} />}
            <div style={{ fontSize:10, fontWeight:700, letterSpacing:"0.18em", color:"var(--ink)", textTransform:"uppercase" }}>
              {title}
            </div>
          </div>
          {sub && <div style={{ fontSize:9, color:"var(--ink-faint)", letterSpacing:"0.1em", textTransform:"uppercase" }}>{sub}</div>}
        </div>
      )}
      <div style={contentStyle}>{children}</div>
    </div>
  );
}

/* ---------- LABEL TAG ---------- */
function Tag({ children, color="var(--ink-soft)", bg="transparent", border=true }) {
  return (
    <span style={{
      fontSize:9, fontWeight:600, letterSpacing:"0.1em", textTransform:"uppercase",
      color, padding:"2px 6px", borderRadius:2,
      border: border ? "1px solid rgba(0,0,0,0.2)" : "none",
      background: bg, display:"inline-block",
    }}>{children}</span>
  );
}

/* ---------- TOGGLE SWITCH (hardware style) ---------- */
function ToggleSwitch({ value=false, onChange, labels=["OFF","ON"] }) {
  return (
    <div style={{ display:"inline-flex", flexDirection:"column", alignItems:"center", gap:4 }}>
      <div
        onClick={() => onChange && onChange(!value)}
        style={{
          width:36, height:18, borderRadius:9, position:"relative", cursor:"pointer",
          background: value ? "var(--accent)" : "#2a251e",
          boxShadow:"inset 0 2px 3px rgba(0,0,0,0.6)",
          border:"1px solid rgba(0,0,0,0.5)",
          transition:"background 120ms",
        }}
      >
        <div style={{
          position:"absolute", top:1, left: value ? 19 : 1, width:14, height:14, borderRadius:"50%",
          background:"linear-gradient(180deg, #f3ecda, #aea58c)",
          boxShadow:"0 1px 2px rgba(0,0,0,0.5), inset 0 1px 0 rgba(255,255,255,0.5)",
          transition:"left 120ms",
        }} />
      </div>
      <div style={{ fontSize:8, letterSpacing:"0.1em", color:"var(--ink-faint)", fontWeight:600 }}>
        {value ? labels[1] : labels[0]}
      </div>
    </div>
  );
}

Object.assign(window, { Knob, Fader, LED, SegmentDisplay, Pad, Screw, Panel, Tag, ToggleSwitch });
