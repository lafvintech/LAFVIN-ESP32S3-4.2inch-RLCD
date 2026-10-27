/* Demo11: the audio task exclusively owns the state machine, codec, and WAV files; the LVGL task owns the UI and refreshes.
  * Recording: ES7210 -> stereo I2S -> mono PCM -> SD WAV.
  * Playback: SD WAV -> mono PCM -> stereo I2S -> ES8311.
  * Reading order: app_main -> audio_task/handle_button -> start/step/stop,
  * then audio_ui, board_audio, and wav_store as needed. Keep file I/O out of UI callbacks. */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "audio_ui.h"
#include "board_audio.h"
#include "board_display.h"
#include "button_service.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "rlcd_lvgl_port.h"
#include "wav_store.h"

/* One I2S frame contains a left and a right 16-bit sample (4 bytes); each WAV sample is 2 bytes.
  * At 16 kHz, recording blocks are 512 frames/32 ms and playback blocks are 1024 frames/64 ms; no whole-recording buffer is used. */
#define RECORD_FRAMES 512
#define PLAY_FRAMES 1024
#define MONO_BYTES_PER_SECOND (APP_AUDIO_SAMPLE_RATE_HZ * 2U)
#define PI_F 3.14159265358979323846f
#define AUDIO_TASK_CORE 0
#define AUDIO_TASK_PRIORITY 5
#define AUDIO_TASK_STACK_BYTES 8192

_Static_assert(APP_AUDIO_SAMPLE_RATE_HZ >= 8000 && APP_AUDIO_SAMPLE_RATE_HZ <= 48000,
               "Choose an audio sample rate between 8000 and 48000 Hz");

typedef enum { RECORDER_IDLE, RECORDER_RECORDING, RECORDER_PLAYING, RECORDER_ERROR }
    recorder_state_t;

static const char *TAG = "voice_recorder";
static QueueHandle_t s_events;
/* Only audio_task accesses the recording/playback state below; button callbacks and the LVGL task do not read/write it directly. */
static recorder_state_t s_state = RECORDER_IDLE;
static int64_t s_last_audio_us;
static int64_t s_last_ui_us;
static uint64_t s_level_energy;
static uint32_t s_level_samples;
static bool s_key_down;
static bool s_key_long;
static char s_idle_detail[80];
static char s_error_detail[80];
/* Fixed-size PCM buffers reside in internal RAM; display buffers still use PSRAM. */
static int16_t s_input_stereo[RECORD_FRAMES * 2];
static int16_t s_record_mono[RECORD_FRAMES];
static int16_t s_play_mono[PLAY_FRAMES];
static int16_t s_play_stereo[PLAY_FRAMES * 2];

static const board_audio_config_t s_audio_config = {
    .sample_rate = APP_AUDIO_SAMPLE_RATE_HZ,
    .speaker_volume = APP_SPEAKER_VOLUME,
    .mic_gain_db = APP_MIC_GAIN_DB,
};

/* Count PCM payload only, excluding the 44-byte WAV header; stereo hardware does not change the file's mono data rate. */
static uint32_t bytes_to_ms(uint32_t bytes)
{
    return (uint32_t)((uint64_t)bytes * 1000 / MONO_BYTES_PER_SECOND);
}

static const char *state_name(void)
{
    switch (s_state) {
    case RECORDER_IDLE: return "IDLE";
    case RECORDER_RECORDING: return "RECORDING";
    case RECORDER_PLAYING: return "PLAYING";
    case RECORDER_ERROR: return "ERROR";
    }
    return "ERROR";
}

static uint8_t consume_mic_level(void)
{
    if (s_level_samples == 0) {
        return 0;
    }
    const double rms = sqrt((double)s_level_energy / s_level_samples);
    s_level_energy = 0;
    s_level_samples = 0;
    if (rms < 1.0) {
        return 0;
    }
    /* Map RMS from -60..0 dBFS to 0..100 as a level indicator, not a sound-pressure measurement. */
    const double db = 20.0 * log10(rms / 32768.0);
    int level = (int)((db + 60.0) * (100.0 / 60.0));
    if (level < 0) {
        level = 0;
    } else if (level > 100) {
        level = 100;
    }
    return (uint8_t)level;
}

/* Publish every 500 ms by default and force publication on state changes; copy snapshots without waiting for LVGL or display transfers.
  * force does not redraw immediately either; the UI fetches the latest snapshot in its own task. */
static void publish_ui_state(bool force)
{
    const bool active = s_state == RECORDER_RECORDING || s_state == RECORDER_PLAYING;
    const int64_t now = esp_timer_get_time();
    if (!force && (!active || now - s_last_ui_us < (int64_t)APP_UI_REFRESH_MS * 1000)) {
        return;
    }
    s_last_ui_us = now;
    const wav_store_info_t info = wav_store_get_info();
    audio_ui_data_t data = {
        .total_ms = bytes_to_ms(info.latest_bytes),
        .show_total = info.has_recording,
    };
    snprintf(data.state, sizeof(data.state), "%s", state_name());
    snprintf(data.file_name, sizeof(data.file_name), "%s", info.file_name);
    const char *detail = s_idle_detail;
    if (s_state == RECORDER_RECORDING) {
        detail = "Writing mono WAV directly to SD";
        data.elapsed_ms = bytes_to_ms(info.recorded_bytes);
        data.show_total = false;
        data.mic_percent = consume_mic_level();
    } else if (s_state == RECORDER_PLAYING) {
        detail = "Streaming latest WAV from SD";
        /* played_bytes is the number of bytes read from SD, not the speaker DMA's exact audible playback position. */
        data.elapsed_ms = bytes_to_ms(info.played_bytes);
        data.total_ms = bytes_to_ms(info.total_bytes);
        data.show_total = true;
        data.playback_percent = info.total_bytes == 0 ? 0 :
            (uint8_t)((uint64_t)info.played_bytes * 100 / info.total_bytes);
    } else if (s_state == RECORDER_ERROR) {
        detail = s_error_detail;
    }
    snprintf(data.detail, sizeof(data.detail), "%s", detail);
    ESP_ERROR_CHECK(audio_ui_publish(&data));
}

static void enter_error(const char *detail)
{
    /* detail may point to wav_store's internal error buffer; copy it before cleanup so later errors cannot overwrite the cause. */
    snprintf(s_error_detail, sizeof(s_error_detail), "%.55s - KEY: retry", detail);
    ESP_LOGE(TAG, "%s", detail);
    /* On failure, discard this unfinished recording rather than finalizing it as a valid WAV; previously saved files remain unchanged. */
    wav_store_abort_recording();
    wav_store_stop_playback();
    ESP_ERROR_CHECK_WITHOUT_ABORT(board_audio_close());
    s_state = RECORDER_ERROR;
    publish_ui_state(true);
}

/* IDLE is an application state, not proof that the codec session is closed; audio_task handles the idle timeout. */
static void set_idle(const char *detail)
{
    s_state = RECORDER_IDLE;
    snprintf(s_idle_detail, sizeof(s_idle_detail), "%s", detail);
    s_last_audio_us = esp_timer_get_time();
    publish_ui_state(true);
}

static void start_recording(void)
{
    if (board_audio_open_mic() != ESP_OK) {
        enter_error("microphone open failed");
        return;
    }
    if (wav_store_start_recording(APP_AUDIO_SAMPLE_RATE_HZ) != ESP_OK) {
        enter_error(wav_store_last_error());
        return;
    }
    s_level_energy = 0;
    s_level_samples = 0;
    s_state = RECORDER_RECORDING;
    s_last_audio_us = esp_timer_get_time();
    const wav_store_info_t info = wav_store_get_info();
    ESP_LOGI(TAG, "record START %s", info.file_name);
    publish_ui_state(true);
}

static void stop_recording(void)
{
    if (wav_store_stop_recording() != ESP_OK) {
        enter_error(wav_store_last_error());
        return;
    }
    const wav_store_info_t info = wav_store_get_info();
    ESP_LOGI(TAG, "record SAVED %s, %.1f s", info.file_name,
             (double)info.latest_bytes / MONO_BYTES_PER_SECOND);
    set_idle("WAV saved - tap KEY to play");
}

static void record_step(void)
{
    if (board_audio_read(s_input_stereo, sizeof(s_input_stereo)) != ESP_OK) {
        enter_error("microphone read failed");
        return;
    }
    /* Sum as 32-bit before averaging to avoid 16-bit overflow; stereo-to-mono conversion does not change the sample rate. */
    for (size_t i = 0; i < RECORD_FRAMES; ++i) {
        const int32_t mixed = ((int32_t)s_input_stereo[i * 2] +
                               s_input_stereo[i * 2 + 1]) / 2;
        s_record_mono[i] = (int16_t)mixed;
        s_level_energy += (uint64_t)((int64_t)mixed * mixed);
    }
    s_level_samples += RECORD_FRAMES;
    if (wav_store_write_mono(s_record_mono, RECORD_FRAMES) != ESP_OK) {
        enter_error(wav_store_last_error());
        return;
    }
    s_last_audio_us = esp_timer_get_time();
}

static void start_playback(void)
{
    if (!wav_store_get_info().has_recording) {
        set_idle("No WAV yet - hold BOOT to record");
        return;
    }
    if (wav_store_start_playback(APP_AUDIO_SAMPLE_RATE_HZ) != ESP_OK) {
        enter_error(wav_store_last_error());
        return;
    }
    if (board_audio_open_speaker() != ESP_OK) {
        enter_error("speaker open failed");
        return;
    }
    s_state = RECORDER_PLAYING;
    s_last_audio_us = esp_timer_get_time();
    const wav_store_info_t info = wav_store_get_info();
    ESP_LOGI(TAG, "play START %s", info.file_name);
    publish_ui_state(true);
}

static void stop_playback(const char *detail)
{
    if (wav_store_stop_playback() != ESP_OK) {
        enter_error(wav_store_last_error());
        return;
    }
    ESP_LOGI(TAG, "play STOP: %s", detail);
    set_idle(detail);
}

static void playback_step(void)
{
    size_t count = 0;
    if (wav_store_read_mono(s_play_mono, PLAY_FRAMES, &count) != ESP_OK) {
        enter_error(wav_store_last_error());
        return;
    }
    if (count == 0) {
        stop_playback("Playback complete");
        return;
    }
    const wav_store_info_t info = wav_store_get_info();
    /* Apply 10 ms fades at the file's start/end using absolute sample positions; do not restart the fade for each audio block. */
    const uint32_t first_sample = info.played_bytes / 2 - (uint32_t)count;
    const uint32_t total_samples = info.total_bytes / 2;
    const uint32_t fade_samples = APP_AUDIO_SAMPLE_RATE_HZ / 100;
    for (size_t i = 0; i < count; ++i) {
        const uint32_t index = first_sample + (uint32_t)i;
        const uint32_t left = total_samples - index;
        float envelope = 1.0f;
        if (index < fade_samples) {
            envelope = (float)index / fade_samples;
        }
        if (left < fade_samples) {
            const float fade_out = (float)left / fade_samples;
            if (fade_out < envelope) {
                envelope = fade_out;
            }
        }
        const int16_t value = (int16_t)(s_play_mono[i] * envelope);
        s_play_stereo[i * 2] = value;
        s_play_stereo[i * 2 + 1] = value;
    }
    /* Send identical L/R output; the writable buffer allows codec software-volume processing. Reuse it only after the call returns. */
    if (board_audio_write(s_play_stereo, count * 2 * sizeof(int16_t)) != ESP_OK) {
        enter_error("speaker write failed");
        return;
    }
    s_last_audio_us = esp_timer_get_time();
}

/* Reuse the playback buffer to generate the startup tone in blocks, testing the speaker path without an SD audio file. */
static void startup_beep(void)
{
    if (board_audio_open_speaker() != ESP_OK) {
        enter_error("speaker open failed");
        return;
    }
    const uint32_t segment_frames = APP_AUDIO_SAMPLE_RATE_HZ * 120 / 1000;
    const uint32_t fade_frames = APP_AUDIO_SAMPLE_RATE_HZ / 100;
    for (unsigned segment = 0; segment < 2; ++segment) {
        const float hz = segment == 0 ? 660.0f : 990.0f;
        for (uint32_t start = 0; start < segment_frames; start += PLAY_FRAMES) {
            const uint32_t remaining = segment_frames - start;
            const size_t count = remaining < PLAY_FRAMES ? remaining : PLAY_FRAMES;
            for (size_t i = 0; i < count; ++i) {
                const uint32_t index = start + (uint32_t)i;
                float envelope = 1.0f;
                if (index < fade_frames) {
                    envelope = (float)index / fade_frames;
                } else if (segment_frames - index < fade_frames) {
                    envelope = (float)(segment_frames - index) / fade_frames;
                }
                const float phase = 2.0f * PI_F * hz * index / APP_AUDIO_SAMPLE_RATE_HZ;
                const int16_t value = (int16_t)(sinf(phase) * 25000.0f * envelope);
                s_play_stereo[i * 2] = value;
                s_play_stereo[i * 2 + 1] = value;
            }
            if (board_audio_write(s_play_stereo, count * 2 * sizeof(int16_t)) != ESP_OK) {
                enter_error("startup beep failed");
                return;
            }
        }
    }
    s_last_audio_us = esp_timer_get_time();
}

/* Remount SD and recreate the codec within the existing audio task without restarting the task or the system. */
static void retry_after_error(void)
{
    if (wav_store_remount() != ESP_OK) {
        enter_error(wav_store_last_error());
        return;
    }
    board_audio_deinit();
    if (board_audio_init(&s_audio_config) != ESP_OK) {
        enter_error("codec initialization failed");
        return;
    }
    set_idle("Recovered - hold BOOT to record");
}

static void handle_key_tap(void)
{
    switch (s_state) {
    case RECORDER_IDLE:
        start_playback();
        break;
    case RECORDER_PLAYING:
        stop_playback("Playback stopped");
        break;
    case RECORDER_ERROR:
        retry_after_error();
        break;
    case RECORDER_RECORDING:
        break;
    }
}

/* Hold BOOT to start recording and release to save; holding BOOT during playback stops playback before recording.
  * KEY counts as a short press only on release before the long-press threshold; ignore it while recording and use it to retry in ERROR. */
static void handle_button(button_service_event_t event)
{
    if (event == BUTTON_SERVICE_EVENT_BOOT_LONG_PRESSED) {
        if (s_state == RECORDER_PLAYING) {
            stop_playback("Starting a new recording");
        }
        if (s_state == RECORDER_IDLE) {
            start_recording();
        }
    } else if (event == BUTTON_SERVICE_EVENT_BOOT_RELEASED) {
        if (s_state == RECORDER_RECORDING) {
            stop_recording();
        }
    } else if (event == BUTTON_SERVICE_EVENT_USER_PRESSED) {
        s_key_down = true;
        s_key_long = false;
    } else if (event == BUTTON_SERVICE_EVENT_USER_LONG_PRESSED) {
        s_key_long = true;
    } else if (event == BUTTON_SERVICE_EVENT_USER_RELEASED) {
        const bool tap = s_key_down && !s_key_long;
        s_key_down = false;
        if (tap) {
            handle_key_tap();
        }
    }
}

/* The button task only posts without waiting; report and drop full-queue events. Do not handle audio/UI in the callback. */
static void button_callback(button_service_event_t event, void *context)
{
    (void)context;
    if (xQueueSend(s_events, &event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "button event queue full");
    }
}

static void audio_task(void *argument)
{
    (void)argument;
    ESP_LOGI(TAG, "audio task on CPU%d, priority %u",
             (int)xPortGetCoreID(), (unsigned)AUDIO_TASK_PRIORITY);

    const esp_err_t sd_err = wav_store_init();
    const esp_err_t codec_err = board_audio_init(&s_audio_config);
    if (sd_err != ESP_OK) {
        enter_error(wav_store_last_error());
    } else if (codec_err != ESP_OK) {
        enter_error("codec initialization failed");
    } else {
        set_idle(wav_store_get_info().has_recording
                     ? "SD ready - tap KEY to play latest"
                     : "SD ready - hold BOOT to record");
        startup_beep();
    }

    s_events = xQueueCreate(16, sizeof(button_service_event_t));
    ESP_ERROR_CHECK(s_events == NULL ? ESP_ERR_NO_MEM : ESP_OK);
    button_service_config_t buttons = BUTTON_SERVICE_CONFIG_DEFAULT(button_callback, NULL);
    buttons.long_press_ms = APP_RECORD_HOLD_MS;
    ESP_ERROR_CHECK(button_service_start(&buttons));

    /* Each iteration handles queued buttons before recording/playing one PCM block; synchronous I2S naturally paces active transfers.
      * When idle, wait up to 50 ms for buttons and check the 1.5 s session-close timeout.
      * Beyond dual-core separation, the critical point is that the audio path never waits for the LVGL lock or a display refresh. */
    for (;;) {
        const bool active = s_state == RECORDER_RECORDING || s_state == RECORDER_PLAYING;
        button_service_event_t event;
        if (xQueueReceive(s_events, &event, active ? 0 : pdMS_TO_TICKS(50)) == pdTRUE) {
            handle_button(event);
            while (xQueueReceive(s_events, &event, 0) == pdTRUE) {
                handle_button(event);
            }
        }
        if (s_state == RECORDER_RECORDING) {
            record_step();
        } else if (s_state == RECORDER_PLAYING) {
            playback_step();
        } else if (s_state == RECORDER_IDLE && board_audio_is_open() &&
                   esp_timer_get_time() - s_last_audio_us >= (int64_t)APP_IDLE_CLOSE_MS * 1000) {
            if (board_audio_close() != ESP_OK) {
                enter_error("codec close failed");
            }
        }
        publish_ui_state(false);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "[11] SD WAV voice recorder");
    const board_display_config_t display_config = BOARD_DISPLAY_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(board_display_init(&display_config));
    ESP_ERROR_CHECK(rlcd_lvgl_port_start());

    /* Hold the LVGL lock only while building the startup UI; once started, audio_task only publishes full snapshots to a single-slot queue.
      * Normal dual-core setup: audio on CPU0, display on CPU1; PCM buffers and files are not shared across tasks. */
    rlcd_lvgl_port_lock(UINT32_MAX);
    const esp_err_t ui_err = audio_ui_init();
    rlcd_lvgl_port_unlock();
    ESP_ERROR_CHECK(ui_err);

    if (xTaskCreatePinnedToCore(
            audio_task, "recorder_audio", AUDIO_TASK_STACK_BYTES, NULL,
            AUDIO_TASK_PRIORITY, NULL, AUDIO_TASK_CORE) != pdPASS) {
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }
    /* Returning from app_main only ends initialization; audio, button, and LVGL tasks continue running. */
}
