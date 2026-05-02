# Planned Work — IRIS / blind_navigator

Single source of truth for what's done, what's next, and what's parked. Updated alongside major releases.

**Current release**: `v0.2.0` (2026-05-02)

---

## Done in v0.2.0 (CP1 → CP11b)

| Checkpoint | Theme | Status |
|---|---|---|
| CP1 | iris_tokens.h + recolor + brand rename | ✅ |
| CP1b | Conversational multi-mode prompts | ✅ |
| CP2 | 3 concentric rotating arcs with state-tuned timings | ✅ |
| CP3 | Mode-hint row + 21-bar audio waveform | ✅ |
| CP4 | Transient mode-switch banner | ✅ |
| CP5 | LVGL gesture detection (replaced in CP7) | ✅ |
| CP6 | Idle layout with READY · LISTENING tag + Tap-to-navigate CTA | ✅ |
| CP7 | Custom Space Grotesk + JetBrains Mono fonts at 4bpp + manual press/release swipe detection | ✅ |
| CP8 | Per-state full-screen layers (capture brackets, think bars, error pill, boot splash) | ✅ |
| CP9 | State flow fix + slower anims + double-tap-to-repeat + Settings overlay built (no activation gesture yet) | ✅ |
| CP10 | Refined speak screen — compact eye + thicker rounded waveform + 4-field structured response | ✅ |
| CP10b | Audio waveform repositioning when eye is compact | ✅ |
| CP10c | Mode-banner duration shortened (1500→700ms) + auto-hide on state transitions | ✅ |
| CP11 | Retina-scanner aesthetic — 24 tick marks + breathing white pupil + transparent boot reveal + speak linger + longer SPOKEN prompts | ✅ |
| CP11b | Dynamic vertical layout for structured response (action wrap pushes WHY below) | ✅ |

Full per-checkpoint detail in [design/CHANGES.md](../design/CHANGES.md).

---

## Phase 2 — Setup-without-rebuild (target v0.3.0)

The biggest usability gap in v0.2.0: Wi-Fi credentials are baked at build time. Anyone who wants to use the device on their own network has to recompile.

### Goals

1. **AP-mode by default when no known Wi-Fi found** — board starts as `IRIS Setup` open AP
2. **Captive portal** at `http://192.168.4.1` — auto-redirects any URL
3. **Web UI for Wi-Fi setup** — list nearby networks, pick + enter password, save to LittleFS KV
4. **Web UI for runtime settings** — volume, brightness, language, voice (matching the Settings overlay screen built in CP9)
5. **mDNS hostname `iris.local`** — easy address after Wi-Fi connect
6. **Time display in status bar** — pull NTP after Wi-Fi up, render `14:22` top-right
7. **Web UI styled to match device** — same Space Grotesk + JetBrains Mono typography, same color tokens, same visual language. CSS-loaded from the device's own LittleFS, no external CDN

### Approach

| Step | Component | Detail |
|---|---|---|
| 1 | TuyaOpen `tal_wifi_set_work_mode(WWM_SOFTAP)` | Bring up AP when station-mode times out |
| 2 | Embedded HTTP server | `tuya_cloud_service` already has one (used by Tuya provisioning); reuse |
| 3 | LittleFS KV | `tal_kv_*` API to persist Wi-Fi creds, volume, brightness, language, voice |
| 4 | Web UI assets | Build a small Vite/Vanilla-JS bundle, embed as compressed C arrays via `xxd -i` |
| 5 | mDNS responder | TuyaOpen has one; just enable + name `iris` |
| 6 | NTP via `tal_time_service` | Already initialized; just expose hours/minutes to display |

Estimated effort: **3-5 dev days**. Lots of small pieces, each individually small.

---

## Sensors stack (target v0.4.0)

The mid-term differentiator vs cloud-only systems.

| Item | Wiring | Effect |
|---|---|---|
| **LD2450 24 GHz mmWave** | UART1 (P00=TX, P01=RX) at 256000 baud | Real DEPTH (nearest target distance) + HAZARDS (count of moving humans) replacing the current randomized THINKING bars. Detects through clothing, works in dark. |
| **VL53L0X / VL53L1X ToF** | I2C1 (P00 SCL / P01 SDA) — conflicts with LD2450 UART, would need second I2C bus or sensor multiplexing | Sub-1cm proximity for "almost touching" alerts |
| **LSM6DSO IMU** | I2C | Heading + step counter — enables pupil parallax (CP6 deferred) and fall detection |
| **Haptic LRA** | PWM | Vibration patterns for direction (1 buzz = forward, 2 = stop, 3 = obstacle) — caregiver-readable |
| **Bone-conduction speaker** | I2S | Hands-free / ear-not-blocked audio output |

Any of these requires schematic + PCB design + soldering. Not a one-evening task.

---

## Wake word & follow-up (target v0.4.0 alongside sensors)

| Item | Approach |
|---|---|
| **Hi Tuya wake word** | Enable `CONFIG_ENABLE_KWS=y` — TuyaOpen ships with the Wanson KWS engine and Tuya wake-word model |
| **Follow-up listening window** | After speaking ends, 5-10 second window for user to ask a follow-up question |
| **Conversation memory** | Last N turns kept in RAM (~5-10 KB, no SD card needed). Reset on each NAVIGATE/READ/IDENTIFY trigger or after 60s idle |

---

## Battery + enclosure (target v0.5.0)

Goes from "dev kit on the desk" to "wearable accessibility tool":
- 3.7V Li-Po (1500-2000 mAh) + LDO/buck regulator
- USB-C charge IC (TP4056 or similar)
- 3D-printed enclosure (Fusion 360 + PLA prototype, then SLA for production fit)
- Lanyard / pocket clip / chest strap mounts

---

## Multi-language prompts (target v0.6.0)

The IRIS framing of "accessibility tool for blind users in developing markets" is the strongest content thesis. Multilingual support is essential to deliver on it:
- Hindi
- Spanish
- Mandarin
- Arabic
- Bengali
- Portuguese (Brazilian)

Each language needs:
- Translated prompts (preserving the labeled-line schema)
- TTS voice selection (OpenAI offers a few non-English voices; may need provider mix)
- LCD glyph baking (Latin range covers Spanish/Portuguese; Hindi needs Devanagari, Arabic needs Arabic script — separate font generation pass)

---

## Caregiver mini-app + community (target v0.7.0)

The full-product picture:
- **Caregiver mobile mini-app** — sees real-time location, emergency alert when device button held >5s, conversation history (with consent)
- **Community release** — open hardware files, kit assembly instructions, school/NGO bulk discount
- **Content series** — YouTube series on the build, the use cases, the global accessibility gap, and the DIY-for-dignity ethos

---

## Status as of 2026-05-02

We're in **post-CP11b polish-complete state** for v0.2.0. The next session focus is **Phase 2** (AP-mode + web UI + time display) which gives the device its core "anyone can use it" usability layer. After Phase 2 ships as v0.3.0, sensor integration becomes the v0.4.0 priority.

The 180-day no-delete clause on the partnership video means v0.2.0 source must remain available and buildable for at least 6 months. That's why we tag now and treat v0.2.0 as the stable demo baseline.
