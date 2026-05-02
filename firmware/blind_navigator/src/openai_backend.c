#include "openai_backend.h"
#include "nav_config.h"
#include "tal_log.h"
#include "tal_memory.h"
#include "http_client_interface.h"
#include "cJSON.h"
#include "mbedtls/base64.h"
#include <string.h>
#include <stdio.h>

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
