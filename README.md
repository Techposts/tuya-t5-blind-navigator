# IRIS — Vision Co-Pilot

> **Powered by [TuyaOpen](https://tuyaopen.ai/)** — the open-source SDK for the T5 family. The entire device stack (display, camera, audio, Wi-Fi, OTA) runs on TuyaOpen.

An AI-powered visual navigation aid for blind and low-vision users, built on the **[Tuya T5AI Dev Board](https://www.aliexpress.us/item/3256808428198925.html?pdp_npi=6%40dis%21USD%21US%20%24106.78%21US%20%2442.19%21%21%21755.13%21298.33%21%402101ceb717615579575237212d11be%2112000045956265390%21sh%21US%216281805244%21X%211%210%21&spm=a2g0o.store_pc_home.allitems_choice_2005577011180.1005008614513677&gatewayAdapt=glo2usa)** ($42 hardware + ~$50 dev kit total — the cheapest "OrCam-class" build path).

---

## How it works

1. **Tap the screen** (or push the button) — camera captures the scene
2. **OpenAI vision** (gpt-4o-mini) analyzes what's ahead and emits a structured 5-line response (PATH STATUS / WHERE / ACTION / WHY / SPOKEN)
3. **The structured fields render** on the bionic-eye LCD (caregiver / sighted helper can read at a glance)
4. **The SPOKEN line plays through TTS** (gpt-4o-mini-tts) — 4-6 sentences of descriptive prose for the user

> *PATH STATUS: CLEAR. WHERE: open sidewalk, person at 1 o'clock, 4 steps. ACTION: Go forward. WHY: low pedestrian flow, no curbs ahead.*

The device is a **navigation co-pilot**, not a tour guide — every response is imperative, action-oriented, and tied to clock positions + distances.

---

## What v0.3.5 ships

Five fixes that turn the v0.3.3 "kinda works" prototype into something a video shoot can actually demo end-to-end.

- **Hi Tuya wake-word range fixed.** Was 5 cm; now ~1-1.5 m. The BK7258 ADC mic gain was sitting at hardware default — a commented-out `tkl_ai_set_vol(0, 0, 80)` in the SDK had been there as a hint. Bypassed the side-effect-laden TKL wrapper and call `bk_aud_adc_set_gain(50)` directly (≈80 % of 0x3F max). With AEC already enabled on board V102, this is the realistic ceiling for the onboard MEMS without beamforming.
- **Multilingual TTS works.** /settings has had EN/ES/HI/AR for ages but the value was stored in KV and never used. Now the language directive is appended to the LLM prompt: `"Write the SPOKEN line in <Hindi/Spanish/Arabic>. Keep the other fields in English for the display."` — GPT-4o-mini and gpt-4o-mini-tts handle these natively via the alloy voice. Switch in /settings, next NAVIGATE response is in the new language.
- **Chunked TTS streaming** (carried from v0.3.4): audio starts ~500 ms after request instead of buffering the full WAV. Per-call memory drops from 320 KB to ~6 KB.
- **Animation matches audio end.** Was a duration-based wait that ignored the streaming overlap; now `remaining_ms = audio_ms - elapsed_during_stream`. The hardcoded 4-s post-speak linger is also interruptible.
- **Universal interrupt.** Tap, swipe up, swipe down, and the physical button — any of these during the SPEAKING screen now stops audio and returns to IDLE within ~500 ms. No more waiting through long responses.

Also in v0.3.5: AP DHCP settle delay (1.2 s after `tal_wifi_ap_start`) so phones get DHCP leases on first connect attempt instead of "network temporarily unavailable".

**Known open issues** (deferred to v0.3.6):

- **Voice follow-up Q&A** — wake word fires but the device doesn't yet record + transcribe a follow-up question. Needs `ai_audio_input_init` recorder + Whisper transcription + dynamic prompt. ~150-250 LOC.
- **Captive portal hijack** — joining the IRIS AP shows "Internet may not be available" because there's no internet on the AP-only network. Workaround: ignore the warning, manually open `192.168.4.1`. v0.3.6 will add a UDP/53 DNS hijack so the standard "Sign in to network" sheet appears.
- **NTP `--:--`** — clock never syncs. `tal_time_check_time_sync` returns 0xffffffff. TuyaOpen's time service likely depends on Tuya cloud auth which we're not running; will need a custom SNTP client (~80 LOC).
- **Boot welcome message** — broke wake word in v0.3.4. Reintroduce in v0.3.6 with KWS-first ordering.
- **180° rotation toggle** — v0.3.4 attempt caused an LVGL UsageFault. v0.3.6 will use an `lv_timer_create` deferred-init that fires after the first display refresh, when `lv_display_get_default()` is safe to call.

## What v0.3.3 / v0.3.4 shipped

v0.3.3 fixed the "Hi Tuya" wake word with a one-line `tkl_kws_init()` and added a boot-time welcome announce. The welcome was removed in v0.3.4 because it broke wake-word + crashed /wifi (the boot-time TTS stream was leaving Wi-Fi scan in a bad state).

v0.3.4 introduced chunked TCP-streamed TTS via `openai_tts_stream()` (per-call memory dropped from 320 KB to 6 KB; latency from 3 s to 500 ms), and fixed the deeper wake-word root cause: the v0.3.3 fix was missing `tkl_kws_enable()` and was passing NULL as the audio frame callback to `tdl_audio_open` — so KWS loaded its model and registered our callback but never received any audio frames. Now `nav_audio_frame_cb` forwards every mic frame to `tkl_kws_feed_with_vad`.

## What v0.3.2 shipped

The audio path now actually works end-to-end. Voice descriptions play through the onboard speaker for every NAVIGATE / READ / IDENTIFY query. Earlier releases had a silent-codec bug where `tkl_ao_put_frame` returned `-23 OPRT_RESOURCE_NOT_READY` because the audio output engine was never initialized — fixed in v0.3.2 by an explicit `tdl_audio_open` during boot.

Other improvements: the response parser now actually surfaces the LLM's `SPOKEN` line (was capped at 4 fields and dropping the 5th); audio + display fields appear together via a `kick + wait` split around the TTS download; camera warmup halved from 3.5 s to 1.5 s; TEST SPEAKER button now plays a real TTS welcome message instead of a generic alert; Mac-side flashing tools shipped in `tools/`.

Everything from v0.3.1 plus a polish wave that addresses every UX gap surfaced by hardware testing:

| Feature | Status |
|---|---|
| **Wi-Fi setup without rebuild** — boot to `IRIS-XXXX` AP, browser to `192.168.4.1`, pick network, save | ✅ v0.3.0 |
| **AP-mode UX** — locked single-page setup wizard, LCD shows SSID + step-by-step instructions, save → green confirmation flash → reboots cleanly | ✅ v0.3.1 |
| **Forget Wi-Fi** — destructive button on `/wifi` page **+** 5-second physical button hold with on-screen progress bar overlay | ✅ v0.3.1 |
| **Live AJAX settings** — slider/select changes save instantly with toast, no page reload | ✅ v0.3.1 |
| **Live home dashboard** — auto-refreshing status (state, IP, signal, heap, wake count) + three large icon buttons for NAVIGATE / READ TEXT / IDENTIFY | ✅ v0.3.1 |
| **Brightness + volume** apply live to hardware via `tdl_disp_set_brightness` and `ai_audio_player_set_vol`; persist across reboots | ✅ v0.3.0 / 0.3.1 |
| **NTP status-bar clock** — appears within 5 s of NTP sync (down from 30 s in 0.3.0) | ✅ v0.3.1 |
| **"Hi Tuya" wake word** — Wanson KWS engine wired, fires NAVIGATE on detect | ✅ v0.3.0 |
| **Diagnostics page** — wake-event counter, free heap, manual NAVIGATE/READ/IDENTIFY trigger buttons | ✅ v0.3.0 |
| **mDNS hostname (`iris.local`)** | 🔜 v0.4.0 (needs lwIP rebuild) |
| **Wake-word audible feedback** | 🔜 v0.4.0 (needs `tal_workq` deferral so KWS thread isn't blocked) |
| **LD2450 mmWave sensor** for real DEPTH/HAZARDS data | 🔜 v0.4.0 |

See [`design/CHANGES.md`](design/CHANGES.md) for the full per-checkpoint changelog from v0.1.0 → v0.3.1.

---

## Hardware

- **[Tuya T5AI Dev Board](https://www.aliexpress.us/item/3256808428198925.html?pdp_npi=6%40dis%21USD%21US%20%24106.78%21US%20%2442.19%21%21%21755.13%21298.33%21%402101ceb717615579575237212d11be%2112000045956265390%21sh%21US%216281805244%21X%211%210%21&spm=a2g0o.store_pc_home.allitems_choice_2005577011180.1005008614513677&gatewayAdapt=glo2usa)** ($42 on Aliexpress at time of writing) — T5-E1-IPEX module (ARM Cortex-M33F @ 480 MHz, 16 MB PSRAM, Wi-Fi 6, BLE 5.4)
- **3.5" TFT LCD** 320×480 with capacitive touch (ILI9488 + GT1151QM)
- **GC2145 camera** (DVP, 1600×1200 array, used at 480×480 @ 15 fps)
- **Onboard speaker + dual MEMS mics**
- **CH342F USB-to-UART** for flashing/debug

See [`docs/HARDWARE.md`](docs/HARDWARE.md) for full pinout and component reference.

---

## Reference video

Inspiration for production tone and content style: **[Cursor + TuyaOpen — Voice-Interactive AI Device](https://www.youtube.com/watch?v=-OsmPXq4Zx4)**. The IRIS build video is on [@ravis1ngh](https://youtube.com/@ravis1ngh).

---

## Cloud / AI access

Two valid paths. Full comparison in [`docs/CLOUD_OPTIONS.md`](docs/CLOUD_OPTIONS.md).

### Option A — Tuya Cloud Multimodal AI Platform (Tuya's official path)

[TuyaOpen](https://github.com/tuya/TuyaOpen) bridges the device to the Tuya Developer Cloud, which then bridges out to **ChatGPT, Gemini, Claude, Qwen, DeepSeek, Amazon Nova** through a single integrated layer. Drag-and-drop AI agent flows, low-latency streaming TTS/STT, custom MCP server, auto-generated mobile mini-app.

**Pick this if** you're building a real product, want multi-model flexibility, and don't need to own every layer.

### Option B — Local proxy (what this repo implements as v0.2.0)

Run a tiny Flask/FastAPI server on your home network. Board → plain HTTP → proxy → HTTPS → OpenAI. ~30 LoC of Python; full control over prompt, model, request shape, cost, logs.

**Pick this if** you're prototyping, learning, doing content, or want to swap providers freely.

---

## Quick start

> **Want to flash and try?** Download the latest `.bin` from [Releases](https://github.com/Techposts/tuya-t5-blind-navigator/releases) and follow the [Flash a Prebuilt Release](https://github.com/Techposts/tuya-t5-blind-navigator/wiki/Flash-Prebuilt) wiki guide — no toolchain, ~10 minutes. **Note**: prebuilt binary expects you to run a local proxy on your LAN; see step 4.
>
> **Want to build from source?**

1. **Clone TuyaOpen** SDK and check out the version this app was developed against:
   ```bash
   git clone https://github.com/tuya/TuyaOpen.git ~/TuyaOpen
   cd ~/TuyaOpen
   git checkout 2240c01e   # or newer; verify with HARDWARE.md
   ```

2. **Drop the app** into the TuyaOpen tree:
   ```bash
   cp -r firmware/blind_navigator ~/TuyaOpen/apps/tuya.ai/
   ```

3. **Configure secrets** locally:
   ```bash
   cp ~/TuyaOpen/apps/tuya.ai/blind_navigator/include/nav_config.h.example \
      ~/TuyaOpen/apps/tuya.ai/blind_navigator/include/nav_config.h
   # Edit nav_config.h:
   #   - Replace REPLACE_ME_WIFI_SSID + REPLACE_ME_WIFI_PASSWORD with your Wi-Fi
   #   - Replace REPLACE_ME_OPENAI_API_KEY with your OpenAI key
   #   - Replace REPLACE_ME_PROXY_HOST with your proxy machine's IP
   #   - Add `#define IRIS_CONFIG_FILLED_IN` near the top to silence the build warning
   ```

   ℹ️ **Easier path in v0.3.0+:** flash the prebuilt binary, boot the device, connect your phone to the `IRIS-XXXX` open Wi-Fi network the device exposes, then visit `http://192.168.4.1` to set your real Wi-Fi via the web UI. You only need the steps above if you want to bake credentials at compile time.

4. **Run the proxy** on a machine on the same Wi-Fi as the board:
   ```bash
   cd proxy
   pip install -r requirements.txt
   export OPENAI_API_KEY=sk-proj-...
   python3 openai_proxy.py
   ```
   For a permanent install see [`docs/PROXY_SETUP.md`](docs/PROXY_SETUP.md).

5. **Build & flash** the firmware:
   ```bash
   cd ~/TuyaOpen/apps/tuya.ai/blind_navigator
   python3 ~/TuyaOpen/tos.py build
   python3 ~/TuyaOpen/tos.py flash -p /dev/ttyUSB0 -b 460800
   ```
   **Always run `tos.py` from the project directory**, not TuyaOpen root. Press the RST button when "Waiting Reset" appears. Use **460800 baud** — 921600 corrupts silently on this CH342F + T5 combo. Full instructions in [`docs/BUILD_AND_FLASH.md`](docs/BUILD_AND_FLASH.md).

6. **Power up the board.** Boot splash for 2s with eye revealed underneath, then idle screen with `READY · LISTENING` and `Tap to navigate`. Tap, swipe up, swipe down to use.

---

## Documentation

- **[design/CHANGES.md](design/CHANGES.md)** — full v0.1.0 → v0.2.0 changelog (11 checkpoints documented)
- **[design/iris-firmware-ui/](design/iris-firmware-ui/)** — locked design source (HTML mockup, design tokens, screens.jsx)
- **[docs/PLANNED.md](docs/PLANNED.md)** — what's done, what's next, deferred items
- **[Flash a Prebuilt Release](https://github.com/Techposts/tuya-t5-blind-navigator/wiki/Flash-Prebuilt)** *(wiki)* — end-user binary flashing
- **[ARCHITECTURE](docs/ARCHITECTURE.md)** — data flow, components, threading
- **[HARDWARE](docs/HARDWARE.md)** — board pinout, camera, display specs
- **[BUILD_AND_FLASH](docs/BUILD_AND_FLASH.md)** — toolchain, common gotchas
- **[CLOUD_OPTIONS](docs/CLOUD_OPTIONS.md)** — Tuya Cloud vs. local proxy vs. direct
- **[PROXY_SETUP](docs/PROXY_SETUP.md)** — Flask proxy install + systemd unit
- **[USE_CASES](docs/USE_CASES.md)** — gesture map (currency, MRP, medication)
- **[SENSORS_ROADMAP](docs/SENSORS_ROADMAP.md)** — mmWave / ToF / haptic plan
- **[LESSONS_LEARNED](docs/LESSONS_LEARNED.md)** — what failed, how it was diagnosed

Same content also browsable as a [GitHub Wiki](https://github.com/Techposts/tuya-t5-blind-navigator/wiki).

---

## TuyaOpen resources (official)

| What | URL |
|---|---|
| **TuyaOpen home** | <https://tuyaopen.ai/> |
| **Source code (GitHub)** | <https://github.com/tuya/TuyaOpen> |
| **Documentation index** | <https://tuyaopen.ai/docs/about-tuyaopen> |
| **AI Agent Platform** | <https://tuyaopen.ai/docs/cloud/tuya-cloud/ai-agent/ai-agent-dev-platform> |
| **T5AI Board overview** | <https://tuyaopen.ai/docs/hardware-specific/tuya-t5/t5-ai-board/overview-t5-ai-board> |
| **Featured projects** | <https://tuyaopen.ai/projects> |
| **Where to buy the board** | <https://tuyaopen.ai/get-hardware> |
| Discord (community) | <https://discord.com/invite/yPPShSTttG> |

---

## Status

**v0.2.0 is prototype-grade firmware** ready for hardware demo and partnership recording. It is not production firmware. Known limitations heading into v0.3.0:

- Wi-Fi credentials baked at build time (captive-portal setup is a v0.3.0 priority)
- No web UI yet (planned for v0.3.0 alongside the AP-mode flow)
- Voice wake word ("Hi Tuya") not wired (planned for v0.3.0)
- LD2450 mmWave sensor not yet integrated — DEPTH/HAZARDS bars are decorative randomized values for now
- No persistent KV usage beyond LittleFS init
- Cloud query latency 3-8 s; not suitable for real-time obstacle avoidance ([SENSORS_ROADMAP](docs/SENSORS_ROADMAP.md) covers haptic+mmWave for that)

---

## Partnership disclosure

This project was built as part of a paid Tuya partnership for [@ravis1ngh on YouTube](https://youtube.com/@ravis1ngh). Tuya provided the T5AI dev board and 30-day right of use of the resulting video. The accessibility framing, software architecture, design, and integration choices are the author's own. Tuya did not direct any specific outcome — they asked for a creative project showcasing TuyaOpen, not a chatbot.

---

## License

MIT — see [LICENSE](LICENSE). Source TTF fonts in `firmware/blind_navigator/fonts/src/` are SIL OFL — see `firmware/blind_navigator/fonts/README.md`.

---

## Acknowledgments

- **[Tuya TuyaOpen](https://tuyaopen.ai/)** — the open-source SDK that makes this device possible. Special credit to `examples/graphics/lvgl_camera` (camera+display init reference) and `apps/tuya.ai/your_chat_bot` (KWS+TTS pattern).
- **[OpenAI](https://platform.openai.com)** — `gpt-4o-mini` for vision and `gpt-4o-mini-tts` for speech
- **[LVGL](https://lvgl.io)** — UI rendering with hardware-accelerated DMA2D
- **[Space Grotesk](https://github.com/floriankarsten/space-grotesk)** by Florian Karsten — display sans typeface (SIL OFL)
- **[JetBrains Mono](https://github.com/JetBrains/JetBrainsMono)** — programmer monospace typeface (SIL OFL)
- **Claude Design** for the IRIS visual identity — bionic-eye motif, design tokens, animation timings (in `design/iris-firmware-ui/`)
