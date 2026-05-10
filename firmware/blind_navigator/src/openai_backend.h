#pragma once
#include "tuya_cloud_types.h"

/* WAV -> transcript only (no chat). Caller must free *out_transcript */
OPERATE_RET openai_transcribe(const uint8_t *wav_data, uint32_t wav_len, char **out_transcript);

/* transcript -> GPT-4o-mini text response. Caller must free *out_text */
OPERATE_RET openai_ask_text(const char *transcript, char **out_text);

/* WAV -> ASR + chat combined (legacy). Caller must free *out_text */
OPERATE_RET openai_ask_audio(const uint8_t *wav_data, uint32_t wav_len, char **out_text);

/* JPEG + prompt -> vision description. Caller must free *out_text */
OPERATE_RET openai_ask_image(const uint8_t *jpeg_data, uint32_t jpeg_len, const char *prompt, char **out_text);

/* text -> PCM audio. Caller must free *out_pcm */
OPERATE_RET openai_tts(const char *text, uint8_t **out_pcm, uint32_t *out_len);

/* v0.3.4: streaming TTS. Opens a raw TCP connection to the proxy, sends the
 * /v1/audio/speech POST, reads the response body in fragments and invokes
 * `cb(ctx, chunk_data, chunk_len)` for each fragment as bytes arrive.
 *
 * Returns OPRT_OK only if the HTTP request reached 200 and the full body
 * was streamed without socket errors. Caller's cb may be invoked multiple
 * times (once per recv() return); each call gives a fresh slice of the
 * response body. The buffer is owned by the openai_backend internally --
 * cb MUST consume (copy / forward) within the callback; pointer is invalid
 * after return.
 *
 * cb returns 0 to continue, non-zero to abort the stream early. */
typedef int (*openai_tts_chunk_cb)(void *ctx, const uint8_t *data, size_t len);
OPERATE_RET openai_tts_stream(const char *text, openai_tts_chunk_cb cb, void *ctx);
