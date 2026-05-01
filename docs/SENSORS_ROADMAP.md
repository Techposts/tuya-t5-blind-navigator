# Sensors Roadmap

The current firmware is **trigger-based only** (button / touch / voice). For
a real-world blind-navigator device, on-device sensors are needed to provide
the **always-on, sub-second collision warnings** that cloud queries (3–8 s
latency) can never deliver.

## Three-tier sensor architecture

| Tier | Latency | Sensor | What it does |
|---|---|---|---|
| **1. Instant** | < 10 ms | **mmWave + ToF + haptic motor** | Continuous obstacle warning (vibration intensity scales with proximity) |
| **2. Fast** | 200–500 ms | On-device camera classifier (future) | Door / stair / person *shape* recognition without cloud round-trip |
| **3. Smart** | 3–8 s | Cloud LLM (today) | Full scene description, read text, identify objects, novel queries |

Each tier compensates for the others: T1 keeps you from walking into a wall,
T2 tells you "stairs ahead" within half a second, T3 provides nuanced scene
understanding when you ask for it.

## Tier 1 — Recommended sensor set

### Primary: LD2410C (24 GHz mmWave radar) — ~$10

| Spec | Value |
|---|---|
| Range | 0.75 – 6 m |
| Detects | Static AND moving humans (sees breathing micro-motion) |
| Coverage | ~60° cone |
| Interface | UART, 256000 baud, 23-byte frames |
| Through-clothing | Yes (radar penetrates fabric) |
| Power | 5 V, ~80 mA peak |

**Why over HC-SR04 ultrasonic:**
- Detects **stationary** people standing in your path (HC-SR04 echoes off
  fabric weakly and gets unreliable returns).
- Through-fabric — won't miss a person in a winter coat.
- Stable distance — no temperature compensation needed.

**T5 wiring:** any spare UART (T5 has 3). Common choice: TX/RX on free GPIOs
not used by the LCD/camera/SD.

### Secondary: VL53L1X (Time-of-Flight laser) — ~$7

| Spec | Value |
|---|---|
| Range | 0.04 – 4 m |
| FOV | 25° narrow cone |
| Accuracy | mm-level at < 1 m |
| Interface | I²C (could share P13/P15 bus with camera/touch — different address) |
| Power | 2.6 – 3.5 V, ~20 mA |

**Why pair with mmWave:** ToF gives **precise, narrow-cone** distance to
walls, curbs, step edges, stair-tops. mmWave gives **wide-cone person
detection**. Combined, you cover the full obstacle space.

### Output: LRA vibration motor — ~$3

| Spec | Value |
|---|---|
| Type | Linear Resonant Actuator (better feel than ERM) |
| Drive | PWM via tiny MOSFET or DRV2605L driver IC |
| Pin (current) | P12 — but P12 is the user button. Move motor to spare GPIO when both are wired. |

**Vibration pattern grammar:**

| Pattern | Meaning |
|---|---|
| Single short pulse (50 ms) | Trigger acknowledged |
| Double pulse | Object 1.5 – 3 m ahead (caution) |
| Continuous pulse, intensity ∝ inverse-distance | Object < 1.5 m (warn / stop) |
| Long buzz (400 ms) | Path clear, ready |
| 5 rapid pulses | Error / system fault |

## Tier 2 — On-device classification (future)

The T5 has no NPU, so we can't run YOLO or similar real-time detectors. But
DSP-style image processing is feasible:

- **Edge detection** (Sobel) on downsampled YUV422 — detects strong vertical
  lines (walls / door frames / poles) at ~10 fps.
- **Step-edge detection** — high contrast between top/bottom thirds of the
  frame is often a stair-edge or curb.
- **Lightweight TFLite micro models** (~50 KB) — could classify "door /
  not-door", "person / not-person" with reasonable accuracy at 2–3 fps.

This tier is **future work**; current firmware doesn't include any.

## Tier 3 — Already implemented

See [USE_CASES.md](USE_CASES.md). Single-click / double-click / long-press
each trigger different cloud prompts.

## Other useful sensors

| Sensor | Use case | Priority |
|---|---|---|
| **9-axis IMU (BMI270 / MPU9250)** | Fall detection, step counter, compass for direction | High |
| **GPS (NEO-M9N)** | Outdoor turn-by-turn navigation: "Walk 50 m north on Maple St" | Medium |
| **Bone-conduction speaker** | Replaces speaker — sound through skull, leaves ears free for ambient cues (critical for blind users — they navigate by sound) | High UX win |
| **APDS-9960** | Gesture sensor — alternative trigger if button is awkward | Low |
| **BME280** | Outdoor/indoor detection, weather context | Low |
| **PIR sensor** | Cheap human-motion fallback if LD2410C is unavailable | Low |

## Power budget

For an always-on wearable form factor (where this could go in v3):

| Component | Avg current | Always on? |
|---|---|---|
| T5-E1-IPEX (Wi-Fi connected idle) | ~80 mA | yes |
| T5-E1-IPEX (Wi-Fi active during query) | ~250 mA peak | bursts |
| GC2145 (idle) | ~10 mA | when previewing |
| LD2410C | ~80 mA | yes |
| VL53L1X | ~20 mA | yes |
| LRA motor | ~80 mA | only on alert |
| 3.5" LCD backlight | ~40 mA | yes (could be off-by-default) |
| Total avg | ~250 mA | – |

A 2000 mAh LiPo gives ~8 hours active use, ~24 hours idle. With LCD off
during navigation, can stretch to 12+ active hours.

## Implementation notes for v2.5

When adding mmWave + ToF + haptic to the existing firmware:

1. **Move the user button** off P12 if you want the haptic motor there
   (or pick a spare GPIO for the motor — cleanest).
2. **Spawn an "alerts" thread** at higher priority than `proc_th`, with
   small (4 KB) stack — runs continuously, polls sensors, drives motor.
   No cloud calls from this thread.
3. **Don't speak distance numbers** through the speaker by default — the
   motor's intensity gradient communicates distance more efficiently and
   doesn't drown out ambient sound.
4. **Reserve voice/screen output for queried information** — the user
   asked for it, so they want it.
