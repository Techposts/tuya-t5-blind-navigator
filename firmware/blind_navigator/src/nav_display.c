/* nav_display.c — Bionic Eye UI for blind_navigator
 * Solid black bg (no vignetting). Multi-ring mechanical iris with glowing pupil.
 * State-driven color. Top status bar. Bottom dark panel with large readable
 * response text in 24pt for visual confirmation.
 */
#include "nav_display.h"
#include "tal_api.h"
#include "lvgl.h"
#include "lv_vendor.h"
#include "lv_port_disp.h"
#include "board_com_api.h"
#include <string.h>
#include <stdio.h>

#define COL_BG          lv_color_hex(0x000000)
#define COL_PANEL       lv_color_hex(0x0C1218)
#define COL_FRAME       lv_color_hex(0x202833)
#define COL_DIM         lv_color_hex(0x808898)
#define COL_HUD         lv_color_hex(0x40D8FF)
#define COL_IDLE        lv_color_hex(0x40D8FF)
#define COL_LISTENING   lv_color_hex(0xFFC840)
#define COL_PROCESSING  lv_color_hex(0xD846FF)
#define COL_SPEAKING    lv_color_hex(0x40FF98)
#define COL_ERROR       lv_color_hex(0xFF4848)

LV_FONT_DECLARE(lv_font_montserrat_14)
LV_FONT_DECLARE(lv_font_montserrat_24)

static bool s_inited = false;
static lv_obj_t *s_screen;
static lv_obj_t *s_iris_outer;       /* outermost chrome ring */
static lv_obj_t *s_iris_mid;         /* iris fill (state color) */
static lv_obj_t *s_iris_inner_dark;  /* dark ring inside iris */
static lv_obj_t *s_pupil;            /* pupil (small, with glow) */
static lv_obj_t *s_orbit;            /* one rotating thin arc */
static lv_obj_t *s_state_label;
static lv_obj_t *s_response_panel;
static lv_obj_t *s_response_label;
static lv_obj_t *s_wifi_label;
static lv_obj_t *s_ip_label;
static lv_anim_t s_orbit_anim;
static char s_resp_buf[1024];

static void rot_cb(void *obj, int32_t v) { lv_arc_set_rotation((lv_obj_t *)obj, v); }

static lv_color_t state_color(disp_state_t st) {
    switch (st) {
        case DISP_STATE_IDLE:        return COL_IDLE;
        case DISP_STATE_CONNECTING:  return COL_DIM;
        case DISP_STATE_LISTENING:   return COL_LISTENING;
        case DISP_STATE_PROCESSING:  return COL_PROCESSING;
        case DISP_STATE_SPEAKING:    return COL_SPEAKING;
        case DISP_STATE_ERROR:       return COL_ERROR;
    }
    return COL_DIM;
}

static const char *state_text(disp_state_t st) {
    switch (st) {
        case DISP_STATE_IDLE:        return "READY";
        case DISP_STATE_CONNECTING:  return "CONNECTING";
        case DISP_STATE_LISTENING:   return "LISTENING";
        case DISP_STATE_PROCESSING:  return "ANALYZING";
        case DISP_STATE_SPEAKING:    return "SPEAKING";
        case DISP_STATE_ERROR:       return "ERROR";
    }
    return "";
}

OPERATE_RET nav_display_init(void) {
    if (s_inited) return OPRT_OK;

    lv_vendor_init(DISPLAY_NAME);
    lv_vendor_start(THREAD_PRIO_0, 1024 * 8);
    lv_vendor_disp_lock();

    int W = LV_HOR_RES, H = LV_VER_RES;
    int eye_size = (W < H ? W : H) * 55 / 100;
    int eye_y = (H - eye_size) / 2 - H / 6;

    /* Solid black root — kills the vignette completely */
    s_screen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(s_screen, W, H);
    lv_obj_set_style_bg_color(s_screen, COL_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(s_screen, COL_BG, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_screen, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_screen, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    /* Top status bar */
    s_wifi_label = lv_label_create(s_screen);
    lv_label_set_text(s_wifi_label, LV_SYMBOL_WIFI " offline");
    lv_obj_set_style_text_color(s_wifi_label, COL_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_wifi_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(s_wifi_label, LV_ALIGN_TOP_LEFT, 8, 6);

    s_ip_label = lv_label_create(s_screen);
    lv_label_set_text(s_ip_label, "");
    lv_obj_set_style_text_color(s_ip_label, COL_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_ip_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(s_ip_label, LV_ALIGN_TOP_RIGHT, -8, 6);

    /* === BIONIC EYE === */
    /* Outer chrome ring — thin border circle, no fill */
    s_iris_outer = lv_obj_create(s_screen);
    lv_obj_set_size(s_iris_outer, eye_size, eye_size);
    lv_obj_align(s_iris_outer, LV_ALIGN_TOP_MID, 0, eye_y);
    lv_obj_set_style_radius(s_iris_outer, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_iris_outer, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_iris_outer, COL_FRAME, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_iris_outer, 3, LV_PART_MAIN);
    lv_obj_set_style_border_opa(s_iris_outer, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(s_iris_outer, LV_OBJ_FLAG_SCROLLABLE);

    /* Iris fill — state-colored disk */
    int iris_size = eye_size * 80 / 100;
    s_iris_mid = lv_obj_create(s_screen);
    lv_obj_set_size(s_iris_mid, iris_size, iris_size);
    lv_obj_align(s_iris_mid, LV_ALIGN_TOP_MID, 0, eye_y + (eye_size - iris_size) / 2);
    lv_obj_set_style_radius(s_iris_mid, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_iris_mid, COL_IDLE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_iris_mid, LV_OPA_30, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_iris_mid, COL_IDLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_iris_mid, 2, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(s_iris_mid, COL_IDLE, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(s_iris_mid, 24, LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(s_iris_mid, LV_OPA_50, LV_PART_MAIN);
    lv_obj_clear_flag(s_iris_mid, LV_OBJ_FLAG_SCROLLABLE);

    /* Inner dark ring — for depth */
    int inner_dark_size = iris_size * 65 / 100;
    s_iris_inner_dark = lv_obj_create(s_screen);
    lv_obj_set_size(s_iris_inner_dark, inner_dark_size, inner_dark_size);
    lv_obj_align(s_iris_inner_dark, LV_ALIGN_TOP_MID, 0, eye_y + (eye_size - inner_dark_size) / 2);
    lv_obj_set_style_radius(s_iris_inner_dark, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_iris_inner_dark, COL_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_iris_inner_dark, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_iris_inner_dark, COL_IDLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_iris_inner_dark, 2, LV_PART_MAIN);
    lv_obj_clear_flag(s_iris_inner_dark, LV_OBJ_FLAG_SCROLLABLE);

    /* Pupil — small bright glowing dot */
    int pupil_size = iris_size / 5;
    s_pupil = lv_obj_create(s_screen);
    lv_obj_set_size(s_pupil, pupil_size, pupil_size);
    lv_obj_align(s_pupil, LV_ALIGN_TOP_MID, 0, eye_y + (eye_size - pupil_size) / 2);
    lv_obj_set_style_radius(s_pupil, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_pupil, COL_IDLE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_pupil, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_opa(s_pupil, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(s_pupil, COL_IDLE, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(s_pupil, 30, LV_PART_MAIN);
    lv_obj_set_style_shadow_spread(s_pupil, 4, LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(s_pupil, LV_OPA_80, LV_PART_MAIN);
    lv_obj_clear_flag(s_pupil, LV_OBJ_FLAG_SCROLLABLE);

    /* Orbital arc — rotating slowly outside the chrome ring */
    int orbit_size = eye_size + 24;
    s_orbit = lv_arc_create(s_screen);
    lv_obj_set_size(s_orbit, orbit_size, orbit_size);
    lv_obj_align(s_orbit, LV_ALIGN_TOP_MID, 0, eye_y - 12);
    lv_arc_set_bg_angles(s_orbit, 0, 360);
    lv_arc_set_angles(s_orbit, 0, 70);
    lv_obj_remove_style(s_orbit, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(s_orbit, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(s_orbit, 2, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_orbit, 2, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(s_orbit, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_orbit, COL_IDLE, LV_PART_INDICATOR);

    /* State label below eye */
    s_state_label = lv_label_create(s_screen);
    lv_label_set_text(s_state_label, "BOOTING");
    lv_obj_set_style_text_color(s_state_label, COL_IDLE, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_state_label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(s_state_label, 4, LV_PART_MAIN);
    lv_obj_align(s_state_label, LV_ALIGN_TOP_MID, 0, eye_y + eye_size + 14);

    /* Response panel — dark bordered box at bottom for the AI's reply */
    int panel_h = H * 30 / 100;
    s_response_panel = lv_obj_create(s_screen);
    lv_obj_set_size(s_response_panel, W - 16, panel_h);
    lv_obj_align(s_response_panel, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_bg_color(s_response_panel, COL_PANEL, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_response_panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_response_panel, COL_FRAME, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_response_panel, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(s_response_panel, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_response_panel, 12, LV_PART_MAIN);
    lv_obj_clear_flag(s_response_panel, LV_OBJ_FLAG_SCROLLABLE);

    s_response_label = lv_label_create(s_response_panel);
    lv_label_set_text(s_response_label, "Press button or tap screen");
    lv_label_set_long_mode(s_response_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_response_label, W - 48);
    lv_obj_set_style_text_color(s_response_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_response_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_response_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    lv_obj_align(s_response_label, LV_ALIGN_TOP_LEFT, 0, 0);

    /* One slow orbit animation */
    lv_anim_init(&s_orbit_anim);
    lv_anim_set_var(&s_orbit_anim, s_orbit);
    lv_anim_set_values(&s_orbit_anim, 0, 3599);
    lv_anim_set_exec_cb(&s_orbit_anim, rot_cb);
    lv_anim_set_time(&s_orbit_anim, 6000);
    lv_anim_set_repeat_count(&s_orbit_anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&s_orbit_anim);

    s_inited = true;
    lv_vendor_disp_unlock();
    return OPRT_OK;
}

void nav_display_set_state(disp_state_t st) {
    if (!s_inited) return;
    lv_color_t c = state_color(st);
    lv_vendor_disp_lock();
    lv_label_set_text(s_state_label, state_text(st));
    lv_obj_set_style_text_color(s_state_label, c, LV_PART_MAIN);
    /* recolor the eye to match state */
    lv_obj_set_style_bg_color(s_iris_mid, c, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_iris_mid, c, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(s_iris_mid, c, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_iris_inner_dark, c, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_pupil, c, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(s_pupil, c, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_orbit, c, LV_PART_INDICATOR);
    lv_vendor_disp_unlock();
}

void nav_display_set_text(const char *text) {
    if (!s_inited || !text) return;
    lv_vendor_disp_lock();
    snprintf(s_resp_buf, sizeof(s_resp_buf), "%s", text);
    lv_label_set_text(s_response_label, s_resp_buf);
    lv_vendor_disp_unlock();
}

void nav_display_stream_text(const char *text, uint32_t wpm) {
    (void)wpm;
    nav_display_set_text(text);
}

void nav_display_set_wifi(bool connected) {
    if (!s_inited) return;
    lv_vendor_disp_lock();
    if (connected) {
        lv_label_set_text(s_wifi_label, LV_SYMBOL_WIFI " online");
        lv_obj_set_style_text_color(s_wifi_label, COL_SPEAKING, LV_PART_MAIN);
    } else {
        lv_label_set_text(s_wifi_label, LV_SYMBOL_WIFI " offline");
        lv_obj_set_style_text_color(s_wifi_label, COL_DIM, LV_PART_MAIN);
    }
    lv_vendor_disp_unlock();
}

void nav_display_set_ip(const char *ip) {
    if (!s_inited || !ip) return;
    lv_vendor_disp_lock();
    lv_label_set_text(s_ip_label, ip);
    lv_vendor_disp_unlock();
}

void nav_display_set_ssid(const char *ssid) {
    if (!s_inited || !ssid) return;
    lv_vendor_disp_lock();
    lv_label_set_text(s_state_label, ssid);
    lv_vendor_disp_unlock();
}

static void (*s_tap_cb)(void) = NULL;
static void s_screen_click_cb(lv_event_t *e) {
    (void)e;
    if (s_tap_cb) s_tap_cb();
}
void nav_display_set_tap_cb(void (*cb)(void)) {
    s_tap_cb = cb;
    if (!s_inited || !s_screen) return;
    lv_vendor_disp_lock();
    lv_obj_add_flag(s_screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_screen, s_screen_click_cb, LV_EVENT_CLICKED, NULL);
    lv_vendor_disp_unlock();
}
