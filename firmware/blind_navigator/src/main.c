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
#include "openai_backend.h"
#include <string.h>
#include <stdio.h>

extern void tkl_log_output(const char *format, ...);

static volatile int s_idle = 1;
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

static void btn_cb(char *name, TDL_BUTTON_TOUCH_EVENT_E ev, void *arg) {
    if (!s_idle) return;
    const char *p = NULL;
    const char *mode_name = NULL;
    uint32_t mode_col = 0x4FE3F0;  /* IRIS_STATE_IDLE cyan */
    switch (ev) {
        case TDL_BUTTON_PRESS_SINGLE_CLICK: p = NAV_VISION_PROMPT; mode_name = "NAVIGATE"; mode_col = 0x4FE3F0; break;
        case TDL_BUTTON_PRESS_DOUBLE_CLICK: p = NAV_READ_PROMPT;   mode_name = "READ";     mode_col = 0xFFB347; break;
        case TDL_BUTTON_LONG_PRESS_START:   p = NAV_OBJECT_PROMPT; mode_name = "IDENTIFY"; mode_col = 0xE879F9; break;
        default: return;
    }
    nav_display_show_mode_banner(mode_name, mode_col);
    s_active_prompt = p;
    s_idle = 0;
    THREAD_CFG_T c = { .stackDepth = 24576, .priority = 3, .thrdname = "proc" };
    tal_thread_create_and_start(&s_th, NULL, NULL, proc_th, NULL, &c);
}

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
    nav_display_set_state(DISP_STATE_CONNECTING);
    tal_wifi_init(NULL); tal_wifi_set_work_mode(WWM_STATION);
    NW_IP_S ip = {0}; int idx = 0;
    const char* ss[] = {"TopFloor 2.4GHZ"};
    while (1) {
        nav_display_set_text(ss[idx]);
        tal_wifi_station_disconnect(); tal_system_sleep(200);
        tal_wifi_station_connect((int8_t *)ss[idx], (int8_t *)"9716582452");
        for (int i=0; i<15; i++) {
            if (tal_wifi_get_ip(WF_STATION, &ip) == OPRT_OK && strlen(ip.ip) >= 7 && strcmp(ip.ip, "0.0.0.0") != 0) goto ok;
            tal_system_sleep(1000);
        }
        idx = 0;
    }
ok:
    nav_display_set_ip(ip.ip); nav_display_set_wifi(true);
    ai_audio_player_init();
    ai_video_init(&(AI_VIDEO_CFG_T){0});
    
    BUTTON_GPIO_CFG_T gpio_bc = {
        .pin = 12,
        .level = TUYA_GPIO_LEVEL_LOW,
        .mode = BUTTON_TIMER_SCAN_MODE,
        .pin_type.gpio_pull = TUYA_GPIO_PULLUP
    };
    tdd_gpio_button_register("ai_chat_button", &gpio_bc);
    
    TDL_BUTTON_CFG_T bc = { .button_debounce_time = 50 };
    tdl_button_create("ai_chat_button", &bc, &s_btn);
    tdl_button_event_register(s_btn, TDL_BUTTON_PRESS_SINGLE_CLICK, btn_cb);
    tdl_button_event_register(s_btn, TDL_BUTTON_PRESS_DOUBLE_CLICK, btn_cb);
    tdl_button_event_register(s_btn, TDL_BUTTON_LONG_PRESS_START, btn_cb);
    nav_display_set_tap_cb(touch_tap_trigger);
    nav_display_set_swipe_callbacks(touch_swipe_up_trigger, touch_swipe_down_trigger);
    nav_display_set_double_tap_cb(touch_double_tap_trigger);  /* CP9: double-tap to repeat */
    
    nav_display_set_state(DISP_STATE_IDLE);
    nav_display_set_text("Tap to navigate");
    for (;;) tal_system_sleep(1000);
}

static void nav_app_thread(void *arg) { nav_app_main(); }
void tuya_app_main(void) {
    THREAD_CFG_T c = { .stackDepth = 8192, .priority = 4, .thrdname = "main" };
    tal_thread_create_and_start(&s_app_th_hdl, NULL, NULL, nav_app_thread, NULL, &c);
}
