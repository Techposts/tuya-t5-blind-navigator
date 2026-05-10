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

## v0.3.1 — Polish + forget-Wi-Fi + AP UX hardening · 2026-05-02

Goal: address every UX gap and bug surfaced by hardware testing on top of v0.3.0. End-to-end Wi-Fi setup flow that anyone — sighted or sight-impaired setup helper — can complete without recompiling firmware. Three checkpoint waves (CP20, CP21, CP22 with letters a-j) shipped under one tag because they were all driven by the same continuous testing loop.

### CP20 — Live AJAX settings + diagnostics + buffer-overflow fix

- **Live AJAX**: drag a slider on `/settings`, fires save 220 ms after release, green toast confirms. No page reload.
- **`/diagnostics` page**: wake-event counter (proves "Hi Tuya" callback is firing), free heap, IP, uptime, manual NAVIGATE/READ/IDENTIFY trigger buttons.
- **Volume now actually applies**: `nav_settings_set_volume` calls `ai_audio_player_set_vol(v)` and applies on every save + on every boot.
- **Buffer-overflow fix**: `route_settings_save` and `route_wifi_save` had small response buffers (1500–2048 bytes) that couldn't hold the 3644-byte HEAD CSS chunk. Fix: stream HEAD/body/FOOT separately via `send_html_page`. Replaces silent ERR_EMPTY_RESPONSE on save with a real 303 redirect that lands the user back on `/settings?saved=1`.
- **Rich `/api/status` JSON**: `state`, `ip`, `rssi_dbm`, `free_heap_b`, `uptime_epoch`, `wake_count`, `volume`, `brightness`. Polled every 3 s by the new home-page live status panel.
- **Polished home page**: SVG-icon nav, three large vision-action buttons, live status card auto-refresh.

### CP21 — Forget Wi-Fi (web button + 5-second physical hold)

- New web `/wifi` page card with a red destructive "FORGET WI-FI" button. Confirm dialog + 303 redirect to a dedicated reboot page.
- Five-second physical button hold triggers the same forget. Detected via `LONG_PRESS_HOLD` event with elapsed-ms tracking.
- New KV key `wifi.force_ap`: when armed, boot path skips both KV creds and build-time `NAV_SSID_LIST` and goes straight to AP mode. Cleared automatically on next successful save via `nav_wifi_save_kv`.

### CP22 — AP visibility + display UX + button hardening (10 polish iterations)

a–c: AP visibility — defensive `nav_wifi_start_ap` (stop+disconnect+sleep+logs), AP-only mode lockdown (303 redirects from /home/settings/diagnostics → /wifi), simplified /wifi setup wizard.

d–f: AP setup screen on the LCD — force-show `s_state_label` with the SSID, hide conflicting per-state widgets, add a multi-line bottom hint label with two short instructions (`JOIN PHONE Wi-Fi` / `THEN OPEN 192.168.4.1`). After save, the same hint area updates in-place to `SAVED · CONNECTING TO <ssid> · REBOOTING…` instead of a too-small overlay banner.

g–h: button gesture fixes — re-registered `TDL_BUTTON_LONG_PRESS_START` (was dropped accidentally; without it, 5-second holds leak SINGLE_CLICK on release and trigger NAVIGATE instead of forget). Explicit `long_start_valid_time = 1000 ms`, `long_keep_timer = 500 ms`. Defensive SINGLE_CLICK suppression based on actual press duration. Serial logs (`[BTN] DOWN/UP/HOLD ...`) for diagnostic visibility.

i: banner readability — shorter forget text (`FORGET %d%%`, `FORGETTING`, `CANCELLED` instead of overflowing 32-pt-font strings), fully opaque banner background. NTP polling accelerated from 30 s to 5 s so the status-bar clock appears within seconds of NTP sync; `[CLOCK] NTP not synced yet (rt=0xN)` log line surfaces sync failures.

j: dedicated forget overlay — replaces the mode banner for forget messages with a centered modal panel (W-16 wide, 140 px tall, fully opaque black, 2-pixel amber border that turns red at ≥80% progress, real progress bar at the bottom). IDENTIFY mode deferred from `LONG_PRESS_START` (1 s) to `PRESS_UP` only when held was 1000–2500 ms — keeps the underlying screen calm during a 5-second forget hold.

### Display states added

- AP setup: top-left `WI-FI SETUP MODE`, top-right `192.168.4.1`, center `IRIS-XXXX` in amber, bottom hint with 2-line instruction.
- AP saved (post `/api/wifi/save`): same area flips green + shows `SAVED ✓ / CONNECTING TO <ssid> / REBOOTING…`.
- Forget gesture (3+ s held): solid centered modal overlay with progress bar.
- Forget cancelled: gray `CANCELLED` banner.

### Deferred from v0.3.1 → v0.4.0

| Item | Why |
|---|---|
| **mDNS hostname `iris.local`** | Needs lwIP rebuild (`LWIP_MDNS_RESPONDER=1`) at platform level |
| **Captive-portal DNS hijack** | Same lwIP-rebuild dependency |
| **Wake-word audible feedback** | Toggle is wired but `ai_audio_player_alert` must run off the KWS callback thread (proven by CP12e regression) — needs `tal_workq` deferral wrapper |
| **LD2450 mmWave radar** | Hardware integration on a separate board iteration |

### Release artifacts

- **Binary**: `iris_v0.3.1_QIO.bin`
- **SHA256**: `57e7465f4fa9f4e24fd1e8ef655b65c6f8ab78a05d9c48a7130bf34cdc298d92`

---

## v0.3.2 — Audio fix + parser fix + speed-up · 2026-05-10

After v0.3.1 hardware testing surfaced silent-audio symptoms post-reboot, this release does the deep root-cause work to actually make the audio output path *reliably* play through the onboard speaker. Plus latency improvements and a UX reorder so audio + display arrive together. Single tag bundling all the diagnostic work + multiple fixes from one continuous testing loop.

### Bugs fixed

#### 1. Codec output path: `OPRT_RESOURCE_NOT_READY` from `tkl_ao_put_frame`

The bug that made the speaker silent on every TTS response and even local alerts:

- The TKL audio driver (`tkl_audio.c`) gates `tkl_ao_put_frame` behind `s_audio_init.audio_init && s_audio_init.audio_start`. Both flags are set only inside `tkl_ai_init` (called via `tdl_audio_open` → `__tdd_audio_open`).
- v0.3.0 / v0.3.1 never called `tdl_audio_open` for our app. The wake-word KWS engine has its own audio path that doesn't go through tdl, so the device's input side worked while output stayed dead.
- Symptom: every `tkl_ao_put_frame` returned `-23`. Visible on serial as `tdd_audio.c:141 ret:-23` repeating.

Fix: explicitly call `tdl_audio_open(audio_codec, NULL)` once during `nav_app_main` after `board_register_hardware`. First attempt used `ai_audio_input_init` which did open the codec but allocated a 12.8-second recorder buffer (~400 KB) that we never used — this dropped free heap from 121 KB to 26-43 KB and broke the *next* layer (TTS WAV download couldn't fit). Final fix calls `tdl_audio_open` directly, no recorder, free heap stays at 136 KB+.

#### 2. SPOKEN line never reached the TTS

The parser at `parse_labeled_response` had a hard cap:

```c
while (line && out->field_count < 4) { ... }
```

The LLM response is 5 labeled lines: `PATH STATUS` / `WHERE` / `ACTION` / `WHY` / `SPOKEN`. After 4 fields the loop exited — never seeing SPOKEN. Fallback then built TTS prose by concatenating the 4 short visual fields, which is exactly the "audio just reads the screen" / "very brief" symptom users reported.

Fix: change the loop to `while (line)` and move the 4-field cap into the *display-field* branch only. SPOKEN always gets parsed regardless of how many display fields preceded it.

#### 3. Display vs audio timing — they now arrive together

Previously: vision LLM → display fields → TTS download (2-3s) → audio playback. User saw display first, silence for a couple seconds, then audio kicked. Felt broken.

Reorder didn't fix it — moving display *after* `play_response` made display appear after audio finished (since `play_response` blocked on `while(ai_audio_player_is_playing())`).

Final fix: split `play_response` into `play_response_kick` (returns once audio is queued, ~2-3s for TTS download) and `play_response_wait` (blocks until playback finishes). Sandwich the display update between them: kick → display → wait. Audio + display now appear within ~50 ms of each other.

### Latency improvement

Camera autoexpose warmup reduced from **3500 ms → 1500 ms**. Saves 2 seconds on every NAVIGATE / READ / IDENTIFY query. The GC2145 stabilizes well within 1.5 s for navigation-quality images; the longer warmup was paranoia.

### TEST SPEAKER button now plays a real welcome message

Was: brief "Hello, I'm here" via local alert WAV (only validated codec + speaker). Now: TTS-generated 4-sentence welcome message via the same pipeline real responses use. Validates the entire chain end-to-end (proxy → OpenAI TTS → device receive → codec → speaker) and serves as a self-introduction for sighted helpers.

### Mac-side flashing setup

Built out `tools/` directory: Python venv (`tools/flash-venv/`) with `pyserial`, `tqdm`, `click`, `PyYAML`, `requests`, `rich` deps; rsynced `tools/tyutool/` from the dev VM. Flashing now works directly from the Mac via `tools/flash-venv/bin/python tools/tyutool/tyutool_cli.py write -d T5AI -f releases/iris_v0.3.2_QIO.bin -p /dev/cu.usbmodem5AAE1675771 -b 460800`. No more VM round-trip for flash; build still requires VM toolchain.

### Web UI favicon

Inline SVG bionic-eye favicon (data URI in `<link rel=icon>`) — outer cyan ring + white center pupil, matches the on-device LCD motif. No additional HTTP route needed.

### Tuya partnership confirmation: BT TWS not feasible on T5AI

7 Bluetooth-capability questions sent to Tuya 2026-05-02; answer received 2026-05-10: T5-E1-IPEX module does not expose classic BT (BR/EDR) at a usable level for A2DP, even though BSP A2DP source files exist. Locked decision: stay with onboard speaker; no external module path either. The "private audio" UX gap remains a known hardware limitation of this board for this firmware family.

### Known open issue (deferred to next release)

- **"Hi Tuya" wake word not firing reliably**. The Wanson KWS engine initializes (serial confirms model load) but `wake_count` stays at 0 even after speaking the wake word at close range. Not a regression from v0.3.1 — same behavior as before. Needs separate debug session: probably mic-routing question (was working when codec wasn't actually opened, may be different path now), or KWS callback registration timing relative to audio init order. Documented for v0.3.3 / v0.4.0.

### Release artifacts

- **Binary**: `iris_v0.3.2_QIO.bin`
- **SHA256**: `14848f1ad0c4b91ec966e545d9c2d249ed43c320cd462d60a886aff22f99b4d3`

---

## v0.3.3 — Wake word + welcome announce + AP-only Wi-Fi · 2026-05-10

The bug-fix release on top of v0.3.2 that closes the "Hi Tuya" wake-word gap, removes the hardcoded Wi-Fi fallback, and gives every fresh boot an audible introduction. Five small surgical edits, no architectural change.

### Bugs fixed

#### 1. "Hi Tuya" wake word never fired (`wake_count` stuck at 0)

The Wanson KWS engine was loaded (model file was present, callback was registered via `tkl_kws_reg_wakeup_cb`) but the engine's I2S capture loop never started, so it never had any audio to score against the model. Saying "Hi Tuya" did nothing.

Root cause: `tkl_kws_reg_wakeup_cb` only stores the callback pointer — it does **not** start the engine. The reference path in TuyaOpen's `ai_chat_main.c` calls `tkl_kws_init()` explicitly right after audio init. We were missing that one call.

Fix: add `tkl_kws_init();` immediately before `tkl_kws_reg_wakeup_cb(wake_word_cb)` in `nav_app_main`. Verified on hardware — saying "Hi Tuya" now fires the NAVIGATE flow without any tap.

#### 2. TEST SPEAKER button got stuck on the speaking screen

Pressing the **TEST SPEAKER** button in the web UI played the welcome TTS, but afterwards the screen stayed on the speaking-state visual (animated eye + waveform) forever. Forced a physical reset.

Root cause: `play_response_kick` sets `DISP_STATE_SPEAKING`, but only the NAVIGATE/READ flow at lines 240-245 of `main.c` owns the IDLE transition. `nav_diag_play_test_alert` borrowed kick+wait without the IDLE cleanup.

Fix: explicit `nav_display_set_state(DISP_STATE_IDLE)` after `play_response_wait` in `nav_diag_play_test_alert`. Plus `s_idle = 1` to re-arm subsequent triggers.

#### 3. TEST SPEAKER while NAVIGATE in flight corrupted the player ring buffer

If you pressed TEST SPEAKER while a real query was still mid-TTS, the static `s_pending_pcm` and the player's ring buffer collided — symptoms `ai_audio_player.c:382 ret:-2` and `player ring buf write failed` on serial.

Fix: `nav_diag_play_test_alert` now refuses (logs `[DIAG] test speaker refused: device busy`) when `s_idle == 0`. Same guard pattern used by NAVIGATE/READ/IDENTIFY entrypoints.

#### 4. "Forget Wi-Fi" hardcoded fallback — planned for removal, **deferred to v0.3.4**

Before v0.3.3, the boot path tried (in order): KV creds → build-time `NAV_SSID_LIST` from `nav_secrets.h` → AP fallback. Pressing "Forget Wi-Fi" cleared KV but the build-time list still let the device attach, defeating the purpose of forgetting.

The plan was to remove the build-time fallback so empty KV → AP-mode-for-provisioning. Late in v0.3.3 testing surfaced a separate AP DHCP issue: post-flash (which wipes KV), the device boots into AP mode, but the DHCP server inside the Tuya AP path is flaky on first connection — phones get "network temporarily unavailable" or a self-assigned 169.254 IP unless they retry several times. Removing the fallback in this state would have trapped the device unrecoverably after every flash for users who couldn't manage the AP retry dance.

Decision: **keep the fallback for v0.3.3, fix AP DHCP first in v0.3.4, then remove the fallback as originally intended.** The fallback's "forget-wifi defeats itself" UX gap remains until both ship.

### New: audible welcome on Wi-Fi connect

After station-mode connect succeeds and the audio chain is up (codec opened, player initialized, volume applied), a brief 3-sentence intro plays via the same TTS pipeline that real responses use:

> "I am IRIS, your vision co-pilot. Tap once to navigate, twice to read text, long press the button to identify objects."

Implemented as `nav_announce_welcome()` in `main.c`. Synchronous: blocks the boot thread for ~5 seconds while TTS downloads + plays. Boot is already 10-15 s, so the extra latency is acceptable for a one-time greeting per power-on. Gracefully no-ops if the proxy is unreachable (TTS download fails → `s_pending_pcm` stays NULL → wait returns immediately → IDLE).

### Known open issues (deferred to v0.3.4)

#### TTS audio cuts off / intermittently silent

A regression introduced by the wake-word fix. With `tkl_kws_init()` now properly starting the KWS engine, the I2S input path is continuously active for wake-word detection. T5AI's audio codec shares I2S lanes between input and output, and the contention surfaces during TTS playback as: (a) the welcome message cutting off before the last sentence, (b) NAVIGATE / READ TTS occasionally not playing at all, (c) when it does play, ending prematurely. Display side is unaffected.

Fix path for v0.3.4: suspend KWS capture during TTS playback (`tkl_kws_stop()` before `play_response_kick`, restart after `play_response_wait`), or move the engine to a second I2S lane if T5AI exposes one. v0.3.3 ships with this tradeoff because wake word working is a bigger user win than glitch-free playback.

#### AP DHCP flakiness on first connect

Post-flash (which wipes KV), device boots into AP mode. Phones see and join the `IRIS-XXXX` SSID, but the first DHCPDISCOVER often gets ignored — phone shows "network temporarily unavailable" or self-assigns a 169.254 address. After 1-3 retries (sometimes 30+ seconds of waiting) the lease eventually arrives.

Hypothesis: race between `bk_wifi_ap_start` returning and `bk_netif_set_ip4_config` actually bringing the netif up. Phone's first DISCOVER lands in the gap. Confirmed AP code matches Tuya's production reference (`ap_netcfg.c:810-846`) byte-for-byte, so this is a platform-level timing issue, not an application bug. Fix likely needs a 500-1000 ms sleep between AP start and "AP ready" signal, or proactive DHCP server warm-up.

#### Captive portal hijack

iOS / Android captive-portal probes (`captive.apple.com`, `connectivitycheck.gstatic.com`) fail because the AP has no internet — phones show "Internet may not be available". Ugly UX. Real fix: minimal DNS hijack on UDP/53 in AP returning `192.168.4.1` for any query. Phone's probe then 302's to our setup page → native "Sign in to network" sheet. ~60 LOC, no lwIP rebuild needed.

### Release artifacts

- **Binary**: `iris_v0.3.3_QIO.bin`
- **SHA256**: `d191819fb8098eef13d926e2ef08844d84843183cefd45f08e88a22dfc789e7e`

---

## v0.3.4 hot-fix `faacc5a` — wake-word real fix + LVGL fault + chunked streaming · 2026-05-10

v0.3.4 commit `7fa93bd` shipped chunked streaming + welcome announce + 180° rotation toggle, but on-hardware testing surfaced a UsageFault (LVGL thread, unaligned access) on every reboot, web-UI crashes, and the wake word stayed broken. The hot-fix `faacc5a` is the stable result.

**Wake-word real root cause** (the v0.3.3 fix wasn't enough): v0.3.3 called `tkl_kws_init()` + `tkl_kws_reg_wakeup_cb()` but never called `tkl_kws_enable()`, AND passed `NULL` as the audio frame callback to `tdl_audio_open`. KWS loaded its model and accepted our wakeup-cb registration, then sat idle forever — no audio routed to it. Fix: define `nav_audio_frame_cb` that forwards every mic frame to `tkl_kws_feed_with_vad`, pass it to `tdl_audio_open` instead of `NULL`, add `tkl_kws_enable()` after the registration. Reference: `apps/tuya.ai/ai_components/ai_mode/src/ai_mode_wakeup.c:148-152`.

**LVGL UsageFault**: removed the `lv_display_get_default()` + `lv_display_set_rotation()` block from `nav_display_init`. Tuya's LVGL wrapper sets up the default display *asynchronously* after `lv_vendor_start`, so calling `get_default()` synchronously inside init returns a pointer that the LVGL render thread later faults on (unaligned access).

**Chunked TTS streaming** (kept): `openai_tts_stream()` in openai_backend.c uses raw TCP via `tal_net_socket` + `tal_net_send/recv`. Streams the WAV from the proxy in 4 KB recv chunks, feeds each chunk straight into `ai_audio_play_tts_stream(DATA, ...)`. Audio starts ~500 ms after request; per-call memory drops from 320 KB to ~6 KB.

**Removed in hot-fix**: KWS suspend/resume around TTS playback (not needed once chunked streaming fixed cut-off), 180° rotation toggle UI (caused the LVGL fault), boot welcome announce (broke wake word + crashed /wifi).

---

## v0.3.5 — Mic gain + multilingual + interruptible animation · 2026-05-10

The release that turns the v0.3.4 base into something a video shoot can demo end-to-end. Five user-facing fixes from on-hardware testing.

### Bugs fixed

#### 1. Wake-word range was 5 cm

User reported having to be within 5 cm of the device for "Hi Tuya" to fire. Audit found `tdd_audio.c:108` has a commented-out `tkl_ai_set_vol(0, 0, 80)` — the boot path leaves the BK7258's ADC mic gain at hardware default which is far too low for far-field MEMS.

`tkl_ai_set_vol` has a side effect of toggling the speaker-enable GPIO (lines 612-617 in `tkl_audio.c`) which can mute the speaker. Bypass it and call the underlying BK SDK directly: `bk_aud_adc_set_gain(50)` where 50 ≈ 80 % of `0x3F` max. With AEC already enabled (board V102 auto-selects `ENABLE_AUDIO_AEC`), this restores usable far-field range — verified ~1-1.5 m on hardware. Beyond that you'd need beamforming.

#### 2. Language setting in /settings did nothing

User changed /settings to Hindi, no audio change. `nav_settings_get_language()` was defined and storing to KV but the value was never read at query time. Fix: in `proc_th`, build a language-aware prompt before calling `openai_ask_image`. Append `"IMPORTANT: Write the SPOKEN line in <Hindi/Spanish/Arabic/English>. Keep the other fields in English for the display."` to the active prompt template. GPT-4o-mini and `gpt-4o-mini-tts` both handle these languages natively via the alloy voice.

Forward-declared `nav_settings_get_language` at the top of main.c since `proc_th` uses it before the definition appears later.

#### 3. SPEAKING animation lingered for several seconds after audio ended

Pure duration-based wait was sleeping the full `audio_ms` after STOP, but with chunked streaming, audio has been playing throughout the multi-second network transfer. By STOP time, much of `audio_ms` has already played; the remainder is much smaller.

Fix: timestamp `s_stream_start_ms` in `play_response_kick`. In the wait, compute `remaining_ms = audio_ms - elapsed_ms`; bound 200-8000 ms. Tail poll on `is_playing` kept (with interrupt check).

#### 4. The 4-second post-speak linger (CP11) ignored every gesture

`tal_system_sleep(4000)` at the end of `proc_th` was hardcoded. Even if the wait loop exited early, this kept SPEAKING up for 4 more seconds with no way to skip. Now broken into a 100 ms-tick poll that exits on `s_play_interrupt`.

#### 5. Tap-to-interrupt only worked from the duration sleep, not from the linger or other gestures

v0.3.4's tap-to-interrupt (commit `c888935`) fired `s_play_interrupt = 1` + `ai_audio_player_stop(AI_AUDIO_PLAYER_FG)`. v0.3.5 extends:
- `touch_swipe_up_trigger` and `touch_swipe_down_trigger` now check `nav_display_get_state() == DISP_STATE_SPEAKING` and set the same flag.
- The CP11 linger now polls the flag (see #4 above).
- Button SINGLE_CLICK funnels into `touch_tap_trigger` so it inherits the same interrupt behavior.

### Also in v0.3.5

- **AP DHCP settle** (commit `a2c0f65`): 1.2 s sleep after `tal_wifi_ap_start` so the underlying lwIP netif + DHCP server task have time to come up before phones DHCPDISCOVER. Should fix the "network temporarily unavailable" UX on first AP join.

### Release artifacts

- **Binary**: `iris_v0.3.5_QIO.bin`
- **SHA256**: `0797aefe1c8b85dc8fb80f69bb4dd6a2ae6cd9a237e0babc3cf617f95741401d`

### Known open issues (deferred to v0.3.6)

- **Voice follow-up Q&A** — wake word fires reliably now but the device doesn't yet record + transcribe a follow-up question. Needs `ai_audio_input_init` recorder + Whisper + dynamic prompt.
- **Captive portal hijack** — UDP/53 listener returning 192.168.4.1 for all queries.
- **NTP `--:--`** — `tal_time_check_time_sync` returns 0xffffffff. Likely needs custom SNTP client.
- **Boot welcome message reintro** — needs KWS-first ordering.
- **180° rotation toggle reintro** — via `lv_timer_create` deferred init.
- **Hardcoded `NAV_SSID_LIST` fallback removal** — only after AP DHCP fix is verified by multiple devices.

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

