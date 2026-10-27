#include <Arduino.h>
#include "src/espressif/codec_board/codec_board.h"
#include "src/espressif/codec_board/codec_init.h"
#include "src/espressif/esp_codec_dev/include/esp_codec_dev.h"
#include "audio_bsp.h"

namespace {
// Matches codec_board's board profile, which determines the actual audio pins.
const char *kBoardType = "S3_RLCD_4_2";

esp_codec_dev_handle_t s_playback = nullptr;
esp_codec_dev_handle_t s_record = nullptr;

esp_codec_dev_sample_info_t toSampleInfo(const AudioFormat &fmt) {
  esp_codec_dev_sample_info_t fs = {};
  fs.sample_rate = fmt.sampleRate;
  fs.channel = fmt.channels;
  fs.bits_per_sample = fmt.bitsPerSample;
  return fs;
}
}  // namespace

bool AudioCodec::begin() {
  if (ready_) return true;

  set_codec_board_type(kBoardType);

  if (init_i2c(0) != 0) {
    Serial.println(F("[audio] I2C0 init FAILED"));
    return false;
  }

  codec_init_cfg_t cfg = {};
  cfg.in_mode = CODEC_I2S_MODE_TDM;
  cfg.out_mode = CODEC_I2S_MODE_TDM;
  cfg.in_use_tdm = false;
  cfg.reuse_dev = false;
  if (init_codec(&cfg) != 0) {
    Serial.println(F("[audio] init_codec FAILED"));
    return false;
  }

  s_playback = get_playback_handle();
  s_record = get_record_handle();
  ready_ = (s_playback != nullptr && s_record != nullptr);
  if (!ready_) {
    Serial.printf("[audio] codec handles missing: playback=%p record=%p\n",
                  s_playback, s_record);
  }
  return ready_;
}

bool AudioCodec::openSpeaker(const AudioFormat &fmt) {
  if (!s_playback) return false;
  if (speakerOpen_) closeSpeaker();
  // This adapter allows only one recording/playback session; close the microphone before opening the speaker.
  if (micOpen_) closeMic();

  esp_codec_dev_sample_info_t fs = toSampleInfo(fmt);
  if (esp_codec_dev_open(s_playback, &fs) != ESP_CODEC_DEV_OK) return false;
  speakerOpen_ = true;
  speakerFmt_ = fmt;
  esp_codec_dev_set_out_vol(s_playback, volume_);
  return true;
}

bool AudioCodec::openMic(const AudioFormat &fmt) {
  if (!s_record) return false;
  if (micOpen_) closeMic();
  if (speakerOpen_) closeSpeaker();  // Mirrors openSpeaker: this example keeps only one direction active

  esp_codec_dev_sample_info_t fs = toSampleInfo(fmt);
  if (esp_codec_dev_open(s_record, &fs) != ESP_CODEC_DEV_OK) return false;
  micOpen_ = true;
  micFmt_ = fmt;
  esp_codec_dev_set_in_gain(s_record, micGainDb_);
  return true;
}

bool AudioCodec::ensureSpeaker(const AudioFormat &fmt) {
  // Reuse an open device when the format is unchanged to avoid clicks from repeatedly powering the amplifier.
  if (speakerOpen_ && audioFormatEqual(speakerFmt_, fmt)) {
    esp_codec_dev_set_out_vol(s_playback, volume_);
    return true;
  }
  return openSpeaker(fmt);
}

bool AudioCodec::ensureMic(const AudioFormat &fmt) {
  if (micOpen_ && audioFormatEqual(micFmt_, fmt)) {
    esp_codec_dev_set_in_gain(s_record, micGainDb_);
    return true;
  }
  return openMic(fmt);
}

void AudioCodec::setSpeakerVolume(int volume) {
  if (volume < 0) volume = 0;
  if (volume > 100) volume = 100;
  volume_ = volume;
  if (s_playback && speakerOpen_) {
    esp_codec_dev_set_out_vol(s_playback, volume_);
  }
}

void AudioCodec::setMicGainDb(float db) {
  micGainDb_ = db;
  if (s_record && micOpen_) {
    esp_codec_dev_set_in_gain(s_record, micGainDb_);
  }
}

void AudioCodec::closeSpeaker() {
  if (s_playback && speakerOpen_) {
    esp_codec_dev_close(s_playback);
  }
  speakerOpen_ = false;
}

void AudioCodec::closeMic() {
  if (s_record && micOpen_) {
    esp_codec_dev_close(s_record);
  }
  micOpen_ = false;
}

void AudioCodec::closeIdle() {
  closeSpeaker();
  closeMic();
}

int AudioCodec::speakerWrite(const void *pcm, size_t bytes) {
  if (!s_playback || !speakerOpen_) return -1;
  int ret = esp_codec_dev_write(s_playback, const_cast<void *>(pcm), bytes);
  if (ret == ESP_CODEC_DEV_OK) return (int)bytes;
  return ret < 0 ? ret : -ret;
}

int AudioCodec::micRead(void *pcm, size_t bytes) {
  if (!s_record || !micOpen_) return -1;
  int ret = esp_codec_dev_read(s_record, pcm, bytes);
  if (ret == ESP_CODEC_DEV_OK) return (int)bytes;
  return ret < 0 ? ret : -ret;
}
