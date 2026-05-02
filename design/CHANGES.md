# IRIS Implementation Changelog

Tracking all firmware + design changes from "Blind Navigator v0.1.0" → "IRIS — Vision Co-Pilot".
Source-of-truth design files in [iris-firmware-ui/](iris-firmware-ui/).

---

## Checkpoint 1 — Tokens + recolor + brand rename · 2026-05-02

**Goal**: Replace ad-hoc color choices with IRIS-locked design tokens; introduce IRIS branding without restructuring widgets yet.

### Files added (VM)

| Path | Purpose | LOC |
|---|---|---|
| `apps/tuya.ai/blind_navigator/include/iris_tokens.h` | All design tokens (colors, type scale, animation timings, spacing, radii, brand strings) — single source of truth for the firmware. Mirrors `design/iris-firmware-ui/project/tokens.css` exactly. | 108 |

### Files modified (VM)

| Path | Change | Notes |
|---|---|---|
| `apps/tuya.ai/blind_navigator/src/nav_display.c` | All 10 `COL_*` macros recolored to IRIS-locked hex values. Added `#include "iris_tokens.h"`. Boot state label "BOOTING" → `IRIS`. Default response text → `IRIS · Vision Co-Pilot \\n Tap to navigate`. | 12 lines changed |
| `apps/tuya.ai/blind_navigator/src/main.c` | Fallback display text "Say Hey Tuya" → "Tap to navigate" (Pro track has no wake word). | 1 line changed |

### Color migrations

| Token | Was | Now (IRIS) | Token name |
|---|---|---|---|
| `COL_BG` | `#000000` pure black | `#05070A` deep obsidian | `IRIS_BG_0` |
| `COL_PANEL` | `#0C1218` | `#0A0E14` | `IRIS_BG_1` |
| `COL_FRAME` | `#202833` | `#1C2430` | `IRIS_BG_LINE` |
| `COL_DIM` | `#808898` | `#7A8696` | `IRIS_FG_2` |
| `COL_IDLE` | `#40D8FF` | `#4FE3F0` cyan | `IRIS_STATE_IDLE` |
| `COL_LISTENING` | `#FFC840` | `#FFB347` amber | `IRIS_STATE_CAPTURE` |
| `COL_PROCESSING` | `#D846FF` | `#E879F9` magenta | `IRIS_STATE_THINK` |
| `COL_SPEAKING` | `#40FF98` | `#4ADE80` green | `IRIS_STATE_SPEAK` |
| `COL_ERROR` | `#FF4848` | `#FF5C5C` red | `IRIS_STATE_ERROR` |

### Backups (timestamp 20260501_205039)

All modified files backed up before edit:
- `~/openai_proxy.py.bak.20260501_205039`
- `apps/tuya.ai/blind_navigator/src/main.c.bak.20260501_205039`
- `apps/tuya.ai/blind_navigator/src/openai_backend.c.bak.20260501_205039`
- `apps/tuya.ai/blind_navigator/include/nav_config.h.bak.20260501_205039`
- `apps/tuya.ai/blind_navigator/src/nav_display.c.bak.20260501_205039`

### Validation

- Build: pending (in flight at time of writing)
- Visual: device reflash needed to confirm new colors render correctly

### What did NOT change (deliberately)

- Bionic-eye widget structure (still 1 chrome ring + 1 iris disk + 1 pupil + 1 orbit arc — Checkpoint 2 replaces with 3 concentric arcs + tick marks + crosshair)
- Touch event handling (Checkpoint 5 adds swipe gestures)
- Screen layout (Checkpoint 3 implements the 8-screen layouts pixel-perfect)
- Custom fonts (Checkpoint 7 adds Space Grotesk + JetBrains Mono bitmap conversions)

---

## Checkpoint 1b — Prompt update (NAV/READ/IDENTIFY) · 2026-05-02

**Goal**: Replace the older structured "PATH STATUS / WHERE / ACTION / WHY" prompts with the new conversational single-output prompts designed earlier in this session. Pro track owns the response pipeline so single-output is correct here (Cloud track's dual-output split was Tuya-Cloud-specific).

### Files modified (VM)

| Path | Change |
|---|---|
| `apps/tuya.ai/blind_navigator/include/nav_config.h` | All 3 mode prompts replaced. NAV_VISION_PROMPT now requests ONE conversational sentence under 18 words with clock position + distance. NAV_READ_PROMPT requests 1-3 conversational sentences, verbatim text first then context. NAV_OBJECT_PROMPT requests 1-3 sentences with safety attributes. All preserve the strict "no I see, no the image shows, no as an AI" rules. |

### Backups
- `apps/tuya.ai/blind_navigator/include/nav_config.h.bak.20260502_045638`

### Behavioral diff at runtime

| Mode | Before | After |
|---|---|---|
| NAVIGATE (P12 single-click / touch tap) | 4-line structured PATH STATUS/WHERE/ACTION/WHY | 1 short conversational sentence: "Path clear, go forward — open hallway extends about five meters." |
| READ (P12 double-click) | "Read every visible word, sign, label..." with structural list | 1-3 conversational sentences: "Five hundred rupee note. Indian currency, Mahatma Gandhi on the front." |
| IDENTIFY (P12 long-press) | 1-2 sentences with (a)(b)(c)(d) shape | 1-3 conversational sentences with safety: "That's a kitchen knife with a metal blade and plastic handle. Be careful, the blade is sharp." |

### Race-condition gotcha encountered

First build started at ~04:58:30; apostrophe-fix sed landed at 04:58:42 (mid-build); main.c had already been compiled at that point. Result: first build succeeded but binary had "oclock" instead of "o'clock". Fix: `touch src/main.c` to invalidate the .o file → ninja rebuild picks up nav_config.h changes correctly. **Saving as workflow rule**: don't modify a header during a build; queue all header changes BEFORE starting the build.

### Validation

- Build: pending (rebuild in flight)
- Binary string check: post-rebuild, expect `o'clock` count = 1, `oclock` (no apostrophe) count = 0

### Next checkpoint

**Checkpoint 2** — Replace bionic-eye widget with proper 3-arc design from `design/iris-firmware-ui/project/eye.jsx`. Adds outer/mid/inner concentric SVG-style arcs with state-tuned rotation timings. Tick marks + crosshair reticle deferred to a polish pass. Estimated ~250 LOC, all in nav_display.c.

---

## Checkpoint 2 — 3 concentric rotating arcs · 2026-05-02

**Goal**: Add the missing two arcs (mid CCW + inner CW) to the bionic eye, with per-state animation timings matching the IRIS design spec.

### Implementation

- Added 5 statics: `s_arc_mid`, `s_arc_inner`, `s_anim_mid`, `s_anim_inner`, plus a state-aware helper `iris_update_arc_anims()`
- Each arc gets its own `lv_arc` widget with a transparent background and a 70-90-60 degree colored span
- Mid arc rotates **counter-clockwise** (values 3599→0); inner rotates clockwise like outer
- `iris_update_arc_anims()` is called from `nav_display_set_state()` and:
  - Stops all 3 anims via `lv_anim_del()`
  - For non-error states, restarts them with state-specific times
  - Error state freezes the arcs (no restart)

### Per-state timings (ms per revolution)

| State | Outer CW | Mid CCW | Inner CW |
|---|---|---|---|
| Idle / Connecting | 18000 | 28000 | 14000 |
| Listening / Capturing | 6000 | 4000 | 3000 |
| Processing / Thinking | 3000 | 2000 | 1400 |
| Speaking | 12000 | 9000 | 6000 |
| Error | (frozen) | (frozen) | (frozen) |

---

## Checkpoint 3 — Mode hints + audio bars · 2026-05-02

**Goal**: Add the two missing widgets from the IRIS design that visually communicate "what can I do here" (idle) and "I'm speaking now" (audio).

### Implementation

- **`s_mode_hints`** — flex row at the bottom of the screen, three columns: ▲ IDENTIFY (magenta) · ● NAVIGATE (cyan) · ▼ READ (amber). Each column shows icon + label + sub-text ("swipe up", "tap", "swipe down"). Visible only when state == IDLE.
- **`s_audio_bars`** — flex row of 5 vertical bars centered above the response panel. Each bar has a transform-scale-y animation with staggered timing (600-880ms) and a green glow. Visible only when state == SPEAKING.
- Visibility logic added to `iris_update_arc_anims()`:
  - IDLE → mode_hints visible, response_panel hidden
  - else → mode_hints hidden, response_panel visible
  - SPEAKING → audio_bars visible + animations started; else hidden + animations deleted

### Design trade-off explained

The IRIS Speaking screen design assumed a *structured 4-line response* (PATH STATUS / WHERE / ACTION / WHY). After our prompt rewrite to conversational single-paragraph responses, that 4-line label-pill aesthetic doesn't apply. Trade-off: keep conversational prompts (better TTS for blind users) and use the response panel + audio bars layout, which is structurally similar but renders the response as natural prose.

---

## Checkpoint 4 — Mode-switch banner · 2026-05-02

**Goal**: Pop a transient banner when mode changes, so the user gets immediate visual confirmation of which mode just engaged.

### Implementation

- New widget `s_mode_banner` (initially hidden) — large bordered card with a 24px label, glow shadow in the mode color
- New public API `nav_display_show_mode_banner(const char *mode_name, uint32_t color_hex)`
- Auto-hide via `lv_timer` after 1500ms (one-shot, then deletes itself)
- Color mapping: NAVIGATE=cyan(#4FE3F0) · READ=amber(#FFB347) · IDENTIFY=magenta(#E879F9)
- Wired from `main.c` btn_cb — every button gesture now also fires the banner

---

## Checkpoint 5 — Touch swipe gesture detection · 2026-05-02

**Goal**: Match the IRIS touch zones — swipe up = IDENTIFY, swipe down = READ — alongside the existing tap = NAVIGATE.

### Implementation

- New event handler `s_screen_gesture_cb` listening on `LV_EVENT_GESTURE`
- Reads `lv_indev_get_gesture_dir()` from active input device:
  - `LV_DIR_TOP` → call registered swipe-up callback
  - `LV_DIR_BOTTOM` → call registered swipe-down callback
- New public API `nav_display_set_swipe_callbacks(swipe_up, swipe_down)` to register handlers from app code
- `main.c` now defines `touch_swipe_up_trigger` (→ IDENTIFY mode + banner) and `touch_swipe_down_trigger` (→ READ mode + banner) and registers both alongside `touch_tap_trigger` (→ NAVIGATE)

### Resulting interaction map (matches design 1:1)

| Gesture | Mode | Color |
|---|---|---|
| Touch tap (anywhere) | NAVIGATE | cyan |
| Swipe up | IDENTIFY | magenta |
| Swipe down | READ | amber |
| P12 single click | NAVIGATE | cyan |
| P12 double click | READ | amber |
| P12 long-press | IDENTIFY | magenta |

---

## Checkpoint 7 — Custom typography + manual swipe detection · 2026-05-02

**Goal**: Close the typography gap to Claude Design — replace Montserrat with Space Grotesk (display sans) + JetBrains Mono (technical mono) at the design-spec sizes, restore real Unicode glyphs (`·`, `▲`, `●`, `▼`), and fix swipe gestures that didn't fire on CP6.

### Why CP7 (un-deferred from v0.1.x)

CP6 verification on hardware revealed two issues that made the gap to design painfully visible:

1. **`READY · LISTENING`** rendered as `READY ▢▢ LISTENING` — Montserrat doesn't include U+00B7 middle dot.
2. **Mode-hint icons `▲ ● ▼`** rendered as thin `^ ●? >` — LVGL's `LV_SYMBOL_UP/DOWN` are anemic FontAwesome chevrons, and Montserrat doesn't ship geometric arrows at all.
3. **Touch tap worked, but swipe-up / swipe-down did nothing** — confirmed at the user level after flash.

### Tooling setup (dev VM)

- nvm + Node 20 LTS installed in `~/.nvm/`
- `lv_font_conv@1.5.3` installed globally
- Python `fontTools` for variable-font slicing

### Source typefaces

| File (`fonts/src/`) | Source | Weight |
|---|---|---|
| `SpaceGrotesk-VF.ttf` | github.com/google/fonts/ofl/spacegrotesk | Variable (300–700) |
| `SpaceGrotesk-Regular.ttf` | sliced via fontTools `instantiateVariableFont` | wght=400 |
| `SpaceGrotesk-SemiBold.ttf` | sliced | wght=600 |
| `SpaceGrotesk-Bold.ttf` | sliced | wght=700 |
| `JetBrainsMono-Regular.ttf` | github.com/JetBrains/JetBrainsMono | wght=400 |

### Generated LVGL bitmap fonts (`fonts/generated/`)

All 4bpp antialiased. Glyph range: ASCII (0x20-0x7F) + Latin-1 supplement (0xA0-0xFF) + em-dash (U+2014) + ellipsis (U+2026) + arrows (U+2190, U+2192). JetBrains Mono fonts additionally include geometric shapes (U+25B2 ▲, U+25BC ▼, U+25CF ●). Generator script preserved at `fonts/gen_fonts.sh` for reproducibility.

| Font | Source TTF | Size | Use |
|---|---|---|---|
| `font_iris_hero_44` | Space Grotesk Bold | 44 px | speak action line |
| `font_iris_title_32` | Space Grotesk SemiBold | 32 px | mode banner, boot title |
| `font_iris_status_24` | Space Grotesk SemiBold | 24 px | state label, "Tap to navigate" CTA |
| `font_iris_body_16` | Space Grotesk Regular | 16 px | response panel body |
| `font_iris_mono_sm_12` | JetBrains Mono Regular | 12 px | tag labels (READY · LISTENING), mode-hint labels |
| `font_iris_mono_xs_10` | JetBrains Mono Regular | 10 px | sub-labels (swipe up / tap / swipe down), status bar |
| `font_iris_icons_24` | JetBrains Mono Regular | 24 px | mode-hint geometric arrows (▲ ▼) |

### Files added (VM)

| Path | Purpose | Notes |
|---|---|---|
| `apps/tuya.ai/blind_navigator/fonts/src/*.ttf` | Source TTF files (4 files: 3 SG variants + JBM) | ~580KB total source |
| `apps/tuya.ai/blind_navigator/fonts/generated/*.c` | 7 LVGL bitmap font C files | ~1.2MB source → ~340KB binary at 4bpp |
| `apps/tuya.ai/blind_navigator/fonts/gen_fonts.sh` | Reproducibility — regen all 7 fonts from sources | preserves exact glyph ranges per typeface |
| `apps/tuya.ai/blind_navigator/include/iris_fonts.h` | externs for all 7 `lv_font_t` symbols + UTF-8 byte literals for `·`, `▲`, `●`, `▼`, `→`, `←`, `—`, `…` | single source of truth for glyphs |

### Files modified (VM)

| Path | Change |
|---|---|
| `apps/tuya.ai/blind_navigator/CMakeLists.txt` | Added `aux_source_directory(${APP_PATH}/fonts/generated APP_FONT_SRCS)` and appended `${APP_FONT_SRCS}` to `target_sources` |
| `apps/tuya.ai/blind_navigator/src/nav_display.c` | Removed `LV_FONT_DECLARE(lv_font_montserrat_*)`, added `#include "iris_fonts.h"`. Replaced 13 `&lv_font_montserrat_*` references with appropriate `&font_iris_*`. Restored real `·` separator via `IRIS_GLYPH_MIDDOT`. Replaced `LV_SYMBOL_UP/DOWN` in mode hint array with `IRIS_GLYPH_UP/IRIS_GLYPH_DOWN` rendered in `font_iris_icons_24`. Added manual press/release-based swipe detection. |

### Manual swipe detection (CP7b)

LVGL's built-in gesture system (`LV_EVENT_GESTURE` + `lv_indev_get_gesture_dir()`) requires the touch driver to emit a continuous stream of pointer-position move events between press-down and release-up. The GT1151QM driver on this hardware appears to emit only the press and release endpoints — enough for `LV_EVENT_CLICKED` but not enough for LVGL to compute `gesture_dir`. So `lv_indev_get_gesture_dir()` always returned `LV_DIR_NONE`.

**Fix**: bypass LVGL's gesture detection. Capture `lv_indev_get_point()` on `LV_EVENT_PRESSED`, capture again on `LV_EVENT_RELEASED`, compute `Δy`. Threshold: `|Δy| ≥ 40px` AND `|Δx| < 80px` AND `|Δy| > |Δx|` → vertical swipe; sign of `Δy` selects up vs down.

**Tap suppression interlock**: when a swipe is classified, set `s_swipe_consumed = true`. The CLICKED handler checks this flag and skips `s_tap_cb` if set (otherwise a swipe would also fire NAVIGATE alongside the swipe action). The flag is cleared in the next `PRESSED` event.

**Defense-in-depth registration**: callbacks registered on both `s_screen` (our app's main container) AND `lv_scr_act()` (the absolute parent of any indev act_obj). Either will catch the events regardless of which child object the press lands on.

### Resulting touch interaction map (CP7)

| Gesture | Detection | Mode | Color |
|---|---|---|---|
| Touch tap | `LV_EVENT_CLICKED` (LVGL native) | NAVIGATE | cyan |
| Swipe up (\|Δy\|≥40px, dy<0) | `PRESSED`+`RELEASED` delta | IDENTIFY | magenta |
| Swipe down (\|Δy\|≥40px, dy>0) | `PRESSED`+`RELEASED` delta | READ | amber |

### Build outputs

| Stage | Result |
|---|---|
| Clean rebuild (732 ninja targets, 7 new font compilation units) | SUCCESS |
| Incremental rebuild after CP7b swipe patch | SUCCESS |
| Flash to `/dev/ttyUSB0` @ 460800 baud | SUCCESS — sync, erase 50.5/50.5, write 683.75/683.75, CRC OK, reboot done |
| Final firmware | `blind_navigator_QIO_1.0.2.bin` |

### Lessons learned (saved to memory)

1. **`tos.py build` and `tos.py flash` must run from the project directory**, not TuyaOpen root. Both fail with `TuyaOpen root cannot be regarded as project root` otherwise.
2. **LVGL gesture detection is unreliable on touch drivers that don't emit move events**. Manual press/release-based swipe is the touch-driver-agnostic alternative.
3. **`-Wcomment` (under `-Wall`) flags `/*` substrings inside block comments** — `*.c` glob patterns inside `/* */` comments need rephrasing (e.g. "the .c files in fonts/generated/").

---

## Checkpoint 8 — Per-state screen layouts + audio-only output · 2026-05-02

**Goal**: Replicate Claude Design's per-state screens. Currently only the bionic eye changed color between states; the screen-level chrome (frame brackets, headers, progress bars, error messages) was missing entirely.

### User direction (verbatim)

> "same as claude design gave replicate that design please as discussed... we can get rid of the streaming text response and rely completely on the output audio now."

> "you have full permission don't stop until everything closed and fixed and flashed"

### Files modified (VM)

| Path | Change |
|---|---|
| `apps/tuya.ai/blind_navigator/src/nav_display.c` | +590 LOC. Added 5 per-state layer containers (capture/think/error/boot + extended speak), restructured `iris_update_arc_anims()` for layer visibility, removed response panel widget tree, stubbed `nav_display_set_text()` as no-op, fixed eye_y overlap, expanded audio bars from 5 → 21 |

### Per-state screen content (matches Claude Design)

| State | DISP_STATE_* | Layer | Content |
|---|---|---|---|
| **IDLE** | IDLE | s_idle_tag, s_idle_cta, s_mode_hints | "READY · LISTENING" tag + "Tap to navigate" CTA + ▲ ● ▼ mode hints (existing CP6/7 widgets) |
| **CAPTURING** | LISTENING | s_layer_capture | 4 amber corner brackets (22×22, weight 2px, opa 0.7) at inset 24px from each screen corner + "● CAPTURING" header (mono 12pt amber) + "F / 1.8 · ISO 400 · 12 MP" technical readout |
| **THINKING** | PROCESSING | s_layer_think | "ANALYZING SCENE" header (mono 12pt magenta) + 4 horizontal data bars labeled OBJECTS / DEPTH / OCR / HAZARDS at 78/62/41/88% with breathing pulse animation (1400-2000ms staggered) and right-aligned percentages |
| **SPEAKING** | SPEAKING | s_audio_bars (expanded) | 21 vertical green bars in centered envelope shape (peaks at center, tapers at edges) with bar-height pulse animation 500-800ms |
| **ERROR** | ERROR | s_layer_error | "X NO IMAGE" header (mono 12pt red) + "Lens may be covered." 24pt headline + "Wipe the camera and tap to retry." 16pt body + red rounded pill "TAP TO RETRY" (200×40, border 1px, radius 999) |
| **BOOT** | (auto on init, dismisses 2s) | s_layer_boot | Full-screen overlay: "IRIS" 32pt title + "VISION CO-PILOT · v2.3.1" mono tag + "CALIBRATING SENSORS" footer + 70%-filled hairline progress bar with cyan glow shadow |

### Removed (per user direction)

- `s_response_panel` (LV_OBJ container, 30% screen height bottom panel)
- `s_response_label` (LV_LABEL inside panel)
- `s_resp_buf[1024]` storage
- All `lv_label_set_text(s_response_label, ...)` calls
- `nav_display_set_text()` body — kept as `(void)text;` no-op so existing call sites in main.c still link (zero rework needed there)

### Layer architecture

Each state has its own LV_OBJ container the size of the full screen, with all interactive flags cleared. Containers are added to `s_screen` (the root) AFTER the bionic-eye widgets, so they sit above eye but below boot splash. Visibility flipped in `iris_update_arc_anims()` as a parallel branch to the existing per-state arc-timing logic — exactly one layer is visible at a time. Boot splash is the last child added so it's on top, dismissed via a one-shot `lv_timer_create(2000ms)` registered at end of `nav_display_init`.

### Eye position fix

Old: `eye_y = (H - eye_size) / 2 - H/6;` — pushes eye too high, top edge at y≈72, collides with idle CTA bottom.
New: `eye_y = (H - eye_size) / 2 - H/12;` — top edge at y≈104, leaves 18-20px clearance under the CTA stack.

### Audio waveform expansion (5 → 21 bars)

The design shows 21 thin bars in an envelope shape. New bar height formula `8 + (10 - |b - 10|) * 3` produces peaks at index 10 (center, 38px tall) tapering to 8px at the edges. Animation timing also tapers: `500 + |b - 10| * 30 ms` — center bars pulse fastest, edges slower (creates the natural "breathing waveform" feel rather than synchronized march).

### Lessons learned (saved to memory)

1. **GCC's `-Werror=unused-function` cascades from missing forward decls**: when a static function is called before its definition appears in the file, GCC parses the call as an implicit declaration, then flags the actual definition as "unused" because nothing links to it. **Fix**: forward-declare static functions used cross-section. Cost: 1 build retry.
2. **Python heredoc UTF-8 in `\\xc2\\xb7` literal != UTF-8 bytes in source**: in Python 3, the str `"\\xc2\\xb7"` is two char codepoints `Â·`, NOT the 2-byte UTF-8 sequence for `·`. To match real `·` in the source, write the raw character or work at byte level. **Workaround used**: line-based filter to strip references after the first surgery missed the multi-line block.

---

## Checkpoint 9 — State flow fixes + slower anims + double-tap-to-repeat + Settings · 2026-05-02

**Goal**: Address user-reported regressions on CP8 hardware + complete the 8-screen design coverage.

### User feedback on CP8 (verbatim)

> "it says analysing scene and goes back to tap to navigate whether read navigate or identify... post analyzing it should switch to that speaking ui wave form screen"
>
> "currently it stops quickly very short, and should ask follow up question user can ask, including Hi tuya wake word that need to work so follow up works, if storage needed like sd card for such feature let me know"
>
> "i see hardcoded objects depth ocr etc on Analyzing screen... that need fix going forward either real based on sensor value or just random"
>
> "tap navigate doesn't respond to tap now"
>
> "animation ring moving too fast normally and very fast while analyzing"
>
> "and double tap to repeat last response feature!"
>
> "Settings overlay to control settings like volume brightness language and voice level"

### Root causes identified

| Symptom | Root cause |
|---|---|
| Thinking → idle skipping speaking | `play_response()` in [main.c:30](firmware/apps/tuya.ai/blind_navigator/src/main.c#L30) called TTS but never set `nav_display_set_state(DISP_STATE_SPEAKING)`. State stayed at PROCESSING through TTS, jumped to IDLE on completion. Speaking layer never visible. |
| Tap NAVIGATE "no response" | `touch_tap_trigger` was the only handler not showing a mode banner. Swipes flashed magenta/amber banner (visible feedback); tap went straight to capture state with no acknowledgement. User perceived this as "tap broken" even when it worked. |
| Animations felt fast | All ring rotation timings designed for desktop-preview (3-18s for full 360°) felt too zippy on the small physical screen where the visible arc segment is short. Slowed everything by ~1.7-2x. |
| Hardcoded data bars | Per-call randomization not implemented; bars showed 78/62/41/88 every time. |

### Files modified (VM)

| File | Change |
|---|---|
| [main.c](firmware/apps/tuya.ai/blind_navigator/src/main.c) | Fixed `proc_th` state flow: LISTENING for camera capture, PROCESSING for HTTP, SPEAKING auto-set inside `play_response`, ERROR for failure paths. Added NAVIGATE mode banner in `touch_tap_trigger`. Added `s_last_response[1024]` buffer + `replay_last_response_th` thread + `touch_double_tap_trigger`. Registered double-tap callback. Calls `nav_display_randomize_think_bars()` before entering PROCESSING. |
| [nav_display.c](firmware/apps/tuya.ai/blind_navigator/src/nav_display.c) | Slowed all arc anim timings 1.7-2x in `iris_update_arc_anims` switch. Boot layer now `lv_obj_del`-ed instead of HIDDEN-flagged so it can never intercept events. Added double-tap detection state machine (320ms timer-based). Added `s_speak_footer` "double-tap to repeat" label. Added full `s_layer_settings` UI (header + 2 sliders + 2 picker rows + footer). New public APIs: `nav_display_randomize_think_bars()`, `nav_display_set_double_tap_cb()`, `nav_display_show_settings()`, `nav_display_hide_settings()`. |
| [nav_display.h](firmware/apps/tuya.ai/blind_navigator/src/nav_display.h) | Exposed 4 new public APIs. |
| [nav_config.h](firmware/apps/tuya.ai/blind_navigator/include/nav_config.h) | All 3 prompts upgraded from 1-3 sentences to 2-5 sentences with richer context, follow-up suggestions ("Want me to scan for a doorway?"), more salient details (texture/lighting/depth/material). |

### State flow (corrected)

```
[ Tap ]              [ Swipe up ]         [ Swipe down ]       [ Double-tap ]
   |                      |                     |                    |
[ NAVIGATE banner ]   [ IDENTIFY ]          [ READ ]              [ replay last ]
   |                      |                     |                    |
[ s_idle = 0 ]            (same)                (same)               |
   |                                                                 |
[ proc_th: ]                                                         |
[   LISTENING (capture screen, 3.5s warmup) ]                        |
[   randomize think bars ]                                           |
[   PROCESSING (think screen, HTTP + LLM) ]                          |
[   play_response: SPEAKING (audio bars + footer) ]<-----------------+
[   IDLE ]
```

### Animation timing changes

| State | Outer | Mid | Inner |
|---|---|---|---|
| IDLE | 18s → 32s | 28s → 48s | 14s → 24s |
| LISTENING | 6s → 10s | 4s → 7s | 3s → 5s |
| PROCESSING | 3s → 5.5s | 2s → 3.8s | 1.4s → 2.6s |
| SPEAKING | 12s → 20s | 9s → 15s | 6s → 10s |

### Double-tap detection

320ms timer-based state machine. First tap starts timer. If second tap arrives before timer fires → cancel timer, fire `s_double_tap_cb`. Otherwise timer fires `s_tap_cb`. Costs 320ms latency on single-tap (acceptable since camera has 3.5s warmup anyway). Suppression interlock with swipe detection preserved.

### Last-response replay

`s_last_response[1024]` snapshotted in `play_response()` before TTS plays. On double-tap, `replay_last_response_th` calls `play_response(s_last_response)` which re-triggers TTS without re-querying the LLM. Cached replay is sub-200ms, free, and deterministic.

### Settings overlay (screen #8)

Full visual layout per Claude Design `screens.jsx::ScreenSettings`:
- Header: "SETTINGS" mono 12pt
- 2 slider cards: VOLUME (72%), BRIGHTNESS (45%) — JBM mono labels, cyan fill bars with glow
- 2 picker rows: LANGUAGE (EN/ES/HI/AR) and VOICE (LO/MID/HI) — chips with cyan-active state
- Footer: "long-press to close" mono 10pt dim

**No activation gesture wired yet** — the layer is built and addressable via `nav_display_show_settings()` but no input path opens it. Pending CP10 decision: long-press 3s on screen, triple-click button, or some other gesture.

### Lessons learned (saved to memory)

1. **Always provide visible feedback for input events**, even if it's a brief mode banner. Users perceive missing feedback as "input broken."
2. **State machines with multiple async stages need explicit state transitions per stage**, not just "set state at start, set IDLE at end." Each stage gets its own state for screen chrome to match phase.
3. **Boot/transient overlays should be DELETED, not HIDDEN**, after they've served their purpose. Hidden-but-extant widgets can come back to bite you in input handling edge cases.

---

## Checkpoint 10 — Speaking screen refinement: structured response + compact eye · 2026-05-02

**Goal**: Match Claude Design's `ScreenSpeaking` exactly — small eye top, thicker rounded waveform with bars closer together, 4-field structured response display (PATH STATUS / WHERE / ACTION / WHY) with big-green ACTION line, "DOUBLE-TAP TO REPEAT" footer.

### User direction (verbatim)

> "Nice now lets we just have to refine the Speaking response screen with green waves need to be a bit thicker rounded corners and the wave lines a bit closer as in screenshot, and since we have space on response screen now, can we also render text like attached screenshot or better?"

### Files modified (VM)

| File | Change |
|---|---|
| [nav_display.c](firmware/apps/tuya.ai/blind_navigator/src/nav_display.c) | +180 LOC. Audio bars: width 3→5, radius 2→3, container 288→200px wide with 2px column gap → bars now closer together and visibly chunkier with rounded ends. Eye compaction helper `iris_set_eye_compact(bool)` resizes all 7 eye widgets to 36% of normal at y=30 when entering SPEAKING; restores on exit. 4 structured field widgets (`s_speak_field_label[4]` mono 10pt + `s_speak_field_value[4]` body 16pt or status 24 green). New API `nav_display_set_speak_response(label1, value1, ..., label4, value4)`. |
| [main.c](firmware/apps/tuya.ai/blind_navigator/src/main.c) | New `parsed_resp_t` struct + `parse_labeled_response()` helper that splits LLM output on \n, then on first colon per line. Detects `SPOKEN:` line for natural-prose TTS, falls back to value-join if absent, falls back to raw text if no labels at all. `proc_th` now: parses response → calls `nav_display_set_speak_response` with 4 (label, value) pairs → TTS speaks the SPOKEN line only. Both error paths (cloud failure, camera failure) populate the structured fields with appropriate STATUS/DETAIL/ACTION/NOTE messages. |
| [nav_config.h](firmware/apps/tuya.ai/blind_navigator/include/nav_config.h) | All 3 prompts rewritten to demand the 5-line labeled format: `LABEL: VALUE` per line, ending with `SPOKEN:` for TTS prose. NAVIGATE uses PATH STATUS/WHERE/ACTION/WHY. READ uses TYPE/TEXT/NOTE/FOLLOWUP. IDENTIFY uses OBJECT/DETAIL/SAFETY/FOLLOWUP. Each prompt includes word-count guidance and example output. |

### Speaking screen layout (matches Claude Design)

```
[ status bar: NAV   14:22 100% ]                             y=  0
[                                                  ]
[             /---compact eye---\\                  ]         y= 30  (63px)
[             |  cyan rotating  |                  ]
[             \\---arcs------/                     ]
[                                                  ]
[              ║║║║║║║║║║║║║║║║║║║║║                ]        y=120  (audio bars 21x5)
[                                                  ]
[  PATH STATUS                                     ]         y=170  (mono 10 dim)
[  CLEAR                                           ]         y=184  (body 16 white)
[                                                  ]
[  WHERE                                           ]         y=215
[  open sidewalk, person at 1 oclock, 4 steps      ]         y=229
[                                                  ]
[  ACTION                                          ]         y=275  (mono 10 dim)
[  Go forward                                      ]         y=289  (24pt SemiBold GREEN)
[                                                  ]
[  WHY                                             ]         y=335
[  low pedestrian flow, no curbs ahead             ]         y=349
[                                                  ]
[       double-tap to repeat                       ]         y=464  (footer)
```

### Parsing strategy

Why labeled lines instead of JSON:
- LLMs reliably emit `LABEL: VALUE` pairs without escape hassles
- On-device parsing is two `strchr` calls per line — no JSON state machine, no malloc churn
- The trailing `SPOKEN:` line gives full control over TTS prose without polluting the visual fields

Robustness:
- Missing fields → empty strings, not crashes
- LLM emits prose without labels → `parsed_resp_t.field_count == 0`, fall back to TTS the raw text, structured display stays empty (acceptable degradation)
- LLM emits JSON anyway → first line of JSON gets parsed as label `{`, recovers fine
- Extra whitespace → `trim()` helper

### Eye compaction

`iris_set_eye_compact(true)` resizes:

| Widget | Normal | Compact (36%) |
|---|---|---|
| `s_iris_outer` | 176px | 63px |
| `s_iris_mid` (80%) | 140px | 50px |
| `s_iris_inner_dark` (52%) | 91px | 33px |
| `s_pupil` (16%) | 28px | 10px |
| `s_orbit` (size+24) | 200px | 71px |
| `s_arc_mid` | 140px | 50px |
| `s_arc_inner` | 99px | 41px |

Position shifts from `y=104 (centered-ish)` to `y=30 (top)`. On exit (any non-SPEAKING state), all dimensions restore to normal.

### Lessons learned (saved to memory)

1. **Regex `[^"]*` matches non-greedy until ANY `"`, including escaped `\"`** — when replacing C string literals that contain escaped quotes, `[^"]*` will stop at the first internal `\"` and replace only a fragment, leaving orphan tokens. **Fix**: use line-based replacement (`startswith("#define ...")`) instead of regex on quoted strings.
2. **Eye scaling for state-specific layouts works** without a re-init — LVGL's `lv_obj_set_size` + `lv_obj_align` are dynamic. Storing the "normal" dimensions once at init and toggling via a helper avoids the temptation of per-state widget trees.

---

## Checkpoint 11 — Retina-scanner eye + boot reveal + speak linger + dynamic layout · 2026-05-02

**Goal**: Final polish before v0.2.0 release. Visual fidelity to the Claude Design retina-scanner aesthetic + behavioral fixes for response screen presence + dynamic vertical layout.

### User direction (verbatim)

> "now let's work and fix the iris that center animation, currently it's basic has no depth, more like round circles and a dot in center, the one Claude designs has aim kinda retina scanner visual"
>
> "with center white glowing animated dot that seems like breathing white dot when analyzing"
>
> "the last screen output where we see waveforms and all kinda stops quickly, should stay for longer and output needs to be descriptive"
>
> "the text printed can be short crisp but the spoken need descriptive"
>
> "and add that iris slowly rotating on boot screen as well"
>
> "minor overlapping on response screen where the green text if longer overlaps the below why part... can we have dynamic why shift a lil below"

### CP11 — eye scanner aesthetic + boot reveal + speak linger

| Change | Where | Effect |
|---|---|---|
| 24 radial **tick marks** at 15° around outer chrome ring | [nav_display.c](../firmware/blind_navigator/src/nav_display.c) | "Scanner halo" — establishes instrument feel |
| Pupil → **pure white** (was state-color) | nav_display.c | More depth — white center reads as a focal hot spot |
| Pupil **breathing pulse animation** | nav_display.c | Opacity 60→100% + shadow_width 18→30px over 1100ms cycle |
| Boot layer transparent (was solid bg) | nav_display.c | Eye visible underneath the boot splash — slowly rotating arcs + breathing pupil during the 2s boot window |
| IRIS title repositioned below eye on boot | nav_display.c | Doesn't collide with the now-visible eye |
| Tick halo **hidden in compact** (speaking) mode | `iris_set_eye_compact()` | Small eye + tick halo would clutter — clean speak screen |
| **4-second linger** after TTS finishes | [main.c](../firmware/blind_navigator/src/main.c) `proc_th` + replay thread | User can read the structured response after audio ends; was flashing for ~150ms |
| **SPOKEN line** bumped to 60-110 words / 4-6 sentences | [nav_config.h](../firmware/blind_navigator/include/nav_config.h.example) | All 3 prompts demand more descriptive audio while visual fields stay short |

### CP11b — dynamic vertical layout for structured response

User reported: "minor overlap where green text if longer overlaps the below why part."

Root cause: speak fields were positioned at hardcoded Y offsets (170, 215, 275, 335). When `LV_LABEL_LONG_WRAP` made the ACTION value wrap to 2-3 lines, it pushed into the WHY zone.

Fix: replaced static offsets with a **running-Y accumulator** in `nav_display_set_speak_response()`. After each `lv_label_set_text` we call `lv_obj_update_layout` to force LVGL to compute the actual rendered height, then advance Y by `label_h + 2 + value_h + 14`. Field 4 now floats below Field 3 regardless of how many lines the ACTION takes.

```c
int y = 170;
for (int i = 0; i < 4; i++) {
    lv_label_set_text(s_speak_field_label[i], lab);
    lv_label_set_text(s_speak_field_value[i], val);
    lv_obj_align(s_speak_field_label[i], LV_ALIGN_TOP_LEFT, 18, y);
    lv_obj_update_layout(s_speak_field_label[i]);
    int lh = lv_obj_get_height(s_speak_field_label[i]);
    int vy = y + lh + 2;
    lv_obj_align(s_speak_field_value[i], LV_ALIGN_TOP_LEFT, 18, vy);
    lv_obj_update_layout(s_speak_field_value[i]);
    int vh = lv_obj_get_height(s_speak_field_value[i]);
    y = vy + vh + 14;
}
```

### Lessons learned (saved to memory)

1. **Pupil = white + state-color shadow** is more visually striking than pupil = state-color. The white acts as a focal hot spot; the colored glow gives state identity. The two-layer approach beats single-layer recoloring.
2. **Transparent overlay layers** are simpler than duplicating widget trees — when something already exists underneath that you want to "reveal," make the overlay transparent and reposition the overlay's own widgets.
3. **`lv_obj_update_layout` is mandatory** before `lv_obj_get_height` on a `LV_LABEL_LONG_WRAP` widget that just had its text changed. Otherwise you get the previous render's height.
4. **Static UI mockups can't predict text-wrap variability** — even well-tuned hardcoded offsets break the moment LLM output shifts. Dynamic-Y layout via the accumulator pattern is the right default for any text-driven structured display.

---

## Release note: v0.2.0 cut here · 2026-05-02

After CP11/CP11b verification on hardware, this was tagged as the partnership-recording firmware. Public source release with sanitized credentials. See top-level README.md for build instructions.

---

## v0.3.0 — Setup-without-rebuild + diagnostics · 2026-05-02

Goal: kill the "edit C and recompile to change Wi-Fi" friction. Anyone with the binary should be able to set up the device on their own network using only a phone or laptop browser.

### CP12 — Wake-word integration · "Hi Tuya" reaches the app

Re-enabled the Wanson KWS engine via Kconfig:

```
CONFIG_ENABLE_AI_LANGUAGE_ENGLISH=y
CONFIG_ENABLE_COMP_AI_AUDIO=y
CONFIG_ENABLE_COMP_AI_MODE_WAKEUP=y
CONFIG_ENABLE_WAKEUP=y
CONFIG_ENABLE_AI_PLAYER=y
CONFIG_AI_PLAYER_ALERT_SOURCE_LOCAL=y
```

Registered a callback via `tkl_kws_reg_wakeup_cb(wake_word_cb)`. The callback is intentionally minimal — `s_wake_count++; touch_tap_trigger();` — because earlier attempts that played an audible alert + slept 700 ms blocked the KWS thread and missed subsequent wake events. Audible-feedback policy is now exposed as a KV setting (CP17) and surfaced in the web Settings page.

Serial-log evidence the KWS engine auto-initializes when these flags are on:

```
[ai_audio_input.c:355] audio input -> wakeup mode set from 0 to 1!
WAKEUP_info: nihaotuya-xiaozhitongxue-heytuya-hituya--200KB--20250804
```

### CP13 — Wi-Fi state machine with KV + AP fallback

Three-tier connect priority in `nav_app_main`:

1. KV-stored creds (set via web UI) — `wifi.ssid` + `wifi.pass` keys
2. Build-time `NAV_SSID_LIST` (existing v0.2.0 path)
3. AP-mode fallback — `IRIS-XXXX` open network on `192.168.4.1`

If station mode times out at all three steps, the device stays in AP mode forever and the web UI is reachable so the user can configure new credentials. After save, the device persists to KV and reboots — next boot tries the new creds first.

### CP14–CP15 — Embedded HTTP server + Wi-Fi setup page

Single-threaded HTTP/1.0 server bound on port 80, INADDR_ANY, backlog 4. Deliberately HTTP/1.0 with `Connection: close` so each request is a fresh socket and we don't track connection state.

Routes shipped in v0.3.0:

| Method | Path | Purpose |
|---|---|---|
| GET | `/` | Home — nav + quick links |
| GET | `/wifi` | Wi-Fi scan + select + password form |
| GET | `/settings` | Volume / brightness / voice / language / wake-feedback form |
| GET | `/diagnostics` | System telemetry + manual NAVIGATE/READ trigger buttons |
| GET | `/api/status` | JSON device status |
| POST | `/api/wifi/save` | Persist Wi-Fi creds → reboot |
| POST | `/api/settings/save` | Persist runtime settings (live-applies brightness) |
| POST | `/api/test/tap` | Fire single-tap (NAVIGATE) handler |
| POST | `/api/test/double_tap` | Fire double-tap (READ TEXT) handler |

Visual style mirrors the device LCD: `#05070A` deep obsidian background, Space Grotesk headings + JetBrains Mono labels (Google Fonts CDN), IRIS cyan `#4FE3F0` accents. Cards, rounded inputs, ring-only buttons. No external CSS framework — everything inlined in the `HEAD` constant.

### CP16 — NTP-driven status-bar clock

`tal_time_service` auto-syncs once Wi-Fi is up. Added `nav_clock_update` running on a `tal_sw_timer` every 30 seconds, formatting POSIX time as `HH:MM` and pushing to a new `s_clock_label` widget anchored top-right on every state's display layer.

### CP17 — Settings persistence

Five KV keys for runtime tuneables, with public accessors consumed by the web UI:

| KV key | Type | Default | Range |
|---|---|---|---|
| `set.vol` | int | 70 | 0–100 |
| `set.bri` | int | 80 | 10–100 |
| `set.voice` | str | "MID" | LO / MID / HI |
| `set.lang` | str | "EN" | EN / ES / HI / AR |
| `set.wakefb` | int | 0 | 0/1 |

`tal_kv` returns heap pointers — every read pairs `tal_kv_get` with `tal_kv_free`. Saves URL-decode the form-encoded body in place via `url_decode` then call the typed setter.

### CP18 — Diagnostics + brightness + buffer-overflow fix

Three landings in one wave:

1. **`/diagnostics` page** — wake-event counter (proves the KWS callback is firing even when behavior seems silent), free heap, IP, uptime, plus two manual-trigger buttons that POST to `/api/test/tap` and `/api/test/double_tap` to simulate gestures without physical touch.
2. **Live brightness application** — `nav_settings_set_brightness` now resolves the display handle via `tdl_disp_find_dev("display")` and calls `tdl_disp_set_brightness(handle, 0–100)`, which scales internally to the PWM duty (1 kHz, 0–10000). Saved value re-applied on every boot just after `nav_display_init()`.
3. **Streaming page renderer** — fixed a class of bug where `route_home` had a 3072-byte buffer but `HEAD` alone is 3644 bytes. `snprintf` returned a length larger than the buffer, the size guard `n < sizeof(resp)` correctly rejected the buffer, and the response was silently dropped — browser saw `ERR_EMPTY_RESPONSE`. New `send_html_page(fd, body)` streams `HTTP headers → HEAD → body → FOOT` in separate `tal_net_send` calls so routes only need to size the body chunk.

### Deferred from v0.3.0 → v0.4.0

| Item | Why deferred |
|---|---|
| **mDNS hostname `iris.local`** | TuyaOpen's lwIP build does not have `LWIP_MDNS_RESPONDER=1` by default; enabling requires a platform-level rebuild beyond the app scope. Use the device IP from `/api/status` or the router admin page until then. |
| **Captive-portal DNS hijack** | Same reason — needs lwIP MDNS or a DNS-redirect helper. AP mode still works; the user just types `192.168.4.1` directly. |
| **USB host (mic / camera / speaker takeover)** | USB host stack init conflicted with the camera DVP path in CP12 experiments → boot loop. Needs platform-level investigation. |
| **Wake-word audible feedback play** | Kconfig + KV toggle are wired, but the actual `ai_audio_player_alert` call must run off the KWS callback thread (otherwise it blocks subsequent wake detections — proven by CP12e regression). Needs a `tal_workq`-based deferral wrapper. |

---

## Deferrals (updated 2026-05-02 after CP11b landed)

| Item | Why deferred |
|---|---|
| **Checkpoint 6 — Pupil parallax** | T5AI board has no accelerometer + no mic-direction beamforming wired up. Would need either an LSM6DSO IMU on the I2C bus or DSP work on dual mics. Re-evaluate when sensor stack ships in Phase 3. |
| **Settings overlay screen** | Cosmetic only without backing logic (volume change, brightness, language switching). Better to ship with Phase 2 web UI work where the same controls live and the persistence story is real. |
| **Boot splash screen** | Current connecting state shows the bionic eye + IRIS label — close enough to the design's boot screen. A separate splash with calibrating-sensors hairline progress + IRIS title is cosmetic for ~2 seconds of boot; not worth the LOC for v0.1.1. Revisit if first impression matters more for the channel video. |
| **Bionic-eye tick marks (24 every 15°)** | Each tick is an `lv_line` widget. 24 widgets per eye state is heavy; visual contribution is subtle. Add only if the design demands "feels like a precision instrument" for the camera. |
| **Crosshair reticle (4 small lines)** | Subtle decorative element. Defer until a polish pass. |
| **Halo glow gradient** | LVGL has no native radial gradient. Could approximate with shadow but adds GPU cost. Skip. |

