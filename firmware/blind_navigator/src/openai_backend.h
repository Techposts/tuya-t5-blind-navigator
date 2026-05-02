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
