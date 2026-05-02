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

## What v0.2.0 ships

| Feature | Status |
|---|---|
| Bionic-eye **retina-scanner UI** with 24 tick marks + breathing white pupil + 3 rotating arcs | ✅ |
| **Boot splash** with eye visible underneath, IRIS title + version, hairline progress | ✅ |
| **Per-state screens** — Idle / Capturing / Thinking / Speaking / Error / Settings (8 total) | ✅ |
| **Custom typography** — Space Grotesk + JetBrains Mono baked LVGL bitmap fonts (4bpp antialiased) | ✅ |
| **Structured response display** — 4 fields with big-green ACTION line, dynamic vertical layout | ✅ |
| **3 vision modes** — NAVIGATE (tap), READ (swipe down), IDENTIFY (swipe up) | ✅ |
| **Double-tap to repeat** — replays last response from cache (no LLM call) | ✅ |
| **Touch gestures** — manual press/release detection (LVGL's gesture system unreliable on GT1151QM) | ✅ |
| **Mode banner** — brief acknowledgement on every input | ✅ |
| **Wi-Fi auto-connect** — hardcoded SSID list, baked at build time | ⚠️ Pending captive-portal AP-mode setup in v0.3.0 |
| **Captive portal AP-mode** — connect to "IRIS Setup" Wi-Fi, configure via web UI | 🔜 v0.3.0 |
| **Web UI for settings** + mDNS hostname (`iris.local`) | 🔜 v0.3.0 |
| **Hi Tuya wake word** + follow-up listening window | 🔜 v0.3.0 |
| **LD2450 mmWave sensor** for real DEPTH/HAZARDS data | 🔜 v0.3.0 |

See [`design/CHANGES.md`](design/CHANGES.md) for the full per-checkpoint changelog from v0.1.0 → v0.2.0.

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

   ⚠️ **Wi-Fi is hardcoded at build time in v0.2.0.** Captive-portal AP-mode setup (no rebuild required) is on the v0.3.0 roadmap. For v0.2.0, every Wi-Fi network change = rebuild + reflash. We chose to ship anyway because the partnership recording is fixed at this firmware revision.

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
