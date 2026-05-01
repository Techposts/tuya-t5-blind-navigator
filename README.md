# Tuya T5 AI Blind Navigator

An AI-powered visual navigation aid for blind and low-vision users, built on the
[Tuya T5AI Dev Board](https://developer.tuya.com/en/docs/iot-device-dev/T5-E1-IPEX-development-board?id=Ke9xehig1cabj).

Press the button (or tap the touchscreen) → camera captures the scene → an
OpenAI vision model describes what's ahead → the description is spoken aloud
through the on-board speaker AND streamed on the 3.5" LCD as readable text.

The device functions as a **navigation co-pilot**, not a tour guide — responses
are imperative and action-oriented:

> *"Clear path ahead. Door 2 meters at your 1 o'clock."*
> *"Person walking toward you 3 steps ahead. Step right to pass."*
> *"Wall at arm's length directly ahead. Turn left or right to continue."*

## Features

- **One-tap scene navigation** — single click describes obstacles, doors, stairs, people
- **Read text mode** — double click reads any visible signs, labels, printed text aloud
- **Identify object mode** — long press identifies an item held close to the camera
- **Touchscreen tap** — eyes-free fallback that fires the same scene query
- **Bionic-eye UI** — state-driven color (cyan idle / yellow capturing / magenta thinking / green speaking / red error)
- **Persistent text response panel** so a sighted helper can read what the device said

## Hardware

- **Tuya T5-E1-IPEX module** (ARM Cortex-M33F @ 480 MHz, 16 MB PSRAM, Wi-Fi 6, BLE 5.4)
- **3.5" TFT LCD** 320×480 with capacitive touch (ILI9488 + GT1151QM)
- **GC2145 camera** (DVP, 1600×1200 array, used at 480×480 @ 15 fps)
- **Onboard speaker + dual MEMS mics**
- **CH342F USB-to-UART** for flashing/debug

See [docs/HARDWARE.md](docs/HARDWARE.md) for full pinout and component reference.

## Architecture

```
[Button/Touch trigger]
        │
        ▼
[Camera capture: 480×480 JPEG]
        │
        ▼
[HTTP POST → local Flask proxy on home network]
        │
        ▼
[Proxy → HTTPS → api.openai.com (gpt-4o-mini vision)]
        │
        ▼
[Response → Bionic Eye UI text panel + gpt-4o-mini-tts audio → speaker]
```

The proxy is required because the T5's mbedTLS implementation can't complete
the TLS handshake with OpenAI directly (cipher-suite mismatch — see
[docs/LESSONS_LEARNED.md](docs/LESSONS_LEARNED.md)). The proxy sidesteps this
by terminating HTTPS server-side; the board only ever speaks plain HTTP to the
proxy on your local network.

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the full data flow.

## Quick start

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

3. **Configure secrets** locally (never commit):
   ```bash
   cp ~/TuyaOpen/apps/tuya.ai/blind_navigator/include/nav_config.h.example \
      ~/TuyaOpen/apps/tuya.ai/blind_navigator/include/nav_config.h
   # Edit nav_config.h: set OPENAI_API_KEY, NAV_WIFI_PASS, NAV_SSID_LIST,
   # and NAV_PROXY_HOST (the IP of the machine running openai_proxy.py).
   ```

4. **Run the proxy** on a machine on the same Wi-Fi as the board:
   ```bash
   cd proxy
   pip install -r requirements.txt
   export OPENAI_API_KEY=sk-proj-...
   python3 openai_proxy.py
   ```
   For a permanent install see [docs/PROXY_SETUP.md](docs/PROXY_SETUP.md).

5. **Build & flash** the firmware:
   ```bash
   cd ~/TuyaOpen/apps/tuya.ai/blind_navigator
   python3 ~/TuyaOpen/tos.py build
   python3 ~/TuyaOpen/tos.py flash -p /dev/ttyUSB0 -b 460800
   ```
   Full instructions in [docs/BUILD_AND_FLASH.md](docs/BUILD_AND_FLASH.md).

6. **Power up the board.** It should connect to your Wi-Fi (top-left status bar
   turns green and shows "online"), display the bionic eye in cyan ("READY"),
   and respond to button presses or screen taps.

## Documentation

- **[ARCHITECTURE](docs/ARCHITECTURE.md)** — data flow, components, threading
- **[HARDWARE](docs/HARDWARE.md)** — board pinout, camera, display specs
- **[BUILD_AND_FLASH](docs/BUILD_AND_FLASH.md)** — toolchain, common gotchas
- **[PROXY_SETUP](docs/PROXY_SETUP.md)** — Flask proxy install + systemd unit
- **[SENSORS_ROADMAP](docs/SENSORS_ROADMAP.md)** — planned mmWave / ToF sensors
- **[LESSONS_LEARNED](docs/LESSONS_LEARNED.md)** — what failed, how it was diagnosed

## Status

This is **prototype-grade** code from a content-creator dev session. It is not
production firmware. Known limitations:

- TLS proxy required for OpenAI access (T5 mbedTLS cipher mismatch)
- Voice wake word ("Hey Tuya") not yet wired — button + touch + KWS-stub only
- No persistent state / KV usage beyond Wi-Fi
- No on-device fallback if Wi-Fi or proxy is down
- Cloud query latency 3–8 s; not suitable for real-time obstacle avoidance
  (see [SENSORS_ROADMAP](docs/SENSORS_ROADMAP.md) for the haptic-mmWave plan
  to address this)

## License

MIT — see [LICENSE](LICENSE).

## Acknowledgments

- [Tuya TuyaOpen](https://github.com/tuya/TuyaOpen) — SDK + examples (especially
  `examples/graphics/lvgl_camera`, which was instrumental as the working
  reference for camera + display init)
- [OpenAI](https://platform.openai.com) — `gpt-4o-mini` for vision and `gpt-4o-mini-tts`
  for speech
- [LVGL](https://lvgl.io) — UI rendering with hardware-accelerated DMA2D
