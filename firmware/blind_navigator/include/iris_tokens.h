/**
 * @file iris_tokens.h
 * @brief IRIS — Vision Co-Pilot · LVGL design tokens (LOCKED 2026-05-02)
 *
 * Source of truth: /design/iris-firmware-ui/project/tokens.css in the repo.
 * Do NOT introduce off-token hex values. Do NOT rename. If a screen needs a
 * shade that's not here, the design is wrong — not the token table.
 */
#ifndef __IRIS_TOKENS_H__
#define __IRIS_TOKENS_H__

#include "lvgl.h"

/* ============ BRAND ============ */
#define IRIS_NAME           "IRIS"
#define IRIS_TAGLINE        "VISION CO-PILOT"
#define IRIS_VERSION        "v2.3.1"

/* ============ BACKGROUNDS ============ */
#define IRIS_BG_0           lv_color_hex(0x05070A)  /* deep obsidian — true canvas */
#define IRIS_BG_1           lv_color_hex(0x0A0E14)  /* card / overlay base */
#define IRIS_BG_2           lv_color_hex(0x131923)  /* raised surface */
#define IRIS_BG_LINE        lv_color_hex(0x1C2430)  /* hairline divider */

/* ============ FOREGROUNDS ============ */
#define IRIS_FG_0           lv_color_hex(0xF4F6FA)  /* primary text — pure-white-ish, warm */
#define IRIS_FG_1           lv_color_hex(0xC7CFDB)  /* secondary text */
#define IRIS_FG_2           lv_color_hex(0x7A8696)  /* tertiary / mono labels */
#define IRIS_FG_3           lv_color_hex(0x4A5363)  /* placeholder */

/* ============ STATE COLORS (drives bionic eye + accents) ============ */
#define IRIS_STATE_IDLE     lv_color_hex(0x4FE3F0)  /* cyan — ready */
#define IRIS_STATE_CAPTURE  lv_color_hex(0xFFB347)  /* amber — shutter */
#define IRIS_STATE_THINK    lv_color_hex(0xE879F9)  /* magenta — processing */
#define IRIS_STATE_SPEAK    lv_color_hex(0x4ADE80)  /* green — audio out */
#define IRIS_STATE_ERROR    lv_color_hex(0xFF5C5C)  /* red — fault */

/* ============ TYPE SCALE (px on 320x480) ============ */
#define IRIS_T_HERO         44   /* speak: ACTION line */
#define IRIS_T_TITLE        32   /* primary status */
#define IRIS_T_STATUS       24   /* MIN status size */
#define IRIS_T_BODY         16   /* secondary copy */
#define IRIS_T_MONO_SM      12   /* technical labels */
#define IRIS_T_MONO_XS      10   /* tags / pills */

/* ============ RADII ============ */
#define IRIS_R_PILL         999  /* fully round (use LV_RADIUS_CIRCLE for circles) */
#define IRIS_R_CARD         14
#define IRIS_R_TAG          6

/* ============ SPACING SCALE ============ */
#define IRIS_S_1            4
#define IRIS_S_2            8
#define IRIS_S_3            12
#define IRIS_S_4            16
#define IRIS_S_5            20
#define IRIS_S_6            24
#define IRIS_S_8            32

/* ============ ANIMATION TIMINGS (ms) ============
 * outer/mid/inner arc rotation per state.
 * Designer spec: idle slow, capture fast, think fastest, speak medium, error frozen.
 */
#define IRIS_ANIM_IDLE_OUTER_MS     18000
#define IRIS_ANIM_IDLE_MID_MS       28000
#define IRIS_ANIM_IDLE_INNER_MS     14000

#define IRIS_ANIM_CAPTURE_OUTER_MS   6000
#define IRIS_ANIM_CAPTURE_MID_MS     4000
#define IRIS_ANIM_CAPTURE_INNER_MS   3000

#define IRIS_ANIM_THINK_OUTER_MS     3000
#define IRIS_ANIM_THINK_MID_MS       2000
#define IRIS_ANIM_THINK_INNER_MS     1400

#define IRIS_ANIM_SPEAK_OUTER_MS    12000
#define IRIS_ANIM_SPEAK_MID_MS       9000
#define IRIS_ANIM_SPEAK_INNER_MS     6000

#define IRIS_ANIM_ERROR_ALL_MS      30000  /* effectively frozen */

/* Pupil pulse periods (ms) per state */
#define IRIS_PUPIL_IDLE_MS           3200
#define IRIS_PUPIL_CAPTURE_MS         400  /* one-shot */
#define IRIS_PUPIL_THINK_MS          1100
#define IRIS_PUPIL_SPEAK_MS           600
#define IRIS_PUPIL_ERROR_MS          1200

/* Halo glow breathe periods (ms) */
#define IRIS_HALO_IDLE_MS            3600
#define IRIS_HALO_CAPTURE_MS          600
#define IRIS_HALO_THINK_MS           1600
#define IRIS_HALO_SPEAK_MS           1000
#define IRIS_HALO_ERROR_MS           1200

/* State transition durations (ms) */
#define IRIS_TRANS_IDLE_TO_CAPTURE   200
#define IRIS_TRANS_CAPTURE_TO_THINK  150
#define IRIS_TRANS_THINK_TO_SPEAK    320
#define IRIS_TRANS_TO_ERROR           80
#define IRIS_TRANS_TO_SETTINGS       240
#define IRIS_FPS_INVALIDATE_MAX_MS    33   /* 30 fps cap */

/* Cinematic upgrade — pupil parallax (Checkpoint 6) */
#define IRIS_PARALLAX_OFFSET_PX      14    /* max pupil drift toward speaker */
#define IRIS_PARALLAX_EASE_MS        600

#endif /* __IRIS_TOKENS_H__ */
