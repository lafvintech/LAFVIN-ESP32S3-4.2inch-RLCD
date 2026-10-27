#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    bool ready;
    bool has_recording;
    char file_name[16];
    /* All counts are PCM payload bytes, excluding the 44-byte header; played is the SD read position, not the audible position. */
    uint32_t latest_bytes;
    uint32_t recorded_bytes;
    uint32_t played_bytes;
    uint32_t total_bytes;
} wav_store_info_t;

/* Single-task use with no internal mutex; supports only a fixed 44-byte header and mono signed 16-bit PCM, not general decoding.
  * Create /sdcard/recordings/REC0001.WAV ... without overwriting old files; /sdcard is the VFS mount point. */
esp_err_t wav_store_init(void);
/* Discard any unfinished recording and close playback before remounting; no automatic formatting or hot-plug detection. */
esp_err_t wav_store_remount(void);
/* info is returned by value; last_error borrows an internal mutable buffer, so copy the reason before further operations. */
wav_store_info_t wav_store_get_info(void);
const char *wav_store_last_error(void);
esp_err_t wav_store_start_recording(uint32_t sample_rate);
/* count/capacity are int16_t sample counts, not bytes; finalize the WAV header when recording stops. */
esp_err_t wav_store_write_mono(const int16_t *samples, size_t count);
esp_err_t wav_store_stop_recording(void);
/* Delete the new unfinished recording without changing the previous valid recording. */
void wav_store_abort_recording(void);
esp_err_t wav_store_start_playback(uint32_t sample_rate);
/* Normal completion requires ESP_OK and *count==0; short reads/truncation are errors, not playback completion. */
esp_err_t wav_store_read_mono(int16_t *samples, size_t capacity, size_t *count);
esp_err_t wav_store_stop_playback(void);
