# Lessons Learned

A real, unedited list of what failed during development and how each was
diagnosed. Written so the next person doing this avoids the same rabbit
holes.

## 1. The TLS handshake nightmare (longest dive)

**Symptom:** Direct HTTPS calls from the T5 to `api.openai.com` failed with:
```
[tuya_tls.c:682] mbedtls_ssl_handshake returned 0x3a00
[tls_transporter.c:97] tls transporter connect err:-14848
```

`0x3a00` = `MBEDTLS_ERR_SSL_HANDSHAKE_FAILURE`. The board never even gets to
the HTTP layer — TLS itself can't agree on a cipher.

**What we tried:**

1. Set `tls_no_verify = true` in `http_client_request_t` — no effect (this
   only disables cert chain validation, not cipher selection).
2. Verified the API key works (curl from VM returned valid OpenAI responses).
3. Inspected the cipher suites OpenAI offers in TLS 1.2 fallback vs what
   TuyaOpen's mbedtls compiles in. OpenAI uses
   `TLSv1.3, TLS_AES_256_GCM_SHA384` natively, falls back to TLS 1.2 with
   modern ECDHE-ECDSA-AES-GCM ciphers.
4. **Enabled TLS 1.3** by uncommenting `MBEDTLS_SSL_PROTO_TLS1_3` and
   `MBEDTLS_SSL_TLS1_3_COMPATIBILITY_MODE` in
   `src/libtls/mbedtls-3.1.0/include/mbedtls/mbedtls_config.h`. **No
   effect** — the actual mbedtls being linked is the platform-bundled
   PSA mbedtls under `platform/T5AI/t5_os/{ap,cp}/components/psa_mbedtls/`.
5. Patched the platform PSA mbedtls configs and forced a full clean
   rebuild including platform tree. Build succeeded but **the handshake
   still failed** — possibly because `tuya_tls.c` hardcodes parts of the
   cipher selection, possibly because the PSA mbedtls is shipped as a
   pre-compiled `.a` and our config patch never reached the compiled
   library.

**Conclusion:** Fighting mbedtls was a multi-hour rabbit hole that
ultimately did not work. The proxy approach was always going to be the
correct solution.

**Final fix:** Bypass the issue entirely. Run a Flask proxy on a machine
with a normal modern TLS stack (any Linux box / Mac / Pi). The board
talks plain HTTP to the proxy on the LAN. The proxy talks proper HTTPS
to OpenAI. This is what `proxy/openai_proxy.py` does.

**Takeaway:** When you're 30+ minutes deep into a low-level fix that
keeps revealing new layers of complexity, **stop and revisit the
architecture** instead of digging deeper. The proxy is 30 lines of
Python and it just works.

## 2. The proxy unreachable from the wrong subnet

**Symptom:** Even after the proxy was deployed, board calls returned
`st=2 http=0`. Asked the user to test the proxy from their phone — they
saw the proxy's "Not Found" 404 page, suggesting it was reachable.

**Diagnosis:** Looked at the proxy's request log. The "Not Found" came from
`192.168.0.219` — the user's *Mac*, not their phone. No request from any
`192.168.1.X` device had ever reached the proxy. The board was on `.1.x`
(<YOUR_WIFI_SSID> Wi-Fi); the proxy/VM was on `.0.x`. Different subnets,
isolated for TCP at the router level despite ICMP working asymmetrically.

**Final fix:** Reduced the firmware's SSID list to just the Wi-Fi that
gives an IP on the same subnet as the proxy, so the board lands on `.0.x`
and the proxy is reachable.

**Takeaway:** When an external test "succeeds", confirm *which device*
made the test request. Browsers auto-switch networks; phones drift to
strongest Wi-Fi; what looked like proof of routing wasn't.

## 3. Hidden hardcoded SSID array in main.c

**Symptom:** Updating `NAV_SSID_LIST` in `nav_config.h` had no effect.
Board kept connecting to whatever was in some other place.

**Diagnosis:** `grep -rE "Techposts|TopFloor"` revealed `main.c` had its own
inline `const char* ss[] = {"<OTHER_SSID_A>", "<OTHER_SSID_B>", "<YOUR_WIFI_SSID>"}`
in `nav_app_main`. The header-defined `NAV_SSID_LIST` was dead code.

**Final fix:** Replaced the inline array with `const char* const* ss = NAV_SSID_LIST;`
so the header is now authoritative.

**Takeaway:** When a "configurable" value seems to be ignored, `grep` for
the literal string. Hardcodes hide everywhere.

## 4. The MemFault-on-boot crash

**Symptom:** Board boots, prints `Thread:sys_timer Exec Start`, then
`AP crash happend, rr: 0x12` followed by a giant register/memory dump.
PC at `0x021c08ca`, MMFAR `0x1c` (NULL pointer + small offset = NULL
struct deref).

**Diagnosis:** Used `arm-none-eabi-addr2line -e app.elf 0x021c08ca`:
```
0x021c08ca: lfs_bd_read at /home/tuya/TuyaOpen/src/tal_kv/littlefs/lfs.c:49
0x021c0fef: lfs_dir_fetchmatch at .../lfs.c:1095
```

LittleFS was being read before `tal_kv_init()` had created its `lfs_config`.
The bootloader log showed `mag->magic=0xFFFFFFFF` — the KV partition was
freshly erased and never re-mounted.

**Final fix:** Added explicit `tal_kv_init()` and `tal_workq_init()` calls
at the top of `nav_app_main`, matching the pattern other working TuyaOpen
apps use:

```c
tal_log_init(...);
tal_kv_init(&(tal_kv_cfg_t){
    .seed = "vmlkasdh93dlvlcy",
    .key  = "dflfuap134ddlduq",
});
tal_sw_timer_init();
tal_workq_init();
tuya_tls_init();
...
```

**Takeaway:** TuyaOpen's `tal_*` init functions don't auto-chain. Apps
must call them explicitly in dependency order. When the README says
"call this init", it really means it.

## 5. The flash-failure cascade caused by `head`

**Symptom:** Three consecutive flashes failed at progressively earlier
points:
- Flash 1: died at 40% writing
- Flash 2: died at sync
- Flash 3: died at 12000-byte erase

**Diagnosis:** I had been wrapping the `tos.py flash` output with
`| head -80 | tail -10` to make it readable. **The `head` consumer was
closing the pipe early, sending SIGPIPE to tyutool**, killing the
flasher partway through. Each "death" left the chip in a
worse-corrupted state, requiring more retries.

**Final fix:** Always redirect the flasher's output to a file
server-side and tail just the end after:

```bash
python3 ~/TuyaOpen/tos.py flash -p /dev/ttyUSB0 -b 460800 > /tmp/flash.log 2>&1
tail -8 /tmp/flash.log
```

**Takeaway:** Long-running embedded tools (flashers, programmers) are
sensitive to SIGPIPE. Never pipe their stdout through `head` / `grep -m`
/ any consumer that can close early.

## 6. Camera demo that "didn't work" — was actually fine

**Symptom:** First boot of the new firmware showed a black screen and
camera capture errors.

**Diagnosis:** Built and flashed the Tuya-provided
`examples/graphics/lvgl_camera` demo as an isolation test. It compiled,
flashed, and ran — the LCD showed "Hello World!", and pressing KEY
toggled to live camera preview at 480×480.

**Conclusion:** GC2145 + DVP + DMA2D pipeline works. The original
"camera broken" symptom was actually downstream of an unrelated NULL
crash. The whole multi-day MCLK-delay theory in old session memory
was a wild goose chase.

**Takeaway:** When debugging a complex board, **always isolate with a
known-good vendor demo first** before touching your own code.

## 7. Build system Kconfig invisible-defaults

**Symptom:** Setting `CONFIG_ENABLE_COMP_AI_AUDIO=y` in `app_default.config`
had no effect. The header `ai_audio_player.h` couldn't be found at compile
time even though Kconfig defined `default y` for the option.

**Diagnosis:** TuyaOpen's CMakeLists for each component gates inclusion
on `if (CONFIG_ENABLE_X STREQUAL "y")`. The component's Kconfig fragment
is only *visible* if the app's CMakeLists does
`add_subdirectory(${APP_PATH}/../ai_components)`. Without that line, the
Kconfig setting is treated as "unknown symbol" and silently dropped.

**Final fix:** Added `add_subdirectory(${APP_PATH}/../ai_components)` to
the app's CMakeLists.txt and copied the matching `config/TUYA_T5AI_BOARD_LCD_3.5_CAMERA.config`
file from the working `blind_navigator` reference.

**Takeaway:** Kconfig "default y" only applies if the symbol is *visible*
to the Kconfig parser. In TuyaOpen, visibility comes via CMakeLists
subdirectories, not just Kconfig sourcing.

## 8. Flask "404 Not Found" was the proxy working correctly

**Symptom:** User opened `http://<PROXY_IP>:8888` in a browser to test
proxy reachability. Saw "404 Not Found".

**Initial reading:** "Proxy must be down."

**Actual situation:** Flask returns 404 for `/` because the proxy only
handles `/v1/*` routes. The 404 *is* the proxy responding correctly.

**Takeaway:** A 404 from a service is success — the service is alive
and answering. Differentiate from "site can't be reached" (network
unreachable) and "connection refused" (service not running).

---

## Things that worked first try (worth noting)

- The `lvgl_camera` example compiled and flashed cleanly — this became
  the template for our display + camera init pattern.
- `addr2line` on the firmware ELF gave precise file:line for crash PCs —
  invaluable.
- The Flask proxy (~30 LoC) was the simplest, most reliable component
  in the whole stack.
- Tuya's `tos.py` build system, once configured correctly, is fast
  (incremental builds < 10 s) and produces correct firmware on every
  call.
- The bionic-eye LVGL UI rendered correctly on first build (LVGL with
  DMA2D handles complex compositions cheaply on the M33).
