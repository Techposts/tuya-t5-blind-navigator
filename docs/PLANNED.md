# Planned Work — IRIS / blind_navigator

Single source of truth for what's done, what's next, and what's parked. Updated alongside major releases.

**Current release**: `v0.3.0` (2026-05-02) — setup-without-rebuild + diagnostics + brightness control

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

## Phase 2 — Setup-without-rebuild — DONE in v0.3.0 ✅

| Goal | Status |
|---|---|
| AP-mode fallback when no known Wi-Fi found | ✅ `IRIS-XXXX` open AP on `192.168.4.1`, station-mode tries KV creds first |
| Web UI for Wi-Fi setup | ✅ `/wifi` route — live scan, select, password, save → reboot |
| Web UI for runtime settings | ✅ `/settings` — volume, brightness (live-applied), voice, language, wake-feedback toggle |
| Time display in status bar | ✅ NTP via `tal_time_service`, `HH:MM` top-right, refreshes every 30 s |
| Web UI styled to match device | ✅ Space Grotesk + JBM, IRIS color tokens, deep obsidian background, ring buttons. Fonts from Google Fonts CDN |
| Diagnostics page | ✅ `/diagnostics` — wake counter, free heap, uptime, manual NAVIGATE/READ trigger buttons |
| Wake-word integration ("Hi Tuya") | ✅ Wanson KWS engine wired via `tkl_kws_reg_wakeup_cb`, fires `touch_tap_trigger` on detect |

### Deferred from Phase 2 → v0.4.0

| Item | Reason |
|---|---|
| **mDNS hostname `iris.local`** | TuyaOpen's lwIP build does not have `LWIP_MDNS_RESPONDER=1` by default; enabling requires a platform-level rebuild |
| **Captive-portal DNS hijack** | Same lwIP-rebuild dependency as mDNS |
| **Wake-word audible feedback play** | KV toggle wired, but `ai_audio_player_alert` must run off the KWS callback thread (CP12e regression proved blocking the KWS thread misses subsequent detections). Needs a `tal_workq`-based deferral wrapper |

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

## Status as of 2026-05-02 (post-v0.3.0)

**v0.3.0 shipped.** Anyone with the binary can now flash, power on, connect to the `IRIS-XXXX` AP, set Wi-Fi via web UI, then reach the device on its station IP for ongoing settings + diagnostics. No recompile required.

Next session focus is **v0.4.0**: lwIP mDNS rebuild (so `iris.local` works), `tal_workq`-deferred audible wake feedback, then sensor stack (LD2450 mmWave + LRA haptic) which becomes the on-device differentiator vs cloud-only assistants.

The 180-day no-delete clause on the partnership video means v0.2.0 source must remain available and buildable through 2026-10-29. v0.3.0 is additive on top of that baseline.
