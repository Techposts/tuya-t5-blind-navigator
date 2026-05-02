#ifndef __NAV_DISPLAY_H__
#define __NAV_DISPLAY_H__

#include "tuya_cloud_types.h"

typedef enum {
    DISP_STATE_IDLE = 0,
    DISP_STATE_CONNECTING,
    DISP_STATE_LISTENING,
    DISP_STATE_PROCESSING,
    DISP_STATE_SPEAKING,
    DISP_STATE_ERROR,
} disp_state_t;

OPERATE_RET nav_display_init(void);
void nav_display_set_state(disp_state_t state);
void nav_display_set_text(const char *text);
void nav_display_stream_text(const char *text, uint32_t wpm);
void nav_display_set_wifi(bool connected);
void nav_display_set_tap_cb(void (*cb)(void));

/* IRIS Cp4/5: mode banner + swipe gestures */
void nav_display_show_mode_banner(const char *mode_name, uint32_t color_hex);
void nav_display_set_swipe_callbacks(void (*swipe_up)(void), void (*swipe_down)(void));

/* New Diagnostic APIs */
void nav_display_set_ip(const char *ip);
void nav_display_set_ssid(const char *ssid);

/* CP9: extra interaction + per-state APIs */
void nav_display_set_double_tap_cb(void (*cb)(void));
void nav_display_randomize_think_bars(void);
void nav_display_show_settings(void);
void nav_display_hide_settings(void);

/* CP10: structured 4-field response for the SPEAKING screen.
 * Pass empty strings for unused slots. Field 3 (index 2) is the BIG GREEN hero. */
/* CP16: status bar clock (HH:MM, 24h) */
void nav_display_set_time(const char *hhmm);

void nav_display_set_speak_response(
    const char *l1, const char *v1,
    const char *l2, const char *v2,
    const char *l3, const char *v3,
    const char *l4, const char *v4);

#endif
