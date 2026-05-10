#include "openai_backend.h"
#include "nav_config.h"
#include "tal_log.h"
#include "tal_memory.h"
#include "tal_network.h"
#include "tal_system.h"
#include "http_client_interface.h"
#include "cJSON.h"
#include "mbedtls/base64.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define RESP_BUF_SIZE (128 * 1024)

static char *base64_encode_alloc(const uint8_t *data, size_t len) {
    size_t out_len = 0;
    mbedtls_base64_encode(NULL, 0, &out_len, data, len);
    char *buf = (char *)tal_psram_malloc(out_len + 1);
    if (!buf) return NULL;
    mbedtls_base64_encode((unsigned char *)buf, out_len + 1, &out_len, data, len);
    buf[out_len] = 0;
    return buf;
}

static char *extract_chat_text(const char *json_str) {
    cJSON *root = cJSON_Parse(json_str);
    if (!root) return NULL;
    char *result = NULL;
    cJSON *choices = cJSON_GetObjectItem(root, "choices");
    if (choices && cJSON_IsArray(choices)) {
        cJSON *msg = cJSON_GetObjectItem(cJSON_GetArrayItem(choices, 0), "message");
        cJSON *content = cJSON_GetObjectItem(msg, "content");
        if (content && cJSON_IsString(content)) {
            result = tal_malloc(strlen(content->valuestring) + 1);
            if (result) strcpy(result, content->valuestring);
        }
    }
    cJSON_Delete(root);
    return result;
}

/* Plain HTTP to local Flask proxy. Proxy injects Authorization and forwards
 * to api.openai.com over HTTPS, sidestepping the T5 mbedtls cipher issue. */
static int openai_post_json(const char *path, const char *body,
                            uint8_t *resp_buf, size_t resp_buf_size,
                            const uint8_t **out_body, size_t *out_len) {
    http_client_header_t headers[] = {
        {"Content-Type", "application/json"}
    };
    http_client_request_t req = {
        .host = NAV_PROXY_HOST,
        .port = NAV_PROXY_PORT,
        .path = path,
        .cacert = NULL,
        .tls_no_verify = false,
        .method = "POST",
        .headers = headers,
        .headers_count = 1,
        .body = (const uint8_t *)body,
        .body_length = strlen(body),
        .timeout_ms = 60000
    };
    http_client_response_t resp = { .buffer = resp_buf, .buffer_length = resp_buf_size };
    http_client_status_t st = http_client_request(&req, &resp);
    if (st != HTTP_CLIENT_SUCCESS || resp.status_code != 200) {
        PR_ERR("proxy POST %s failed: st=%d http=%d", path, (int)st, resp.status_code);
        return -1;
    }
    if (out_body) *out_body = resp.body;
    if (out_len)  *out_len  = resp.body_length;
    return 0;
}

OPERATE_RET openai_transcribe(const uint8_t *wav, uint32_t len, char **out) { return OPRT_COM_ERROR; }
OPERATE_RET openai_ask_text(const char *text, char **out) { return OPRT_COM_ERROR; }
OPERATE_RET openai_ask_audio(const uint8_t *wav, uint32_t len, char **out) { return OPRT_COM_ERROR; }

OPERATE_RET openai_ask_image(const uint8_t *jpeg, uint32_t len, const char *prompt, char **out) {
    *out = NULL;
    if (!jpeg || !prompt) return OPRT_INVALID_PARM;

    char *b64 = base64_encode_alloc(jpeg, len);
    if (!b64) return OPRT_MALLOC_FAILED;

    /* Build the data URI: "data:image/jpeg;base64,<b64>" */
    size_t uri_len = strlen("data:image/jpeg;base64,") + strlen(b64) + 1;
    char *data_uri = (char *)tal_psram_malloc(uri_len);
    if (!data_uri) { tal_psram_free(b64); return OPRT_MALLOC_FAILED; }
    snprintf(data_uri, uri_len, "data:image/jpeg;base64,%s", b64);
    tal_psram_free(b64);

    /* Build body via cJSON so prompt + URI are properly JSON-escaped */
    cJSON *root = cJSON_CreateObject();
    if (!root) { tal_psram_free(data_uri); return OPRT_MALLOC_FAILED; }
    cJSON_AddStringToObject(root, "model", "gpt-4o-mini");
    cJSON *messages = cJSON_AddArrayToObject(root, "messages");
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "role", "user");
    cJSON *content = cJSON_AddArrayToObject(msg, "content");
    cJSON *text_part = cJSON_CreateObject();
    cJSON_AddStringToObject(text_part, "type", "text");
    cJSON_AddStringToObject(text_part, "text", prompt);
    cJSON_AddItemToArray(content, text_part);
    cJSON *img_part = cJSON_CreateObject();
    cJSON_AddStringToObject(img_part, "type", "image_url");
    cJSON *img_url = cJSON_AddObjectToObject(img_part, "image_url");
    cJSON_AddStringToObject(img_url, "url", data_uri);
    cJSON_AddItemToArray(content, img_part);
    cJSON_AddItemToArray(messages, msg);
    cJSON_AddNumberToObject(root, "max_tokens", 120);

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    tal_psram_free(data_uri);
    if (!body) return OPRT_MALLOC_FAILED;

    uint8_t *rbuf = (uint8_t *)tal_psram_malloc(RESP_BUF_SIZE);
    if (!rbuf) { cJSON_free(body); return OPRT_MALLOC_FAILED; }
    const uint8_t *rbody = NULL; size_t rlen = 0;
    int res = openai_post_json("/v1/chat/completions", body, rbuf, RESP_BUF_SIZE, &rbody, &rlen);
    if (res == 0 && rbody && rlen > 0) {
        ((char *)rbuf)[(rlen < RESP_BUF_SIZE) ? rlen : RESP_BUF_SIZE - 1] = 0;
        *out = extract_chat_text((const char *)rbody);
    }
    cJSON_free(body);
    tal_psram_free(rbuf);
    return (*out) ? OPRT_OK : OPRT_COM_ERROR;
}

OPERATE_RET openai_tts(const char *text, uint8_t **out, uint32_t *out_len) {
    *out = NULL;
    if (!text) return OPRT_INVALID_PARM;
    /* Build body via cJSON so input text is properly JSON-escaped (quotes, backslashes, newlines) */
    cJSON *root = cJSON_CreateObject();
    if (!root) return OPRT_MALLOC_FAILED;
    cJSON_AddStringToObject(root, "model", "gpt-4o-mini-tts");
    cJSON_AddStringToObject(root, "input", text);
    cJSON_AddStringToObject(root, "voice", "alloy");
    cJSON_AddStringToObject(root, "response_format", "wav");
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) return OPRT_MALLOC_FAILED;
    uint8_t *rbuf = (uint8_t *)tal_psram_malloc(RESP_BUF_SIZE);
    if (!rbuf) return OPRT_MALLOC_FAILED;
    const uint8_t *rbody = NULL; size_t rlen = 0;
    if (openai_post_json("/v1/audio/speech", body, rbuf, RESP_BUF_SIZE, &rbody, &rlen) == 0 && rlen > 0) {
        *out = (uint8_t *)tal_psram_malloc(rlen);
        if (*out) {
            memcpy(*out, rbody, rlen);
            *out_len = (uint32_t)rlen;
        }
    }
    tal_psram_free(rbuf);
    return (*out) ? OPRT_OK : OPRT_COM_ERROR;
}

/* ============================================================
 * Streaming TTS over raw TCP (v0.3.4 / v0.3.5)
 * ============================================================
 * The synchronous openai_tts() above downloads the entire WAV before any
 * audio plays. For a 10 s welcome that's 3-5 s perceived latency before
 * any sound + a fixed-size response buffer that has to hold the whole
 * payload (~320 KB).
 *
 * openai_tts_stream() opens a raw TCP socket to the Flask proxy, writes
 * the HTTP/1.1 POST manually, parses just enough response headers to find
 * the body, then forwards each recv() chunk to the caller's callback.
 * Per-call memory: ~6 KB (one recv buffer + small header buffer),
 * independent of audio length. Audio starts playing within ~500 ms of
 * the first chunk arriving instead of waiting for the full download. */

#define TTS_STREAM_RECV_BUF  4096
#define TTS_STREAM_HDR_BUF   2048

static int parse_status_code(const char *line) {
    /* "HTTP/1.1 200 OK\r\n" -> 200 */
    const char *sp = strchr(line, ' ');
    if (!sp) return -1;
    return atoi(sp + 1);
}

OPERATE_RET openai_tts_stream(const char *text, openai_tts_chunk_cb cb, void *ctx) {
    if (!text || !cb) return OPRT_INVALID_PARM;

    /* Build JSON body via cJSON so quotes / newlines are escaped. */
    cJSON *root = cJSON_CreateObject();
    if (!root) return OPRT_MALLOC_FAILED;
    cJSON_AddStringToObject(root, "model",  "gpt-4o-mini-tts");
    cJSON_AddStringToObject(root, "input",  text);
    cJSON_AddStringToObject(root, "voice",  "alloy");
    cJSON_AddStringToObject(root, "response_format", "wav");
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) return OPRT_MALLOC_FAILED;
    int body_len = (int)strlen(body);

    /* NAV_PROXY_HOST is an IP literal so str2addr suffices -- no DNS. */
    int fd = tal_net_socket_create(PROTOCOL_TCP);
    if (fd < 0) { cJSON_free(body); return OPRT_COM_ERROR; }

    TUYA_IP_ADDR_T addr = tal_net_str2addr(NAV_PROXY_HOST);
    if (tal_net_connect(fd, addr, NAV_PROXY_PORT) != 0) {
        PR_ERR("[TTS-STREAM] connect %s:%d failed", NAV_PROXY_HOST, (int)NAV_PROXY_PORT);
        tal_net_close(fd);
        cJSON_free(body);
        return OPRT_COM_ERROR;
    }

    /* Connection: close so the proxy signals EOF by closing the socket --
     * simpler than parsing chunked transfer encoding or tracking
     * Content-Length on receive. Flask's default response uses
     * Content-Length, but Connection: close is honored either way.
     *
     * v0.3.5 fix: build the entire request (headers + body) in a single
     * buffer and write with one tal_net_send call. Splitting into two
     * sends caused intermittent failures where Flask's HTTP parser saw
     * the headers, decided the request was malformed (no body yet
     * within its read window), and rejected. */
    int req_cap = 256 + body_len;
    char *req = (char *)tal_malloc(req_cap);
    if (!req) { tal_net_close(fd); cJSON_free(body); return OPRT_MALLOC_FAILED; }
    int req_len = snprintf(req, req_cap,
        "POST /v1/audio/speech HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n%s",
        NAV_PROXY_HOST, (int)NAV_PROXY_PORT, body_len, body);
    cJSON_free(body);
    int sent = tal_net_send(fd, req, req_len);
    tal_free(req);
    if (sent != req_len) {
        PR_ERR("[TTS-STREAM] send failed: sent=%d expected=%d", sent, req_len);
        tal_net_close(fd);
        return OPRT_COM_ERROR;
    }

    /* Phase 1: read until end-of-headers ("\r\n\r\n"). */
    uint8_t *recv_buf = (uint8_t *)tal_malloc(TTS_STREAM_RECV_BUF);
    if (!recv_buf) { tal_net_close(fd); return OPRT_MALLOC_FAILED; }
    char hdr_buf[TTS_STREAM_HDR_BUF];
    int hdr_used = 0;
    int status = -1;
    int total_body = 0;

    while (1) {
        int n = tal_net_recv(fd, recv_buf, TTS_STREAM_RECV_BUF);
        if (n <= 0) {
            PR_ERR("[TTS-STREAM] recv during headers returned %d", n);
            tal_free(recv_buf);
            tal_net_close(fd);
            return OPRT_COM_ERROR;
        }
        int can_take = (int)sizeof(hdr_buf) - hdr_used - 1;
        int take = (n < can_take) ? n : can_take;
        if (take <= 0) { tal_free(recv_buf); tal_net_close(fd); return OPRT_COM_ERROR; }
        memcpy(hdr_buf + hdr_used, recv_buf, take);
        hdr_used += take;
        hdr_buf[hdr_used] = 0;

        char *body_start = strstr(hdr_buf, "\r\n\r\n");
        if (!body_start) continue;  /* keep reading */

        status = parse_status_code(hdr_buf);
        if (status != 200) {
            PR_ERR("[TTS-STREAM] HTTP status %d", status);
            tal_free(recv_buf);
            tal_net_close(fd);
            return OPRT_COM_ERROR;
        }
        int header_len = (int)(body_start - hdr_buf) + 4;
        /* Body bytes captured along with headers in hdr_buf. */
        int body_in_hdr = hdr_used - header_len;
        if (body_in_hdr > 0) {
            if (cb(ctx, (const uint8_t *)(hdr_buf + header_len), (size_t)body_in_hdr) != 0) {
                tal_free(recv_buf); tal_net_close(fd); return OPRT_COM_ERROR;
            }
            total_body += body_in_hdr;
        }
        /* Any body bytes still sitting in recv_buf beyond `take`. */
        int body_in_recv = n - take;
        if (body_in_recv > 0) {
            if (cb(ctx, recv_buf + take, (size_t)body_in_recv) != 0) {
                tal_free(recv_buf); tal_net_close(fd); return OPRT_COM_ERROR;
            }
            total_body += body_in_recv;
        }
        break;
    }

    /* Phase 2: pure body streaming until socket close. */
    PR_NOTICE("[TTS-STREAM] headers ok (status %d), streaming body", status);
    while (1) {
        int n = tal_net_recv(fd, recv_buf, TTS_STREAM_RECV_BUF);
        if (n == 0) break;       /* clean EOF */
        if (n < 0) {
            PR_ERR("[TTS-STREAM] recv error %d after %d body bytes", n, total_body);
            tal_free(recv_buf);
            tal_net_close(fd);
            return OPRT_COM_ERROR;
        }
        total_body += n;
        if (cb(ctx, recv_buf, (size_t)n) != 0) {
            PR_NOTICE("[TTS-STREAM] caller aborted at %d body bytes", total_body);
            break;
        }
    }
    PR_NOTICE("[TTS-STREAM] done, %d body bytes", total_body);

    tal_free(recv_buf);
    tal_net_close(fd);
    return (total_body > 0) ? OPRT_OK : OPRT_COM_ERROR;
}
