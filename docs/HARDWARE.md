# Hardware Reference

Specs and pinout for the **Tuya T5AI Dev Board** as used by this project.
Source of truth: [official Tuya doc](https://developer.tuya.com/en/docs/iot-device-dev/T5-E1-IPEX-development-board?id=Ke9xehig1cabj),
plus the device's actual silkscreen.

## Module / SoC

| Spec | Value |
|---|---|
| Module | T5-E1-IPEX |
| SoC | ARMv8-M Star (Cortex-M33F derivative) @ 480 MHz |
| Internal RAM | 16 KB ITCM + 16 KB DTCM + 640 KB shared SRAM |
| External RAM | 16 MB SiP PSRAM |
| Flash | 8 MB SiP |
| Wi-Fi | 802.11 b/g/n/ax (Wi-Fi 6) |
| Bluetooth | LE 5.4, +6 dBm TX, −97 dBm RX |
| Hardware acceleration | DMA2D, JPEG codec, H.264 encoder |
| NPU | none |

## Display sub-board

| Spec | Value |
|---|---|
| Panel | 3.5" TFT, 320 × 480 |
| Driver IC | ILI9488 |
| Touch IC | GT1151QM (capacitive) |
| Color depth | 16.7 M (RGB565 in software) |

## Camera (on the LCD display sub-board)

| Spec | Value |
|---|---|
| Module label | OVX4388 |
| Sensor IC | **GC2145** |
| Array | 1600 × 1200 |
| Used resolution | 480 × 480 @ 15 fps (matches the working `lvgl_camera` demo) |
| I²C address | 0x78 / 0x79 |
| Interface | DVP 8-bit |
| Driver | `src/peripherals/camera/tdd_camera/src/dvp/tdd_camera_dvp_gc2145.c` |
| Supported PPI | 240×240, 480×480, 640×480, 800×480, 864×480, 1280×720 |

## DVP / LCD / TF / Touch pinout

From official PDF §6:

| Function | Pin | Function | Pin | Function | Pin |
|---|---|---|---|---|---|
| **DVP_MCLK** | P27 | LCD R3 | P23 | TF SDIO_D3 | TXD/P11 |
| **DVP_PCLK** | P29 | LCD R4 | P22 | TF SDIO_D2 | RXD/P10 |
| **DVP_VSYNC** | P31 | LCD R5 | P21 | TF SDIO_D1 | P5 |
| **DVP_HSYNC** | P33 | LCD R6 | P20 | TF SDIO_D0 | P4 |
| **DVP_D0** | P30 | LCD R7 | P19 | TF SDIO_CMD | P3 |
| **DVP_D1** | P32 | LCD G2 | P42 | TF SDIO_CLK | P2 |
| **DVP_D2** | P34 | LCD G3 | P41 | TF SDIO_CD | P8 |
| **DVP_D3** | P35 | LCD G4 | P40 | LCDC_PCLK | P14 |
| **DVP_D4** | P36 | LCD G5 | P26 | LCDC_DE | P16 |
| **DVP_D5** | P37 | LCD G6 | P25 | LCD_HSYNC | P17 |
| **DVP_D6** | P38 | LCD G7 | P24 | LCD_VSYNC | P18 |
| **DVP_D7** | P39 | LCD B3 | P47 | LCD_SDI (IO) | P50 |
| **DVP_RST** | P51 | LCD B4 | P46 | LCD_CLK (IO) | P49 |
| **I²C SCL** | **P13** | LCD B5 | P45 | LCD_CS | P48 |
| **I²C SDA** | **P15** | LCD B6 | P44 | LCD_RST | P53 |
| **TP_RSTn** | P54 | LCD B7 | P43 | LCDC_TE | P52 |
| **TP_INTn** | P55 |  |  | PWM_BL | P9 |

⚠ **Camera and touchpad share the same I²C bus (P13/P15).** Both peripherals
are driven by bus-aware drivers (GC2145 at 0x78/0x79, GT1151QM at a different
address).

## Onboard buttons & I/O

| Element | Pin | Notes |
|---|---|---|
| RST button | EN of T5-E1-IPEX | Hardware module reset |
| User button (KEY) | **P12** | Active-low, primary trigger |
| User LED | P01 | Status indicator |
| Speaker | 1.25 mm pitch | Up to 3 W into 4 Ω (CS8302M PA) |
| MIC1, MIC2 | onboard | Two-channel 16-bit ADC, beamforming-capable |

## USB / Serial

| Element | Spec |
|---|---|
| USB-to-UART chip | **CH342F** (QinHeng), `1a86:55d2` |
| Type-C port | Power + flash/debug (provides two virtual serial ports) |
| Type-A port | USB 2.0 host (e.g. for USB cameras) |
| Flash UART | `/dev/ttyUSB0` (→ `ttyACM0` via udev) — **460800 baud** |
| Debug UART | `/dev/ttyUSB1` (→ `ttyACM1`) — **460800 baud** |
| 4-bit DIP | Connects/disconnects two serial ports on the CH342F |

The CH342F enumerates as a CDC-ACM device — Linux requires the `cdc_acm`
and `ch341` modules from `linux-modules-extra-$(uname -r)`. On Ubuntu cloud
images these are not installed by default. See
[BUILD_AND_FLASH.md](BUILD_AND_FLASH.md).

## Tested SDK version

This codebase was developed against **TuyaOpen commit `2240c01e`** (master,
March 2026). The build glue (CMakeLists / Kconfig fragments) is sensitive to
upstream changes; if a newer TuyaOpen breaks compilation, try checking out
`2240c01e` first.
