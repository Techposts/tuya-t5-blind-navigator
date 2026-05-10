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
 * to api.openai.com over HTTPS, sidestepping the T5 mbedtls cipher issue.
 *
 * v0.3.6: generalised to accept arbitrary Content-Type + binary bodies so
 * the multipart Whisper upload can reuse the same path. JSON path delegates
 * to openai_post_typed with content_type="application/json". */
static int openai_post_typed(const char *path, const char *content_type,
                             const uint8_t *body, size_t body_len,
                             uint8_t *resp_buf, size_t resp_buf_size,
                             const uint8_t **out_body, size_t *out_len) {
    http_client_header_t headers[] = {
        {"Content-Type", content_type}
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
        .body = body,
        .body_length = body_len,
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

static int openai_post_json(const char *path, const char *body,
                            uint8_t *resp_buf, size_t resp_buf_size,
                            const uint8_t **out_body, size_t *out_len) {
    return openai_post_typed(path, "application/json",
                             (const uint8_t *)body, strlen(body),
                             resp_buf, resp_buf_size, out_body, out_len);
}

/* ============================================================
 * v0.3.6: Whisper transcription (multipart upload)
 * ============================================================
 * Caller passes a complete RIFF/WAVE buffer (header + PCM). We wrap
 * it in multipart/form-data with model=whisper-1 + response_format=json
 * and POST to /v1/audio/transcriptions. The Flask proxy is a transparent
 * /v1/<path> passthrough so no proxy changes are needed.
 *
 * Boundary is fixed -- multipart only requires the boundary string not
 * appear in the body, and our WAV PCM payload will not contain this
 * exact 32-char ASCII sequence.
 *
 * Memory: one PSRAM allocation of ~(wav_len + 400 bytes) for the body
 * plus an 8 KB response buffer. Both freed before return. Whisper's
 * JSON response for an 8 s clip is ~50-200 bytes. */
#define WHISPER_BOUNDARY  "----iris-followup-0a1b2c3d4e5f6789"
#define WHISPER_RESP_BUF  (8 * 1024)

OPERATE_RET openai_transcribe(const uint8_t *wav, uint32_t len, char **out) {
    *out = NULL;
    if (!wav || len == 0) return OPRT_INVALID_PARM;

    static const char preamble[] =
        "--" WHISPER_BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"model\"\r\n"
        "\r\n"
        "whisper-1\r\n"
        "--" WHISPER_BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"response_format\"\r\n"
        "\r\n"
        "json\r\n"
        "--" WHISPER_BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n"
        "Content-Type: audio/wav\r\n"
        "\r\n";
    static const char trailer[] = "\r\n--" WHISPER_BOUNDARY "--\r\n";

    const size_t plen = sizeof(preamble) - 1;  /* drop trailing NUL */
    const size_t tlen = sizeof(trailer)  - 1;
    const size_t body_len = plen + len + tlen;

    uint8_t *body = (uint8_t *)tal_psram_malloc(body_len);
    if (!body) {
        PR_ERR("[WHISPER] body psram alloc %u bytes failed", (unsigned)body_len);
        return OPRT_MALLOC_FAILED;
    }
    size_t off = 0;
    memcpy(body + off, preamble, plen); off += plen;
    memcpy(body + off, wav, len);       off += len;
    memcpy(body + off, trailer, tlen);  off += tlen;

    uint8_t *rbuf = (uint8_t *)tal_psram_malloc(WHISPER_RESP_BUF);
    if (!rbuf) { tal_psram_free(body); return OPRT_MALLOC_FAILED; }

    const uint8_t *rbody = NULL; size_t rlen = 0;
    int res = openai_post_typed("/v1/audio/transcriptions",
                                "multipart/form-data; boundary=" WHISPER_BOUNDARY,
                                body, body_len,
                                rbuf, WHISPER_RESP_BUF, &rbody, &rlen);
    tal_psram_free(body);

    if (res == 0 && rbody && rlen > 0) {
        /* Whisper response: {"text":"..."} -- needs its own parser
         * because extract_chat_text walks choices/message/content. */
        cJSON *root = cJSON_Parse((const char *)rbody);
        if (root) {
            cJSON *t = cJSON_GetObjectItem(root, "text");
            if (t && cJSON_IsString(t) && t->valuestring) {
                size_t n = strlen(t->valuestring);
                *out = (char *)tal_malloc(n + 1);
                if (*out) { memcpy(*out, t->valuestring, n); (*out)[n] = 0; }
            }
            cJSON_Delete(root);
        }
    }
    tal_psram_free(rbuf);
    return (*out) ? OPRT_OK : OPRT_COM_ERROR;
}

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

/* ============================================================
 * v0.3.6: follow-up Q&A with prior-turn history
 * ============================================================
 * Builds a chat completions request with:
 *   1. system message (sets language + 1-2 sentence reply length cap)
 *   2. for each prior turn in history: user (text-only) + assistant (text)
 *   3. current user turn: text + image
 *
 * The image lives only on the current turn -- the assistant's prior textual
 * response in the message list keeps the model anchored on what was seen,
 * so we do not pay the base64 + bandwidth cost of replaying the image on
 * every follow-up. Verified empirically with gpt-4o-mini: it correctly
 * reasons about objects in a single image referenced across multi-turn text
 * exchanges. */
OPERATE_RET openai_ask_followup(const uint8_t *jpeg, uint32_t jpeg_len,
                                const char *current_question,
                                const char *language_name,
                                const openai_turn_t *history, int history_count,
                                char **out) {
    *out = NULL;
    if (!jpeg || !current_question || !language_name) return OPRT_INVALID_PARM;

    char *b64 = base64_encode_alloc(jpeg, jpeg_len);
    if (!b64) return OPRT_MALLOC_FAILED;
    size_t uri_len = strlen("data:image/jpeg;base64,") + strlen(b64) + 1;
    char *data_uri = (char *)tal_psram_malloc(uri_len);
    if (!data_uri) { tal_psram_free(b64); return OPRT_MALLOC_FAILED; }
    snprintf(data_uri, uri_len, "data:image/jpeg;base64,%s", b64);
    tal_psram_free(b64);

    cJSON *root = cJSON_CreateObject();
    if (!root) { tal_psram_free(data_uri); return OPRT_MALLOC_FAILED; }
    cJSON_AddStringToObject(root, "model", "gpt-4o-mini");
    cJSON *messages = cJSON_AddArrayToObject(root, "messages");

    /* 1. system: language + length cap */
    {
        char sys[320];
        snprintf(sys, sizeof(sys),
                 "You previously answered a question about this image for a blind person. "
                 "The user is asking a follow-up. Reply in 1 to 2 conversational sentences in %s. "
                 "Be specific and use exactly what is visible. Do not say 'I see' or 'the image shows'. "
                 "If the follow-up cannot be answered from this image, say so briefly.",
                 language_name);
        cJSON *m = cJSON_CreateObject();
        cJSON_AddStringToObject(m, "role", "system");
        cJSON_AddStringToObject(m, "content", sys);
        cJSON_AddItemToArray(messages, m);
    }

    /* 2. history (text-only) -- oldest first */
    for (int i = 0; i < history_count; i++) {
        if (history[i].user && history[i].user[0]) {
            cJSON *u = cJSON_CreateObject();
            cJSON_AddStringToObject(u, "role", "user");
            cJSON_AddStringToObject(u, "content", history[i].user);
            cJSON_AddItemToArray(messages, u);
        }
        if (history[i].assistant && history[i].assistant[0]) {
            cJSON *a = cJSON_CreateObject();
            cJSON_AddStringToObject(a, "role", "assistant");
            cJSON_AddStringToObject(a, "content", history[i].assistant);
            cJSON_AddItemToArray(messages, a);
        }
    }

    /* 3. current user turn: text + image */
    {
        cJSON *u = cJSON_CreateObject();
        cJSON_AddStringToObject(u, "role", "user");
        cJSON *content = cJSON_AddArrayToObject(u, "content");
        cJSON *text_part = cJSON_CreateObject();
        cJSON_AddStringToObject(text_part, "type", "text");
        cJSON_AddStringToObject(text_part, "text", current_question);
        cJSON_AddItemToArray(content, text_part);
        cJSON *img_part = cJSON_CreateObject();
        cJSON_AddStringToObject(img_part, "type", "image_url");
        cJSON *img_url = cJSON_AddObjectToObject(img_part, "image_url");
        cJSON_AddStringToObject(img_url, "url", data_uri);
        cJSON_AddItemToArray(content, img_part);
        cJSON_AddItemToArray(messages, u);
    }

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
