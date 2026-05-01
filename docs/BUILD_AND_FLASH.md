# Build & Flash

## Prerequisites

You need a Linux build host (Ubuntu 22.04 was used during development; a VM
or WSL2 works fine). macOS works for the proxy, but the firmware build itself
expects the Tuya toolchain shipped under `platform/tools/gcc-arm-none-eabi-...`
inside TuyaOpen, which assumes Linux.

The board flashes over USB to the build host. If your build host is a VM,
make sure USB passthrough is configured for `1a86:55d2` (the CH342F).

## One-time setup on a fresh Linux host

```bash
# Toolchain dependencies
sudo apt update
sudo apt install -y python3 python3-pip git cmake ninja-build

# USB-serial drivers (Ubuntu cloud images strip them by default)
sudo apt install -y linux-modules-extra-$(uname -r)
echo ch341  | sudo tee /etc/modules-load.d/ch341.conf
echo cdc_acm | sudo tee /etc/modules-load.d/cdc_acm.conf
sudo modprobe ch341 cdc_acm

# Add user to dialout for /dev/ttyUSB* access
sudo usermod -aG dialout $USER  # log out and back in

# TuyaOpen SDK
git clone https://github.com/tuya/TuyaOpen.git ~/TuyaOpen
cd ~/TuyaOpen
git checkout 2240c01e   # tested version (or pick newer at your own risk)
```

## Drop the app into the SDK tree

```bash
# From the root of this repo:
cp -r firmware/blind_navigator ~/TuyaOpen/apps/tuya.ai/
```

## Configure secrets locally (gitignored)

```bash
cd ~/TuyaOpen/apps/tuya.ai/blind_navigator
cp include/nav_config.h.example include/nav_config.h
$EDITOR include/nav_config.h
```

Set:
- `OPENAI_API_KEY` — your OpenAI project key (`sk-proj-...`)
- `NAV_WIFI_PASS` — Wi-Fi password
- `NAV_SSID_LIST` — array of SSIDs to try (in priority order)
- `NAV_PROXY_HOST` — IP of the machine running `openai_proxy.py`
- `NAV_PROXY_PORT` — port (default 8888)

## Build

```bash
cd ~/TuyaOpen/apps/tuya.ai/blind_navigator
python3 ~/TuyaOpen/tos.py build
```

A successful build produces:
```
.build/bin/blind_navigator_QIO_1.0.X.bin   # ~2.6 MB, what gets flashed
.build/bin/blind_navigator_UA_1.0.X.bin    # OTA-upgrade variant (not used here)
```

You may see a warning about `diff2ya` failing with a GLIBC mismatch — that
only affects the OTA-upgrade binary and is benign for direct UART flash.

### Common build failures

- **`ai_audio_player.h: No such file or directory`** — the
  `app_default.config` is missing `CONFIG_ENABLE_AI_UI_TEXT_STREAMING=y`
  and/or `CONFIG_ENABLE_COMP_AI_VIDEO=y`. Without these, `ai_components/`
  isn't pulled into the include path.
- **`'lvgl.h': No such file`** — same root cause, plus the LVGL flag chain.
  The board-specific config file in `config/TUYA_T5AI_BOARD_LCD_3.5_CAMERA.config`
  must exist and match the `app_default.config`.
- **`'unused function ...'`** with `-Werror` — TuyaOpen builds with
  `-Werror=unused-function`, so don't leave dead code.

## Flash

The Tuya `tos.py flash` wrapper does NOT support DTR-based auto-reset on this
hardware — when prompted, **press the RST button on the board** to enter
the bootloader.

```bash
python3 ~/TuyaOpen/tos.py flash -p /dev/ttyUSB0 -b 460800
# When you see "[INFO]: Waiting Reset ..." press RST on the board.
```

⚠ **Use 460800 baud, not 921600.** Higher baud rates cause corrupt writes
on the CH342F + T5 combo.

⚠ **Do not pipe the flash output through `head` / `tail` / `grep`.** The
flasher writes a tqdm progress bar to stdout; if the consumer closes the
pipe early, SIGPIPE kills the flasher mid-write and you get a partially
flashed (corrupted) firmware. Always redirect to a file:

```bash
python3 ~/TuyaOpen/tos.py flash -p /dev/ttyUSB0 -b 460800 > flash.log 2>&1
```

A successful flash ends with:
```
[INFO]: Erase flash success
[INFO]: Write flash success
[INFO]: CRC check success
[INFO]: Reboot done
[INFO]: Flash write success.
```

## Monitor (debug serial)

The Tuya `tyutool monitor` is unreliable on this board — it doesn't pick up
output. Use raw pyserial instead:

```python
import serial, time, re
s = serial.Serial("/dev/ttyUSB1", 460800, timeout=0.3)
t = time.time()
while time.time() - t < 30:
    d = s.read(8192)
    if d:
        # strip ANSI color codes for readability
        print(re.sub(r"\x1b\[[0-9;]*m", "", d.decode("utf-8", errors="replace")), end="")
s.close()
```

The board outputs ANSI-colored log lines like:
```
[01-01 00:00:15 ty I][tal_thread.c:226] thread_create name:proc,...
[01-01 00:00:33 ty D][ai_video_input.c:100] JPEG frame captured, len: 56787
```

Memory dumps after a crash produce many `addr: ... data: ...` lines —
filter those out when grepping.

## Decoding crash addresses

If the firmware crashes with a `MemFault`, use `addr2line`:

```bash
ELF=~/TuyaOpen/platform/T5AI/t5_os/build/bk7258/tuya_app/bk7258_ap/app.elf
A2L=~/TuyaOpen/platform/tools/gcc-arm-none-eabi-10.3-2021.10/bin/arm-none-eabi-addr2line
$A2L -e $ELF -a -f -p 0x021c08ca   # PC at the crash
```

This is what told us the original boot crash was in
`tal_kv/littlefs/lfs.c:49` (NULL `lfs_config` deref) — see
[LESSONS_LEARNED.md](LESSONS_LEARNED.md).
