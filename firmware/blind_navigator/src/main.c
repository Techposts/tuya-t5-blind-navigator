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
static TDL_BUTTON_HANDLE s_btn = NULL;
static THREAD_HANDLE s_th = NULL;
static THREAD_HANDLE s_app_th_hdl = NULL;


static void play_response(const char *text) {
    if (!text) return;
    nav_display_set_text(text);
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

static void proc_th(void *arg) {
    nav_display_set_state(DISP_STATE_PROCESSING);
    nav_display_set_text("Looking...");
    ai_video_display_start(); tal_system_sleep(3500);
    uint8_t *jpeg = NULL; uint32_t jlen = 0;
    OPERATE_RET ret = ai_video_get_jpeg_frame(&jpeg, &jlen);
    ai_video_display_stop();
    if (ret == OPRT_OK && jpeg && jlen > 0) {
        char *resp = NULL;
        if (openai_ask_image(jpeg, jlen, s_active_prompt, &resp) == OPRT_OK) {
            play_response(resp); tal_free(resp);
        } else { play_response("Cloud error."); }
        ai_video_jpeg_image_free(&jpeg);
    } else { play_response("Camera error."); }
    nav_display_set_state(DISP_STATE_IDLE);
    /* keep response text visible so the user can read it; do not overwrite */
    s_idle = 1;
    tal_thread_delete(s_th); s_th = NULL;
}


static void touch_tap_trigger(void) {
    if (!s_idle) return;
    s_active_prompt = NAV_VISION_PROMPT;
    s_idle = 0;
    THREAD_CFG_T c = { .stackDepth = 24576, .priority = 3, .thrdname = "proc" };
    tal_thread_create_and_start(&s_th, NULL, NULL, proc_th, NULL, &c);
}

static void btn_cb(char *name, TDL_BUTTON_TOUCH_EVENT_E ev, void *arg) {
    if (!s_idle) return;
    const char *p = NULL;
    switch (ev) {
        case TDL_BUTTON_PRESS_SINGLE_CLICK: p = NAV_VISION_PROMPT; break;
        case TDL_BUTTON_PRESS_DOUBLE_CLICK: p = NAV_READ_PROMPT;   break;
        case TDL_BUTTON_LONG_PRESS_START:   p = NAV_OBJECT_PROMPT; break;
        default: return;
    }
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
    const char* const* ss = NAV_SSID_LIST;
    while (1) {
        nav_display_set_text(ss[idx]);
        tal_wifi_station_disconnect(); tal_system_sleep(200);
        tal_wifi_station_connect((int8_t *)ss[idx], (int8_t *)NAV_WIFI_PASS);
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
    
    nav_display_set_state(DISP_STATE_IDLE);
    nav_display_set_text("Say Hey Tuya");
    for (;;) tal_system_sleep(1000);
}

static void nav_app_thread(void *arg) { nav_app_main(); }
void tuya_app_main(void) {
    THREAD_CFG_T c = { .stackDepth = 8192, .priority = 4, .thrdname = "main" };
    tal_thread_create_and_start(&s_app_th_hdl, NULL, NULL, nav_app_thread, NULL, &c);
}
