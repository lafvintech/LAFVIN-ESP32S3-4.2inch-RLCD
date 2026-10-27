#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    uint32_t sample_rate;
    int speaker_volume;
    float mic_gain_db;
} board_audio_config_t;

#define BOARD_AUDIO_CONFIG_DEFAULT() \
    {.sample_rate = 16000, .speaker_volume = 100, .mic_gain_db = 35.0f}

/* Exclusive to one application task, with no internal mutex; PCM is fixed to interleaved L/R signed 16-bit samples.
  * This adapter makes recording/playback sessions mutually exclusive; that policy is not a hardware full-duplex limitation.
  * init creates resources, open starts a session, close ends it, and deinit destroys resources; shared I2C is not destroyed here. */
esp_err_t board_audio_init(const board_audio_config_t *config);
void board_audio_deinit(void);
/* ready means device resources exist; open means a recording/playback session is active. These are distinct states. */
bool board_audio_is_ready(void);
bool board_audio_is_open(void);
esp_err_t board_audio_open_speaker(void);
esp_err_t board_audio_open_mic(void);
esp_err_t board_audio_close(void);
/* Synchronous whole-block I/O returns esp_err_t, not a byte count; bytes must be nonzero and a multiple of 4.
  * Reuse the buffer only after the call returns; do not call from an ISR or UI callback. */
esp_err_t board_audio_read(void *pcm, size_t bytes);
/* Software volume control may modify samples in place; the PCM buffer must be writable. */
esp_err_t board_audio_write(void *pcm, size_t bytes);
