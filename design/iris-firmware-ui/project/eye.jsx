/* =====================================================
   BionicEye — three concentric rotating arcs + pupil
   state: 'idle' | 'capture' | 'think' | 'speak' | 'error'
   size:  pixel diameter (default 200)
   ===================================================== */

const STATE_COLOR = {
  idle:    "var(--state-idle)",
  capture: "var(--state-capture)",
  think:   "var(--state-think)",
  speak:   "var(--state-speak)",
  error:   "var(--state-error)",
};

function BionicEye({ state = "idle", size = 200, intensity = 1 }) {
  const color = STATE_COLOR[state] || STATE_COLOR.idle;

  // Animation tuning per state
  const spin = {
    idle:    { o: 18, m: 28, i: 14 },   // sec / rev
    capture: { o:  6, m:  4, i:  3 },
    think:   { o:  3, m:  2, i:  1.4 },
    speak:   { o: 12, m:  9, i:  6 },
    error:   { o: 30, m: 30, i: 30 },
  }[state] || { o: 18, m: 28, i: 14 };

  const pupilAnim = {
    idle:    "pupil-pulse 3.2s ease-in-out infinite",
    capture: "pupil-pulse 0.4s ease-out 1",
    think:   "pupil-think 1.1s ease-in-out infinite",
    speak:   "pupil-pulse 0.6s ease-in-out infinite",
    error:   "error-pulse 1.2s ease-out infinite",
  }[state];

  const s = size;
  const cx = s / 2;
  const cy = s / 2;

  // Arc radii
  const rOuter = s * 0.46;
  const rMid   = s * 0.36;
  const rInner = s * 0.26;
  const rPupil = s * 0.13;

  return (
    <div
      style={{
        position: "relative",
        width: s,
        height: s,
        color,
        filter: state === "error" ? "none" : `drop-shadow(0 0 ${12*intensity}px ${color}66)`,
      }}
    >
      {/* Outer glow halo */}
      <div
        style={{
          position: "absolute",
          inset: 0,
          borderRadius: "50%",
          background: `radial-gradient(circle, ${color}22 0%, transparent 60%)`,
          animation: "glow-breathe 3.6s ease-in-out infinite",
        }}
      />

      {/* Outer arc */}
      <svg
        width={s} height={s}
        style={{
          position: "absolute", inset: 0,
          animation: `spin-cw ${spin.o}s linear infinite`,
        }}
      >
        <defs>
          <linearGradient id={`g-out-${state}`} x1="0" y1="0" x2="1" y2="1">
            <stop offset="0%" stopColor={color} stopOpacity="0.95" />
            <stop offset="55%" stopColor={color} stopOpacity="0.15" />
            <stop offset="100%" stopColor={color} stopOpacity="0" />
          </linearGradient>
        </defs>
        {/* Faint full ring */}
        <circle cx={cx} cy={cy} r={rOuter} fill="none"
          stroke={color} strokeOpacity="0.08" strokeWidth="1.2" />
        {/* Bright arc */}
        <path
          d={describeArc(cx, cy, rOuter, 0, 220)}
          fill="none"
          stroke={`url(#g-out-${state})`}
          strokeWidth="2"
          strokeLinecap="round"
        />
        {/* Tick marks */}
        {Array.from({length: 24}).map((_,i) => {
          const a = (i * 15) * Math.PI / 180;
          const r1 = rOuter - 4;
          const r2 = rOuter + 4;
          return (
            <line key={i}
              x1={cx + Math.cos(a)*r1} y1={cy + Math.sin(a)*r1}
              x2={cx + Math.cos(a)*r2} y2={cy + Math.sin(a)*r2}
              stroke={color} strokeOpacity={i%6===0 ? 0.55 : 0.18}
              strokeWidth={i%6===0 ? 1.2 : 0.8}
            />
          );
        })}
      </svg>

      {/* Mid arc (counter rotating) */}
      <svg
        width={s} height={s}
        style={{
          position: "absolute", inset: 0,
          animation: `spin-ccw ${spin.m}s linear infinite`,
        }}
      >
        <defs>
          <linearGradient id={`g-mid-${state}`} x1="0" y1="0" x2="1" y2="0">
            <stop offset="0%" stopColor={color} stopOpacity="0" />
            <stop offset="50%" stopColor={color} stopOpacity="0.9" />
            <stop offset="100%" stopColor={color} stopOpacity="0" />
          </linearGradient>
        </defs>
        <circle cx={cx} cy={cy} r={rMid} fill="none"
          stroke={color} strokeOpacity="0.12" strokeWidth="1" strokeDasharray="2 4" />
        <path
          d={describeArc(cx, cy, rMid, 30, 150)}
          fill="none"
          stroke={`url(#g-mid-${state})`}
          strokeWidth="1.5"
          strokeLinecap="round"
        />
        <path
          d={describeArc(cx, cy, rMid, 210, 330)}
          fill="none"
          stroke={`url(#g-mid-${state})`}
          strokeWidth="1.5"
          strokeLinecap="round"
          opacity="0.55"
        />
      </svg>

      {/* Inner arc */}
      <svg
        width={s} height={s}
        style={{
          position: "absolute", inset: 0,
          animation: `spin-cw ${spin.i}s linear infinite`,
        }}
      >
        <circle cx={cx} cy={cy} r={rInner} fill="none"
          stroke={color} strokeOpacity="0.22" strokeWidth="1" />
        <path
          d={describeArc(cx, cy, rInner, 60, 280)}
          fill="none"
          stroke={color}
          strokeOpacity="0.85"
          strokeWidth="2"
          strokeLinecap="round"
        />
        {/* Orbital dot */}
        <circle
          cx={cx + rInner} cy={cy}
          r="2.2" fill={color}
          style={{ filter: `drop-shadow(0 0 4px ${color})` }}
        />
      </svg>

      {/* Pupil core */}
      <div style={{
        position: "absolute",
        left: cx - rPupil,
        top:  cy - rPupil,
        width: rPupil*2,
        height: rPupil*2,
        borderRadius: "50%",
        background: `radial-gradient(circle at 35% 35%, ${color}, ${color}33 70%, transparent 100%)`,
        boxShadow: `0 0 ${20*intensity}px ${color}, inset 0 0 ${rPupil}px ${color}88`,
        animation: pupilAnim,
      }}>
        {/* Inner highlight */}
        <div style={{
          position: "absolute",
          width: "30%", height: "30%",
          left: "20%", top: "20%",
          background: "rgba(255,255,255,0.7)",
          borderRadius: "50%",
          filter: "blur(2px)",
        }} />
      </div>

      {/* Crosshair reticle (subtle) */}
      <svg width={s} height={s} style={{ position: "absolute", inset: 0, opacity: 0.25 }}>
        <line x1={cx} y1={cy-rOuter-6} x2={cx} y2={cy-rOuter+2} stroke={color} strokeWidth="0.8" />
        <line x1={cx} y1={cy+rOuter-2} x2={cx} y2={cy+rOuter+6} stroke={color} strokeWidth="0.8" />
        <line x1={cx-rOuter-6} y1={cy} x2={cx-rOuter+2} y2={cy} stroke={color} strokeWidth="0.8" />
        <line x1={cx+rOuter-2} y1={cy} x2={cx+rOuter+6} y2={cy} stroke={color} strokeWidth="0.8" />
      </svg>
    </div>
  );
}

function polarToCartesian(cx, cy, r, angleDeg) {
  const a = (angleDeg - 90) * Math.PI / 180;
  return { x: cx + r * Math.cos(a), y: cy + r * Math.sin(a) };
}

function describeArc(cx, cy, r, startAngle, endAngle) {
  const start = polarToCartesian(cx, cy, r, endAngle);
  const end   = polarToCartesian(cx, cy, r, startAngle);
  const large = endAngle - startAngle <= 180 ? "0" : "1";
  return [
    "M", start.x, start.y,
    "A", r, r, 0, large, 0, end.x, end.y,
  ].join(" ");
}

window.BionicEye = BionicEye;
