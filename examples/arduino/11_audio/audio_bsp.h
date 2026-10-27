#pragma once

#include <stdint.h>
#include <stddef.h>

// ============================================================
// audio_bsp -- ES8311 (playback) / ES7210 (recording) codec wrapper
//
// Uses Espressif's official esp_codec_dev and esp-codec-board components (see their
// licenses in src/espressif/). This thin wrapper maps initialization, speaker/microphone
// opening, and PCM I/O into the consistent action-oriented names used by this course.
//
// Parameters (a key concept in this example):
//   Sample rate, channel count, and bit depth together determine
//   data rate = hz * ch * (bits/8) bytes/second. Voice memos/notification sounds use
//   16000Hz。
//
// Note: devices open with this board's stereo configuration (2ch);
// recording and playback both use interleaved left/right 16-bit stereo frames.
//
// Only one direction is active in this example; closing the other direction does not mean the hardware lacks full duplex.
// The .ino closes sessions after about 1.5 seconds idle; this class has no background timer.
// Underlying device handles are shared; only one AudioCodec instance may be used sequentially by loop.
// ============================================================

struct AudioFormat {
  uint32_t sampleRate;
  int channels;      // 2 = interleaved left/right
  int bitsPerSample; // 16
};

inline bool audioFormatEqual(const AudioFormat &a, const AudioFormat &b) {
  return a.sampleRate == b.sampleRate && a.channels == b.channels &&
         a.bitsPerSample == b.bitsPerSample;
}

class AudioCodec {
 public:
  bool begin();

  bool openSpeaker(const AudioFormat &fmt);
  bool openMic(const AudioFormat &fmt);
  // Reuse an open device with the same format; otherwise actually open it (closing the other direction).
  bool ensureSpeaker(const AudioFormat &fmt);
  bool ensureMic(const AudioFormat &fmt);

  void closeSpeaker();
  void closeMic();
  void closeIdle();   // Close recording/playback sessions without destroying shared I2C/I2S or codec handles

  // Settings may be cached before open and take effect immediately afterward; volume is 0..100, mic gain is in dB.
  void setSpeakerVolume(int volume);
  void setMicGainDb(float db);
  int speakerVolume() const { return volume_; }
  float micGainDb() const { return micGainDb_; }

  // Synchronous I/O: returns the full byte count on success or a negative value on failure; each L/R frame is 4 bytes.
  // Although speakerWrite accepts const, software volume may modify PCM; the supplied buffer must be writable.
  int speakerWrite(const void *pcm, size_t bytes);
  int micRead(void *pcm, size_t bytes);

  bool isReady() const { return ready_; }
  bool speakerIsOpen() const { return speakerOpen_; }
  bool micIsOpen() const { return micOpen_; }

 private:
  bool ready_ = false;
  bool speakerOpen_ = false;
  bool micOpen_ = false;
  int volume_ = 100;
  float micGainDb_ = 35.0f;
  AudioFormat speakerFmt_ = {};
  AudioFormat micFmt_ = {};
};
