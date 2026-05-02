/* =====================================================
   Status bar shared by all screens
   ===================================================== */
function StatusBar({ time = "14:22", state = "idle", batt = 78, mode = "NAV" }) {
  const dotColor = {
    idle: "var(--state-idle)", capture: "var(--state-capture)",
    think: "var(--state-think)", speak: "var(--state-speak)", error: "var(--state-error)",
  }[state];

  return (
    <div className="statusbar">
      <div>
        <span className="dot" style={{ background: dotColor, color: dotColor }} />
        <span>{mode}</span>
      </div>
      <div style={{ display: "flex", gap: 10, alignItems: "center" }}>
        <span>{time}</span>
        <BattGlyph pct={batt} />
      </div>
    </div>
  );
}

function BattGlyph({ pct = 78 }) {
  const w = 18, h = 8;
  return (
    <svg width={w + 3} height={h + 2} viewBox={`0 0 ${w + 3} ${h + 2}`}>
      <rect x="0.5" y="0.5" width={w} height={h} rx="1.5"
        fill="none" stroke="currentColor" strokeOpacity="0.55" />
      <rect x={w + 0.8} y={h*0.25 + 0.5} width="2" height={h*0.5} rx="0.5"
        fill="currentColor" fillOpacity="0.55" />
      <rect x="2" y="2" width={(w-3) * (pct/100)} height={h-3}
        fill={pct < 20 ? "var(--state-error)" : "var(--fg-1)"} />
    </svg>
  );
}

/* =====================================================
   Screen 1 — BOOT / SPLASH (2-second startup)
   ===================================================== */
function ScreenBoot() {
  return (
    <div className="screen">
      <div style={{
        position: "absolute", inset: 0,
        display: "flex", flexDirection: "column",
        alignItems: "center", justifyContent: "center",
        gap: 28,
      }}>
        <div style={{ animation: "boot-iris 1.4s cubic-bezier(.2,.9,.3,1) both" }}>
          <BionicEye state="idle" size={180} />
        </div>
        <div style={{ textAlign: "center" }}>
          <div style={{
            fontFamily: "var(--font-display)",
            fontSize: 30, fontWeight: 600,
            letterSpacing: "0.18em",
            color: "var(--fg-0)",
          }}>
            IRIS
          </div>
          <div className="mono upper" style={{
            fontSize: 10, color: "var(--fg-2)",
            marginTop: 6, letterSpacing: "0.32em",
          }}>
            VISION CO-PILOT · v2.3.1
          </div>
        </div>
      </div>

      {/* Boot progress hairline */}
      <div style={{
        position: "absolute", left: 40, right: 40, bottom: 56,
        height: 1, background: "var(--bg-line)",
      }}>
        <div style={{
          height: "100%", width: "100%",
          background: "var(--state-idle)",
          transformOrigin: "left",
          animation: "spin-cw 0s",
          transform: "scaleX(0.7)",
          boxShadow: "0 0 6px var(--state-idle)",
        }} />
      </div>
      <div className="mono upper" style={{
        position: "absolute", left: 0, right: 0, bottom: 32,
        textAlign: "center",
        fontSize: 10, color: "var(--fg-2)",
        letterSpacing: "0.24em",
      }}>
        CALIBRATING SENSORS
      </div>
    </div>
  );
}

/* =====================================================
   Screen 2 — IDLE READY (default home)
   ===================================================== */
function ScreenIdle() {
  return (
    <div className="screen">
      <StatusBar state="idle" mode="NAV" />

      <div style={{
        position: "absolute", inset: 0,
        display: "flex", flexDirection: "column",
        alignItems: "center", justifyContent: "center",
      }}>
        <BionicEye state="idle" size={220} />
      </div>

      {/* Top hint */}
      <div style={{
        position: "absolute", top: 48, left: 0, right: 0,
        textAlign: "center",
      }}>
        <div className="mono upper" style={{
          fontSize: 10, color: "var(--fg-2)",
          letterSpacing: "0.32em",
        }}>
          READY · LISTENING
        </div>
        <div style={{
          marginTop: 6,
          fontSize: 24, fontWeight: 500,
          color: "var(--fg-0)",
          letterSpacing: "-0.01em",
        }}>
          Tap to navigate
        </div>
      </div>

      {/* Mode hints — gesture cheatsheet */}
      <ModeHints />
    </div>
  );
}

function ModeHints() {
  const items = [
    { dir: "▲", label: "IDENTIFY", note: "swipe up" },
    { dir: "▼", label: "READ",     note: "swipe down" },
    { dir: "●", label: "NAVIGATE", note: "tap" },
  ];
  return (
    <div style={{
      position: "absolute", bottom: 24, left: 16, right: 16,
      display: "flex", justifyContent: "space-between",
      alignItems: "center",
    }}>
      {items.map((it, i) => (
        <div key={i} style={{
          flex: 1, textAlign: "center",
          color: "var(--fg-2)",
        }}>
          <div style={{
            fontSize: 14, color: "var(--fg-1)",
            marginBottom: 2,
          }}>{it.dir}</div>
          <div className="mono upper" style={{
            fontSize: 10, letterSpacing: "0.18em",
            color: "var(--fg-1)",
          }}>{it.label}</div>
          <div className="mono" style={{
            fontSize: 9, color: "var(--fg-3)",
            marginTop: 1, letterSpacing: "0.1em",
          }}>{it.note}</div>
        </div>
      ))}
    </div>
  );
}

/* =====================================================
   Screen 3 — CAPTURING (200ms shutter)
   ===================================================== */
function ScreenCapturing() {
  return (
    <div className="screen">
      <StatusBar state="capture" mode="NAV" />

      {/* shutter flash */}
      <div style={{
        position: "absolute", inset: 0,
        background: "radial-gradient(circle, var(--state-capture)33 0%, transparent 70%)",
        animation: "shutter-flash 0.6s ease-out infinite",
        zIndex: 5,
      }} />

      {/* Frame brackets */}
      <FrameBrackets color="var(--state-capture)" />

      <div style={{
        position: "absolute", inset: 0,
        display: "flex", flexDirection: "column",
        alignItems: "center", justifyContent: "center",
      }}>
        <BionicEye state="capture" size={200} />
      </div>

      <div style={{
        position: "absolute", top: 56, left: 0, right: 0,
        textAlign: "center",
      }}>
        <div className="mono upper" style={{
          fontSize: 11, color: "var(--state-capture)",
          letterSpacing: "0.3em",
        }}>
          ◉ CAPTURING
        </div>
      </div>

      <div style={{
        position: "absolute", bottom: 60, left: 0, right: 0,
        textAlign: "center",
      }}>
        <div className="mono" style={{
          fontSize: 11, color: "var(--fg-2)",
          letterSpacing: "0.16em",
        }}>
          F / 1.8 · ISO 400 · 12 MP
        </div>
      </div>
    </div>
  );
}

function FrameBrackets({ color = "var(--state-idle)" }) {
  const inset = 24;
  const len = 22;
  const w = 2;
  const corners = [
    { top: inset, left: inset, t: true, l: true },
    { top: inset, right: inset, t: true, r: true },
    { bottom: inset, left: inset, b: true, l: true },
    { bottom: inset, right: inset, b: true, r: true },
  ];
  return (
    <>
      {corners.map((c, i) => (
        <div key={i} style={{
          position: "absolute",
          width: len, height: len,
          ...(c.top !== undefined && { top: c.top }),
          ...(c.bottom !== undefined && { bottom: c.bottom }),
          ...(c.left !== undefined && { left: c.left }),
          ...(c.right !== undefined && { right: c.right }),
          borderTop:    c.t ? `${w}px solid ${color}` : "none",
          borderBottom: c.b ? `${w}px solid ${color}` : "none",
          borderLeft:   c.l ? `${w}px solid ${color}` : "none",
          borderRight:  c.r ? `${w}px solid ${color}` : "none",
          opacity: 0.7,
          zIndex: 4,
        }} />
      ))}
    </>
  );
}

/* =====================================================
   Screen 4 — THINKING
   ===================================================== */
function ScreenThinking() {
  return (
    <div className="screen">
      <StatusBar state="think" mode="NAV" />

      <div style={{
        position: "absolute", inset: 0,
        display: "flex", flexDirection: "column",
        alignItems: "center", justifyContent: "center",
      }}>
        <BionicEye state="think" size={210} intensity={1.4} />
      </div>

      <div style={{
        position: "absolute", top: 52, left: 0, right: 0,
        textAlign: "center",
      }}>
        <div className="mono upper" style={{
          fontSize: 11, color: "var(--state-think)",
          letterSpacing: "0.3em",
          animation: "glow-breathe 1.6s ease-in-out infinite",
        }}>
          ◈ ANALYZING SCENE
        </div>
      </div>

      {/* Sweeping data bars */}
      <div style={{
        position: "absolute", bottom: 52, left: 32, right: 32,
        display: "flex", flexDirection: "column", gap: 6,
      }}>
        {[
          { label: "OBJECTS",  delay: 0,    pct: 78 },
          { label: "DEPTH",    delay: 0.3,  pct: 62 },
          { label: "OCR",      delay: 0.6,  pct: 41 },
          { label: "HAZARDS",  delay: 0.9,  pct: 88 },
        ].map((b, i) => (
          <DataBar key={i} {...b} />
        ))}
      </div>
    </div>
  );
}

function DataBar({ label, delay, pct }) {
  return (
    <div style={{ display: "flex", alignItems: "center", gap: 8 }}>
      <div className="mono upper" style={{
        fontSize: 9, width: 56,
        color: "var(--fg-2)", letterSpacing: "0.18em",
      }}>{label}</div>
      <div style={{
        flex: 1, height: 3, background: "var(--bg-line)",
        position: "relative", overflow: "hidden", borderRadius: 2,
      }}>
        <div style={{
          height: "100%",
          width: `${pct}%`,
          background: "linear-gradient(90deg, transparent, var(--state-think), transparent)",
          animation: `glow-breathe 1.4s ease-in-out infinite ${delay}s`,
          boxShadow: "0 0 6px var(--state-think)",
        }} />
      </div>
      <div className="mono" style={{
        fontSize: 9, color: "var(--fg-1)", width: 24, textAlign: "right",
      }}>{pct}%</div>
    </div>
  );
}

/* =====================================================
   Screen 5 — SPEAKING + RESPONSE
   ===================================================== */
function ScreenSpeaking() {
  const lines = [
    { k: "PATH STATUS", v: "CLEAR",            tone: "ok"   },
    { k: "WHERE",       v: "open sidewalk · person at 1 o'clock · 4 steps", tone: "info" },
    { k: "ACTION",      v: "Go forward",       tone: "ok"   },
    { k: "WHY",         v: "low pedestrian flow, no curbs ahead", tone: "info" },
  ];

  return (
    <div className="screen">
      <StatusBar state="speak" mode="NAV" />

      {/* Eye small in top */}
      <div style={{
        position: "absolute", top: 30, left: 0, right: 0,
        display: "flex", justifyContent: "center",
      }}>
        <BionicEye state="speak" size={70} />
      </div>

      {/* Audio waveform under eye */}
      <AudioBars />

      {/* Response text */}
      <div style={{
        position: "absolute",
        top: 200, left: 18, right: 18,
        display: "flex", flexDirection: "column", gap: 12,
      }}>
        {lines.map((ln, i) => (
          <div key={i} style={{
            animation: `type-in 0.5s ${0.1 + i*0.4}s ease-out both`,
          }}>
            <div className="mono upper" style={{
              fontSize: 9, letterSpacing: "0.24em",
              color: "var(--fg-2)", marginBottom: 3,
            }}>{ln.k}</div>
            <div style={{
              fontSize: ln.k === "ACTION" ? 26 : 16,
              fontWeight: ln.k === "ACTION" ? 600 : 500,
              lineHeight: 1.18,
              color: ln.k === "ACTION" ? "var(--state-speak)" : "var(--fg-0)",
              letterSpacing: "-0.005em",
            }}>{ln.v}</div>
          </div>
        ))}
      </div>

      <div style={{
        position: "absolute", bottom: 16, left: 0, right: 0,
        textAlign: "center",
      }}>
        <div className="mono upper" style={{
          fontSize: 9, color: "var(--fg-3)",
          letterSpacing: "0.24em",
        }}>
          double-tap to repeat
        </div>
      </div>
    </div>
  );
}

function AudioBars() {
  const bars = Array.from({ length: 21 });
  return (
    <div style={{
      position: "absolute", top: 122, left: 0, right: 0,
      display: "flex", justifyContent: "center",
      alignItems: "center", gap: 3, height: 48,
    }}>
      {bars.map((_, i) => {
        const dist = Math.abs(i - 10) / 10;
        const baseH = 8 + (1 - dist) * 28;
        return (
          <div key={i} style={{
            width: 3,
            height: baseH,
            background: "var(--state-speak)",
            borderRadius: 1.5,
            transformOrigin: "center",
            animation: `audio-bar ${0.6 + dist * 0.4}s ease-in-out infinite`,
            animationDelay: `${i * 0.04}s`,
            boxShadow: `0 0 ${4 + (1-dist)*4}px var(--state-speak)`,
            opacity: 0.4 + (1 - dist) * 0.6,
          }} />
        );
      })}
    </div>
  );
}

/* =====================================================
   Screen 6 — MODE-SWITCH BANNER
   ===================================================== */
function ScreenModeBanner() {
  return (
    <div className="screen">
      <StatusBar state="idle" mode="READ" />

      {/* Background eye dimmed */}
      <div style={{
        position: "absolute", inset: 0,
        display: "flex", alignItems: "center", justifyContent: "center",
        opacity: 0.35,
      }}>
        <BionicEye state="idle" size={220} />
      </div>

      {/* Banner */}
      <div style={{
        position: "absolute", top: 30, left: 16, right: 16,
        background: "linear-gradient(180deg, rgba(255,179,71,0.18), rgba(255,179,71,0.04))",
        border: "1px solid var(--state-capture)",
        borderRadius: 14,
        padding: "18px 16px",
        boxShadow: "0 0 24px rgba(255,179,71,0.2)",
        animation: "boot-iris 0.5s ease-out",
      }}>
        <div style={{ display: "flex", alignItems: "center", gap: 12 }}>
          <ReadGlyph />
          <div>
            <div className="mono upper" style={{
              fontSize: 9, color: "var(--state-capture)",
              letterSpacing: "0.28em",
            }}>MODE</div>
            <div style={{
              fontSize: 28, fontWeight: 600,
              color: "var(--fg-0)",
              letterSpacing: "-0.01em",
              lineHeight: 1,
            }}>READ</div>
          </div>
        </div>
        <div style={{
          marginTop: 12,
          fontSize: 14, color: "var(--fg-1)",
          lineHeight: 1.35,
        }}>
          Reads text, currency, expiry dates and labels aloud — verbatim.
        </div>

        {/* progress bar */}
        <div style={{
          marginTop: 14,
          height: 2, background: "var(--bg-line)",
          borderRadius: 2, overflow: "hidden",
        }}>
          <div style={{
            height: "100%",
            width: "100%",
            background: "var(--state-capture)",
            transformOrigin: "left",
            animation: "spin-ccw 1.6s linear",
            transform: "scaleX(0.7)",
          }} />
        </div>
      </div>

      {/* All three modes — inactive ones below */}
      <div style={{
        position: "absolute", bottom: 70, left: 16, right: 16,
        display: "flex", flexDirection: "column", gap: 8,
      }}>
        <ModeRow icon={<NavGlyph dim />} label="NAVIGATE"  desc="path · obstacles · doors" dim />
        <ModeRow icon={<IdGlyph dim  />} label="IDENTIFY"  desc="object · safety · context" dim />
      </div>

      <div style={{
        position: "absolute", bottom: 22, left: 0, right: 0,
        textAlign: "center",
      }}>
        <div className="mono upper" style={{
          fontSize: 9, color: "var(--fg-3)",
          letterSpacing: "0.24em",
        }}>
          swipe to switch · double-click to read
        </div>
      </div>
    </div>
  );
}

function ModeRow({ icon, label, desc, dim }) {
  return (
    <div style={{
      display: "flex", alignItems: "center", gap: 12,
      padding: "8px 12px",
      background: "rgba(255,255,255,0.02)",
      border: "1px solid var(--bg-line)",
      borderRadius: 10,
      opacity: dim ? 0.55 : 1,
    }}>
      {icon}
      <div>
        <div className="mono upper" style={{
          fontSize: 11, color: "var(--fg-1)",
          letterSpacing: "0.2em", fontWeight: 600,
        }}>{label}</div>
        <div className="mono" style={{
          fontSize: 9, color: "var(--fg-2)",
          letterSpacing: "0.08em", marginTop: 1,
        }}>{desc}</div>
      </div>
    </div>
  );
}

/* Mode glyphs — minimal, recognizable to firmware */
function NavGlyph({ dim }) {
  return (
    <svg width="22" height="22" viewBox="0 0 22 22" fill="none">
      <circle cx="11" cy="11" r="9" stroke="var(--state-idle)" strokeOpacity={dim?0.4:1} strokeWidth="1.2" />
      <path d="M11 4 L14 14 L11 12 L8 14 Z" fill="var(--state-idle)" fillOpacity={dim?0.4:1} />
    </svg>
  );
}
function ReadGlyph() {
  return (
    <svg width="34" height="34" viewBox="0 0 34 34" fill="none">
      <rect x="5" y="7" width="24" height="20" rx="2" stroke="var(--state-capture)" strokeWidth="1.4" />
      <line x1="9" y1="13" x2="25" y2="13" stroke="var(--state-capture)" strokeWidth="1.2" />
      <line x1="9" y1="17" x2="22" y2="17" stroke="var(--state-capture)" strokeWidth="1.2" opacity="0.7" />
      <line x1="9" y1="21" x2="19" y2="21" stroke="var(--state-capture)" strokeWidth="1.2" opacity="0.4" />
    </svg>
  );
}
function IdGlyph({ dim }) {
  return (
    <svg width="22" height="22" viewBox="0 0 22 22" fill="none">
      <rect x="3.5" y="3.5" width="15" height="15" rx="2"
        stroke="var(--state-think)" strokeOpacity={dim?0.4:1} strokeWidth="1.2" />
      <circle cx="11" cy="11" r="3" fill="var(--state-think)" fillOpacity={dim?0.4:1} />
    </svg>
  );
}

/* =====================================================
   Screen 7 — ERROR / NO IMAGE
   ===================================================== */
function ScreenError() {
  return (
    <div className="screen">
      <StatusBar state="error" mode="NAV" />

      <div style={{
        position: "absolute", inset: 0,
        display: "flex", flexDirection: "column",
        alignItems: "center", justifyContent: "center",
      }}>
        <BionicEye state="error" size={180} />
      </div>

      <div style={{
        position: "absolute", top: 56, left: 0, right: 0,
        textAlign: "center",
      }}>
        <div className="mono upper" style={{
          fontSize: 11, color: "var(--state-error)",
          letterSpacing: "0.3em",
        }}>
          ✕ NO IMAGE
        </div>
      </div>

      <div style={{
        position: "absolute", bottom: 100, left: 24, right: 24,
        textAlign: "center",
      }}>
        <div style={{
          fontSize: 24, fontWeight: 500,
          color: "var(--fg-0)",
          lineHeight: 1.18,
          marginBottom: 10,
        }}>
          Lens may be covered.
        </div>
        <div style={{
          fontSize: 14, color: "var(--fg-1)",
          lineHeight: 1.4,
        }}>
          Wipe the camera and tap to retry.
        </div>
      </div>

      {/* Retry chip */}
      <div style={{
        position: "absolute", bottom: 32, left: 0, right: 0,
        display: "flex", justifyContent: "center",
      }}>
        <div style={{
          padding: "10px 22px",
          border: "1px solid var(--state-error)",
          borderRadius: 999,
          color: "var(--state-error)",
          fontFamily: "var(--font-mono)",
          fontSize: 11, letterSpacing: "0.24em",
          textTransform: "uppercase",
          background: "rgba(255,92,92,0.06)",
        }}>
          ⟲ tap to retry
        </div>
      </div>
    </div>
  );
}

/* =====================================================
   Screen 8 — SETTINGS OVERLAY
   ===================================================== */
function ScreenSettings() {
  return (
    <div className="screen">
      <StatusBar state="idle" mode="SET" />

      {/* Background eye dimmed */}
      <div style={{
        position: "absolute", inset: 0,
        display: "flex", alignItems: "center", justifyContent: "center",
        opacity: 0.12,
      }}>
        <BionicEye state="idle" size={260} />
      </div>

      <div style={{
        position: "absolute", top: 38, left: 0, right: 0,
        textAlign: "center",
      }}>
        <div className="mono upper" style={{
          fontSize: 10, color: "var(--fg-2)",
          letterSpacing: "0.32em",
        }}>SETTINGS</div>
      </div>

      <div style={{
        position: "absolute", top: 78, left: 16, right: 16,
        display: "flex", flexDirection: "column", gap: 14,
      }}>
        <SettingSlider icon="🔊" label="VOLUME"     value={72} unit="%" />
        <SettingSlider icon="☀"  label="BRIGHTNESS" value={45} unit="%" />
        <SettingPicker label="LANGUAGE" options={["EN","ES","HI","AR"]} active="EN" />
        <SettingPicker label="VOICE"    options={["LO","MID","HI"]}     active="MID" />
      </div>

      {/* Footer hint */}
      <div style={{
        position: "absolute", bottom: 22, left: 0, right: 0,
        textAlign: "center",
      }}>
        <div className="mono upper" style={{
          fontSize: 9, color: "var(--fg-3)",
          letterSpacing: "0.24em",
        }}>
          long-press to close
        </div>
      </div>
    </div>
  );
}

function SettingSlider({ icon, label, value, unit }) {
  return (
    <div style={{
      padding: "12px 14px",
      background: "rgba(255,255,255,0.025)",
      border: "1px solid var(--bg-line)",
      borderRadius: 12,
    }}>
      <div style={{
        display: "flex", justifyContent: "space-between",
        alignItems: "center", marginBottom: 8,
      }}>
        <div style={{ display: "flex", alignItems: "center", gap: 8 }}>
          <span style={{ fontSize: 14, color: "var(--state-idle)" }}>{icon}</span>
          <span className="mono upper" style={{
            fontSize: 11, color: "var(--fg-1)",
            letterSpacing: "0.2em",
          }}>{label}</span>
        </div>
        <span className="mono" style={{
          fontSize: 14, color: "var(--fg-0)", fontWeight: 600,
        }}>{value}{unit}</span>
      </div>
      <div style={{
        height: 4, background: "var(--bg-line)",
        borderRadius: 2, position: "relative", overflow: "hidden",
      }}>
        <div style={{
          width: `${value}%`, height: "100%",
          background: "linear-gradient(90deg, var(--state-idle)44, var(--state-idle))",
          boxShadow: "0 0 6px var(--state-idle)",
        }} />
        <div style={{
          position: "absolute",
          left: `calc(${value}% - 6px)`, top: -4,
          width: 12, height: 12, borderRadius: "50%",
          background: "var(--state-idle)",
          boxShadow: "0 0 8px var(--state-idle), inset 0 0 0 2px var(--bg-0)",
        }} />
      </div>
    </div>
  );
}

function SettingPicker({ label, options, active }) {
  return (
    <div style={{
      padding: "10px 14px",
      background: "rgba(255,255,255,0.025)",
      border: "1px solid var(--bg-line)",
      borderRadius: 12,
    }}>
      <div className="mono upper" style={{
        fontSize: 11, color: "var(--fg-1)",
        letterSpacing: "0.2em", marginBottom: 8,
      }}>{label}</div>
      <div style={{ display: "flex", gap: 6 }}>
        {options.map(opt => {
          const on = opt === active;
          return (
            <div key={opt} style={{
              flex: 1,
              padding: "8px 0",
              textAlign: "center",
              fontFamily: "var(--font-mono)",
              fontSize: 12, fontWeight: 600,
              letterSpacing: "0.12em",
              border: `1px solid ${on ? "var(--state-idle)" : "var(--bg-line)"}`,
              background: on ? "rgba(79,227,240,0.12)" : "transparent",
              color: on ? "var(--state-idle)" : "var(--fg-2)",
              borderRadius: 8,
              boxShadow: on ? "0 0 8px rgba(79,227,240,0.3)" : "none",
            }}>{opt}</div>
          );
        })}
      </div>
    </div>
  );
}

window.IRIS = {
  ScreenBoot, ScreenIdle, ScreenCapturing, ScreenThinking,
  ScreenSpeaking, ScreenModeBanner, ScreenError, ScreenSettings,
};
