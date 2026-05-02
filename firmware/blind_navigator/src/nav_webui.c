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

/* CP18: diagnostic counters + test triggers (defined in main.c) */
extern uint32_t nav_diag_get_wake_count(void);
extern void     nav_diag_trigger_tap(void);
extern void     nav_diag_trigger_double_tap(void);

static bool s_started = false;

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
static void route_home(int fd) {
    /* Body only -- HEAD and FOOT are streamed by send_html_page */
    static const char *body =
        "<div class=tag>READY \xc2\xb7 ONLINE</div>"
        "<div class=title>IRIS</div>"
        "<div class=version>VISION CO-PILOT \xc2\xb7 v0.3.0-dev</div>"
        "<nav>"
        "<a href='/' class=active>HOME</a>"
        "<a href='/wifi'>WI-FI</a>"
        "<a href='/settings'>SETTINGS</a>"
        "<a href='/diagnostics'>DIAGNOSTICS</a>"
        "</nav>"
        "<div class=eye></div>"
        "<div class=card>"
        "<h2>Quick links</h2>"
        "<div class=row><span class=k>Setup Wi-Fi</span><span class=v><a href='/wifi'>Configure &rarr;</a></span></div>"
        "<div class=row><span class=k>Tune device</span><span class=v><a href='/settings'>Settings &rarr;</a></span></div>"
        "<div class=row><span class=k>Live status</span><span class=v><a href='/diagnostics'>Diagnostics &rarr;</a></span></div>"
        "</div>";
    send_html_page(fd, body);
}

/* Render Wi-Fi setup page. Performs an in-process scan and lists results. */
static void route_wifi(int fd) {
    /* Buffer for the rendered page; large enough for ~30 SSIDs */
    static char page[6144];
    int off = 0;
    off += snprintf(page + off, sizeof(page) - off,
        "%s"
        "<div class=tag>WI-FI SETUP</div>"
        "<div class=title>IRIS</div>"
        "<div class=version>v0.3.0-dev</div>"
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
        "</div>"
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
        "<div class=version>v0.3.0-dev</div>"
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

static void route_settings_save(int fd, const char *body) {
    char vol[16] = {0}, bri[16] = {0}, voice[16] = {0}, lang[16] = {0}, wakefb[8] = {0};
    if (form_get(body, "volume", vol, sizeof(vol)))      nav_settings_set_volume(atoi(vol));
    if (form_get(body, "brightness", bri, sizeof(bri)))  nav_settings_set_brightness(atoi(bri));
    if (form_get(body, "voice", voice, sizeof(voice)))   nav_settings_set_voice(voice);
    if (form_get(body, "language", lang, sizeof(lang)))  nav_settings_set_language(lang);
    if (form_get(body, "wakefb", wakefb, sizeof(wakefb))) nav_settings_set_wake_feedback(atoi(wakefb));

    char resp[2048];
    snprintf(resp, sizeof(resp),
        "%s"
        "<div class=tag>SAVED</div>"
        "<div class=title>IRIS</div>"
        "<div class=version>v0.3.0-dev</div>"
        "<nav>"
        "<a href='/'>HOME</a>"
        "<a href='/settings' class=active>SETTINGS</a>"
        "</nav>"
        "<div class=card>"
        "<h2>Settings saved</h2>"
        "<div class=row><span class=k>Volume</span><span class=v>%s%%</span></div>"
        "<div class=row><span class=k>Brightness</span><span class=v>%s%%</span></div>"
        "<div class=row><span class=k>Voice</span><span class=v>%s</span></div>"
        "<div class=row><span class=k>Language</span><span class=v>%s</span></div>"
        "<div class=row><span class=k>Wake feedback</span><span class=v>%s</span></div>"
        "<div class=hint style='margin-top:14px;text-align:left'>some settings (language, voice) take effect on next response. brightness applies immediately if hardware backlight control is wired.</div>"
        "</div>"
        "<a href='/settings'><button type=button style='margin-top:12px'>BACK TO SETTINGS</button></a>"
        "%s",
        HEAD,
        vol[0] ? vol : "(unchanged)",
        bri[0] ? bri : "(unchanged)",
        voice[0] ? voice : "(unchanged)",
        lang[0] ? lang : "(unchanged)",
        wakefb[0] ? (atoi(wakefb) ? "ON" : "OFF") : "(unchanged)",
        FOOT);
    send_resp_html(fd, resp);
}

/* JSON status endpoint -- dynamic device state */
static void route_status(int fd) {
    NW_IP_S ip = {0};
    tal_wifi_get_ip(WF_STATION, &ip);
    char json[256];
    snprintf(json, sizeof(json),
        "{\"product\":\"IRIS\",\"version\":\"v0.3.0-dev\",\"ip\":\"%s\",\"uptime_s\":%u}",
        ip.ip[0] ? ip.ip : "", (unsigned)(tal_time_get_posix() % 100000));
    send_resp_json(fd, json);
}

/* Process /api/wifi/save POST body, save creds, reboot */
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

    char resp[1500];
    snprintf(resp, sizeof(resp),
        "%s"
        "<div class=tag>SAVED \xc2\xb7 REBOOTING</div>"
        "<div class=title>IRIS</div>"
        "<div class=version>v0.3.0-dev</div>"
        "<div class=eye></div>"
        "<div class=card>"
        "<h2>Credentials saved</h2>"
        "<div class=row><span class=k>SSID</span><span class=v>%s</span></div>"
        "<div class=row><span class=k>Status</span><span class=v style='color:#FFB347'>Rebooting in 3s...</span></div>"
        "</div>"
        "<div class=hint>If the new network connects, the device reappears at its new IP. If not, it falls back to AP mode \"IRIS-XXXX\".</div>"
        "%s",
        HEAD, ssid, FOOT);
    send_resp_html(fd, resp);

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
        "<div class=version>v0.3.0-dev</div>"
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

    /* Strip query string from path for matching */
    char *q = strchr(path, '?');
    if (q) *q = 0;

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
        else if (strcmp(path, "/api/settings/save") == 0) route_settings_save(client_fd, body);
        else if (strcmp(path, "/api/test/tap") == 0)        route_test_tap(client_fd);
        else if (strcmp(path, "/api/test/double_tap") == 0) route_test_double_tap(client_fd);
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
