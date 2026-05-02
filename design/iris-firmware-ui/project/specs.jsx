/* =====================================================
   Spec sheet panels — animation, touch zones, tokens, recommendation
   These render INSIDE design-canvas artboards (sized larger than 320×480)
   ===================================================== */

/* ---- Touch zone diagram for IDLE screen ---- */
function TouchZoneSpec() {
  return (
    <div style={{
      width: 720, height: 600,
      background: "var(--bg-0)",
      color: "var(--fg-0)",
      fontFamily: "var(--font-display)",
      padding: 32,
      display: "grid",
      gridTemplateColumns: "320px 1fr",
      gap: 36,
      alignItems: "start",
    }}>
      {/* Device with overlay */}
      <div style={{ position: "relative" }}>
        <div className="screen" style={{ position: "relative" }}>
          <StatusBar state="idle" mode="NAV" />
          <div style={{
            position: "absolute", inset: 0,
            display: "flex", alignItems: "center", justifyContent: "center",
            opacity: 0.4,
          }}>
            <BionicEye state="idle" size={200} />
          </div>

          {/* Overlay zones */}
          <ZoneBox top={22} left={0} right={0} h={120} label="SWIPE ↑" sub="IDENTIFY" col="var(--state-think)" />
          <ZoneBox top={142} left={0} right={0} h={196} label="TAP" sub="NAVIGATE" col="var(--state-idle)" main />
          <ZoneBox top={338} left={0} right={0} h={142} label="SWIPE ↓" sub="READ" col="var(--state-capture)" />

          {/* Two-finger tap badge */}
          <div style={{
            position: "absolute", top: 240, right: 10,
            width: 36, height: 36, borderRadius: "50%",
            border: "1px dashed var(--state-speak)",
            display: "flex", alignItems: "center", justifyContent: "center",
            color: "var(--state-speak)",
            fontFamily: "var(--font-mono)", fontSize: 9,
            background: "rgba(74,222,128,0.08)",
          }}>2F</div>
        </div>
      </div>

      {/* Legend */}
      <div>
        <div className="mono upper" style={{
          fontSize: 10, color: "var(--fg-2)",
          letterSpacing: "0.32em", marginBottom: 8,
        }}>03 · TOUCH ZONES</div>
        <div style={{
          fontSize: 32, fontWeight: 600,
          letterSpacing: "-0.01em", marginBottom: 24,
          lineHeight: 1.05,
        }}>Idle screen<br/>gesture map</div>

        <ZoneRow col="var(--state-idle)" label="TAP — anywhere" desc="Triggers NAVIGATE: capture frame, describe path." />
        <ZoneRow col="var(--state-capture)" label="SWIPE ↓ — top half" desc="Switch to READ mode (text · currency · expiry)." />
        <ZoneRow col="var(--state-think)" label="SWIPE ↑ — bottom half" desc="Switch to IDENTIFY mode (held object + safety)." />
        <ZoneRow col="var(--state-speak)" label="TWO-FINGER TAP" desc="Repeat last spoken response." dashed />
        <ZoneRow col="var(--fg-1)" label="LONG-PRESS · 3s" desc="Open settings overlay." dashed />

        <div style={{
          marginTop: 20,
          padding: 14,
          border: "1px dashed var(--bg-line)",
          borderRadius: 10,
          background: "rgba(255,255,255,0.015)",
        }}>
          <div className="mono upper" style={{
            fontSize: 9, color: "var(--fg-2)",
            letterSpacing: "0.24em", marginBottom: 6,
          }}>HARDWARE BUTTON · SAME ACTIONS</div>
          <div className="mono" style={{ fontSize: 11, color: "var(--fg-1)", lineHeight: 1.7 }}>
            CLICK&nbsp;&nbsp;&nbsp;&nbsp;→ NAVIGATE<br/>
            DOUBLE&nbsp;&nbsp;→ READ<br/>
            HOLD&nbsp;&nbsp;&nbsp;&nbsp;→ IDENTIFY
          </div>
        </div>
      </div>
    </div>
  );
}

function ZoneBox({ top, left, right, h, label, sub, col, main }) {
  return (
    <div style={{
      position: "absolute",
      top, left, right, height: h,
      border: `1px ${main ? "solid" : "dashed"} ${col}`,
      background: `${col}10`,
      display: "flex", flexDirection: "column",
      alignItems: "center", justifyContent: "center",
      gap: 2,
      zIndex: 8,
    }}>
      <div className="mono upper" style={{
        fontSize: main ? 14 : 10, color: col,
        letterSpacing: "0.24em", fontWeight: 700,
      }}>{label}</div>
      <div className="mono upper" style={{
        fontSize: 9, color: col, opacity: 0.7,
        letterSpacing: "0.2em",
      }}>{sub}</div>
    </div>
  );
}

function ZoneRow({ col, label, desc, dashed }) {
  return (
    <div style={{
      display: "grid",
      gridTemplateColumns: "20px 1fr",
      gap: 12, alignItems: "start",
      padding: "10px 0",
      borderBottom: "1px solid var(--bg-line)",
    }}>
      <div style={{
        width: 14, height: 14, marginTop: 3, borderRadius: 3,
        border: `1.5px ${dashed ? "dashed" : "solid"} ${col}`,
        background: `${col}22`,
      }} />
      <div>
        <div className="mono upper" style={{
          fontSize: 11, color: "var(--fg-0)",
          letterSpacing: "0.14em", fontWeight: 600,
        }}>{label}</div>
        <div style={{ fontSize: 13, color: "var(--fg-1)", marginTop: 2, lineHeight: 1.35 }}>
          {desc}
        </div>
      </div>
    </div>
  );
}

/* ---- Animation timing spec ---- */
function AnimationSpec() {
  const rows = [
    { el: "Outer arc",   idle: "18s CW",   capture: "6s CW",  think: "3s CW",   speak: "12s CW",  err: "30s CW" },
    { el: "Mid arc",     idle: "28s CCW",  capture: "4s CCW", think: "2s CCW",  speak: "9s CCW",  err: "30s CCW" },
    { el: "Inner arc",   idle: "14s CW",   capture: "3s CW",  think: "1.4s CW", speak: "6s CW",   err: "30s CW" },
    { el: "Pupil pulse", idle: "3.2s sin", capture: "0.4s 1×",think: "1.1s ease",speak:"0.6s sin",err: "1.2s pulse" },
    { el: "Halo glow",   idle: "3.6s",    capture: "0.6s",   think: "1.6s",     speak: "1.0s",    err: "1.2s" },
  ];

  return (
    <div style={{
      width: 920, height: 600,
      background: "var(--bg-0)",
      color: "var(--fg-0)",
      fontFamily: "var(--font-display)",
      padding: 32,
    }}>
      <div className="mono upper" style={{
        fontSize: 10, color: "var(--fg-2)",
        letterSpacing: "0.32em", marginBottom: 8,
      }}>02 · ANIMATION TIMING</div>
      <div style={{
        fontSize: 32, fontWeight: 600, marginBottom: 24,
        letterSpacing: "-0.01em",
      }}>Bionic-eye motion spec</div>

      <table style={{
        width: "100%",
        borderCollapse: "collapse",
        fontFamily: "var(--font-mono)",
        fontSize: 12,
      }}>
        <thead>
          <tr style={{ color: "var(--fg-2)" }}>
            {["ELEMENT","IDLE","CAPTURE","THINK","SPEAK","ERROR"].map((h, i) => (
              <th key={h} style={{
                textAlign: i===0 ? "left" : "center",
                padding: "10px 12px",
                fontSize: 10, letterSpacing: "0.2em",
                borderBottom: "1px solid var(--bg-line)",
                fontWeight: 500,
              }}>{h}</th>
            ))}
          </tr>
        </thead>
        <tbody>
          {rows.map((r, i) => (
            <tr key={i}>
              <td style={{ padding: "12px", fontFamily: "var(--font-display)", fontSize: 14, color: "var(--fg-0)", borderBottom: "1px solid var(--bg-line)" }}>{r.el}</td>
              <td style={cellStyle("var(--state-idle)")}>{r.idle}</td>
              <td style={cellStyle("var(--state-capture)")}>{r.capture}</td>
              <td style={cellStyle("var(--state-think)")}>{r.think}</td>
              <td style={cellStyle("var(--state-speak)")}>{r.speak}</td>
              <td style={cellStyle("var(--state-error)")}>{r.err}</td>
            </tr>
          ))}
        </tbody>
      </table>

      <div style={{
        display: "grid", gridTemplateColumns: "1fr 1fr", gap: 16, marginTop: 28,
      }}>
        <SpecCard title="EASING CURVES">
          <pre style={preS}>
{`spin            → linear (continuous)
pupil-pulse     → cubic-bezier(.4,0,.6,1)
boot-iris       → cubic-bezier(.2,.9,.3,1)
mode-banner     → cubic-bezier(.16,1,.3,1)
state-transition→ 240ms ease-out crossfade`}
          </pre>
        </SpecCard>
        <SpecCard title="TRANSITION RULES">
          <pre style={preS}>
{`idle  → capture : 200ms shutter flash
capture → think : 150ms color morph
think → speak   : 320ms arc decel + flash
* → error      : 80ms desaturate then red
any → settings : 240ms scale-in 0.92→1.0
fps cap        : 30 (LVGL invalidate ≤ 33ms)`}
          </pre>
        </SpecCard>
      </div>
    </div>
  );
}

const preS = {
  fontFamily: "var(--font-mono)",
  fontSize: 11,
  color: "var(--fg-1)",
  lineHeight: 1.7,
  margin: 0,
  letterSpacing: "0.04em",
  whiteSpace: "pre-wrap",
};

const cellStyle = (col) => ({
  padding: "12px",
  textAlign: "center",
  color: col,
  fontFamily: "var(--font-mono)",
  fontSize: 12,
  borderBottom: "1px solid var(--bg-line)",
  letterSpacing: "0.04em",
});

function SpecCard({ title, children }) {
  return (
    <div style={{
      padding: 16,
      border: "1px solid var(--bg-line)",
      background: "rgba(255,255,255,0.015)",
      borderRadius: 12,
    }}>
      <div className="mono upper" style={{
        fontSize: 9, color: "var(--fg-2)",
        letterSpacing: "0.28em", marginBottom: 10,
      }}>{title}</div>
      {children}
    </div>
  );
}

/* ---- Color + type tokens ---- */
function TokenSpec() {
  const colors = [
    { name: "bg.0",       hex: "#05070A", note: "deep obsidian · canvas" },
    { name: "bg.1",       hex: "#0A0E14", note: "card / overlay" },
    { name: "bg.2",       hex: "#131923", note: "raised surface" },
    { name: "bg.line",    hex: "#1C2430", note: "hairline divider" },
    { name: "fg.0",       hex: "#F4F6FA", note: "primary text" },
    { name: "fg.1",       hex: "#C7CFDB", note: "secondary text" },
    { name: "fg.2",       hex: "#7A8696", note: "tertiary / mono" },
    { name: "state.idle",    hex: "#4FE3F0", note: "cyan · ready" },
    { name: "state.capture", hex: "#FFB347", note: "amber · shutter" },
    { name: "state.think",   hex: "#E879F9", note: "magenta · processing" },
    { name: "state.speak",   hex: "#4ADE80", note: "green · audio out" },
    { name: "state.error",   hex: "#FF5C5C", note: "red · fault" },
  ];

  const types = [
    { name: "t.hero",     px: 44, weight: 600, note: "speak: ACTION line" },
    { name: "t.title",    px: 32, weight: 600, note: "primary status" },
    { name: "t.status",   px: 24, weight: 500, note: "MIN status size" },
    { name: "t.body",     px: 16, weight: 500, note: "secondary copy" },
    { name: "t.mono.sm",  px: 12, weight: 500, note: "technical labels" },
    { name: "t.mono.xs",  px: 10, weight: 500, note: "tags · pills" },
  ];

  return (
    <div style={{
      width: 920, height: 600,
      background: "var(--bg-0)",
      color: "var(--fg-0)",
      fontFamily: "var(--font-display)",
      padding: 32,
      display: "grid",
      gridTemplateColumns: "1.2fr 1fr",
      gap: 32,
    }}>
      <div>
        <div className="mono upper" style={{
          fontSize: 10, color: "var(--fg-2)",
          letterSpacing: "0.32em", marginBottom: 8,
        }}>04 · COLOR TOKENS</div>
        <div style={{
          fontSize: 28, fontWeight: 600, marginBottom: 18,
          letterSpacing: "-0.01em",
        }}>Palette · LVGL-ready</div>

        <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: 6 }}>
          {colors.map((c, i) => (
            <div key={i} style={{
              display: "grid",
              gridTemplateColumns: "28px 1fr",
              alignItems: "center",
              gap: 10,
              padding: "8px 10px",
              border: "1px solid var(--bg-line)",
              borderRadius: 8,
              background: "rgba(255,255,255,0.015)",
            }}>
              <div style={{
                width: 22, height: 22, borderRadius: 6,
                background: c.hex,
                border: "1px solid rgba(255,255,255,0.06)",
                boxShadow: c.name.startsWith("state.") ? `0 0 8px ${c.hex}66` : "none",
              }} />
              <div style={{ minWidth: 0 }}>
                <div className="mono" style={{
                  fontSize: 11, color: "var(--fg-0)",
                  letterSpacing: "0.04em",
                }}>{c.name}</div>
                <div className="mono" style={{
                  fontSize: 9, color: "var(--fg-2)",
                  letterSpacing: "0.04em",
                }}>{c.hex} · {c.note}</div>
              </div>
            </div>
          ))}
        </div>
      </div>

      <div>
        <div className="mono upper" style={{
          fontSize: 10, color: "var(--fg-2)",
          letterSpacing: "0.32em", marginBottom: 8,
        }}>05 · TYPE</div>
        <div style={{
          fontSize: 28, fontWeight: 600, marginBottom: 18,
          letterSpacing: "-0.01em",
        }}>Display · Mono</div>

        <div style={{
          padding: "12px 14px",
          border: "1px solid var(--bg-line)",
          borderRadius: 10,
          marginBottom: 14,
        }}>
          <div className="mono" style={{ fontSize: 9, color: "var(--fg-2)", letterSpacing: "0.16em" }}>FONTS</div>
          <div style={{ fontSize: 16, marginTop: 4 }}>Space Grotesk — display</div>
          <div className="mono" style={{ fontSize: 12, color: "var(--fg-1)", marginTop: 2 }}>JetBrains Mono — technical</div>
        </div>

        <div style={{ display: "flex", flexDirection: "column", gap: 6 }}>
          {types.map((t, i) => (
            <div key={i} style={{
              display: "grid",
              gridTemplateColumns: "1fr 80px",
              alignItems: "baseline",
              padding: "8px 12px",
              border: "1px solid var(--bg-line)",
              borderRadius: 8,
            }}>
              <div style={{
                fontSize: t.px, fontWeight: t.weight,
                lineHeight: 1, letterSpacing: t.px > 24 ? "-0.01em" : "0",
              }}>Aa</div>
              <div>
                <div className="mono" style={{ fontSize: 11, color: "var(--fg-0)" }}>{t.name}</div>
                <div className="mono" style={{ fontSize: 9, color: "var(--fg-2)" }}>{t.px}px · {t.weight} · {t.note}</div>
              </div>
            </div>
          ))}
        </div>

        <div style={{
          marginTop: 14,
          padding: 12,
          border: "1px dashed var(--bg-line)",
          borderRadius: 10,
          fontSize: 12, color: "var(--fg-1)", lineHeight: 1.5,
        }}>
          <span className="mono upper" style={{ fontSize: 9, letterSpacing: "0.24em", color: "var(--fg-2)" }}>I18N SLACK · </span>
          All text containers reserve <span style={{ color: "var(--state-idle)" }}>+30%</span> horizontal width.
          Hindi & Arabic line-heights set at 1.4× display size to fit diacritics & RTL marks.
        </div>
      </div>
    </div>
  );
}

/* ---- Cinematic recommendation ---- */
function RecommendationSpec() {
  return (
    <div style={{
      width: 720, height: 600,
      background: "linear-gradient(180deg, #05070A, #0A0E14)",
      color: "var(--fg-0)",
      fontFamily: "var(--font-display)",
      padding: 36,
      position: "relative",
      overflow: "hidden",
    }}>
      {/* Decorative giant eye, dimmed */}
      <div style={{
        position: "absolute", right: -120, bottom: -120,
        opacity: 0.18, pointerEvents: "none",
      }}>
        <BionicEye state="speak" size={420} />
      </div>

      <div style={{ position: "relative", zIndex: 2, maxWidth: 540 }}>
        <div className="mono upper" style={{
          fontSize: 10, color: "var(--state-speak)",
          letterSpacing: "0.32em", marginBottom: 12,
        }}>06 · OPINIONATED RECOMMENDATION</div>

        <div style={{
          fontSize: 44, fontWeight: 600, lineHeight: 1.05,
          letterSpacing: "-0.015em", marginBottom: 18,
        }}>
          Make the iris <span style={{ color: "var(--state-speak)" }}>track the
          person it's helping</span>.
        </div>

        <div style={{
          fontSize: 16, color: "var(--fg-1)", lineHeight: 1.5, marginBottom: 22,
        }}>
          One change does more for the camera than any other: when the user's voice or
          phone proximity is detected, have the pupil
          <span style={{ color: "var(--state-speak)" }}> drift toward the speaker</span> —
          a 14-pixel parallax offset, eased over 600ms. The arcs hold steady; only the
          pupil moves. It instantly reads as <em>aware</em> and <em>attentive</em>,
          not just "running."
        </div>

        <div style={{
          padding: 16,
          border: "1px solid var(--bg-line)",
          background: "rgba(74,222,128,0.06)",
          borderRadius: 12,
        }}>
          <div className="mono upper" style={{
            fontSize: 9, color: "var(--state-speak)",
            letterSpacing: "0.24em", marginBottom: 8,
          }}>WHY IT FILMS WELL</div>
          <ul style={{
            margin: 0, paddingLeft: 16,
            fontSize: 13, color: "var(--fg-1)", lineHeight: 1.6,
          }}>
            <li>4K close-ups capture the sub-pixel parallax cleanly.</li>
            <li>Reads as eye-contact — the most photogenic emotion on a screen.</li>
            <li>Costs ~12 lines of LVGL (offset center on accelerometer / mic gate).</li>
            <li>Works in silence — no copy needed for the cut.</li>
          </ul>
        </div>

        <div style={{
          marginTop: 18,
          fontFamily: "var(--font-mono)",
          fontSize: 10, color: "var(--fg-2)",
          letterSpacing: "0.18em",
        }}>
          BUDGET: 1 sprint · ~80 LOC · zero new assets
        </div>
      </div>
    </div>
  );
}

window.IRIS_SPECS = { TouchZoneSpec, AnimationSpec, TokenSpec, RecommendationSpec };
