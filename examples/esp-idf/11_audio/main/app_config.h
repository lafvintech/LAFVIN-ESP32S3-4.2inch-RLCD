#pragma once

/* User settings: rebuild and flash after changes; no custom menuconfig menu is needed.
  * I2S always uses stereo 16-bit samples; saved WAV files use mono 16-bit samples. */
#define APP_AUDIO_SAMPLE_RATE_HZ  16000U
/* Speaker volume is 0..100 and microphone gain is in dB; check for recording clipping after changing gain. */
#define APP_SPEAKER_VOLUME        100
#define APP_MIC_GAIN_DB           35.0f
/* Hold BOOT for this duration to start recording; a short press does not record. */
#define APP_RECORD_HOLD_MS        700U
/* Interval for publishing the latest UI snapshot from the audio task; do not wait for UI consumption or screen refresh. */
#define APP_UI_REFRESH_MS         500U
/* Delay before closing the codec session after IDLE; this does not delete devices or stop the audio task. */
#define APP_IDLE_CLOSE_MS         1500U
