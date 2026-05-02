/* nav_webui.h -- IRIS embedded web UI (CP14+)
 *
 * Minimal HTTP/1.0 server listening on :80 once Wi-Fi (station or AP)
 * is up. Serves a hello-world page in CP14; full Wi-Fi setup + settings
 * UI in CP15+. Styled to match the device LCD (Space Grotesk, deep
 * obsidian, IRIS state colors). */
#ifndef __NAV_WEBUI_H__
#define __NAV_WEBUI_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Spawn the web UI thread. Idempotent: subsequent calls are no-ops. */
void nav_webui_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __NAV_WEBUI_H__ */
