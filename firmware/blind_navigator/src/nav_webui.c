/* nav_webui.c -- IRIS embedded web UI (CP14 foundation, CP15 Wi-Fi setup)
 *
 * Multi-route HTTP/1.0 server. Routes:
 *   GET  /              -- home (links to /wifi + /status)
 *   GET  /wifi          -- Wi-Fi setup page with scan results
 *   POST /api/wifi/save -- save SSID+pass to KV + reboot
 *   GET  /api/status    -- JSON status
 *
 * Style matches the device LCD: deep obsidian background, Space Grotesk
 * + JetBrains Mono fonts (Google Fonts CDN), state colors from iris_tokens.
 */
#include "nav_webui.h"
#include "tal_api.h"
#include "tal_thread.h"
#include "tal_network.h"
#include "tal_wifi.h"
#include "tal_system.h"
#include "tuya_cloud_types.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define WEBUI_PORT      80
#define MAX_REQ_LEN     4096
#define MAX_BODY_LEN    1024
#define WEBUI_BACKLOG   4

extern void nav_wifi_save_kv(const char *ssid, const char *pass);
extern void nav_wifi_forget(void);  /* CP21: clear KV creds + arm force_ap */
extern int  nav_settings_get_volume(void);
extern int  nav_settings_get_brightness(void);
extern int  nav_settings_get_wake_feedback(void);
extern void nav_settings_get_voice(char *out, size_t n);
extern void nav_settings_get_language(char *out, size_t n);
extern void nav_settings_set_volume(int v);
extern void nav_settings_set_brightness(int v);
extern void nav_settings_set_wake_feedback(int v);
extern void nav_settings_set_voice(const char *s);
extern void nav_settings_set_language(const char *s);

/* CP18 + CP20: diagnostic counters + test triggers (defined in main.c) */
extern uint32_t nav_diag_get_wake_count(void);
extern void     nav_diag_trigger_tap(void);
extern void     nav_diag_trigger_double_tap(void);
extern void     nav_diag_trigger_identify(void);

/* CP20: state getter for live home dashboard */
#include "nav_display.h"
extern int tal_wifi_station_get_conn_ap_rssi(int8_t *rssi);
extern int tal_system_get_free_heap_size(void);

/* Map disp_state_t → short label for /api/status JSON */
static const char *state_label(disp_state_t st) {
    switch (st) {
        case DISP_STATE_IDLE:        return "idle";
        case DISP_STATE_CONNECTING:  return "connecting";
        case DISP_STATE_LISTENING:   return "capturing";
        case DISP_STATE_PROCESSING:  return "thinking";
        case DISP_STATE_SPEAKING:    return "speaking";
        case DISP_STATE_ERROR:       return "error";
        default:                     return "unknown";
    }
}

static bool s_started = false;
/* CP22b: when true, only /wifi + /api/wifi/save + /api/status are reachable.
 * All other paths 303-redirect to /wifi. Set by main.c when AP mode is active.
 * Reasoning: in AP mode the device has no real Wi-Fi, so /home, /settings,
 * /diagnostics show stale/nonsense info. Captive-portal-style lock-down. */
static bool s_ap_only_mode = false;
void nav_webui_set_ap_only(bool on) { s_ap_only_mode = on; }

/* ============================================================
 * Common HTML <head> styled to match the device LCD UI
 * ============================================================ */
static const char *HEAD =
    "<!DOCTYPE html><html lang=en><head><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>IRIS</title>"
    "<link href='https://fonts.googleapis.com/css2?family=Space+Grotesk:wght@400;500;600;700&family=JetBrains+Mono:wght@400;500;600&display=swap' rel=stylesheet>"
    "<style>"
    "*{box-sizing:border-box;margin:0;padding:0}"
    "html,body{background:#05070A;color:#F4F6FA;font-family:'Space Grotesk',system-ui,sans-serif;min-height:100vh}"
    "body{padding:32px;max-width:560px;margin:0 auto}"
    ".tag{font-family:'JetBrains Mono',monospace;font-size:11px;color:#7A8696;letter-spacing:0.32em;text-transform:uppercase;margin-bottom:8px}"
    ".title{font-size:48px;font-weight:600;letter-spacing:0.18em;color:#F4F6FA;margin-bottom:6px}"
    ".version{font-family:'JetBrains Mono',monospace;font-size:10px;color:#7A8696;letter-spacing:0.24em;margin-bottom:32px}"
    ".eye{width:96px;height:96px;border-radius:50%;border:2px solid #1C2430;margin:24px auto 24px;position:relative;animation:spin 18s linear infinite}"
    ".eye::before{content:'';position:absolute;inset:14%;border-radius:50%;background:rgba(79,227,240,0.18);border:2px solid #4FE3F0;box-shadow:0 0 18px rgba(79,227,240,0.5)}"
    ".eye::after{content:'';position:absolute;left:50%;top:50%;transform:translate(-50%,-50%);width:18px;height:18px;border-radius:50%;background:#F4F6FA;box-shadow:0 0 22px rgba(79,227,240,0.85);animation:pulse 1.1s ease-in-out infinite alternate}"
    "@keyframes spin{from{transform:rotate(0)}to{transform:rotate(360deg)}}"
    "@keyframes pulse{from{opacity:0.6}to{opacity:1}}"
    ".card{background:#0A0E14;border:1px solid #1C2430;border-radius:14px;padding:20px;margin-bottom:16px}"
    ".card h2{font-family:'JetBrains Mono',monospace;font-size:11px;color:#C7CFDB;letter-spacing:0.24em;text-transform:uppercase;margin-bottom:12px}"
    ".row{display:flex;justify-content:space-between;align-items:center;padding:8px 0;border-bottom:1px solid #131923}"
    ".row:last-child{border:none}"
    ".row .k{font-family:'JetBrains Mono',monospace;font-size:11px;color:#7A8696;letter-spacing:0.16em;text-transform:uppercase}"
    ".row .v{font-family:'JetBrains Mono',monospace;font-size:13px;color:#F4F6FA}"
    "label{display:block;font-family:'JetBrains Mono',monospace;font-size:10px;color:#7A8696;letter-spacing:0.24em;text-transform:uppercase;margin:14px 0 6px}"
    "select,input[type=text],input[type=password]{width:100%;background:#05070A;color:#F4F6FA;border:1px solid #1C2430;border-radius:8px;padding:12px;font-family:'Space Grotesk',sans-serif;font-size:15px;outline:none}"
    "select:focus,input:focus{border-color:#4FE3F0;box-shadow:0 0 12px rgba(79,227,240,0.3)}"
    "button{background:#0A0E14;color:#4FE3F0;border:1px solid #4FE3F0;border-radius:999px;padding:14px 28px;font-family:'JetBrains Mono',monospace;font-size:12px;letter-spacing:0.24em;text-transform:uppercase;cursor:pointer;margin-top:20px;width:100%;transition:all 0.2s}"
    "button:hover{background:rgba(79,227,240,0.12);box-shadow:0 0 16px rgba(79,227,240,0.4)}"
    ".rssi{font-family:'JetBrains Mono',monospace;font-size:11px;color:#7A8696;letter-spacing:0.1em}"
    "a{color:#4FE3F0;text-decoration:none}"
    "a:hover{text-decoration:underline}"
    "nav{display:flex;gap:12px;margin-bottom:24px;flex-wrap:wrap}"
    "nav a{font-family:'JetBrains Mono',monospace;font-size:10px;color:#C7CFDB;letter-spacing:0.2em;text-transform:uppercase;padding:8px 14px;border:1px solid #1C2430;border-radius:6px;transition:all 0.2s}"
    "nav a:hover{border-color:#4FE3F0;color:#4FE3F0}"
    "nav a.active{border-color:#4FE3F0;color:#4FE3F0;background:rgba(79,227,240,0.08)}"
    ".hint{font-family:'JetBrains Mono',monospace;font-size:9px;color:#4A5363;letter-spacing:0.24em;text-transform:uppercase;text-align:center;margin-top:32px}"
    "</style></head><body>";

static const char *FOOT =
    "<div class=hint>iris vision co-pilot \xc2\xb7 admin web ui</div>"
    "</body></html>";

/* ============================================================
 * URL/form-encoded decode (in-place)
 * ============================================================ */
static void url_decode(char *s) {
    char *r = s, *w = s;
    while (*r) {
        if (*r == '+') { *w++ = ' '; r++; }
        else if (*r == '%' && r[1] && r[2]) {
            int v;
            if (sscanf(r + 1, "%2x", &v) == 1) {
                *w++ = (char)v;
                r += 3;
            } else { *w++ = *r++; }
        } else { *w++ = *r++; }
    }
    *w = 0;
}

/* Find form field "name=" in a URL-encoded body, copy decoded value to out */
static bool form_get(const char *body, const char *name, char *out, size_t out_max) {
    char prefix[64];
    snprintf(prefix, sizeof(prefix), "%s=", name);
    const char *p = strstr(body, prefix);
    if (!p) return false;
    p += strlen(prefix);
    const char *e = strchr(p, '&');
    size_t len = e ? (size_t)(e - p) : strlen(p);
    if (len >= out_max) len = out_max - 1;
    memcpy(out, p, len);
    out[len] = 0;
    url_decode(out);
    return true;
}

/* ============================================================
 * Send response helpers
 * ============================================================ */
static void send_str(int fd, const char *s) {
    tal_net_send(fd, s, strlen(s));
}
static void send_resp_html(int fd, const char *body) {
    char hdr[160];
    snprintf(hdr, sizeof(hdr),
        "HTTP/1.0 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\nCache-Control: no-store\r\n\r\n");
    send_str(fd, hdr);
    send_str(fd, body);
}
static void send_resp_json(int fd, const char *json) {
    char hdr[160];
    snprintf(hdr, sizeof(hdr),
        "HTTP/1.0 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nCache-Control: no-store\r\n\r\n");
    send_str(fd, hdr);
    send_str(fd, json);
}
static void send_404(int fd) {
    send_str(fd, "HTTP/1.0 404 Not Found\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n404\n");
}

/* Stream HEAD + body + FOOT as separate sends. Why: HEAD alone is ~3644 bytes
 * and earlier routes used a 3072-byte buffer for HEAD+body+FOOT, so snprintf
 * overflowed and the size guard silently dropped the response. Streaming avoids
 * the trap entirely -- routes only need to size the body chunk. */
static void send_html_page(int fd, const char *body_section) {
    char hdr[160];
    snprintf(hdr, sizeof(hdr),
        "HTTP/1.0 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\nCache-Control: no-store\r\n\r\n");
    send_str(fd, hdr);
    send_str(fd, HEAD);
    send_str(fd, body_section);
    send_str(fd, FOOT);
}

/* ============================================================
 * Routes
 * ============================================================ */
/* CP20: home page is now a live dashboard.
 *
 * - Top: brand block + nav with SVG glyphs
 * - Bionic eye that recolors based on the current device state
 * - "Vision actions" card: 3 large buttons for NAVIGATE / READ TEXT / IDENTIFY
 *   that POST directly to /api/test endpoints (single-tap, double-tap,
 *   long-press equivalents). Lets a sighted user remote-control the device.
 * - "Live status" card with rows that auto-refresh every 3s via fetch('/api/status').
 *   State, IP, RSSI bars, free heap, wake-event count.
 * - All inline JS, no external libs. Long string literal kept readable by
 *   chunking the snprintf %s slots. */
static void route_home(int fd) {
    static const char *body =
        "<div class=tag>READY \xc2\xb7 ONLINE</div>"
        "<div class=title>IRIS</div>"
        "<div class=version>VISION CO-PILOT \xc2\xb7 v0.3.1</div>"
        "<nav>"
        "<a href='/' class=active>"
        "<svg width=10 height=10 viewBox='0 0 24 24' fill=none stroke=currentColor stroke-width=2.4 style='vertical-align:middle;margin-right:4px'><path d='M3 12L12 4l9 8'/><path d='M5 11v9h4v-6h6v6h4v-9'/></svg>"
        "HOME</a>"
        "<a href='/wifi'>"
        "<svg width=10 height=10 viewBox='0 0 24 24' fill=none stroke=currentColor stroke-width=2.4 style='vertical-align:middle;margin-right:4px'><path d='M2 8.5C5 6 8.5 4.5 12 4.5s7 1.5 10 4'/><path d='M5 12c2-1.6 4.5-2.5 7-2.5s5 .9 7 2.5'/><path d='M8 15.5c1.2-.9 2.5-1.4 4-1.4s2.8.5 4 1.4'/><circle cx=12 cy=19 r=1/></svg>"
        "WI-FI</a>"
        "<a href='/settings'>"
        "<svg width=10 height=10 viewBox='0 0 24 24' fill=none stroke=currentColor stroke-width=2.4 style='vertical-align:middle;margin-right:4px'><circle cx=12 cy=12 r=3/><path d='M19 12c0 1-.1 2-.4 2.9l2 1.5-2 3.5-2.4-.8c-.7.7-1.5 1.2-2.4 1.6l-.4 2.5h-4l-.4-2.5c-.9-.4-1.7-.9-2.4-1.6l-2.4.8-2-3.5 2-1.5C5.1 14 5 13 5 12s.1-2 .4-2.9l-2-1.5 2-3.5 2.4.8c.7-.7 1.5-1.2 2.4-1.6L10.6 1h4l.4 2.5c.9.4 1.7.9 2.4 1.6l2.4-.8 2 3.5-2 1.5c.3.9.4 1.9.4 2.9z'/></svg>"
        "SETTINGS</a>"
        "<a href='/diagnostics'>"
        "<svg width=10 height=10 viewBox='0 0 24 24' fill=none stroke=currentColor stroke-width=2.4 style='vertical-align:middle;margin-right:4px'><path d='M3 12h4l2-7 4 14 2-7h6'/></svg>"
        "DIAG</a>"
        "</nav>"
        "<div class=eye id=eye></div>"

        "<div class=card>"
        "<h2>Vision actions</h2>"
        "<div class=action-grid>"
        "<button class=action onclick=\"fire('/api/test/tap', this)\">"
        "<svg width=22 height=22 viewBox='0 0 24 24' fill=none stroke=currentColor stroke-width=2'><path d='M5 12h14M13 6l6 6-6 6'/></svg>"
        "<span class=action-name>NAVIGATE</span>"
        "<span class=action-sub>What's around me?</span>"
        "</button>"
        "<button class=action onclick=\"fire('/api/test/double_tap', this)\">"
        "<svg width=22 height=22 viewBox='0 0 24 24' fill=none stroke=currentColor stroke-width=2'><path d='M4 6h16M4 12h12M4 18h16'/></svg>"
        "<span class=action-name>READ TEXT</span>"
        "<span class=action-sub>OCR + summarize</span>"
        "</button>"
        "<button class=action onclick=\"fire('/api/test/identify', this)\">"
        "<svg width=22 height=22 viewBox='0 0 24 24' fill=none stroke=currentColor stroke-width=2'><circle cx=11 cy=11 r=7/><path d='M16 16l5 5'/></svg>"
        "<span class=action-name>IDENTIFY</span>"
        "<span class=action-sub>What is this object?</span>"
        "</button>"
        "</div>"
        "</div>"

        "<div class=card>"
        "<h2>Live status <span class=pulse-dot></span></h2>"
        "<div class=row><span class=k>State</span><span class=v id=v-state>\xe2\x80\x94</span></div>"
        "<div class=row><span class=k>IP address</span><span class=v id=v-ip>\xe2\x80\x94</span></div>"
        "<div class=row><span class=k>Signal</span><span class=v id=v-rssi>\xe2\x80\x94</span></div>"
        "<div class=row><span class=k>Volume / Bright</span><span class=v id=v-vb>\xe2\x80\x94</span></div>"
        "<div class=row><span class=k>Free heap</span><span class=v id=v-heap>\xe2\x80\x94</span></div>"
        "<div class=row><span class=k>Wake events</span><span class=v id=v-wake>\xe2\x80\x94</span></div>"
        "</div>"

        "<style>"
        ".action-grid{display:grid;grid-template-columns:1fr;gap:10px}"
        "@media (min-width:480px){.action-grid{grid-template-columns:1fr 1fr 1fr}}"
        ".action{background:#0A0E14;border:1px solid #1C2430;border-radius:14px;padding:18px 14px;color:#F4F6FA;display:flex;flex-direction:column;align-items:center;gap:8px;width:100%;margin:0;cursor:pointer;transition:all .2s}"
        ".action:hover{border-color:#4FE3F0;color:#4FE3F0;transform:translateY(-2px);box-shadow:0 0 18px rgba(79,227,240,.3)}"
        ".action svg{stroke:#4FE3F0}"
        ".action-name{font-family:'JetBrains Mono',monospace;font-size:11px;letter-spacing:.24em}"
        ".action-sub{font-size:11px;color:#7A8696;letter-spacing:.04em;text-transform:none;text-align:center}"
        ".action.fired{border-color:#7AE38A;color:#7AE38A}"
        ".action.fired svg{stroke:#7AE38A}"
        ".pulse-dot{display:inline-block;width:6px;height:6px;border-radius:50%;background:#7AE38A;margin-left:8px;animation:dotpulse 1.4s ease-in-out infinite}"
        "@keyframes dotpulse{0%,100%{opacity:.4}50%{opacity:1;box-shadow:0 0 8px #7AE38A}}"
        "</style>"

        "<script>"
        "function fire(p,btn){"
          "btn.classList.add('fired');"
          "fetch(p,{method:'POST'}).then(()=>setTimeout(()=>btn.classList.remove('fired'),1500))"
            ".catch(()=>btn.classList.remove('fired'));"
        "}"
        "function bars(rssi){"
          "if(!rssi||rssi===0)return '\xe2\x80\x94';"
          "var n = rssi>-55?4:rssi>-65?3:rssi>-75?2:rssi>-85?1:0;"
          "var s='';for(var i=0;i<4;i++)s+=i<n?'\xe2\x96\x88':'\xe2\x96\x91';"
          "return s+' '+rssi+' dBm';"
        "}"
        "function fmtKB(b){return b>1024?Math.round(b/1024)+' KB':b+' B';}"
        "async function tick(){"
          "try{"
            "var j=await fetch('/api/status').then(r=>r.json());"
            "document.getElementById('v-state').textContent=j.state||'\xe2\x80\x94';"
            "document.getElementById('v-ip').textContent=j.ip||'\xe2\x80\x94';"
            "document.getElementById('v-rssi').textContent=bars(j.rssi_dbm);"
            "document.getElementById('v-vb').textContent=(j.volume!==undefined?j.volume:'?')+'% / '+(j.brightness!==undefined?j.brightness:'?')+'%';"
            "document.getElementById('v-heap').textContent=fmtKB(j.free_heap_b);"
            "document.getElementById('v-wake').textContent=j.wake_count;"
          "}catch(e){"
            "document.getElementById('v-state').textContent='offline';"
          "}"
        "}"
        "tick();setInterval(tick,3000);"
        "</script>";
    send_html_page(fd, body);
}

/* Render Wi-Fi setup page. Performs an in-process scan and lists results.
 * CP22b: in AP-only mode the page is simplified -- no nav, no forget card,
 * different copy ("Set up your Wi-Fi" vs "Wi-Fi setup") to match captive
 * portal expectations. */
static void route_wifi(int fd) {
    /* Buffer for the rendered page; large enough for ~30 SSIDs */
    static char page[6144];
    int off = 0;
    if (s_ap_only_mode) {
        off += snprintf(page + off, sizeof(page) - off,
            "%s"
            "<div class=tag>SETUP \xc2\xb7 STEP 1 OF 1</div>"
            "<div class=title>IRIS</div>"
            "<div class=version>v0.3.1 \xc2\xb7 SETUP MODE</div>"
            "<div class=card>"
            "<h2>Connect IRIS to your Wi-Fi</h2>"
            "<div class=row><span class=k>Why am I here?</span><span class=v>IRIS needs Wi-Fi to call the vision API. Pick your network below; IRIS will reboot and join it.</span></div>"
            "</div>"
            "<div class=card>"
            "<h2>Available networks</h2>"
            "<form action='/api/wifi/save' method=POST>"
            "<label>NETWORK</label>"
            "<select name=ssid required>",
            HEAD);
    } else {
        off += snprintf(page + off, sizeof(page) - off,
            "%s"
            "<div class=tag>WI-FI SETUP</div>"
            "<div class=title>IRIS</div>"
            "<div class=version>v0.3.1</div>"
            "<nav>"
            "<a href='/'>HOME</a>"
            "<a href='/wifi' class=active>WI-FI</a>"
            "<a href='/settings'>SETTINGS</a>"
            "<a href='/diagnostics'>DIAGNOSTICS</a>"
            "</nav>"
            "<div class=card>"
            "<h2>Available networks</h2>"
            "<form action='/api/wifi/save' method=POST>"
            "<label>NETWORK</label>"
            "<select name=ssid required>",
            HEAD);
    }

    AP_IF_S *ap_list = NULL;
    uint32_t ap_num = 0;
    OPERATE_RET rt = tal_wifi_all_ap_scan(&ap_list, &ap_num);
    if (rt == OPRT_OK && ap_list && ap_num > 0) {
        for (uint32_t i = 0; i < ap_num && off < (int)sizeof(page) - 256; i++) {
            char ssid[33];
            uint8_t slen = ap_list[i].s_len;
            if (slen > 32) slen = 32;
            memcpy(ssid, ap_list[i].ssid, slen);
            ssid[slen] = 0;
            off += snprintf(page + off, sizeof(page) - off,
                "<option value='%s'>%s &middot; %d dBm</option>",
                ssid, ssid, ap_list[i].rssi);
        }
        tal_wifi_release_ap(ap_list);
    } else {
        off += snprintf(page + off, sizeof(page) - off,
            "<option value=''>(no networks found)</option>");
    }

    off += snprintf(page + off, sizeof(page) - off,
        "</select>"
        "<label>PASSWORD</label>"
        "<input name=pass type=password autocomplete='off' placeholder='leave empty for open network'>"
        "<button type=submit>SAVE \xe2\x80\xa2 REBOOT</button>"
        "</form>"
        "<div class=hint style='margin-top:16px;text-align:left'>after save, the device reboots and tries the new credentials. if connection fails, falls back to ap mode \"iris-xxxx\".</div>"
        "</div>");

    /* CP22b: Forget card only makes sense when there ARE stored creds to forget.
     * In AP mode the KV is already empty, so showing this would just confuse. */
    if (!s_ap_only_mode) {
        off += snprintf(page + off, sizeof(page) - off,
            "<div class=card>"
            "<h2>Forget saved Wi-Fi</h2>"
            "<div class=row><span class=k>State</span><span class=v>Clears stored creds + reboots to AP mode</span></div>"
            "<form action='/api/wifi/forget' method=POST onsubmit=\"return confirm('Forget Wi-Fi and reboot into IRIS-XXXX setup mode? You will need to reconnect from your phone.');\">"
            "<button type=submit style='background:rgba(255,107,107,.08);color:#FF6B6B;border-color:#FF6B6B;margin-top:14px'>"
            "FORGET WI-FI \xe2\x80\xa2 RESET TO AP"
            "</button>"
            "</form>"
            "<div class=hint style='margin-top:10px;text-align:left'>physical equivalent: hold the device button for 5 seconds. useful for testing the captive-portal AP flow without changing networks.</div>"
            "</div>");
    }

    off += snprintf(page + off, sizeof(page) - off,
        "%s", FOOT);

    send_resp_html(fd, page);
}

/* CP17: Settings page -- volume / brightness / voice / language / wake-feedback */
static void route_settings(int fd) {
    char voice[16], lang[16];
    nav_settings_get_voice(voice, sizeof(voice));
    nav_settings_get_language(lang, sizeof(lang));
    int vol = nav_settings_get_volume();
    int bri = nav_settings_get_brightness();
    int wakefb = nav_settings_get_wake_feedback();

    static char page[8192];
    snprintf(page, sizeof(page),
        "%s"
        "<div class=tag>SETTINGS</div>"
        "<div class=title>IRIS</div>"
        "<div class=version>v0.3.1</div>"
        "<nav>"
        "<a href='/'>HOME</a>"
        "<a href='/wifi'>WI-FI</a>"
        "<a href='/settings' class=active>SETTINGS</a>"
        "<a href='/diagnostics'>DIAGNOSTICS</a>"
        "</nav>"
        "<form action='/api/settings/save' method=POST>"
        "<div class=card>"
        "<h2>Audio</h2>"
        "<label>VOLUME (%d%%)</label>"
        "<input name=volume type=range min=0 max=100 step=5 value=%d oninput='this.previousElementSibling.textContent=`VOLUME (${this.value}%%)`'>"
        "<label>VOICE</label>"
        "<select name=voice>"
        "<option value=LO%s>LO (deeper)</option>"
        "<option value=MID%s>MID (default)</option>"
        "<option value=HI%s>HI (brighter)</option>"
        "</select>"
        "</div>"
        "<div class=card>"
        "<h2>Display</h2>"
        "<label>BRIGHTNESS (%d%%)</label>"
        "<input name=brightness type=range min=10 max=100 step=5 value=%d oninput='this.previousElementSibling.textContent=`BRIGHTNESS (${this.value}%%)`'>"
        "</div>"
        "<div class=card>"
        "<h2>Language</h2>"
        "<label>SPEECH LANGUAGE</label>"
        "<select name=language>"
        "<option value=EN%s>English</option>"
        "<option value=ES%s>Espa\xc3\xb1ol</option>"
        "<option value=HI%s>Hindi</option>"
        "<option value=AR%s>Arabic</option>"
        "</select>"
        "</div>"
        "<div class=card>"
        "<h2>Wake word</h2>"
        "<label>HI TUYA AUDIBLE FEEDBACK</label>"
        "<select name=wakefb>"
        "<option value=0%s>OFF (silent + snappy)</option>"
        "<option value=1%s>ON (\"Hello, I'm here\" alert)</option>"
        "</select>"
        "</div>"
        "<button type=submit>SAVE \xe2\x80\xa2 APPLY</button>"
        "</form>"

        /* CP20: live AJAX -- slider/select changes save instantly without
         * page reload. Form submit still works as a fallback for no-JS. */
        "<div id=toast style='position:fixed;bottom:24px;left:50%;transform:translate(-50%,40px);background:#7AE38A;color:#05070A;padding:10px 18px;border-radius:999px;font-family:JetBrains Mono,monospace;font-size:11px;letter-spacing:.16em;text-transform:uppercase;opacity:0;transition:all .25s;pointer-events:none;z-index:99'></div>"

        "<script>"
        "var t=null;"
        "function toast(msg,bad){"
          "var el=document.getElementById('toast');"
          "el.textContent=msg;"
          "el.style.background=bad?'#FF6B6B':'#7AE38A';"
          "el.style.opacity=1;el.style.transform='translate(-50%,0)';"
          "clearTimeout(t);t=setTimeout(function(){el.style.opacity=0;el.style.transform='translate(-50%,40px)'},1400);"
        "}"
        "function saveOne(name,val){"
          "fetch('/api/settings/save?ajax=1',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:name+'='+encodeURIComponent(val)})"
            ".then(r=>r.ok?toast(name+' \xe2\x86\x92 '+val):toast('save failed',1))"
            ".catch(()=>toast('offline',1));"
        "}"
        "var dbnc={};"
        "document.querySelectorAll('input[type=range],select').forEach(function(el){"
          "var ev=(el.tagName=='SELECT')?'change':'input';"
          "el.addEventListener(ev,function(){"
            "clearTimeout(dbnc[el.name]);"
            "dbnc[el.name]=setTimeout(function(){saveOne(el.name,el.value);},220);"
          "});"
        "});"
        "var f=document.querySelector('form');"
        "f.addEventListener('submit',function(e){e.preventDefault();toast('all settings saved');});"
        "if(location.search.indexOf('saved=1')>=0){toast('settings saved');history.replaceState({},'','/settings');}"
        "</script>"
        "%s",
        HEAD,
        vol, vol,
        strcmp(voice, "LO") == 0 ? " selected" : "",
        strcmp(voice, "MID") == 0 ? " selected" : "",
        strcmp(voice, "HI") == 0 ? " selected" : "",
        bri, bri,
        strcmp(lang, "EN") == 0 ? " selected" : "",
        strcmp(lang, "ES") == 0 ? " selected" : "",
        strcmp(lang, "HI") == 0 ? " selected" : "",
        strcmp(lang, "AR") == 0 ? " selected" : "",
        wakefb == 0 ? " selected" : "",
        wakefb == 1 ? " selected" : "",
        FOOT);
    send_resp_html(fd, page);
}

/* CP20: Settings save now responds two ways:
 *   - AJAX call (path "/api/settings/save?ajax=1"): returns 200 OK + tiny JSON.
 *     Settings page JS uses this for live slider drags + select changes.
 *   - Form submit (no ?ajax=1): returns 303 See Other → /settings, so the
 *     browser ends up on the settings page (with new values) instead of
 *     stuck on the /api/settings/save URL with a blank body. */
static void route_settings_save(int fd, const char *body, bool ajax) {
    char vol[16] = {0}, bri[16] = {0}, voice[16] = {0}, lang[16] = {0}, wakefb[8] = {0};
    if (form_get(body, "volume", vol, sizeof(vol)))      nav_settings_set_volume(atoi(vol));
    if (form_get(body, "brightness", bri, sizeof(bri)))  nav_settings_set_brightness(atoi(bri));
    if (form_get(body, "voice", voice, sizeof(voice)))   nav_settings_set_voice(voice);
    if (form_get(body, "language", lang, sizeof(lang)))  nav_settings_set_language(lang);
    if (form_get(body, "wakefb", wakefb, sizeof(wakefb))) nav_settings_set_wake_feedback(atoi(wakefb));

    if (ajax) {
        send_resp_json(fd, "{\"ok\":true}");
    } else {
        send_str(fd,
            "HTTP/1.0 303 See Other\r\n"
            "Location: /settings?saved=1\r\n"
            "Connection: close\r\n\r\n");
    }
}

/* CP20: rich JSON for the home dashboard's auto-refresh.
 *
 * Polled every 3 seconds by the home page JS. Every field is meant to be
 * cheap to read -- no Wi-Fi scans, no synchronous network calls. RSSI
 * may return 0 when not associated, and free_heap_b reflects PSRAM usage. */
static void route_status(int fd) {
    NW_IP_S ip = {0};
    tal_wifi_get_ip(WF_STATION, &ip);
    int8_t rssi = 0;
    tal_wifi_station_get_conn_ap_rssi(&rssi);
    int free_heap = tal_system_get_free_heap_size();
    uint32_t uptime = (uint32_t)tal_time_get_posix();
    uint32_t wakes = nav_diag_get_wake_count();
    disp_state_t st = nav_display_get_state();
    int vol = nav_settings_get_volume();
    int bri = nav_settings_get_brightness();

    char json[512];
    snprintf(json, sizeof(json),
        "{"
        "\"product\":\"IRIS\","
        "\"version\":\"v0.3.1\","
        "\"state\":\"%s\","
        "\"ip\":\"%s\","
        "\"rssi_dbm\":%d,"
        "\"free_heap_b\":%d,"
        "\"uptime_epoch\":%u,"
        "\"wake_count\":%u,"
        "\"volume\":%d,"
        "\"brightness\":%d"
        "}",
        state_label(st),
        ip.ip[0] ? ip.ip : "",
        (int)rssi,
        free_heap,
        (unsigned)uptime,
        (unsigned)wakes,
        vol,
        bri);
    send_resp_json(fd, json);
}

/* Process /api/wifi/save POST body, save creds, reboot */
/* CP21: Clear stored creds, arm force_ap flag, render confirmation, reboot.
 * Same buffer-streaming pattern as route_wifi_save -- send_html_page handles
 * the HEAD overflow trap. */
static void route_wifi_forget(int fd) {
    nav_wifi_forget();
    /* CP22: amber banner on device LCD so the physical user knows what happened
     * (otherwise the device just reboots silently). */
    nav_display_show_mode_banner("FORGET WI-FI", 0xFFB347);

    static const char *body =
        "<div class=tag>FORGET \xc2\xb7 REBOOTING</div>"
        "<div class=title>IRIS</div>"
        "<div class=version>v0.3.1</div>"
        "<div class=eye></div>"
        "<div class=card>"
        "<h2>Wi-Fi forgotten</h2>"
        "<div class=row><span class=k>Stored SSID</span><span class=v style='color:#FF6B6B'>cleared</span></div>"
        "<div class=row><span class=k>Stored password</span><span class=v style='color:#FF6B6B'>cleared</span></div>"
        "<div class=row><span class=k>Next boot</span><span class=v style='color:#FFB347'>AP mode</span></div>"
        "</div>"
        "<div class=hint style='text-align:left'>device is rebooting now. when it comes back, look for the open Wi-Fi network <strong style='color:#4FE3F0'>IRIS-XXXX</strong> on your phone, connect, then visit <strong style='color:#4FE3F0'>http://192.168.4.1</strong> to set a new network.</div>";
    send_html_page(fd, body);

    /* Give the response time to send, then reboot. */
    tal_system_sleep(2000);
    tal_system_reset();
}

static void route_wifi_save(int fd, const char *body) {
    char ssid[64] = {0};
    char pass[64] = {0};
    if (!form_get(body, "ssid", ssid, sizeof(ssid)) || ssid[0] == 0) {
        send_str(fd, "HTTP/1.0 400 Bad Request\r\nConnection: close\r\n\r\nMissing ssid\n");
        return;
    }
    form_get(body, "pass", pass, sizeof(pass));

    /* Persist + acknowledge BEFORE reboot so the user sees confirmation */
    nav_wifi_save_kv(ssid, pass);
    /* CP22: green flash on the device LCD so the user knows the device received
     * the credentials -- without this they just see the device reboot blindly. */
    nav_display_show_wifi_received(ssid);

    /* CP20: stream via send_html_page so the 3644-byte HEAD doesn't overflow.
     * Body section only -- HEAD/FOOT are emitted by the helper. */
    char body_buf[1500];
    snprintf(body_buf, sizeof(body_buf),
        "<div class=tag>SAVED \xc2\xb7 REBOOTING</div>"
        "<div class=title>IRIS</div>"
        "<div class=version>v0.3.1</div>"
        "<div class=eye></div>"
        "<div class=card>"
        "<h2>Credentials saved</h2>"
        "<div class=row><span class=k>SSID</span><span class=v>%s</span></div>"
        "<div class=row><span class=k>Status</span><span class=v style='color:#FFB347'>Rebooting in 3s...</span></div>"
        "</div>"
        "<div class=hint>If the new network connects, the device reappears at its new IP. If not, it falls back to AP mode \"IRIS-XXXX\".</div>",
        ssid);
    send_html_page(fd, body_buf);

    /* Give the response time to send, then reboot */
    tal_system_sleep(3000);
    tal_system_reset();
}

/* ============================================================
 * CP18: Diagnostics page -- live device telemetry + manual test triggers
 *
 * Buttons POST to /api/test endpoints, which directly invoke the same
 * gesture handlers the touch driver uses. This lets a builder validate
 * the navigate / read-text path without speaking the wake word and
 * without physical touch hardware -- crucial when debugging via SSH.
 * ============================================================ */
static void route_diagnostics(int fd) {
    NW_IP_S ip = {0};
    tal_wifi_get_ip(WF_STATION, &ip);
    int    free_heap = tal_system_get_free_heap_size();
    uint32_t up_s    = (uint32_t)tal_time_get_posix();
    uint32_t wakes   = nav_diag_get_wake_count();

    static char page[6144];
    snprintf(page, sizeof(page),
        "%s"
        "<div class=tag>DIAGNOSTICS</div>"
        "<div class=title>IRIS</div>"
        "<div class=version>v0.3.1</div>"
        "<nav>"
        "<a href='/'>HOME</a>"
        "<a href='/wifi'>WI-FI</a>"
        "<a href='/settings'>SETTINGS</a>"
        "<a href='/diagnostics' class=active>DIAGNOSTICS</a>"
        "</nav>"
        "<div class=card>"
        "<h2>System</h2>"
        "<div class=row><span class=k>IP address</span><span class=v>%s</span></div>"
        "<div class=row><span class=k>Free heap</span><span class=v>%d B</span></div>"
        "<div class=row><span class=k>Uptime (epoch)</span><span class=v>%u s</span></div>"
        "<div class=row><span class=k>Wake events</span><span class=v>%u</span></div>"
        "</div>"
        "<div class=card>"
        "<h2>Manual triggers</h2>"
        "<div class=row><span class=k>Navigate (single tap)</span>"
        "<span class=v><form action='/api/test/tap' method=POST style='margin:0'>"
        "<button type=submit style='margin:0;padding:8px 14px;width:auto'>FIRE</button></form></span></div>"
        "<div class=row><span class=k>Read text (double tap)</span>"
        "<span class=v><form action='/api/test/double_tap' method=POST style='margin:0'>"
        "<button type=submit style='margin:0;padding:8px 14px;width:auto'>FIRE</button></form></span></div>"
        "<div class=row><span class=k>Identify (long press)</span>"
        "<span class=v><form action='/api/test/identify' method=POST style='margin:0'>"
        "<button type=submit style='margin:0;padding:8px 14px;width:auto'>FIRE</button></form></span></div>"
        "</div>"
        "<div class=hint style='text-align:left'>tip: the wake counter increments each time the kws engine confirms \"hi tuya\" or \"hello tuya\". if it's stuck at 0 even when speaking, the issue is upstream of the callback (mic / VAD / model).</div>"
        "%s",
        HEAD,
        ip.ip[0] ? ip.ip : "(none)",
        free_heap,
        (unsigned)up_s,
        (unsigned)wakes,
        FOOT);
    send_resp_html(fd, page);
}

static void route_test_tap(int fd) {
    nav_diag_trigger_tap();
    send_resp_json(fd, "{\"ok\":true,\"action\":\"tap\"}");
}

static void route_test_double_tap(int fd) {
    nav_diag_trigger_double_tap();
    send_resp_json(fd, "{\"ok\":true,\"action\":\"double_tap\"}");
}

static void route_test_identify(int fd) {
    nav_diag_trigger_identify();
    send_resp_json(fd, "{\"ok\":true,\"action\":\"identify\"}");
}

/* ============================================================
 * Request parser + dispatch
 * ============================================================ */
static void handle_client(int client_fd) {
    static char buf[MAX_REQ_LEN];
    int n = tal_net_recv(client_fd, buf, sizeof(buf) - 1);
    if (n <= 0) { tal_net_close(client_fd); return; }
    buf[n] = 0;

    /* Parse method + path. Format: "GET /path?... HTTP/1.x\r\n..." */
    char method[8] = {0};
    char path[128] = {0};
    sscanf(buf, "%7s %127s", method, path);

    /* CP20: detect ?ajax=1 before stripping query string. Used by /api/settings/save
     * to choose between a JSON 200 response (AJAX path) and a 303 redirect (form path). */
    bool ajax = (strstr(path, "?ajax=1") || strstr(path, "&ajax=1")) ? true : false;

    /* Strip query string from path for matching */
    char *q = strchr(path, '?');
    if (q) *q = 0;

    /* CP22b: AP-mode lockdown. When the device is running its setup AP, the
     * other pages are meaningless (no Wi-Fi → no clock/status/LLM/diagnostics).
     * Force the user toward /wifi only. /api/wifi/save and /api/status are
     * still allowed because the wifi page form needs them. The home (/) path
     * also redirects to /wifi so even bookmarks land on the right page. */
    if (s_ap_only_mode) {
        bool ap_allowed =
            strcmp(path, "/wifi") == 0 ||
            strcmp(path, "/api/wifi/save") == 0 ||
            strcmp(path, "/api/status") == 0;
        if (!ap_allowed) {
            send_str(client_fd,
                "HTTP/1.0 303 See Other\r\n"
                "Location: /wifi\r\n"
                "Connection: close\r\n\r\n");
            tal_net_close(client_fd);
            return;
        }
    }

    if (strcmp(method, "GET") == 0) {
        if (strcmp(path, "/") == 0)                      route_home(client_fd);
        else if (strcmp(path, "/wifi") == 0)             route_wifi(client_fd);
        else if (strcmp(path, "/settings") == 0)         route_settings(client_fd);
        else if (strcmp(path, "/diagnostics") == 0)      route_diagnostics(client_fd);
        else if (strcmp(path, "/api/status") == 0)       route_status(client_fd);
        else                                             send_404(client_fd);
    }
    else if (strcmp(method, "POST") == 0) {
        /* Find body after blank line */
        const char *body = strstr(buf, "\r\n\r\n");
        body = body ? body + 4 : "";

        if (strcmp(path, "/api/wifi/save") == 0)         route_wifi_save(client_fd, body);
        else if (strcmp(path, "/api/wifi/forget") == 0)  route_wifi_forget(client_fd);
        else if (strcmp(path, "/api/settings/save") == 0) route_settings_save(client_fd, body, ajax);
        else if (strcmp(path, "/api/test/tap") == 0)        route_test_tap(client_fd);
        else if (strcmp(path, "/api/test/double_tap") == 0) route_test_double_tap(client_fd);
        else if (strcmp(path, "/api/test/identify") == 0)   route_test_identify(client_fd);
        else                                             send_404(client_fd);
    }
    else {
        send_404(client_fd);
    }
    tal_net_close(client_fd);
}

static void webui_thread(void *arg) {
    (void)arg;
    int srv = tal_net_socket_create(PROTOCOL_TCP);
    if (srv < 0) { PR_ERR("[webui] socket: %d", srv); return; }
    if (tal_net_bind(srv, 0, WEBUI_PORT) != OPRT_OK) {
        PR_ERR("[webui] bind :%d failed", WEBUI_PORT);
        tal_net_close(srv); return;
    }
    if (tal_net_listen(srv, WEBUI_BACKLOG) != OPRT_OK) {
        PR_ERR("[webui] listen failed");
        tal_net_close(srv); return;
    }
    PR_NOTICE("[webui] listening on :%d", WEBUI_PORT);

    for (;;) {
        TUYA_IP_ADDR_T peer = 0;
        uint16_t port = 0;
        int client = tal_net_accept(srv, &peer, &port);
        if (client < 0) { tal_system_sleep(100); continue; }
        handle_client(client);
    }
}

void nav_webui_init(void) {
    if (s_started) return;
    s_started = true;
    THREAD_HANDLE th = NULL;
    THREAD_CFG_T cfg = { .stackDepth = 8192, .priority = 4, .thrdname = "webui" };
    tal_thread_create_and_start(&th, NULL, NULL, webui_thread, NULL, &cfg);
}
