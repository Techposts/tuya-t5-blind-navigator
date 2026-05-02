/* nav_display.c — Bionic Eye UI for blind_navigator
 * Solid black bg (no vignetting). Multi-ring mechanical iris with glowing pupil.
 * State-driven color. Top status bar. Bottom dark panel with large readable
 * response text in 24pt for visual confirmation.
 */
#include "nav_display.h"
#include "iris_tokens.h"
#include "tal_api.h"
#include "lvgl.h"
#include "lv_vendor.h"
#include "lv_port_disp.h"
#include "board_com_api.h"
#include <string.h>
#include <stdio.h>

#define COL_BG          lv_color_hex(0x05070A)  /* IRIS bg.0 — deep obsidian */
#define COL_PANEL       lv_color_hex(0x0A0E14)  /* IRIS bg.1 — card overlay */
#define COL_FRAME       lv_color_hex(0x1C2430)  /* IRIS bg.line — hairline */
#define COL_DIM         lv_color_hex(0x7A8696)  /* IRIS fg.2 — tertiary */
#define COL_HUD         lv_color_hex(0x4FE3F0)  /* IRIS state.idle */
#define COL_IDLE        lv_color_hex(0x4FE3F0)  /* IRIS state.idle — cyan */
#define COL_LISTENING   lv_color_hex(0xFFB347)  /* IRIS state.capture — amber */
#define COL_PROCESSING  lv_color_hex(0xE879F9)  /* IRIS state.think — magenta */
#define COL_SPEAKING    lv_color_hex(0x4ADE80)  /* IRIS state.speak — green */
#define COL_ERROR       lv_color_hex(0xFF5C5C)  /* IRIS state.error — red */

#include "iris_fonts.h"

static bool s_inited = false;
static lv_obj_t *s_screen;
static lv_obj_t *s_iris_outer;       /* outermost chrome ring */
static lv_obj_t *s_iris_mid;         /* iris fill (state color) */
static lv_obj_t *s_iris_inner_dark;  /* dark ring inside iris */
static lv_obj_t *s_pupil;            /* pupil (small, with glow) */
static lv_obj_t *s_orbit;            /* one rotating thin arc */
static lv_obj_t *s_state_label;
/* CP8: response panel removed -- output is audio-only via TTS */
static lv_obj_t *s_wifi_label;
static lv_obj_t *s_ip_label;
static lv_obj_t *s_clock_label;  /* CP16: HH:MM in status bar */
static lv_anim_t s_orbit_anim;
static lv_obj_t *s_arc_mid;     /* IRIS Cp2: CCW mid arc */
static lv_obj_t *s_arc_inner;   /* IRIS Cp2: CW inner arc */
static lv_anim_t s_anim_mid;
static lv_anim_t s_anim_inner;
static lv_obj_t *s_mode_hints;       /* IRIS Cp3: bottom mode-hint row (idle only) */
static lv_obj_t *s_audio_bars;       /* IRIS Cp3: speaking-state audio waveform */
static lv_obj_t *s_idle_tag;         /* IRIS Cp6: READY tag */
static lv_obj_t *s_idle_cta;         /* IRIS Cp6: Tap to navigate big text */
/* CP8: 21-bar audio waveform (was 5 bars) */
static lv_anim_t s_bar_anims[21];
static lv_obj_t *s_bars[21];

/* CP8: per-state screen layers -- Claude Design fidelity */
static lv_obj_t *s_layer_capture;
static lv_obj_t *s_capture_bracket[4];
static lv_obj_t *s_capture_header;
static lv_obj_t *s_capture_meta;

static lv_obj_t *s_layer_think;
static lv_obj_t *s_think_header;
static lv_obj_t *s_think_bar_row[4];
static lv_obj_t *s_think_bar_fill[4];
static lv_anim_t s_think_bar_anims[4];

static lv_obj_t *s_layer_error;
static lv_obj_t *s_error_header;
static lv_obj_t *s_error_title;
static lv_obj_t *s_error_subtitle;
static lv_obj_t *s_error_pill;

static lv_obj_t *s_layer_boot;
static lv_obj_t *s_boot_progress;
static lv_timer_t *s_boot_timer = NULL;

/* CP9: speaking screen footer hint */
static lv_obj_t *s_speak_footer;

/* CP10: structured 4-field response display for speaking state */
static lv_obj_t *s_speak_field_label[4];
static lv_obj_t *s_speak_field_value[4];

/* CP10: eye compaction memory (set in init, used by iris_set_eye_compact) */
static int s_eye_size_normal = 0;
static int s_eye_y_normal = 0;
static int s_audio_y_normal = 0;  /* CP10b */

/* CP11: retina-scanner enhancements */
static lv_obj_t *s_iris_ticks_container;     /* parent for 24 radial tick marks */
static lv_obj_t *s_iris_ticks[24];           /* small rotated rectangles */
static lv_anim_t s_pupil_pulse_anim;         /* breathing pulse on the white pupil */

/* CP9: settings overlay (screen #8 per Claude Design) */
static lv_obj_t *s_layer_settings;
static lv_obj_t *s_mode_banner;       /* IRIS Cp4: transient mode-switch banner */
static lv_obj_t *s_mode_banner_label;
static lv_timer_t *s_mode_banner_timer;
static void (*s_swipe_up_cb)(void)   = NULL;  /* IRIS Cp5 */
static void (*s_swipe_down_cb)(void) = NULL;

static void rot_cb(void *obj, int32_t v) { lv_arc_set_rotation((lv_obj_t *)obj, v); }

/* CP11: pupil breathing pulse -- opacity 100->60->100 + shadow_width 30->18->30 */
static void pupil_pulse_cb(void *obj, int32_t v) {
    /* v: 0..255 (anim oscillates with playback). Map to opacity 153..255 (60-100%)
     * and shadow width 18..30 px. */
    lv_obj_t *p = (lv_obj_t *)obj;
    int opa = 153 + (v * 102 / 255);
    int sh  = 18  + (v * 12  / 255);
    lv_obj_set_style_bg_opa(p, opa, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(p, sh, LV_PART_MAIN);
}
static void bar_h_cb(void *obj, int32_t v) {
    lv_obj_set_style_transform_scale_y((lv_obj_t *)obj, v, 0);
}

static void s_boot_hide_cb(lv_timer_t *t);  /* CP8: forward decl */

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
    int eye_y = (H - eye_size) / 2 - H / 12;  /* CP8: nudged down to clear idle CTA */
    s_eye_size_normal = eye_size;  /* CP10: remember for compact toggle */
    s_eye_y_normal    = eye_y;

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
    lv_obj_set_style_text_font(s_wifi_label, &font_iris_mono_xs_10, LV_PART_MAIN);
    lv_obj_align(s_wifi_label, LV_ALIGN_TOP_LEFT, 8, 6);

    s_ip_label = lv_label_create(s_screen);
    lv_label_set_text(s_ip_label, "");
    lv_obj_set_style_text_color(s_ip_label, COL_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_ip_label, &font_iris_mono_xs_10, LV_PART_MAIN);
    lv_obj_align(s_ip_label, LV_ALIGN_TOP_RIGHT, -8, 18);  /* CP16: shift down to make room for clock */

    /* CP16: HH:MM clock at top-right (above IP). Updates every 30s via lv_timer. */
    s_clock_label = lv_label_create(s_screen);
    lv_label_set_text(s_clock_label, "--:--");
    lv_obj_set_style_text_color(s_clock_label, lv_color_hex(0xC7CFDB), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_clock_label, &font_iris_mono_sm_12, LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(s_clock_label, 1, LV_PART_MAIN);
    lv_obj_align(s_clock_label, LV_ALIGN_TOP_RIGHT, -8, 4);

    /* IRIS Cp6: idle CTA stack — small mono tag + big text, both above the eye */
    s_idle_tag = lv_label_create(s_screen);
    lv_label_set_text(s_idle_tag, "READY  " IRIS_GLYPH_MIDDOT "  LISTENING");
    lv_obj_set_style_text_color(s_idle_tag, COL_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_idle_tag, &font_iris_mono_sm_12, LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(s_idle_tag, 4, LV_PART_MAIN);
    lv_obj_align(s_idle_tag, LV_ALIGN_TOP_MID, 0, 36);

    s_idle_cta = lv_label_create(s_screen);
    lv_label_set_text(s_idle_cta, "Tap to navigate");
    lv_obj_set_style_text_color(s_idle_cta, lv_color_hex(0xF4F6FA), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_idle_cta, &font_iris_status_24, LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(s_idle_cta, 0, LV_PART_MAIN);
    lv_obj_align(s_idle_cta, LV_ALIGN_TOP_MID, 0, 56);

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
    /* CP11: pupil is pure white -- the state color comes through as the radial glow shadow */
    lv_obj_set_style_bg_color(s_pupil, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_pupil, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_opa(s_pupil, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(s_pupil, COL_IDLE, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(s_pupil, 30, LV_PART_MAIN);
    lv_obj_set_style_shadow_spread(s_pupil, 4, LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(s_pupil, LV_OPA_80, LV_PART_MAIN);
    lv_obj_clear_flag(s_pupil, LV_OBJ_FLAG_SCROLLABLE);

    /* CP11: 24 radial tick marks around outer ring (retina-scanner halo) */
    s_iris_ticks_container = lv_obj_create(s_screen);
    int ticks_size = eye_size + 24;
    lv_obj_set_size(s_iris_ticks_container, ticks_size, ticks_size);
    lv_obj_align(s_iris_ticks_container, LV_ALIGN_TOP_MID, 0, eye_y - 12);
    lv_obj_set_style_bg_opa(s_iris_ticks_container, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_iris_ticks_container, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_iris_ticks_container, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_iris_ticks_container, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    {
        int half = ticks_size / 2;
        int tick_radius = half - 2;          /* distance from center to outer end of tick */
        int tick_len = 7;                    /* radial length of each tick */
        int tick_w = 1;
        for (int i = 0; i < 24; i++) {
            s_iris_ticks[i] = lv_obj_create(s_iris_ticks_container);
            lv_obj_set_size(s_iris_ticks[i], tick_w, tick_len);
            lv_obj_set_style_bg_color(s_iris_ticks[i], COL_DIM, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(s_iris_ticks[i], LV_OPA_60, LV_PART_MAIN);
            lv_obj_set_style_border_width(s_iris_ticks[i], 0, LV_PART_MAIN);
            lv_obj_set_style_pad_all(s_iris_ticks[i], 0, LV_PART_MAIN);
            lv_obj_set_style_radius(s_iris_ticks[i], 0, LV_PART_MAIN);
            lv_obj_clear_flag(s_iris_ticks[i], LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
            /* Position at top-center of container with the tick's outer edge near the rim */
            lv_obj_align(s_iris_ticks[i], LV_ALIGN_TOP_MID, 0, half - tick_radius);
            /* Pivot at container center so transform_angle rotates the tick around it */
            lv_obj_set_style_transform_pivot_x(s_iris_ticks[i], tick_w / 2, 0);
            lv_obj_set_style_transform_pivot_y(s_iris_ticks[i], tick_radius, 0);
            lv_obj_set_style_transform_angle(s_iris_ticks[i], i * 150, 0);  /* deciDegree */
        }
    }

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

    /* IRIS Cp2: middle counter-rotating arc inside chrome ring */
    s_arc_mid = lv_arc_create(s_screen);
    lv_obj_set_size(s_arc_mid, iris_size, iris_size);
    lv_obj_align(s_arc_mid, LV_ALIGN_TOP_MID, 0, eye_y + (eye_size - iris_size) / 2);
    lv_arc_set_bg_angles(s_arc_mid, 0, 360);
    lv_arc_set_angles(s_arc_mid, 0, 90);
    lv_obj_remove_style(s_arc_mid, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(s_arc_mid, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(s_arc_mid, 2, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(s_arc_mid, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_arc_mid, COL_IDLE, LV_PART_INDICATOR);

    /* IRIS Cp2: inner CW arc just outside the pupil */
    int inner_arc_size = inner_dark_size + 8;
    s_arc_inner = lv_arc_create(s_screen);
    lv_obj_set_size(s_arc_inner, inner_arc_size, inner_arc_size);
    lv_obj_align(s_arc_inner, LV_ALIGN_TOP_MID, 0, eye_y + (eye_size - inner_arc_size) / 2);
    lv_arc_set_bg_angles(s_arc_inner, 0, 360);
    lv_arc_set_angles(s_arc_inner, 0, 60);
    lv_obj_remove_style(s_arc_inner, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(s_arc_inner, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(s_arc_inner, 2, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(s_arc_inner, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_arc_inner, COL_IDLE, LV_PART_INDICATOR);

    /* State label below eye */
    s_state_label = lv_label_create(s_screen);
    lv_label_set_text(s_state_label, "IRIS");
    lv_obj_set_style_text_color(s_state_label, COL_IDLE, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_state_label, &font_iris_status_24, LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(s_state_label, 4, LV_PART_MAIN);
    lv_obj_align(s_state_label, LV_ALIGN_TOP_MID, 0, eye_y + eye_size + 14);



    /* IRIS Cp3: mode-hint row across the bottom (visible only when IDLE) */
    s_mode_hints = lv_obj_create(s_screen);
    lv_obj_set_size(s_mode_hints, W - 32, 56);
    lv_obj_align(s_mode_hints, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_set_style_bg_opa(s_mode_hints, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_mode_hints, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_mode_hints, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(s_mode_hints, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_mode_hints, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(s_mode_hints, LV_OBJ_FLAG_SCROLLABLE);
    {
        const char *icons[3]  = {IRIS_GLYPH_UP, "", IRIS_GLYPH_DOWN};
        const char *labels[3] = {"IDENTIFY", "NAVIGATE", "READ"};
        const char *subs[3]   = {"swipe up", "tap", "swipe down"};
        lv_color_t cols[3]    = {COL_PROCESSING, COL_IDLE, COL_LISTENING};
        for (int k = 0; k < 3; k++) {
            lv_obj_t *col = lv_obj_create(s_mode_hints);
            lv_obj_set_size(col, LV_PCT(33), LV_SIZE_CONTENT);
            lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_border_width(col, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_all(col, 4, LV_PART_MAIN);
            lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            if (k == 1) {
                /* Navigate column: a drawn filled circle dot instead of a glyph */
                lv_obj_t *dot = lv_obj_create(col);
                lv_obj_set_size(dot, 8, 8);
                lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
                lv_obj_set_style_bg_color(dot, cols[k], LV_PART_MAIN);
                lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, LV_PART_MAIN);
                lv_obj_set_style_border_width(dot, 0, LV_PART_MAIN);
                lv_obj_set_style_shadow_color(dot, cols[k], LV_PART_MAIN);
                lv_obj_set_style_shadow_width(dot, 8, LV_PART_MAIN);
                lv_obj_set_style_shadow_opa(dot, LV_OPA_70, LV_PART_MAIN);
                lv_obj_set_style_pad_all(dot, 0, LV_PART_MAIN);
            } else {
                lv_obj_t *ic = lv_label_create(col);
                lv_label_set_text(ic, icons[k]);
                lv_obj_set_style_text_color(ic, cols[k], LV_PART_MAIN);
                lv_obj_set_style_text_font(ic, &font_iris_icons_24, LV_PART_MAIN);
            }
            lv_obj_t *lb = lv_label_create(col);
            lv_label_set_text(lb, labels[k]);
            lv_obj_set_style_text_color(lb, cols[k], LV_PART_MAIN);
            lv_obj_set_style_text_font(lb, &font_iris_mono_sm_12, LV_PART_MAIN);
            lv_obj_set_style_text_letter_space(lb, 2, LV_PART_MAIN);
            lv_obj_t *sb = lv_label_create(col);
            lv_label_set_text(sb, subs[k]);
            lv_obj_set_style_text_color(sb, COL_DIM, LV_PART_MAIN);
            lv_obj_set_style_text_font(sb, &font_iris_mono_xs_10, LV_PART_MAIN);
            lv_obj_set_style_text_opa(sb, LV_OPA_60, LV_PART_MAIN);
        }
    }

    /* CP8: audio waveform (21 bars per Claude Design), visible only during SPEAKING */
    s_audio_bars = lv_obj_create(s_screen);
    /* CP10: narrower container so 21 bars sit closer together */
    lv_obj_set_size(s_audio_bars, 200, 48);
    lv_obj_set_style_pad_column(s_audio_bars, 2, LV_PART_MAIN);
    /* CP10b: remember normal audio bar Y so iris_set_eye_compact() can restore it */
    int audio_y_normal = eye_y + eye_size + 50;
    s_audio_y_normal = audio_y_normal;
    lv_obj_align(s_audio_bars, LV_ALIGN_TOP_MID, 0, audio_y_normal);
    lv_obj_set_style_bg_opa(s_audio_bars, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_audio_bars, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_audio_bars, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(s_audio_bars, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_audio_bars, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(s_audio_bars, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_audio_bars, LV_OBJ_FLAG_HIDDEN);

    /* CP9: speaking-state footer hint */
    s_speak_footer = lv_label_create(s_screen);
    lv_label_set_text(s_speak_footer, "double-tap to repeat");
    lv_obj_set_style_text_color(s_speak_footer, lv_color_hex(0x4A5363), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_speak_footer, &font_iris_mono_xs_10, LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(s_speak_footer, 4, LV_PART_MAIN);
    lv_obj_align(s_speak_footer, LV_ALIGN_BOTTOM_MID, 0, -16);
    lv_obj_add_flag(s_speak_footer, LV_OBJ_FLAG_HIDDEN);

    /* CP10/CP11b: structured 4-field speak response widgets.
     * Init configures fonts/colors/widths only. Vertical positioning happens
     * dynamically in nav_display_set_speak_response so wrap-induced overflow
     * (especially on the BIG GREEN field 3 ACTION) pushes field 4 down. */
    for (int i = 0; i < 4; i++) {
        bool big_green = (i == 2);
        s_speak_field_label[i] = lv_label_create(s_screen);
        lv_label_set_text(s_speak_field_label[i], "");
        lv_obj_set_style_text_color(s_speak_field_label[i], COL_DIM, LV_PART_MAIN);
        lv_obj_set_style_text_font(s_speak_field_label[i], &font_iris_mono_xs_10, LV_PART_MAIN);
        lv_obj_set_style_text_letter_space(s_speak_field_label[i], 5, LV_PART_MAIN);
        lv_obj_set_width(s_speak_field_label[i], W - 36);
        lv_obj_add_flag(s_speak_field_label[i], LV_OBJ_FLAG_HIDDEN);

        s_speak_field_value[i] = lv_label_create(s_screen);
        lv_label_set_text(s_speak_field_value[i], "");
        lv_label_set_long_mode(s_speak_field_value[i], LV_LABEL_LONG_WRAP);
        lv_obj_set_width(s_speak_field_value[i], W - 36);
        if (big_green) {
            lv_obj_set_style_text_color(s_speak_field_value[i], COL_SPEAKING, LV_PART_MAIN);
            lv_obj_set_style_text_font(s_speak_field_value[i], &font_iris_status_24, LV_PART_MAIN);
        } else {
            lv_obj_set_style_text_color(s_speak_field_value[i], lv_color_hex(0xF4F6FA), LV_PART_MAIN);
            lv_obj_set_style_text_font(s_speak_field_value[i], &font_iris_body_16, LV_PART_MAIN);
        }
        lv_obj_add_flag(s_speak_field_value[i], LV_OBJ_FLAG_HIDDEN);
    }
    for (int b = 0; b < 21; b++) {
        s_bars[b] = lv_obj_create(s_audio_bars);
        /* CP10: thicker (5px), more rounded, closer together */
        lv_obj_set_size(s_bars[b], 5, 10 + (10 - (b > 10 ? 20 - b : b)) * 3);
        lv_obj_set_style_bg_color(s_bars[b], COL_SPEAKING, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_bars[b], LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(s_bars[b], 3, LV_PART_MAIN);  /* CP10: more rounded */
        lv_obj_set_style_border_width(s_bars[b], 0, LV_PART_MAIN);
        lv_obj_set_style_shadow_color(s_bars[b], COL_SPEAKING, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(s_bars[b], 6, LV_PART_MAIN);
        lv_obj_set_style_shadow_opa(s_bars[b], LV_OPA_50, LV_PART_MAIN);
        lv_obj_set_style_transform_pivot_x(s_bars[b], LV_PCT(50), 0);
        lv_obj_set_style_transform_pivot_y(s_bars[b], LV_PCT(50), 0);
        lv_anim_init(&s_bar_anims[b]);
        lv_anim_set_var(&s_bar_anims[b], s_bars[b]);
        lv_anim_set_values(&s_bar_anims[b], 80, 256);
        lv_anim_set_exec_cb(&s_bar_anims[b], bar_h_cb);
        int base_ms = 500 + (b > 10 ? 20 - b : b) * 30;
        lv_anim_set_time(&s_bar_anims[b], base_ms);
        lv_anim_set_playback_time(&s_bar_anims[b], base_ms);
        lv_anim_set_repeat_count(&s_bar_anims[b], LV_ANIM_REPEAT_INFINITE);
    }

    /* IRIS Cp4: mode-switch banner (hidden by default) */
    s_mode_banner = lv_obj_create(s_screen);
    lv_obj_set_size(s_mode_banner, W - 32, 64);
    lv_obj_align(s_mode_banner, LV_ALIGN_TOP_MID, 0, 28);
    lv_obj_set_style_bg_color(s_mode_banner, COL_PANEL, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_mode_banner, LV_OPA_90, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_mode_banner, COL_IDLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_mode_banner, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(s_mode_banner, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_mode_banner, 12, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(s_mode_banner, COL_IDLE, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(s_mode_banner, 24, LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(s_mode_banner, LV_OPA_30, LV_PART_MAIN);
    lv_obj_clear_flag(s_mode_banner, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_mode_banner, LV_OBJ_FLAG_HIDDEN);
    s_mode_banner_label = lv_label_create(s_mode_banner);
    lv_label_set_text(s_mode_banner_label, "");
    lv_obj_set_style_text_color(s_mode_banner_label, COL_IDLE, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_mode_banner_label, &font_iris_title_32, LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(s_mode_banner_label, 4, LV_PART_MAIN);
    lv_obj_center(s_mode_banner_label);

    /* ===== CP8: per-state screen layers ===== */

    /* CAPTURE LAYER */
    s_layer_capture = lv_obj_create(s_screen);
    lv_obj_set_size(s_layer_capture, W, H);
    lv_obj_align(s_layer_capture, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_opa(s_layer_capture, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_layer_capture, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_layer_capture, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_layer_capture, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_layer_capture, LV_OBJ_FLAG_HIDDEN);

    {
        const int inset = 24, len = 22, weight = 2;
        const lv_align_t aligns[4] = {
            LV_ALIGN_TOP_LEFT, LV_ALIGN_TOP_RIGHT,
            LV_ALIGN_BOTTOM_LEFT, LV_ALIGN_BOTTOM_RIGHT,
        };
        for (int c = 0; c < 4; c++) {
            s_capture_bracket[c] = lv_obj_create(s_layer_capture);
            lv_obj_set_size(s_capture_bracket[c], len, len);
            int xo = (c % 2 == 0) ? inset : -inset;
            int yo = (c < 2)      ? inset : -inset;
            lv_obj_align(s_capture_bracket[c], aligns[c], xo, yo);
            lv_obj_set_style_bg_opa(s_capture_bracket[c], LV_OPA_TRANSP, LV_PART_MAIN);
            int t = (c < 2) ? weight : 0;
            int b = (c >= 2) ? weight : 0;
            int l = (c % 2 == 0) ? weight : 0;
            int r = (c % 2 == 1) ? weight : 0;
            lv_obj_set_style_border_color(s_capture_bracket[c], COL_LISTENING, LV_PART_MAIN);
            lv_obj_set_style_border_side(s_capture_bracket[c],
                (t ? LV_BORDER_SIDE_TOP : 0) |
                (b ? LV_BORDER_SIDE_BOTTOM : 0) |
                (l ? LV_BORDER_SIDE_LEFT : 0) |
                (r ? LV_BORDER_SIDE_RIGHT : 0), LV_PART_MAIN);
            lv_obj_set_style_border_width(s_capture_bracket[c], weight, LV_PART_MAIN);
            lv_obj_set_style_border_opa(s_capture_bracket[c], LV_OPA_70, LV_PART_MAIN);
            lv_obj_set_style_radius(s_capture_bracket[c], 0, LV_PART_MAIN);
            lv_obj_set_style_pad_all(s_capture_bracket[c], 0, LV_PART_MAIN);
            lv_obj_clear_flag(s_capture_bracket[c], LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        }
    }

    s_capture_header = lv_label_create(s_layer_capture);
    lv_label_set_text(s_capture_header, IRIS_GLYPH_DOT "  CAPTURING");
    lv_obj_set_style_text_color(s_capture_header, COL_LISTENING, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_capture_header, &font_iris_mono_sm_12, LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(s_capture_header, 5, LV_PART_MAIN);
    lv_obj_align(s_capture_header, LV_ALIGN_TOP_MID, 0, 56);

    s_capture_meta = lv_label_create(s_layer_capture);
    lv_label_set_text(s_capture_meta,
        "F / 1.8  " IRIS_GLYPH_MIDDOT "  ISO 400  " IRIS_GLYPH_MIDDOT "  12 MP");
    lv_obj_set_style_text_color(s_capture_meta, COL_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_capture_meta, &font_iris_mono_sm_12, LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(s_capture_meta, 2, LV_PART_MAIN);
    lv_obj_align(s_capture_meta, LV_ALIGN_BOTTOM_MID, 0, -60);

    /* THINK LAYER */
    s_layer_think = lv_obj_create(s_screen);
    lv_obj_set_size(s_layer_think, W, H);
    lv_obj_align(s_layer_think, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_opa(s_layer_think, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_layer_think, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_layer_think, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_layer_think, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_layer_think, LV_OBJ_FLAG_HIDDEN);

    s_think_header = lv_label_create(s_layer_think);
    lv_label_set_text(s_think_header, "ANALYZING SCENE");
    lv_obj_set_style_text_color(s_think_header, COL_PROCESSING, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_think_header, &font_iris_mono_sm_12, LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(s_think_header, 5, LV_PART_MAIN);
    lv_obj_align(s_think_header, LV_ALIGN_TOP_MID, 0, 52);

    {
        const char *bar_labels[4] = {"OBJECTS", "DEPTH", "OCR", "HAZARDS"};
        const int bar_pcts[4]   = {78, 62, 41, 88};
        const int row_h = 14;
        for (int r = 0; r < 4; r++) {
            int y_off = -52 - (3 - r) * (row_h + 6);
            s_think_bar_row[r] = lv_obj_create(s_layer_think);
            lv_obj_set_size(s_think_bar_row[r], W - 64, row_h);
            lv_obj_align(s_think_bar_row[r], LV_ALIGN_BOTTOM_MID, 0, y_off);
            lv_obj_set_style_bg_opa(s_think_bar_row[r], LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_border_width(s_think_bar_row[r], 0, LV_PART_MAIN);
            lv_obj_set_style_pad_all(s_think_bar_row[r], 0, LV_PART_MAIN);
            lv_obj_clear_flag(s_think_bar_row[r], LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

            lv_obj_t *lab = lv_label_create(s_think_bar_row[r]);
            lv_label_set_text(lab, bar_labels[r]);
            lv_obj_set_style_text_color(lab, COL_DIM, LV_PART_MAIN);
            lv_obj_set_style_text_font(lab, &font_iris_mono_xs_10, LV_PART_MAIN);
            lv_obj_set_style_text_letter_space(lab, 2, LV_PART_MAIN);
            lv_obj_align(lab, LV_ALIGN_LEFT_MID, 0, 0);

            lv_obj_t *track = lv_obj_create(s_think_bar_row[r]);
            int track_w = (W - 64) - 88;
            lv_obj_set_size(track, track_w, 3);
            lv_obj_align(track, LV_ALIGN_LEFT_MID, 64, 0);
            lv_obj_set_style_bg_color(track, COL_FRAME, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(track, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_border_width(track, 0, LV_PART_MAIN);
            lv_obj_set_style_radius(track, 2, LV_PART_MAIN);
            lv_obj_set_style_pad_all(track, 0, LV_PART_MAIN);
            lv_obj_clear_flag(track, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

            int fill_w = track_w * bar_pcts[r] / 100;
            s_think_bar_fill[r] = lv_obj_create(track);
            lv_obj_set_size(s_think_bar_fill[r], fill_w, 3);
            lv_obj_align(s_think_bar_fill[r], LV_ALIGN_LEFT_MID, 0, 0);
            lv_obj_set_style_bg_color(s_think_bar_fill[r], COL_PROCESSING, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(s_think_bar_fill[r], LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_border_width(s_think_bar_fill[r], 0, LV_PART_MAIN);
            lv_obj_set_style_radius(s_think_bar_fill[r], 2, LV_PART_MAIN);
            lv_obj_set_style_shadow_color(s_think_bar_fill[r], COL_PROCESSING, LV_PART_MAIN);
            lv_obj_set_style_shadow_width(s_think_bar_fill[r], 6, LV_PART_MAIN);
            lv_obj_set_style_shadow_opa(s_think_bar_fill[r], LV_OPA_60, LV_PART_MAIN);
            lv_obj_set_style_pad_all(s_think_bar_fill[r], 0, LV_PART_MAIN);
            lv_obj_clear_flag(s_think_bar_fill[r], LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

            lv_anim_init(&s_think_bar_anims[r]);
            lv_anim_set_var(&s_think_bar_anims[r], s_think_bar_fill[r]);
            lv_anim_set_values(&s_think_bar_anims[r], 80, 255);
            lv_anim_set_exec_cb(&s_think_bar_anims[r], bar_h_cb);
            lv_anim_set_time(&s_think_bar_anims[r], 1400 + r * 200);
            lv_anim_set_playback_time(&s_think_bar_anims[r], 1400 + r * 200);
            lv_anim_set_repeat_count(&s_think_bar_anims[r], LV_ANIM_REPEAT_INFINITE);

            char pct_buf[8]; snprintf(pct_buf, sizeof(pct_buf), "%d%%", bar_pcts[r]);
            lv_obj_t *pct = lv_label_create(s_think_bar_row[r]);
            lv_label_set_text(pct, pct_buf);
            lv_obj_set_style_text_color(pct, lv_color_hex(0xC7CFDB), LV_PART_MAIN);
            lv_obj_set_style_text_font(pct, &font_iris_mono_xs_10, LV_PART_MAIN);
            lv_obj_align(pct, LV_ALIGN_RIGHT_MID, 0, 0);
        }
    }

    /* ERROR LAYER */
    s_layer_error = lv_obj_create(s_screen);
    lv_obj_set_size(s_layer_error, W, H);
    lv_obj_align(s_layer_error, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_opa(s_layer_error, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_layer_error, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_layer_error, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_layer_error, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_layer_error, LV_OBJ_FLAG_HIDDEN);

    s_error_header = lv_label_create(s_layer_error);
    lv_label_set_text(s_error_header, "X  NO IMAGE");
    lv_obj_set_style_text_color(s_error_header, COL_ERROR, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_error_header, &font_iris_mono_sm_12, LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(s_error_header, 5, LV_PART_MAIN);
    lv_obj_align(s_error_header, LV_ALIGN_TOP_MID, 0, 56);

    s_error_title = lv_label_create(s_layer_error);
    lv_label_set_text(s_error_title, "Lens may be covered.");
    lv_obj_set_style_text_color(s_error_title, lv_color_hex(0xF4F6FA), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_error_title, &font_iris_status_24, LV_PART_MAIN);
    lv_obj_align(s_error_title, LV_ALIGN_BOTTOM_MID, 0, -120);

    s_error_subtitle = lv_label_create(s_layer_error);
    lv_label_set_text(s_error_subtitle, "Wipe the camera and tap to retry.");
    lv_obj_set_style_text_color(s_error_subtitle, lv_color_hex(0xC7CFDB), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_error_subtitle, &font_iris_body_16, LV_PART_MAIN);
    lv_obj_align(s_error_subtitle, LV_ALIGN_BOTTOM_MID, 0, -90);

    s_error_pill = lv_obj_create(s_layer_error);
    lv_obj_set_size(s_error_pill, 200, 40);
    lv_obj_align(s_error_pill, LV_ALIGN_BOTTOM_MID, 0, -32);
    lv_obj_set_style_bg_color(s_error_pill, COL_ERROR, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_error_pill, LV_OPA_10, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_error_pill, COL_ERROR, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_error_pill, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(s_error_pill, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_error_pill, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_error_pill, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *pill_lbl = lv_label_create(s_error_pill);
    lv_label_set_text(pill_lbl, "TAP TO RETRY");
    lv_obj_set_style_text_color(pill_lbl, COL_ERROR, LV_PART_MAIN);
    lv_obj_set_style_text_font(pill_lbl, &font_iris_mono_sm_12, LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(pill_lbl, 4, LV_PART_MAIN);
    lv_obj_center(pill_lbl);

    /* ---------- SETTINGS OVERLAY (screen #8) ---------- */
    s_layer_settings = lv_obj_create(s_screen);
    lv_obj_set_size(s_layer_settings, W, H);
    lv_obj_align(s_layer_settings, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(s_layer_settings, COL_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_layer_settings, LV_OPA_90, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_layer_settings, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_layer_settings, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_layer_settings, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_layer_settings, LV_OBJ_FLAG_HIDDEN);

    /* SETTINGS header */
    lv_obj_t *settings_hdr = lv_label_create(s_layer_settings);
    lv_label_set_text(settings_hdr, "SETTINGS");
    lv_obj_set_style_text_color(settings_hdr, COL_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(settings_hdr, &font_iris_mono_sm_12, LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(settings_hdr, 5, LV_PART_MAIN);
    lv_obj_align(settings_hdr, LV_ALIGN_TOP_MID, 0, 38);

    /* Sliders + pickers stacked vertically */
    {
        const struct { const char *label; int value_pct; const char *suffix; } sliders[2] = {
            {"VOLUME",     72, "%"},
            {"BRIGHTNESS", 45, "%"},
        };
        int row_y = 78;
        for (int i = 0; i < 2; i++) {
            lv_obj_t *card = lv_obj_create(s_layer_settings);
            lv_obj_set_size(card, W - 32, 56);
            lv_obj_align(card, LV_ALIGN_TOP_MID, 0, row_y + i * 64);
            lv_obj_set_style_bg_color(card, COL_PANEL, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(card, LV_OPA_70, LV_PART_MAIN);
            lv_obj_set_style_border_color(card, COL_FRAME, LV_PART_MAIN);
            lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
            lv_obj_set_style_radius(card, 12, LV_PART_MAIN);
            lv_obj_set_style_pad_all(card, 12, LV_PART_MAIN);
            lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

            lv_obj_t *lab = lv_label_create(card);
            lv_label_set_text(lab, sliders[i].label);
            lv_obj_set_style_text_color(lab, lv_color_hex(0xC7CFDB), LV_PART_MAIN);
            lv_obj_set_style_text_font(lab, &font_iris_mono_sm_12, LV_PART_MAIN);
            lv_obj_set_style_text_letter_space(lab, 4, LV_PART_MAIN);
            lv_obj_align(lab, LV_ALIGN_TOP_LEFT, 0, 0);

            char vbuf[16]; snprintf(vbuf, sizeof(vbuf), "%d%s", sliders[i].value_pct, sliders[i].suffix);
            lv_obj_t *vl = lv_label_create(card);
            lv_label_set_text(vl, vbuf);
            lv_obj_set_style_text_color(vl, lv_color_hex(0xF4F6FA), LV_PART_MAIN);
            lv_obj_set_style_text_font(vl, &font_iris_mono_sm_12, LV_PART_MAIN);
            lv_obj_align(vl, LV_ALIGN_TOP_RIGHT, 0, 0);

            lv_obj_t *track = lv_obj_create(card);
            lv_obj_set_size(track, W - 32 - 24, 4);
            lv_obj_align(track, LV_ALIGN_BOTTOM_MID, 0, -6);
            lv_obj_set_style_bg_color(track, COL_FRAME, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(track, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_border_width(track, 0, LV_PART_MAIN);
            lv_obj_set_style_radius(track, 2, LV_PART_MAIN);
            lv_obj_set_style_pad_all(track, 0, LV_PART_MAIN);
            lv_obj_clear_flag(track, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

            lv_obj_t *fill = lv_obj_create(track);
            int track_w = W - 32 - 24;
            lv_obj_set_size(fill, track_w * sliders[i].value_pct / 100, 4);
            lv_obj_align(fill, LV_ALIGN_LEFT_MID, 0, 0);
            lv_obj_set_style_bg_color(fill, COL_IDLE, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_shadow_color(fill, COL_IDLE, LV_PART_MAIN);
            lv_obj_set_style_shadow_width(fill, 6, LV_PART_MAIN);
            lv_obj_set_style_shadow_opa(fill, LV_OPA_60, LV_PART_MAIN);
            lv_obj_set_style_border_width(fill, 0, LV_PART_MAIN);
            lv_obj_set_style_radius(fill, 2, LV_PART_MAIN);
            lv_obj_set_style_pad_all(fill, 0, LV_PART_MAIN);
            lv_obj_clear_flag(fill, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        }

        /* Picker rows: LANGUAGE + VOICE */
        const struct { const char *label; const char *opts[4]; int n; int active; } pickers[2] = {
            {"LANGUAGE", {"EN", "ES", "HI", "AR"}, 4, 0},
            {"VOICE",    {"LO", "MID", "HI", NULL}, 3, 1},
        };
        for (int p = 0; p < 2; p++) {
            int yoff = row_y + 128 + p * 64;
            lv_obj_t *card = lv_obj_create(s_layer_settings);
            lv_obj_set_size(card, W - 32, 56);
            lv_obj_align(card, LV_ALIGN_TOP_MID, 0, yoff);
            lv_obj_set_style_bg_color(card, COL_PANEL, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(card, LV_OPA_70, LV_PART_MAIN);
            lv_obj_set_style_border_color(card, COL_FRAME, LV_PART_MAIN);
            lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
            lv_obj_set_style_radius(card, 12, LV_PART_MAIN);
            lv_obj_set_style_pad_all(card, 10, LV_PART_MAIN);
            lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

            lv_obj_t *lab = lv_label_create(card);
            lv_label_set_text(lab, pickers[p].label);
            lv_obj_set_style_text_color(lab, lv_color_hex(0xC7CFDB), LV_PART_MAIN);
            lv_obj_set_style_text_font(lab, &font_iris_mono_sm_12, LV_PART_MAIN);
            lv_obj_set_style_text_letter_space(lab, 4, LV_PART_MAIN);
            lv_obj_align(lab, LV_ALIGN_TOP_LEFT, 0, 0);

            int chip_w = (W - 32 - 24) / pickers[p].n - 4;
            int chip_x = 0;
            for (int o = 0; o < pickers[p].n; o++) {
                lv_obj_t *chip = lv_obj_create(card);
                lv_obj_set_size(chip, chip_w, 22);
                lv_obj_align(chip, LV_ALIGN_BOTTOM_LEFT, chip_x, 0);
                bool on = (o == pickers[p].active);
                lv_obj_set_style_bg_color(chip, on ? COL_IDLE : COL_PANEL, LV_PART_MAIN);
                lv_obj_set_style_bg_opa(chip, on ? LV_OPA_20 : LV_OPA_TRANSP, LV_PART_MAIN);
                lv_obj_set_style_border_color(chip, on ? COL_IDLE : COL_FRAME, LV_PART_MAIN);
                lv_obj_set_style_border_width(chip, 1, LV_PART_MAIN);
                lv_obj_set_style_radius(chip, 6, LV_PART_MAIN);
                lv_obj_set_style_pad_all(chip, 0, LV_PART_MAIN);
                lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

                lv_obj_t *txt = lv_label_create(chip);
                lv_label_set_text(txt, pickers[p].opts[o]);
                lv_obj_set_style_text_color(txt, on ? COL_IDLE : COL_DIM, LV_PART_MAIN);
                lv_obj_set_style_text_font(txt, &font_iris_mono_sm_12, LV_PART_MAIN);
                lv_obj_set_style_text_letter_space(txt, 2, LV_PART_MAIN);
                lv_obj_center(txt);

                chip_x += chip_w + 4;
            }
        }
    }

    /* SETTINGS footer */
    lv_obj_t *settings_ftr = lv_label_create(s_layer_settings);
    lv_label_set_text(settings_ftr, "long-press to close");
    lv_obj_set_style_text_color(settings_ftr, lv_color_hex(0x4A5363), LV_PART_MAIN);
    lv_obj_set_style_text_font(settings_ftr, &font_iris_mono_xs_10, LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(settings_ftr, 4, LV_PART_MAIN);
    lv_obj_align(settings_ftr, LV_ALIGN_BOTTOM_MID, 0, -22);

    /* BOOT SPLASH LAYER (auto-hides after 2s) */
    s_layer_boot = lv_obj_create(s_screen);
    lv_obj_set_size(s_layer_boot, W, H);
    lv_obj_align(s_layer_boot, LV_ALIGN_TOP_LEFT, 0, 0);
    /* CP11: transparent boot layer -- the bionic eye underneath shows through
     * with its slow rotating arcs. Boot layer only adds title + tagline + footer
     * + progress hairline. Eye animations are already running, so user sees a
     * gently rotating retina scanner behind the splash text. */
    lv_obj_set_style_bg_opa(s_layer_boot, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_layer_boot, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_layer_boot, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_layer_boot, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *boot_title = lv_label_create(s_layer_boot);
    lv_label_set_text(boot_title, "IRIS");
    lv_obj_set_style_text_color(boot_title, lv_color_hex(0xF4F6FA), LV_PART_MAIN);
    lv_obj_set_style_text_font(boot_title, &font_iris_title_32, LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(boot_title, 8, LV_PART_MAIN);
    /* CP11: positioned below the eye so the slowly-rotating iris is visible above */
    lv_obj_align(boot_title, LV_ALIGN_TOP_MID, 0, 305);

    lv_obj_t *boot_tag = lv_label_create(s_layer_boot);
    lv_label_set_text(boot_tag, "VISION CO-PILOT  " IRIS_GLYPH_MIDDOT "  v2.3.1");
    lv_obj_set_style_text_color(boot_tag, COL_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(boot_tag, &font_iris_mono_sm_12, LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(boot_tag, 4, LV_PART_MAIN);
    lv_obj_align(boot_tag, LV_ALIGN_TOP_MID, 0, 348);

    lv_obj_t *boot_footer = lv_label_create(s_layer_boot);
    lv_label_set_text(boot_footer, "CALIBRATING SENSORS");
    lv_obj_set_style_text_color(boot_footer, COL_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(boot_footer, &font_iris_mono_xs_10, LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(boot_footer, 4, LV_PART_MAIN);
    lv_obj_align(boot_footer, LV_ALIGN_BOTTOM_MID, 0, -32);

    lv_obj_t *boot_track = lv_obj_create(s_layer_boot);
    lv_obj_set_size(boot_track, W - 80, 1);
    lv_obj_align(boot_track, LV_ALIGN_BOTTOM_MID, 0, -56);
    lv_obj_set_style_bg_color(boot_track, COL_FRAME, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(boot_track, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(boot_track, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(boot_track, 0, LV_PART_MAIN);
    lv_obj_clear_flag(boot_track, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    s_boot_progress = lv_obj_create(boot_track);
    lv_obj_set_size(s_boot_progress, (W - 80) * 70 / 100, 1);
    lv_obj_align(s_boot_progress, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(s_boot_progress, COL_IDLE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_boot_progress, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(s_boot_progress, COL_IDLE, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(s_boot_progress, 6, LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(s_boot_progress, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_boot_progress, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_boot_progress, 0, LV_PART_MAIN);

    /* IRIS Cp2: 3 concentric arc animations with idle defaults */
    lv_anim_init(&s_orbit_anim);
    lv_anim_set_var(&s_orbit_anim, s_orbit);
    lv_anim_set_values(&s_orbit_anim, 0, 3599);
    lv_anim_set_exec_cb(&s_orbit_anim, rot_cb);
    lv_anim_set_time(&s_orbit_anim, 18000);  /* idle outer CW */
    lv_anim_set_repeat_count(&s_orbit_anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&s_orbit_anim);

    lv_anim_init(&s_anim_mid);
    lv_anim_set_var(&s_anim_mid, s_arc_mid);
    lv_anim_set_values(&s_anim_mid, 3599, 0);  /* CCW */
    lv_anim_set_exec_cb(&s_anim_mid, rot_cb);
    lv_anim_set_time(&s_anim_mid, 28000);  /* idle mid */
    lv_anim_set_repeat_count(&s_anim_mid, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&s_anim_mid);

    lv_anim_init(&s_anim_inner);
    lv_anim_set_var(&s_anim_inner, s_arc_inner);
    lv_anim_set_values(&s_anim_inner, 0, 3599);
    lv_anim_set_exec_cb(&s_anim_inner, rot_cb);
    lv_anim_set_time(&s_anim_inner, 14000);  /* idle inner CW */
    lv_anim_set_repeat_count(&s_anim_inner, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&s_anim_inner);

    /* CP11: pupil breathing pulse (1100ms cycle, oscillates with playback) */
    lv_anim_init(&s_pupil_pulse_anim);
    lv_anim_set_var(&s_pupil_pulse_anim, s_pupil);
    lv_anim_set_values(&s_pupil_pulse_anim, 0, 255);
    lv_anim_set_exec_cb(&s_pupil_pulse_anim, pupil_pulse_cb);
    lv_anim_set_time(&s_pupil_pulse_anim, 1100);
    lv_anim_set_playback_time(&s_pupil_pulse_anim, 1100);
    lv_anim_set_repeat_count(&s_pupil_pulse_anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&s_pupil_pulse_anim);

    s_inited = true;
    lv_vendor_disp_unlock();

    /* CP8: auto-hide boot splash after 2s */
    s_boot_timer = lv_timer_create(s_boot_hide_cb, 2000, NULL);
    lv_timer_set_repeat_count(s_boot_timer, 1);

    return OPRT_OK;
}



/* CP10: scale and reposition the bionic-eye widgets for speaking state.
 * Compact = 36% size at y=30 (per Claude Design ScreenSpeaking layout). */
static void iris_set_eye_compact(bool compact) {
    int target_size = compact ? (s_eye_size_normal * 36 / 100) : s_eye_size_normal;
    int target_y    = compact ? 30 : s_eye_y_normal;

    int iris_size = target_size * 80 / 100;
    int inner_dark_size = iris_size * 65 / 100;
    int pupil_size = iris_size / 5;
    int inner_arc_size = inner_dark_size + 8;
    int orbit_size = target_size + (compact ? 8 : 24);

    lv_obj_set_size(s_iris_outer, target_size, target_size);
    lv_obj_align(s_iris_outer, LV_ALIGN_TOP_MID, 0, target_y);

    lv_obj_set_size(s_iris_mid, iris_size, iris_size);
    lv_obj_align(s_iris_mid, LV_ALIGN_TOP_MID, 0, target_y + (target_size - iris_size) / 2);

    lv_obj_set_size(s_iris_inner_dark, inner_dark_size, inner_dark_size);
    lv_obj_align(s_iris_inner_dark, LV_ALIGN_TOP_MID, 0, target_y + (target_size - inner_dark_size) / 2);

    lv_obj_set_size(s_pupil, pupil_size, pupil_size);
    lv_obj_align(s_pupil, LV_ALIGN_TOP_MID, 0, target_y + (target_size - pupil_size) / 2);

    lv_obj_set_size(s_orbit, orbit_size, orbit_size);
    lv_obj_align(s_orbit, LV_ALIGN_TOP_MID, 0, target_y - (orbit_size - target_size) / 2);

    lv_obj_set_size(s_arc_mid, iris_size, iris_size);
    lv_obj_align(s_arc_mid, LV_ALIGN_TOP_MID, 0, target_y + (target_size - iris_size) / 2);

    lv_obj_set_size(s_arc_inner, inner_arc_size, inner_arc_size);
    lv_obj_align(s_arc_inner, LV_ALIGN_TOP_MID, 0, target_y + (target_size - inner_arc_size) / 2);

    /* CP10b: also reposition audio bars so they sit right under the compact
     * eye (or back to their normal slot below the full-size eye). Otherwise
     * the waveform overlaps the structured response text fields. */
    if (s_audio_bars) {
        int audio_y = compact ? (target_y + target_size + 8) : s_audio_y_normal;
        lv_obj_align(s_audio_bars, LV_ALIGN_TOP_MID, 0, audio_y);
    }
    /* CP11: hide tick halo in compact mode (small eye + ticks would clutter) */
    if (s_iris_ticks_container) {
        if (compact) lv_obj_add_flag(s_iris_ticks_container, LV_OBJ_FLAG_HIDDEN);
        else         lv_obj_clear_flag(s_iris_ticks_container, LV_OBJ_FLAG_HIDDEN);
    }
}

static void iris_update_arc_anims(disp_state_t st) {
    int o_ms, m_ms, i_ms;
    switch (st) {
        /* CP9: all timings slowed 1.7-2x for calmer feel per user feedback */
        case DISP_STATE_IDLE:        o_ms=32000; m_ms=48000; i_ms=24000; break;
        case DISP_STATE_CONNECTING:  o_ms=32000; m_ms=48000; i_ms=24000; break;
        case DISP_STATE_LISTENING:   o_ms=10000; m_ms= 7000; i_ms= 5000; break;
        case DISP_STATE_PROCESSING:  o_ms= 5500; m_ms= 3800; i_ms= 2600; break;
        case DISP_STATE_SPEAKING:    o_ms=20000; m_ms=15000; i_ms=10000; break;
        case DISP_STATE_ERROR:       o_ms=60000; m_ms=60000; i_ms=60000; break;
        default:                     o_ms=32000; m_ms=48000; i_ms=24000;
    }
    /* Stop existing, restart with new timings */
    lv_anim_del(s_orbit, rot_cb);
    lv_anim_del(s_arc_mid, rot_cb);
    lv_anim_del(s_arc_inner, rot_cb);
    if (st != DISP_STATE_ERROR) {
        lv_anim_set_time(&s_orbit_anim, o_ms);
        lv_anim_start(&s_orbit_anim);
        lv_anim_set_time(&s_anim_mid, m_ms);
        lv_anim_start(&s_anim_mid);
        lv_anim_set_time(&s_anim_inner, i_ms);
        lv_anim_start(&s_anim_inner);
    }

    /* CP8: per-state layer visibility -- exactly one layer visible at a time */
    bool is_idle      = (st == DISP_STATE_IDLE);
    bool is_capture   = (st == DISP_STATE_LISTENING);
    bool is_thinking  = (st == DISP_STATE_PROCESSING);
    bool is_speaking  = (st == DISP_STATE_SPEAKING);
    bool is_error     = (st == DISP_STATE_ERROR);

    if (is_idle) {
        lv_obj_clear_flag(s_mode_hints, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_idle_tag,   LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_idle_cta,   LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_state_label,  LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_mode_hints,   LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_idle_tag,     LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_idle_cta,     LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_state_label,  LV_OBJ_FLAG_HIDDEN);
    }

    if (is_capture) lv_obj_clear_flag(s_layer_capture, LV_OBJ_FLAG_HIDDEN);
    else            lv_obj_add_flag(s_layer_capture,   LV_OBJ_FLAG_HIDDEN);

    if (is_thinking) {
        lv_obj_clear_flag(s_layer_think, LV_OBJ_FLAG_HIDDEN);
        for (int r = 0; r < 4; r++) lv_anim_start(&s_think_bar_anims[r]);
    } else {
        lv_obj_add_flag(s_layer_think,   LV_OBJ_FLAG_HIDDEN);
        for (int r = 0; r < 4; r++) lv_anim_del(s_think_bar_fill[r], bar_h_cb);
    }

    /* CP10: speaking state -- compact eye + audio bars + 4 fields + footer */
    if (is_speaking) {
        iris_set_eye_compact(true);
        lv_obj_clear_flag(s_audio_bars,   LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_speak_footer, LV_OBJ_FLAG_HIDDEN);
        for (int i = 0; i < 4; i++) {
            lv_obj_clear_flag(s_speak_field_label[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(s_speak_field_value[i], LV_OBJ_FLAG_HIDDEN);
        }
        for (int b = 0; b < 21; b++) lv_anim_start(&s_bar_anims[b]);
    } else {
        iris_set_eye_compact(false);
        lv_obj_add_flag(s_audio_bars,     LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_speak_footer,   LV_OBJ_FLAG_HIDDEN);
        for (int i = 0; i < 4; i++) {
            lv_obj_add_flag(s_speak_field_label[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_speak_field_value[i], LV_OBJ_FLAG_HIDDEN);
        }
        for (int b = 0; b < 21; b++) lv_anim_del(s_bars[b], bar_h_cb);
    }

    if (is_error) lv_obj_clear_flag(s_layer_error, LV_OBJ_FLAG_HIDDEN);
    else          lv_obj_add_flag(s_layer_error,   LV_OBJ_FLAG_HIDDEN);
}


/* CP8: boot splash auto-hide after 2s */
static void s_boot_hide_cb(lv_timer_t *t) {
    (void)t;
    lv_vendor_disp_lock();
    /* CP9: fully delete the boot widget tree so it can never intercept
     * touch events later (defense against the CP8 tap-not-firing report). */
    if (s_layer_boot) {
        lv_obj_del(s_layer_boot);
        s_layer_boot = NULL;
    }
    lv_vendor_disp_unlock();
    if (s_boot_timer) {
        lv_timer_del(s_boot_timer);
        s_boot_timer = NULL;
    }
}

void nav_display_set_state(disp_state_t st) {
    if (!s_inited) return;
    lv_color_t c = state_color(st);
    lv_vendor_disp_lock();
    /* CP10c: banner shows for tap-acknowledgement only. As soon as the state
     * machine progresses (LISTENING/PROCESSING/SPEAKING/IDLE), hide it so it
     * never overlaps the next screen's content. */
    if (s_mode_banner) lv_obj_add_flag(s_mode_banner, LV_OBJ_FLAG_HIDDEN);
    if (s_mode_banner_timer) {
        lv_timer_del(s_mode_banner_timer);
        s_mode_banner_timer = NULL;
    }
    lv_label_set_text(s_state_label, state_text(st));
    lv_obj_set_style_text_color(s_state_label, c, LV_PART_MAIN);
    /* recolor the eye to match state */
    lv_obj_set_style_bg_color(s_iris_mid, c, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_iris_mid, c, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(s_iris_mid, c, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_iris_inner_dark, c, LV_PART_MAIN);
    /* CP11: pupil stays white; only its glow shadow takes the state color */
    lv_obj_set_style_shadow_color(s_pupil, c, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_orbit, c, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_arc_mid, c, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_arc_inner, c, LV_PART_INDICATOR);
    iris_update_arc_anims(st);
    lv_vendor_disp_unlock();
}

void nav_display_set_text(const char *text) {
    /* CP8: text response panel removed -- audio-only output. */
    (void)text;
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
/* CP7b: manual swipe detection (LVGL gesture system unreliable on GT1151QM
 * because the touch driver doesn't emit continuous move events between
 * press-down and release-up — only the endpoints. We capture press/release
 * points ourselves and compute the delta. This works with single-shot touch
 * drivers AND keeps tap detection working via LVGL's CLICKED event. */
#define IRIS_SWIPE_MIN_DY 40   /* px — vertical travel needed to count as swipe */
#define IRIS_SWIPE_MAX_DX 80   /* px — horizontal slop tolerated before rejecting */

static lv_point_t s_press_point = {0, 0};
static bool s_swipe_consumed = false;

/* CP9: double-tap detection (320ms window). First tap starts a timer. If a
 * second tap arrives before the timer fires, it's a double-tap. Otherwise
 * the timer fires the single-tap cb. Costs ~320ms latency on single-tap
 * but lets us cleanly distinguish double-tap-to-repeat from regular taps. */
#define IRIS_DOUBLE_TAP_MS 320
static lv_timer_t *s_tap_timer = NULL;
static void (*s_double_tap_cb)(void) = NULL;

static void s_single_tap_fire_cb(lv_timer_t *t) {
    (void)t;
    s_tap_timer = NULL;
    if (s_tap_cb) s_tap_cb();
}

static void s_screen_pressed_cb(lv_event_t *e) {
    lv_indev_t *indev = lv_event_get_indev(e);
    if (!indev) return;
    lv_indev_get_point(indev, &s_press_point);
    s_swipe_consumed = false;
}

static void s_screen_released_cb(lv_event_t *e) {
    lv_indev_t *indev = lv_event_get_indev(e);
    if (!indev) return;
    lv_point_t release;
    lv_indev_get_point(indev, &release);
    int32_t dy = release.y - s_press_point.y;
    int32_t dx = release.x - s_press_point.x;
    int32_t adx = dx < 0 ? -dx : dx;
    int32_t ady = dy < 0 ? -dy : dy;
    if (ady >= IRIS_SWIPE_MIN_DY && adx < IRIS_SWIPE_MAX_DX && ady > adx) {
        s_swipe_consumed = true;
        if (dy < 0 && s_swipe_up_cb)        s_swipe_up_cb();
        else if (dy > 0 && s_swipe_down_cb) s_swipe_down_cb();
    }
}

static void s_screen_click_cb(lv_event_t *e) {
    (void)e;
    /* Suppress click if the press-release pair was a swipe. */
    if (s_swipe_consumed) { s_swipe_consumed = false; return; }
    /* CP9: double-tap detection. */
    if (s_tap_timer) {
        /* Second tap within the window -- it's a double-tap. Cancel the
         * pending single-tap timer and fire the double-tap callback. */
        lv_timer_del(s_tap_timer);
        s_tap_timer = NULL;
        if (s_double_tap_cb) s_double_tap_cb();
    } else {
        /* First tap -- start the timer. If no second tap arrives before it
         * fires, the single-tap callback runs. */
        s_tap_timer = lv_timer_create(s_single_tap_fire_cb, IRIS_DOUBLE_TAP_MS, NULL);
        lv_timer_set_repeat_count(s_tap_timer, 1);
    }
}

static void s_screen_gesture_cb(lv_event_t *e) {
    lv_indev_t *indev = lv_event_get_indev(e);
    if (!indev) return;
    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir == LV_DIR_TOP && s_swipe_up_cb)         s_swipe_up_cb();
    else if (dir == LV_DIR_BOTTOM && s_swipe_down_cb) s_swipe_down_cb();
}
void nav_display_set_tap_cb(void (*cb)(void)) {
    s_tap_cb = cb;
    if (!s_inited || !s_screen) return;
    lv_vendor_disp_lock();
    lv_obj_add_flag(s_screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_screen, s_screen_click_cb,    LV_EVENT_CLICKED,  NULL);
    lv_obj_add_event_cb(s_screen, s_screen_gesture_cb,  LV_EVENT_GESTURE,  NULL);
    lv_obj_add_event_cb(s_screen, s_screen_pressed_cb,  LV_EVENT_PRESSED,  NULL);
    lv_obj_add_event_cb(s_screen, s_screen_released_cb, LV_EVENT_RELEASED, NULL);
    /* CP7 belt-and-braces: register the same listeners on the active screen
     * so that even if a child clears GESTURE_BUBBLE the events still land. */
    lv_obj_t *act_scr = lv_scr_act();
    if (act_scr) {
        lv_obj_add_flag(act_scr, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(act_scr, s_screen_click_cb,    LV_EVENT_CLICKED,  NULL);
        lv_obj_add_event_cb(act_scr, s_screen_gesture_cb,  LV_EVENT_GESTURE,  NULL);
        lv_obj_add_event_cb(act_scr, s_screen_pressed_cb,  LV_EVENT_PRESSED,  NULL);
        lv_obj_add_event_cb(act_scr, s_screen_released_cb, LV_EVENT_RELEASED, NULL);
    }
    lv_vendor_disp_unlock();
}

static void s_mode_banner_hide_cb(lv_timer_t *t) {
    (void)t;
    lv_vendor_disp_lock();
    if (s_mode_banner) lv_obj_add_flag(s_mode_banner, LV_OBJ_FLAG_HIDDEN);
    lv_vendor_disp_unlock();
    if (s_mode_banner_timer) {
        lv_timer_del(s_mode_banner_timer);
        s_mode_banner_timer = NULL;
    }
}

void nav_display_show_mode_banner(const char *mode_name, uint32_t color_hex) {
    if (!s_inited || !mode_name) return;
    lv_color_t c = lv_color_hex(color_hex);
    lv_vendor_disp_lock();
    lv_label_set_text(s_mode_banner_label, mode_name);
    lv_obj_set_style_text_color(s_mode_banner_label, c, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_mode_banner, c, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(s_mode_banner, c, LV_PART_MAIN);
    lv_obj_clear_flag(s_mode_banner, LV_OBJ_FLAG_HIDDEN);
    lv_vendor_disp_unlock();
    if (s_mode_banner_timer) lv_timer_del(s_mode_banner_timer);
    /* CP10c: shortened from 1500ms -- a banner that's a flash, not a sentence. */
    s_mode_banner_timer = lv_timer_create(s_mode_banner_hide_cb, 700, NULL);
    lv_timer_set_repeat_count(s_mode_banner_timer, 1);
}

void nav_display_set_swipe_callbacks(void (*swipe_up)(void), void (*swipe_down)(void)) {
    s_swipe_up_cb = swipe_up;
    s_swipe_down_cb = swipe_down;
}

/* ============================================================
 * CP9 public APIs
 * ============================================================ */

/* Randomize the 4 THINK data bars on each call. Called by main.c when
 * entering the PROCESSING state so the bars look alive instead of
 * displaying the same hardcoded 78/62/41/88 every time. */
void nav_display_randomize_think_bars(void) {
    if (!s_inited) return;
    static unsigned int seed = 0xCAFEBABE;
    int track_w = (LV_HOR_RES - 64) - 88;
    lv_vendor_disp_lock();
    for (int r = 0; r < 4; r++) {
        seed = seed * 1103515245u + 12345u;
        int pct = 35 + (int)((seed >> 16) & 0xFFFF) % 60;  /* 35-94% */
        if (s_think_bar_fill[r]) {
            int fill_w = track_w * pct / 100;
            lv_obj_set_size(s_think_bar_fill[r], fill_w, 3);
        }
        /* Update the right-side percentage label (3rd child of the row) */
        if (s_think_bar_row[r]) {
            uint32_t cnt = lv_obj_get_child_cnt(s_think_bar_row[r]);
            if (cnt >= 3) {
                lv_obj_t *pct_lbl = lv_obj_get_child(s_think_bar_row[r], cnt - 1);
                if (pct_lbl) {
                    char pct_buf[8]; snprintf(pct_buf, sizeof(pct_buf), "%d%%", pct);
                    lv_label_set_text(pct_lbl, pct_buf);
                }
            }
        }
    }
    lv_vendor_disp_unlock();
}

void nav_display_show_settings(void) {
    if (!s_inited || !s_layer_settings) return;
    lv_vendor_disp_lock();
    lv_obj_clear_flag(s_layer_settings, LV_OBJ_FLAG_HIDDEN);
    lv_vendor_disp_unlock();
}

void nav_display_hide_settings(void) {
    if (!s_inited || !s_layer_settings) return;
    lv_vendor_disp_lock();
    lv_obj_add_flag(s_layer_settings, LV_OBJ_FLAG_HIDDEN);
    lv_vendor_disp_unlock();
}

void nav_display_set_double_tap_cb(void (*cb)(void)) {
    s_double_tap_cb = cb;
}

/* CP10/CP11b: structured response display for speaking state.
 *
 * Sets all 4 (label, value) pairs AND dynamically lays them out vertically.
 * After each label or value text is set, we call lv_obj_update_layout() to
 * force LVGL to recompute the widget's height for LV_LABEL_LONG_WRAP, then
 * advance a running-Y accumulator. The next field's label is placed at that
 * Y. This means a 3-line wrapped ACTION value pushes WHY down naturally
 * instead of overlapping it.
 *
 * Vertical layout: y starts at 170 (just below audio bars in compact mode).
 *   field_label  --> y
 *   field_value  --> y + label_h + 2
 *   y           +=  label_h + 2 + value_h + 14   (14px gap between fields)
 */
void nav_display_set_speak_response(
    const char *l1, const char *v1,
    const char *l2, const char *v2,
    const char *l3, const char *v3,
    const char *l4, const char *v4)
{
    if (!s_inited) return;
    const char *labels[4] = {l1, l2, l3, l4};
    const char *values[4] = {v1, v2, v3, v4};
    lv_vendor_disp_lock();
    int y = 170;
    const int xstart = 18;
    const int label_to_value_gap = 2;
    const int between_fields_gap = 14;
    for (int i = 0; i < 4; i++) {
        if (!s_speak_field_label[i] || !s_speak_field_value[i]) continue;
        const char *lab = labels[i] ? labels[i] : "";
        const char *val = values[i] ? values[i] : "";
        lv_label_set_text(s_speak_field_label[i], lab);
        lv_label_set_text(s_speak_field_value[i], val);
        /* Position label first */
        lv_obj_align(s_speak_field_label[i], LV_ALIGN_TOP_LEFT, xstart, y);
        lv_obj_update_layout(s_speak_field_label[i]);
        int lh = lv_obj_get_height(s_speak_field_label[i]);
        if (lh < 12) lh = 12;  /* safety floor for empty strings */
        /* Then value, immediately under label */
        int vy = y + lh + label_to_value_gap;
        lv_obj_align(s_speak_field_value[i], LV_ALIGN_TOP_LEFT, xstart, vy);
        lv_obj_update_layout(s_speak_field_value[i]);
        int vh = lv_obj_get_height(s_speak_field_value[i]);
        if (vh < 16) vh = 16;
        /* Advance the running Y past this field */
        y = vy + vh + between_fields_gap;
    }
    lv_vendor_disp_unlock();
}

/* CP16: HH:MM clock setter, called by main.c's clock-update timer. */
void nav_display_set_time(const char *hhmm) {
    if (!s_inited || !hhmm || !s_clock_label) return;
    lv_vendor_disp_lock();
    lv_label_set_text(s_clock_label, hhmm);
    lv_vendor_disp_unlock();
}
