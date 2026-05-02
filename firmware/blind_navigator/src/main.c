#include "tuya_cloud_types.h"
#include "tal_api.h"
#include "tal_wifi.h"
#include "tal_thread.h"
#include "tal_sw_timer.h"
#include "tal_kv.h"
#include "tuya_tls.h"
#include "board_com_api.h"
#include "ai_audio_player.h"
#include "ai_video_input.h"
#include "tdl_button_manage.h"
#include "tdd_button_gpio.h"
#include "nav_config.h"
#include "nav_display.h"
#include "nav_webui.h"
#include "openai_backend.h"
#include <string.h>
/* CP12: wake word includes */
#include "tkl_kws.h"
#include "ai_audio_input.h"

#include <stdio.h>

extern void tkl_log_output(const char *format, ...);

static volatile int s_idle = 1;
/* CP12d: forward decl so wake_word_cb can fire NAVIGATE */
static void touch_tap_trigger(void);
void nav_wifi_forget(void);  /* CP21: defined further down; declared here so btn_cb can call it */
static const char *s_active_prompt = NAV_VISION_PROMPT;
/* CP9: last spoken response, kept for double-tap-to-repeat */
static char s_last_response[1024] = "";
static TDL_BUTTON_HANDLE s_btn = NULL;
static THREAD_HANDLE s_th = NULL;
static THREAD_HANDLE s_app_th_hdl = NULL;


static void play_response(const char *text) {
    if (!text) return;
    /* CP9: snapshot last response for double-tap replay. Truncate at buf-1. */
    snprintf(s_last_response, sizeof(s_last_response), "%s", text);
    /* CP9: switch to SPEAKING so the audio-bar waveform + green eye + footer show. */
    nav_display_set_state(DISP_STATE_SPEAKING);
    uint8_t *pcm = NULL; uint32_t len = 0;
    if (openai_tts(text, &pcm, &len) == OPRT_OK && pcm) {
        /* OpenAI returns full WAV (RIFF header + PCM payload). Stream as-is. */
        ai_audio_play_tts_stream(AI_AUDIO_PLAYER_TTS_START, AI_AUDIO_CODEC_WAV, NULL, 0);
        ai_audio_play_tts_stream(AI_AUDIO_PLAYER_TTS_DATA, AI_AUDIO_CODEC_WAV, (char*)pcm, len);
        ai_audio_play_tts_stream(AI_AUDIO_PLAYER_TTS_STOP, AI_AUDIO_CODEC_WAV, NULL, 0);
        tal_psram_free(pcm);
        int timeout = 200; while(ai_audio_player_is_playing() && timeout--) tal_system_sleep(100);
    }
}

/* CP9: replay the last response without re-querying the LLM. Used for
 * double-tap-to-repeat. Idempotent and safe to call when no prior response. */
static void replay_last_response_th(void *arg) {
    (void)arg;
    if (s_last_response[0]) {
        play_response(s_last_response);
    }
    /* CP11: same linger after a replay so the structured fields stay readable */
    tal_system_sleep(4000);
    nav_display_set_state(DISP_STATE_IDLE);
    s_idle = 1;
    tal_thread_delete(s_th); s_th = NULL;
}

static void touch_double_tap_trigger(void) {
    if (!s_idle) return;
    if (s_last_response[0] == 0) return;  /* nothing to repeat */
    s_idle = 0;
    THREAD_CFG_T c = { .stackDepth = 16384, .priority = 3, .thrdname = "rep" };
    tal_thread_create_and_start(&s_th, NULL, NULL, replay_last_response_th, NULL, &c);
}


/* CP10: parse a labeled-line response from the LLM into 4 (label, value)
 * pairs plus an optional SPOKEN: line for natural-prose TTS.
 *
 * Expected format (one field per line, label and value separated by ":"):
 *   PATH STATUS: CLEAR
 *   WHERE: open sidewalk, person at 1 o'clock, 4 steps
 *   ACTION: Go forward
 *   WHY: low pedestrian flow, no curbs ahead
 *   SPOKEN: Path clear. Open sidewalk, person at 1 o'clock, 4 steps. Go forward.
 *
 * Robust to extra whitespace, missing fields, and non-conforming responses
 * (in which case structured fields stay empty and the full text is used for TTS).
 */
typedef struct {
    char labels[4][32];
    char values[4][192];
    char spoken[768];
    int  field_count;
    int  has_spoken;
} parsed_resp_t;

static void trim(char *s) {
    if (!s) return;
    char *p = s; while (*p == ' ' || *p == '\t') p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    int n = strlen(s);
    while (n > 0 && (s[n-1] == ' ' || s[n-1] == '\t' || s[n-1] == '\r' || s[n-1] == '\n')) s[--n] = 0;
}

static void parse_labeled_response(const char *resp, parsed_resp_t *out) {
    memset(out, 0, sizeof(*out));
    if (!resp) return;
    char buf[2048];
    snprintf(buf, sizeof(buf), "%s", resp);
    char *line = strtok(buf, "\n");
    while (line && out->field_count < 4) {
        char *colon = strchr(line, ':');
        if (!colon) { line = strtok(NULL, "\n"); continue; }
        *colon = 0;
        char *lab = line;
        char *val = colon + 1;
        trim(lab); trim(val);
        if (strlen(lab) == 0) { line = strtok(NULL, "\n"); continue; }
        /* Detect SPOKEN line for TTS */
        if (strcasecmp(lab, "SPOKEN") == 0 || strcasecmp(lab, "SPEECH") == 0) {
            snprintf(out->spoken, sizeof(out->spoken), "%s", val);
            out->has_spoken = 1;
        } else {
            snprintf(out->labels[out->field_count], sizeof(out->labels[0]), "%s", lab);
            snprintf(out->values[out->field_count], sizeof(out->values[0]), "%s", val);
            out->field_count++;
        }
        line = strtok(NULL, "\n");
    }
    /* Fallback: if no SPOKEN line, build prose by joining the values */
    if (!out->has_spoken && out->field_count > 0) {
        out->spoken[0] = 0;
        for (int i = 0; i < out->field_count; i++) {
            int n = strlen(out->spoken);
            int rem = (int)sizeof(out->spoken) - n - 1;
            if (rem <= 0) break;
            snprintf(out->spoken + n, rem, "%s%s.", i ? " " : "", out->values[i]);
        }
    }
    /* Last fallback: if response had no labels at all, use raw text for TTS */
    if (!out->has_spoken && out->field_count == 0) {
        snprintf(out->spoken, sizeof(out->spoken), "%s", resp);
    }
}

static void proc_th(void *arg) {
    /* CP9 state flow: LISTENING during camera capture, PROCESSING during
     * HTTP/LLM, SPEAKING during TTS playback (set inside play_response),
     * back to IDLE after. */
    nav_display_set_state(DISP_STATE_LISTENING);  /* CAPTURE screen */
    ai_video_display_start();
    tal_system_sleep(3500);  /* camera autoexpose + warmup */
    uint8_t *jpeg = NULL; uint32_t jlen = 0;
    OPERATE_RET ret = ai_video_get_jpeg_frame(&jpeg, &jlen);
    ai_video_display_stop();

    if (ret == OPRT_OK && jpeg && jlen > 0) {
        /* CP9: switch to THINKING + randomize the data bars so they look alive */
        nav_display_randomize_think_bars();
        nav_display_set_state(DISP_STATE_PROCESSING);

        char *resp = NULL;
        if (openai_ask_image(jpeg, jlen, s_active_prompt, &resp) == OPRT_OK) {
            /* CP10: parse labeled-line response, populate structured display
             * with the 4 (label, value) pairs, then TTS the SPOKEN line. */
            parsed_resp_t pr;
            parse_labeled_response(resp, &pr);
            nav_display_set_speak_response(
                pr.labels[0], pr.values[0],
                pr.labels[1], pr.values[1],
                pr.labels[2], pr.values[2],
                pr.labels[3], pr.values[3]);
            play_response(pr.has_spoken || pr.field_count > 0 ? pr.spoken : resp);
            tal_free(resp);
        } else {
            nav_display_set_state(DISP_STATE_ERROR);
            nav_display_set_speak_response(
                "STATUS", "Cloud error",
                "DETAIL", "Could not reach the AI service",
                "ACTION", "Try again",
                "NOTE", "Check Wi-Fi or proxy");
            play_response("Cloud error. Please try again.");
        }
        ai_video_jpeg_image_free(&jpeg);
    } else {
        nav_display_set_state(DISP_STATE_ERROR);
        nav_display_set_speak_response(
            "STATUS", "No image",
            "DETAIL", "Camera could not capture a frame",
            "ACTION", "Wipe lens and retry",
            "NOTE", "Lens may be covered");
        play_response("Camera error. Lens may be covered. Wipe and try again.");
    }
    /* CP11: linger on speaking screen so user/onlooker can read the structured
     * response. State stays SPEAKING (or ERROR) for 4 seconds after TTS ends,
     * then transitions to IDLE. Without this, the screen flashed for ~150ms
     * after speech ended and the user saw nothing. */
    tal_system_sleep(4000);
    nav_display_set_state(DISP_STATE_IDLE);
    s_idle = 1;
    tal_thread_delete(s_th); s_th = NULL;
}


static void touch_tap_trigger(void) {
    if (!s_idle) return;
    /* CP9: visible acknowledgement -- show NAVIGATE banner like swipes do */
    nav_display_show_mode_banner("NAVIGATE", 0x4FE3F0);
    s_active_prompt = NAV_VISION_PROMPT;
    s_idle = 0;
    THREAD_CFG_T c = { .stackDepth = 24576, .priority = 3, .thrdname = "proc" };
    tal_thread_create_and_start(&s_th, NULL, NULL, proc_th, NULL, &c);
}

/* IRIS Cp5: swipe gesture handlers */
static void touch_swipe_up_trigger(void) {
    if (!s_idle) return;
    nav_display_show_mode_banner("IDENTIFY", 0xE879F9);
    s_active_prompt = NAV_OBJECT_PROMPT;
    s_idle = 0;
    THREAD_CFG_T c = { .stackDepth = 24576, .priority = 3, .thrdname = "proc" };
    tal_thread_create_and_start(&s_th, NULL, NULL, proc_th, NULL, &c);
}

static void touch_swipe_down_trigger(void) {
    if (!s_idle) return;
    nav_display_show_mode_banner("READ", 0xFFB347);
    s_active_prompt = NAV_READ_PROMPT;
    s_idle = 0;
    THREAD_CFG_T c = { .stackDepth = 24576, .priority = 3, .thrdname = "proc" };
    tal_thread_create_and_start(&s_th, NULL, NULL, proc_th, NULL, &c);
}

/* CP21: track press start so HOLD can detect the 5-second forget-wifi gesture.
 * s_forget_armed prevents repeated firing while button is still held after 5s.
 * CP22h: also track last completed press duration so SINGLE_CLICK can be
 * suppressed if it fires after a long hold (defensive against driver edge cases). */
static uint32_t s_press_start_ms       = 0;
static uint32_t s_last_press_duration_ms = 0;
static bool     s_forget_armed         = false;
#define NAV_FORGET_WIFI_HOLD_MS  5000

static void btn_cb(char *name, TDL_BUTTON_TOUCH_EVENT_E ev, void *arg) {
    /* PRESS_DOWN / PRESS_UP / HOLD always run, even when device is busy.
     * The forget-wifi escape hatch must work regardless of state. */
    if (ev == TDL_BUTTON_PRESS_DOWN) {
        s_press_start_ms = (uint32_t)tal_system_get_millisecond();
        s_forget_armed = false;
        PR_NOTICE("[BTN] DOWN");
        return;
    }
    if (ev == TDL_BUTTON_PRESS_UP) {
        uint32_t now = (uint32_t)tal_system_get_millisecond();
        uint32_t held = now - s_press_start_ms;
        s_last_press_duration_ms = held;  /* CP22h: remember for SINGLE_CLICK gate */
        PR_NOTICE("[BTN] UP held=%u ms", (unsigned)held);
        /* CP22j: hide the forget overlay regardless of outcome */
        nav_display_hide_forget_overlay();
        if (s_forget_armed) {
            s_forget_armed = false;
            return;
        }
        if (held >= 3500 && held < NAV_FORGET_WIFI_HOLD_MS) {
            nav_display_show_mode_banner("CANCELLED", 0x7A8696);
            return;
        }
        /* CP22j: deferred IDENTIFY -- only fire on release, between 1000-2500ms.
         * Avoids triggering IDENTIFY pipeline during a hold that might be a
         * forget gesture. Below 1s = SINGLE_CLICK handles it; above 2.5s = was
         * forget territory (silent cancel or actual forget). */
        if (s_idle && held >= 1000 && held < 2500) {
            nav_display_show_mode_banner("IDENTIFY", 0xE879F9);
            s_active_prompt = NAV_OBJECT_PROMPT;
            s_idle = 0;
            THREAD_CFG_T c = { .stackDepth = 24576, .priority = 3, .thrdname = "proc" };
            tal_thread_create_and_start(&s_th, NULL, NULL, proc_th, NULL, &c);
        }
        return;
    }
    if (ev == TDL_BUTTON_LONG_PRESS_HOLD) {
        if (s_forget_armed) return;
        uint32_t now = (uint32_t)tal_system_get_millisecond();
        uint32_t held = now - s_press_start_ms;
        PR_NOTICE("[BTN] HOLD held=%u ms", (unsigned)held);
        /* CP22g: don't show forget progress until 50% of the threshold (2500ms).
         * Below that, the user is just doing a normal IDENTIFY long-press and
         * shouldn't see a "FORGET WI-FI 20%" banner that competes with the
         * IDENTIFY visual. Once they cross the 2500ms boundary we know they
         * mean it -- start showing progress. */
        if (held >= 2500) {
            int pct = (int)((held * 100) / NAV_FORGET_WIFI_HOLD_MS);
            if (pct > 100) pct = 100;
            nav_display_show_forget_progress(pct);
        }
        if (held >= NAV_FORGET_WIFI_HOLD_MS) {
            s_forget_armed = true;
            nav_wifi_forget();
            tal_system_sleep(800);
            tal_system_reset();
        }
        return;
    }

    /* CP22h: log mode events so the serial monitor reveals which click/long
     * the driver actually classified. Useful when on-device behavior
     * disagrees with expectations. */
    PR_NOTICE("[BTN] event=%d s_idle=%d last_press_dur=%u",
              (int)ev, s_idle ? 1 : 0, (unsigned)s_last_press_duration_ms);

    if (!s_idle) return;
    const char *p = NULL;
    const char *mode_name = NULL;
    uint32_t mode_col = 0x4FE3F0;  /* IRIS_STATE_IDLE cyan */
    switch (ev) {
        case TDL_BUTTON_PRESS_SINGLE_CLICK:
            /* CP22h: if the driver fires SINGLE_CLICK after a hold longer than
             * a real click should be (1000ms), suppress it. */
            if (s_last_press_duration_ms > 1000) {
                PR_NOTICE("[BTN] suppressing SINGLE_CLICK (was a %u ms hold)",
                          (unsigned)s_last_press_duration_ms);
                return;
            }
            p = NAV_VISION_PROMPT; mode_name = "NAVIGATE"; mode_col = 0x4FE3F0; break;
        case TDL_BUTTON_PRESS_DOUBLE_CLICK: p = NAV_READ_PROMPT;   mode_name = "READ";     mode_col = 0xFFB347; break;
        /* CP22j: LONG_PRESS_START intentionally NOT handled here.
         * IDENTIFY now fires from PRESS_UP (in the upper block) only when
         * held was 1000-2500ms. This stops IDENTIFY from triggering during a
         * forget hold and changing the underlying screen state, which kept
         * showing through the forget overlay. */
        default: return;
    }
    nav_display_show_mode_banner(mode_name, mode_col);
    s_active_prompt = p;
    s_idle = 0;
    THREAD_CFG_T c = { .stackDepth = 24576, .priority = 3, .thrdname = "proc" };
    tal_thread_create_and_start(&s_th, NULL, NULL, proc_th, NULL, &c);
}



/* ============================================================
 * CP12d: Hi Tuya wake word -- callback-only integration
 *
 * Serial debug log proves TuyaOpen auto-initializes the KWS engine
 * + AI_AUDIO_VAD_AUTO mode + record_task when WAKEUP/AUDIO components
 * are enabled in Kconfig. Model "nihaotuya-xiaozhitongxue-heytuya-hituya"
 * is loaded. We just register OUR callback to receive the wake event.
 * ============================================================ */
/* CP18: lightweight diagnostic counters readable by web UI. Volatile because
 * the KWS callback runs on a different thread from the web UI request handler.
 * No mutex needed: 32-bit aligned reads are atomic on Cortex-M33, and a single
 * misaligned wake count in the diagnostics page is harmless. */
static volatile uint32_t s_wake_count = 0;

static void wake_word_cb(TKL_KWS_WAKEUP_WORD_E word) {
    (void)word;
    /* CP12d (reverted from CP12e): minimal callback. The audible alert +
     * 700ms sleep regressed wake reliability -- KWS thread was blocked
     * during sleep, missing subsequent detections. CP18 adds a wake-event
     * counter for diagnostics. Audible feedback toggle is wired in the
     * settings KV and surfaced in /settings; future implementation must
     * defer the alert via tal_workq so the KWS thread isn't blocked. */
    s_wake_count++;
    touch_tap_trigger();
}

/* Public diag accessors consumed by nav_webui.c */
uint32_t nav_diag_get_wake_count(void) { return s_wake_count; }
void     nav_diag_trigger_tap(void)        { touch_tap_trigger(); }
void     nav_diag_trigger_double_tap(void) { touch_double_tap_trigger(); }
void     nav_diag_trigger_identify(void)   { touch_swipe_up_trigger(); }

/* ============================================================
 * CP13: Wi-Fi state machine helpers (KV + station + AP fallback)
 * ============================================================ */
#define NAV_KV_KEY_WIFI_SSID    "wifi.ssid"
#define NAV_KV_KEY_WIFI_PASS    "wifi.pass"
#define NAV_KV_KEY_WIFI_FORCE_AP "wifi.force_ap"  /* CP21: 1 = boot straight to AP */
#define NAV_WIFI_CONNECT_TIMEOUT_S  15
#define NAV_AP_IP    "192.168.4.1"
#define NAV_AP_GW    "192.168.4.1"
#define NAV_AP_MASK  "255.255.255.0"
#define NAV_AP_CHAN  6

static bool nav_wifi_try_station(const char *ssid, const char *pass, NW_IP_S *out_ip) {
    if (!ssid || !ssid[0]) return false;
    nav_display_set_text(ssid);
    tal_wifi_station_disconnect();
    tal_system_sleep(200);
    tal_wifi_station_connect((int8_t *)ssid, (int8_t *)(pass ? pass : ""));
    for (int i = 0; i < NAV_WIFI_CONNECT_TIMEOUT_S; i++) {
        if (tal_wifi_get_ip(WF_STATION, out_ip) == OPRT_OK
            && strlen(out_ip->ip) >= 7
            && strcmp(out_ip->ip, "0.0.0.0") != 0) {
            return true;
        }
        tal_system_sleep(1000);
    }
    return false;
}

static bool nav_wifi_load_kv(char *ssid_out, size_t ssid_max,
                             char *pass_out, size_t pass_max) {
    /* tal_kv_get allocates heap via the storage layer; we copy into our
     * caller-provided buffer then free. SSID is required, pass may be empty. */
    uint8_t *val = NULL;
    size_t len = 0;

    if (tal_kv_get(NAV_KV_KEY_WIFI_SSID, &val, &len) != OPRT_OK || !val || len == 0) {
        if (val) tal_kv_free(val);
        return false;
    }
    size_t copy = (len < ssid_max - 1) ? len : ssid_max - 1;
    memcpy(ssid_out, val, copy);
    ssid_out[copy] = 0;
    tal_kv_free(val);

    /* Pass is optional (open networks have empty pass) */
    val = NULL; len = 0;
    pass_out[0] = 0;
    if (tal_kv_get(NAV_KV_KEY_WIFI_PASS, &val, &len) == OPRT_OK && val && len > 0) {
        copy = (len < pass_max - 1) ? len : pass_max - 1;
        memcpy(pass_out, val, copy);
        pass_out[copy] = 0;
    }
    if (val) tal_kv_free(val);
    return true;
}

void nav_wifi_save_kv(const char *ssid, const char *pass) {
    if (ssid && ssid[0]) {
        tal_kv_set(NAV_KV_KEY_WIFI_SSID, (const uint8_t *)ssid, strlen(ssid) + 1);
    }
    tal_kv_set(NAV_KV_KEY_WIFI_PASS, (const uint8_t *)(pass ? pass : ""),
               (pass ? strlen(pass) : 0) + 1);
    /* CP21: saving fresh creds clears the force-AP flag so next boot tries
     * station mode against the new network. */
    tal_kv_del(NAV_KV_KEY_WIFI_FORCE_AP);
}

/* CP21: Clear stored creds + arm force-AP flag, then caller reboots. After
 * reboot, nav_app_main detects the flag and goes straight to AP mode -- skips
 * both KV (now empty) and build-time NAV_SSID_LIST. Once the user saves new
 * creds via the AP web UI, nav_wifi_save_kv clears the flag so subsequent
 * boots resume normal station-mode behavior. */
void nav_wifi_forget(void) {
    tal_kv_del(NAV_KV_KEY_WIFI_SSID);
    tal_kv_del(NAV_KV_KEY_WIFI_PASS);
    char one[2] = "1";
    tal_kv_set(NAV_KV_KEY_WIFI_FORCE_AP, (const uint8_t *)one, 2);
}

/* Read+consume the force-AP flag. Returns true if it was set; resets it after
 * read so a single forget triggers exactly one AP boot. (Even before the user
 * saves new creds, if they reset manually the device still goes to AP because
 * KV creds are empty -- this just makes the FIRST boot after forget obvious.) */
static bool nav_wifi_force_ap_consume(void) {
    uint8_t *val = NULL; size_t len = 0;
    bool armed = false;
    if (tal_kv_get(NAV_KV_KEY_WIFI_FORCE_AP, &val, &len) == OPRT_OK && val && len > 0) {
        if (val[0] == '1') armed = true;
    }
    if (val) tal_kv_free(val);
    return armed;
}

/* CP22: defensive AP startup. The previous version assumed a clean radio,
 * but if force_ap is armed the chip was just in WWM_STATION actively scanning,
 * and switching to SOFTAP mid-scan can silently fail to broadcast. Sequence:
 *   1. tear down any prior AP
 *   2. disconnect from any station network so the radio is idle
 *   3. small sleep to let the driver settle
 *   4. set SOFTAP mode (with another small sleep)
 *   5. start AP
 *   6. log every step so the bug is visible on serial if it recurs
 */
static OPERATE_RET nav_wifi_start_ap(void) {
    PR_NOTICE("[AP] starting AP setup");

    /* Step 1: stop any prior AP. Returns error if none was running -- that's fine. */
    tal_wifi_ap_stop();

    /* Step 2: ensure station radio is idle */
    tal_wifi_station_disconnect();
    tal_system_sleep(150);

    /* Step 3: read MAC for SSID suffix */
    NW_MAC_S mac = {0};
    OPERATE_RET rt = tal_wifi_get_mac(WF_AP, &mac);
    if (rt != OPRT_OK) {
        PR_ERR("[AP] tal_wifi_get_mac failed: 0x%x", rt);
        return rt;
    }
    PR_NOTICE("[AP] mac: %02X:%02X:%02X:%02X:%02X:%02X",
              mac.mac[0], mac.mac[1], mac.mac[2], mac.mac[3], mac.mac[4], mac.mac[5]);

    char ap_ssid[32];
    snprintf(ap_ssid, sizeof(ap_ssid), "IRIS-%02X%02X", mac.mac[4], mac.mac[5]);

    /* Step 4: switch to SOFTAP mode. Sleep so radio reconfigures. */
    rt = tal_wifi_set_work_mode(WWM_SOFTAP);
    PR_NOTICE("[AP] tal_wifi_set_work_mode(WWM_SOFTAP) = 0x%x", rt);
    if (rt != OPRT_OK) return rt;
    tal_system_sleep(300);

    /* Step 5: configure + start. Set every field explicitly (s_len, p_len=0
     * for OPEN, ssid_hidden=0 to broadcast, max_conn=4 so multiple phones can
     * peek without locking each other out). */
    WF_AP_CFG_IF_S ap_cfg = {0};
    strncpy(ap_cfg.ip.ip,   NAV_AP_IP,   sizeof(ap_cfg.ip.ip)   - 1);
    strncpy(ap_cfg.ip.gw,   NAV_AP_GW,   sizeof(ap_cfg.ip.gw)   - 1);
    strncpy(ap_cfg.ip.mask, NAV_AP_MASK, sizeof(ap_cfg.ip.mask) - 1);
    strncpy((char *)ap_cfg.ssid, ap_ssid, sizeof(ap_cfg.ssid) - 1);
    ap_cfg.s_len       = strlen((char *)ap_cfg.ssid);
    ap_cfg.p_len       = 0;
    ap_cfg.md          = WAAM_OPEN;
    ap_cfg.chan        = NAV_AP_CHAN;
    ap_cfg.ssid_hidden = 0;
    ap_cfg.max_conn    = 4;
    ap_cfg.ms_interval = 100;

    rt = tal_wifi_ap_start(&ap_cfg);
    PR_NOTICE("[AP] tal_wifi_ap_start = 0x%x ssid=%s ip=%s chan=%d",
              rt, ap_ssid, NAV_AP_IP, NAV_AP_CHAN);

    if (rt == OPRT_OK) {
        /* CP22: surface the SSID + URL on the LCD so the user knows what to do.
         * Without this, the user sees only the bionic-eye animation and has no
         * idea the device is in setup mode. */
        nav_display_show_ap_setup(ap_ssid);
        nav_display_set_ip(NAV_AP_IP);
        nav_display_set_wifi(true);
    } else {
        nav_display_set_text("AP START FAILED");
    }
    return rt;
}

/* CP16: status-bar clock updater. Called every 5s by the software timer.
 * Falls back to "--:--" until NTP sync (tal_time_check_time_sync returns OK).
 * CP22i: faster cadence (5s vs old 30s) means the time appears within
 * seconds of NTP completing, instead of up to a half-minute later. Logs
 * sync state every tick so a stuck "--:--" reveals whether the sync ever
 * happens. */
static void nav_clock_update(TIMER_ID timer_id, void *arg) {
    (void)timer_id; (void)arg;
    OPERATE_RET sync_rt = tal_time_check_time_sync();
    if (sync_rt != OPRT_OK) {
        static int s_clock_warn_count = 0;
        if (s_clock_warn_count++ < 12) {
            PR_NOTICE("[CLOCK] NTP not synced yet (rt=0x%x), tick %d", sync_rt, s_clock_warn_count);
        }
        nav_display_set_time("--:--");
        return;
    }
    TIME_T now = tal_time_get_cur_posix();
    POSIX_TM_S tm = {0};
    if (tal_time_get_local_time_custom(now, &tm) != OPRT_OK) {
        nav_display_set_time("--:--");
        return;
    }
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
    nav_display_set_time(buf);
}

/* CP17: settings persistence (volume / brightness / voice / language / wake-feedback).
 * KV keys are flat strings; values are short ASCII (0-100, EN/ES/HI/AR, on/off).
 * Reads happen lazily via accessors so the web UI can update without main.c changes. */
#define NAV_KV_VOLUME      "set.vol"
#define NAV_KV_BRIGHTNESS  "set.bri"
#define NAV_KV_VOICE       "set.voice"
#define NAV_KV_LANGUAGE    "set.lang"
#define NAV_KV_WAKE_FB     "set.wakefb"

static int nav_kv_get_int(const char *key, int def) {
    uint8_t *val = NULL; size_t len = 0;
    if (tal_kv_get(key, &val, &len) != OPRT_OK || !val || len == 0) {
        if (val) tal_kv_free(val);
        return def;
    }
    char buf[16]; size_t copy = (len < sizeof(buf) - 1) ? len : sizeof(buf) - 1;
    memcpy(buf, val, copy); buf[copy] = 0;
    tal_kv_free(val);
    return atoi(buf);
}

static void nav_kv_set_int(const char *key, int v) {
    char buf[16]; int n = snprintf(buf, sizeof(buf), "%d", v);
    if (n > 0) tal_kv_set(key, (const uint8_t *)buf, n + 1);
}

static void nav_kv_get_str(const char *key, char *out, size_t out_max, const char *def) {
    uint8_t *val = NULL; size_t len = 0;
    if (tal_kv_get(key, &val, &len) != OPRT_OK || !val || len == 0) {
        if (val) tal_kv_free(val);
        snprintf(out, out_max, "%s", def);
        return;
    }
    size_t copy = (len < out_max - 1) ? len : out_max - 1;
    memcpy(out, val, copy); out[copy] = 0;
    if (val) tal_kv_free(val);
}

static void nav_kv_set_str(const char *key, const char *v) {
    if (!v) v = "";
    tal_kv_set(key, (const uint8_t *)v, strlen(v) + 1);
}

/* Public accessors for nav_webui.c */
int  nav_settings_get_volume(void)     { return nav_kv_get_int(NAV_KV_VOLUME, 70); }
int  nav_settings_get_brightness(void) { return nav_kv_get_int(NAV_KV_BRIGHTNESS, 80); }
int  nav_settings_get_wake_feedback(void) { return nav_kv_get_int(NAV_KV_WAKE_FB, 0); }
void nav_settings_get_voice(char *out, size_t n)    { nav_kv_get_str(NAV_KV_VOICE, out, n, "MID"); }
void nav_settings_get_language(char *out, size_t n) { nav_kv_get_str(NAV_KV_LANGUAGE, out, n, "EN"); }

/* CP20: volume now applies live via the AI audio player (0-100, scales the
 * I2S codec output). The `set_vol` API is part of ai_audio_player. */
extern OPERATE_RET ai_audio_player_set_vol(int vol);

void nav_settings_set_volume(int v) {
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    nav_kv_set_int(NAV_KV_VOLUME, v);
    ai_audio_player_set_vol(v);
}
/* CP18 fix: brightness now actually applies to the LCD backlight. The TDL
 * display device "display" is registered by board_register_hardware() with a
 * PWM-driven backlight (1 kHz, duty 0-10000). tdl_disp_set_brightness scales
 * the 0-100 input into the PWM duty internally. */
extern OPERATE_RET tdl_disp_set_brightness(void *disp_hdl, uint8_t brightness);
extern void *tdl_disp_find_dev(char *name);

void nav_settings_set_brightness(int v) {
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    nav_kv_set_int(NAV_KV_BRIGHTNESS, v);
    void *disp = tdl_disp_find_dev("display");
    if (disp) tdl_disp_set_brightness(disp, (uint8_t)v);
}
void nav_settings_set_wake_feedback(int v) { nav_kv_set_int(NAV_KV_WAKE_FB, v ? 1 : 0); }
void nav_settings_set_voice(const char *s)    { nav_kv_set_str(NAV_KV_VOICE, s); }
void nav_settings_set_language(const char *s) { nav_kv_set_str(NAV_KV_LANGUAGE, s); }

void nav_app_main(void) {
    tal_log_init(TAL_LOG_LEVEL_DEBUG, 1024, (TAL_LOG_OUTPUT_CB)tkl_log_output);
    tal_kv_init(&(tal_kv_cfg_t){
        .seed = "vmlkasdh93dlvlcy",
        .key  = "dflfuap134ddlduq",
    });
    tal_sw_timer_init();
    tal_workq_init(); tuya_tls_init(); tal_time_service_init();
    board_register_hardware();
    nav_display_init();
    /* CP18 fix: re-apply saved brightness so the value persists across reboots */
    {
        void *disp = tdl_disp_find_dev("display");
        if (disp) tdl_disp_set_brightness(disp, (uint8_t)nav_settings_get_brightness());
    }
    nav_display_set_state(DISP_STATE_CONNECTING);
    tal_wifi_init(NULL);
    tal_wifi_set_work_mode(WWM_STATION);
    NW_IP_S ip = {0};
    bool connected = false;

    /* CP13 priority chain: 1) KV-stored creds, 2) build-time NAV_SSID_LIST,
     * 3) AP-mode fallback. The "happy path" (build-time creds reach a
     * working network) is identical to v0.2.0 behavior. */

    /* CP21: if forget-wifi was triggered (web button or 5s physical hold),
     * the KV flag is set. Skip both station tries and go directly to AP so
     * the user can configure a new network. */
    bool force_ap = nav_wifi_force_ap_consume();
    if (force_ap) {
        nav_display_set_text("FORGET WI-FI\nSetup mode");
    }

    /* Try 1: KV-stored creds (set via web UI in CP15+) */
    if (!force_ap) {
        char kv_ssid[64] = {0};
        char kv_pass[64] = {0};
        if (nav_wifi_load_kv(kv_ssid, sizeof(kv_ssid), kv_pass, sizeof(kv_pass))) {
            connected = nav_wifi_try_station(kv_ssid, kv_pass, &ip);
        }
    }

    /* Try 2: build-time NAV_SSID_LIST (skip when force_ap is armed -- the user
     * explicitly asked to test AP mode, don't silently fall back to a hardcoded
     * SSID that would defeat the purpose of forgetting Wi-Fi). */
    if (!connected && !force_ap) {
        for (int idx = 0; idx < NAV_SSID_COUNT && !connected; idx++) {
            connected = nav_wifi_try_station(NAV_SSID_LIST[idx], NAV_WIFI_PASS, &ip);
        }
    }

    /* Try 3: AP-mode fallback. Stay forever until user provides creds via web UI. */
    if (!connected) {
        if (nav_wifi_start_ap() == OPRT_OK) {
            /* CP22b: tell the web UI we're in AP-only mode so it locks to /wifi
             * and hides irrelevant pages (settings/diagnostics make no sense
             * without real internet). Must be called BEFORE nav_webui_init so
             * the flag is set when the accept loop starts. */
            extern void nav_webui_set_ap_only(bool on);
            nav_webui_set_ap_only(true);
            nav_webui_init();
            for (;;) tal_system_sleep(1000);
        }
        /* AP failed too -- shouldn't happen on T5. Fall through, IRIS will be offline. */
    }

    /* CP14: Wi-Fi connected via station mode -- start web UI for ongoing admin */
    nav_webui_init();

    /* CP16: NTP-driven status-bar clock. Set IST as default tz; web UI in CP17 will allow change. */
    tal_time_set_time_zone("+05:30");
    static TIMER_ID s_clock_timer = NULL;
    tal_sw_timer_create(nav_clock_update, NULL, &s_clock_timer);
    /* CP22i: 5s cycle so clock appears quickly after NTP syncs (post-Wi-Fi). */
    tal_sw_timer_start(s_clock_timer, 5000, TAL_TIMER_CYCLE);
    nav_clock_update(NULL, NULL);  /* fire once now */

    nav_display_set_ip(ip.ip); nav_display_set_wifi(true);
    ai_audio_player_init();
    /* CP20 fix: apply saved volume so it persists across reboots, just like
     * brightness. Must run AFTER ai_audio_player_init or the codec isn't ready. */
    ai_audio_player_set_vol(nav_settings_get_volume());
    ai_video_init(&(AI_VIDEO_CFG_T){0});
    
    BUTTON_GPIO_CFG_T gpio_bc = {
        .pin = 12,
        .level = TUYA_GPIO_LEVEL_LOW,
        .mode = BUTTON_TIMER_SCAN_MODE,
        .pin_type.gpio_pull = TUYA_GPIO_PULLUP
    };
    tdd_gpio_button_register("ai_chat_button", &gpio_bc);
    
    /* CP21: long_keep_timer = 500 ms means HOLD events fire every 500 ms while
     * the button is held. Our forget-wifi gesture needs >=5 s, so we'll see
     * roughly 10 HOLD events over the threshold.
     *
     * CP22g: TDL_BUTTON_LONG_PRESS_START must stay registered. Without it, the
     * button driver's internal state machine doesn't classify long presses as
     * long, and SINGLE_CLICK fires on release of a 5-second hold -- which
     * triggers NAVIGATE instead of forget-wifi. Bug found on hardware.
     *
     * CP22h: explicitly set long_start_valid_time = 1000 ms so the driver
     * definitely arms long-press detection at 1 second. Default is 1500 ms
     * but relying on defaults left an edge case where SINGLE_CLICK still
     * leaked through; setting it explicitly removes the ambiguity. Plus
     * a defensive duration check on SINGLE_CLICK in btn_cb above. */
    TDL_BUTTON_CFG_T bc = {
        .button_debounce_time   = 50,
        .long_start_valid_time  = 1000,
        .long_keep_timer        = 500,
    };
    tdl_button_create("ai_chat_button", &bc, &s_btn);
    tdl_button_event_register(s_btn, TDL_BUTTON_PRESS_DOWN,         btn_cb);
    tdl_button_event_register(s_btn, TDL_BUTTON_PRESS_UP,           btn_cb);
    tdl_button_event_register(s_btn, TDL_BUTTON_LONG_PRESS_START,   btn_cb);
    tdl_button_event_register(s_btn, TDL_BUTTON_LONG_PRESS_HOLD,    btn_cb);
    tdl_button_event_register(s_btn, TDL_BUTTON_PRESS_SINGLE_CLICK, btn_cb);
    tdl_button_event_register(s_btn, TDL_BUTTON_PRESS_DOUBLE_CLICK, btn_cb);
    tdl_button_event_register(s_btn, TDL_BUTTON_LONG_PRESS_START, btn_cb);
    nav_display_set_tap_cb(touch_tap_trigger);
    nav_display_set_swipe_callbacks(touch_swipe_up_trigger, touch_swipe_down_trigger);
    nav_display_set_double_tap_cb(touch_double_tap_trigger);  /* CP9: double-tap to repeat */

    /* CP12d: hook our callback into the already-running KWS engine.
     * No init needed -- TuyaOpen auto-starts KWS + audio_input when the
     * WAKEUP/AUDIO components are compiled in (per Kconfig). */
    tkl_kws_reg_wakeup_cb(wake_word_cb);

    
    nav_display_set_state(DISP_STATE_IDLE);
    nav_display_set_text("Tap to navigate");
    for (;;) tal_system_sleep(1000);
}

static void nav_app_thread(void *arg) { nav_app_main(); }
void tuya_app_main(void) {
    THREAD_CFG_T c = { .stackDepth = 8192, .priority = 4, .thrdname = "main" };
    tal_thread_create_and_start(&s_app_th_hdl, NULL, NULL, nav_app_thread, NULL, &c);
}
