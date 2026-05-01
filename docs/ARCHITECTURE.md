# Architecture

## High-level data flow

```
┌─────────────────────┐  press / tap   ┌─────────────────────┐
│  P12 button (KEY)   │ ──────────────▶│   trigger dispatch  │
└─────────────────────┘                └──────────┬──────────┘
                                                  │   selects prompt
┌─────────────────────┐  tap            single ▶  NAV_VISION_PROMPT
│  Touchscreen tap    │ ──────▶         double ▶  NAV_READ_PROMPT
└─────────────────────┘                 long   ▶  NAV_OBJECT_PROMPT
                                                  │
                                                  ▼
                            ┌────────────────────────────────────┐
                            │  proc_th worker (24 KB stack)      │
                            │   ┌────────────────────────────┐   │
                            │   │ 1. nav_display_set_state   │   │
                            │   │    (LOOKING / ANALYZING)   │   │
                            │   ├────────────────────────────┤   │
                            │   │ 2. ai_video_get_jpeg_frame │   │
                            │   │    → 480x480 JPEG ~30-60KB │   │
                            │   ├────────────────────────────┤   │
                            │   │ 3. base64 encode           │   │
                            │   │    + JSON request body     │   │
                            │   ├────────────────────────────┤   │
                            │   │ 4. HTTP POST → proxy:8888  │   │
                            │   │    /v1/chat/completions    │   │
                            │   ├────────────────────────────┤   │
                            │   │ 5. parse cJSON,            │   │
                            │   │    nav_display_set_text    │   │
                            │   ├────────────────────────────┤   │
                            │   │ 6. HTTP POST → proxy:8888  │   │
                            │   │    /v1/audio/speech (WAV)  │   │
                            │   ├────────────────────────────┤   │
                            │   │ 7. ai_audio_play_tts_stream│   │
                            │   │    → onboard speaker       │   │
                            │   ├────────────────────────────┤   │
                            │   │ 8. nav_display_set_state   │   │
                            │   │    (IDLE) — text persists  │   │
                            │   └────────────────────────────┘   │
                            └────────────────────────────────────┘
                                              │
                                              ▼
                            ┌────────────────────────────────────┐
                            │  Flask proxy (separate machine)    │
                            │   adds Authorization header,       │
                            │   forwards over HTTPS to OpenAI,   │
                            │   streams response back over HTTP  │
                            └────────────────────────────────────┘
                                              │
                                              ▼
                            ┌────────────────────────────────────┐
                            │  api.openai.com                    │
                            │   gpt-4o-mini       (vision/chat)  │
                            │   gpt-4o-mini-tts   (text→speech)  │
                            └────────────────────────────────────┘
```

## Threading model

- **`tuya_app_main`** spawns `nav_app_thread` (8 KB stack, prio 4) — Tuya's
  RTOS entry point.
- **`nav_app_thread`** does all init: log, KV, sw timer, workq, TLS, time,
  hardware, Wi-Fi, audio player, camera, button, then idles forever.
- **`proc_th` worker** (24 KB stack, prio 3) is spawned per button/touch
  event. It does the capture → cloud → playback chain, then deletes itself.
  This pattern avoids running cloud calls (which need ~16 KB) on a system
  thread.
- **LVGL** runs on its own internal thread (`lv_vendor_start(prio 0, 8 KB)`).
  Every UI update from the app thread or proc_th wraps the call in
  `lv_vendor_disp_lock()` / `_unlock()` — required to avoid race conditions
  with the LVGL render loop.

## State machine

The on-screen state is driven by `nav_display_set_state()` and color-coded:

| State | Color | Trigger |
|---|---|---|
| `BOOTING` | dim grey | initial |
| `CONNECTING` | dim grey | Wi-Fi attaching |
| `READY` (`IDLE`) | cyan | post-init, ready for trigger |
| `LOOKING` (`LISTENING`) | amber | camera capture in progress |
| `ANALYZING` (`PROCESSING`) | magenta | cloud round-trip in progress |
| `SPEAKING` | green | TTS audio playing |
| `ERROR` | red | network/proxy failure |

The bionic-eye iris, pupil glow, and orbit ring all recolor in unison.

## Components

### Firmware (T5 board)

- **`main.c`** — entry, Wi-Fi connect loop, button + touch dispatch,
  `proc_th` worker.
- **`nav_display.c`** — LVGL UI (top status bar, bionic eye, state label,
  response panel). State setters are thread-safe via `lv_vendor_disp_lock`.
- **`openai_backend.c`** — HTTP client → proxy. Functions:
  `openai_ask_image(jpeg, prompt) → text` and `openai_tts(text) → wav_pcm`.
- **`nav_config.h`** (gitignored) — Wi-Fi creds, OpenAI API key, prompts,
  proxy IP. Template at `nav_config.h.example`.

### Off-board (Flask proxy)

- **`openai_proxy.py`** — single-file Flask app. Listens on `0.0.0.0:8888`,
  forwards every `/v1/*` to `api.openai.com` over TLS, injects the
  `Authorization` header, streams response back.
- **`openai-proxy.service`** — systemd unit for permanent install.

### Why a proxy?

The T5's mbedtls is built without TLS 1.3 and lacks the cipher suites
OpenAI's TLS 1.2 endpoint accepts (handshake fails with `0x3a00 =
MBEDTLS_ERR_SSL_HANDSHAKE_FAILURE`). The proxy terminates HTTPS on a
machine that has a normal modern TLS stack (Linux/macOS), so the board
only ever speaks plain HTTP on the local network.

See [LESSONS_LEARNED.md](LESSONS_LEARNED.md) for the full diagnosis chain.

## Network requirements

- Board and proxy machine **must be on the same broadcast domain or routed
  for plain TCP**. Many home networks have multiple subnets (e.g.
  192.168.0.x for wired + 192.168.1.x for guest Wi-Fi) — the board needs
  TCP reachability to the proxy, not just ICMP.
- Proxy machine needs outbound HTTPS to `api.openai.com` (port 443).
