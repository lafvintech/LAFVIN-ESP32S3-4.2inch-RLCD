#pragma once

#include <stddef.h>
#include <stdint.h>

// SD mono WAV storage: /recordings/REC0001.WAV ... REC9999.WAV.
// Supports only 16-bit PCM with a fixed 44-byte header, not general-purpose WAV decoding.
// The .ino converts stereo I2S to/from mono files; this layer handles files only.
// File handles are shared; only one instance may be called sequentially by loop. SD I/O is synchronous.
class SdWavStore {
 public:
  bool begin();
  bool remount();  // Close the current file and remount; not hot-plug detection and never auto-formats
  bool isReady() const { return ready_; }

  bool startRecording(uint32_t sampleRate);
  // sampleCount counts int16_t samples, not bytes; the WAV header is filled only when recording stops.
  bool writeMono(const int16_t *samples, size_t sampleCount);
  bool stopRecording();
  bool isRecording() const { return recording_; }

  bool startPlayback(uint32_t expectedSampleRate);
  // Returns sample count; 0 means EOF or a zero-byte underlying read, negative means a detected error.
  int readMono(int16_t *samples, size_t maxSamples);
  void stopPlayback();
  bool isPlaying() const { return playing_; }

  bool hasRecording() const { return hasRecording_; }
  // Filename/error pointers belong to this object and may change on later calls; do not free them.
  const char *lastFileName() const;
  const char *activeFileName() const;
  // Byte counts below exclude WAV headers; playbackBytesRead is the SD read position, not the audible playback position.
  uint32_t latestDataBytes() const { return latestDataBytes_; }
  uint32_t playbackBytesRead() const { return playbackBytesRead_; }
  uint32_t playbackTotalBytes() const { return playbackTotalBytes_; }
  const char *lastError() const { return lastError_; }

 private:
  bool ready_ = false;
  bool recording_ = false;
  bool playing_ = false;
  bool hasRecording_ = false;
  bool writeFailed_ = false;
  uint16_t nextNumber_ = 1;
  uint32_t recordSampleRate_ = 0;
  uint32_t recordDataBytes_ = 0;
  uint32_t latestDataBytes_ = 0;
  uint32_t playbackBytesRead_ = 0;
  uint32_t playbackTotalBytes_ = 0;
  char currentPath_[48] = {};
  char lastPath_[48] = {};
  char lastName_[20] = {};
  char lastError_[64] = {};

  bool scanRecordings();
  bool readWavHeader(const char *path, uint32_t expectedSampleRate,
                     uint32_t *dataBytes);
  bool finishWavHeader();
  bool fail(const char *message);
};
