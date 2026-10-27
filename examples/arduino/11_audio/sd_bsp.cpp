#include <Arduino.h>
#include <FS.h>
#include <SD_MMC.h>
#include <stdio.h>
#include <string.h>
#include "board_pins.h"
#include "sd_bsp.h"

namespace {
// Arduino SD_MMC paths omit the /sdcard mount prefix; the on-card directory is /recordings.
constexpr char kRecordingDirectory[] = "/recordings";
constexpr size_t kWavHeaderBytes = 44;
constexpr uint16_t kBitsPerSample = 16;
constexpr uint16_t kMonoChannels = 1;

File s_audioFile;

// Encode the WAV header explicitly as little-endian bytes, not as a C struct, to avoid padding/alignment changes.
void writeU16(uint8_t *dst, uint16_t value) {
  dst[0] = (uint8_t)(value & 0xFF);
  dst[1] = (uint8_t)(value >> 8);
}

void writeU32(uint8_t *dst, uint32_t value) {
  dst[0] = (uint8_t)(value & 0xFF);
  dst[1] = (uint8_t)((value >> 8) & 0xFF);
  dst[2] = (uint8_t)((value >> 16) & 0xFF);
  dst[3] = (uint8_t)((value >> 24) & 0xFF);
}

uint16_t readU16(const uint8_t *src) {
  return (uint16_t)(src[0] | ((uint16_t)src[1] << 8));
}

uint32_t readU32(const uint8_t *src) {
  return (uint32_t)src[0] | ((uint32_t)src[1] << 8) |
         ((uint32_t)src[2] << 16) | ((uint32_t)src[3] << 24);
}

const char *baseName(const char *path) {
  const char *slash = strrchr(path, '/');
  return slash ? slash + 1 : path;
}

bool parseRecordingNumber(const char *path, uint16_t *number) {
  const char *name = baseName(path);
  if (strlen(name) != 11) return false;
  if (name[0] != 'R' || name[1] != 'E' || name[2] != 'C' ||
      name[7] != '.' || name[8] != 'W' || name[9] != 'A' ||
      name[10] != 'V') {
    return false;
  }

  uint16_t value = 0;
  for (int i = 3; i <= 6; ++i) {
    if (name[i] < '0' || name[i] > '9') return false;
    value = (uint16_t)(value * 10 + (name[i] - '0'));
  }
  if (value == 0) return false;
  *number = value;
  return true;
}

void buildWavHeader(uint8_t *header, uint32_t sampleRate,
                    uint32_t dataBytes) {
  memset(header, 0, kWavHeaderBytes);
  memcpy(header + 0, "RIFF", 4);
  writeU32(header + 4, 36 + dataBytes);
  memcpy(header + 8, "WAVE", 4);
  memcpy(header + 12, "fmt ", 4);
  writeU32(header + 16, 16);
  writeU16(header + 20, 1);  // PCM
  writeU16(header + 22, kMonoChannels);
  writeU32(header + 24, sampleRate);
  writeU32(header + 28, sampleRate * kMonoChannels * kBitsPerSample / 8);
  writeU16(header + 32, kMonoChannels * kBitsPerSample / 8);
  writeU16(header + 34, kBitsPerSample);
  memcpy(header + 36, "data", 4);
  writeU32(header + 40, dataBytes);
}
}  // namespace

bool SdWavStore::fail(const char *message) {
  strncpy(lastError_, message, sizeof(lastError_) - 1);
  lastError_[sizeof(lastError_) - 1] = '\0';
  Serial.printf("[sd] %s\n", lastError_);
  return false;
}

bool SdWavStore::begin() {
  if (ready_) return true;
  lastError_[0] = '\0';
  hasRecording_ = false;
  nextNumber_ = 1;
  latestDataBytes_ = 0;
  lastPath_[0] = '\0';
  lastName_[0] = '\0';

  if (!SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0)) {
    return fail("setPins failed");
  }
  if (!SD_MMC.begin("/sdcard", true, false)) {
    SD_MMC.end();
    return fail("mount failed; insert a FAT32 card");
  }
  if (SD_MMC.cardType() == CARD_NONE) {
    SD_MMC.end();
    return fail("no card detected");
  }

  if (!SD_MMC.exists(kRecordingDirectory) &&
      !SD_MMC.mkdir(kRecordingDirectory)) {
    SD_MMC.end();
    return fail("cannot create /recordings");
  }

  ready_ = true;
  if (!scanRecordings()) {
    ready_ = false;
    SD_MMC.end();
    return false;
  }
  Serial.printf("[sd] mounted, %llu MB, latest=%s\n",
                (unsigned long long)(SD_MMC.cardSize() / (1024ULL * 1024ULL)),
                lastFileName());
  return true;
}

bool SdWavStore::remount() {
  if (recording_) stopRecording();
  if (playing_) stopPlayback();
  SD_MMC.end();
  ready_ = false;
  return begin();
}

// Only checks this example's fixed header layout; does not scan extension chunks such as LIST. Zero means any sample rate.
bool SdWavStore::readWavHeader(const char *path,
                               uint32_t expectedSampleRate,
                               uint32_t *dataBytes) {
  File probe = SD_MMC.open(path, FILE_READ);
  if (!probe) return false;

  uint8_t header[kWavHeaderBytes];
  const int bytesRead = probe.read(header, sizeof(header));
  const uint32_t fileBytes = probe.size();
  probe.close();

  if (bytesRead != (int)sizeof(header) || memcmp(header, "RIFF", 4) != 0 ||
      memcmp(header + 8, "WAVEfmt ", 8) != 0 ||
      memcmp(header + 36, "data", 4) != 0 || readU16(header + 20) != 1 ||
      readU16(header + 22) != kMonoChannels ||
      readU16(header + 34) != kBitsPerSample) {
    return false;
  }

  const uint32_t sampleRate = readU32(header + 24);
  const uint32_t pcmBytes = readU32(header + 40);
  if ((expectedSampleRate != 0 && sampleRate != expectedSampleRate) ||
      fileBytes < kWavHeaderBytes || pcmBytes > fileBytes - kWavHeaderBytes ||
      (pcmBytes & 1U) != 0) {
    return false;
  }

  *dataBytes = pcmBytes;
  return true;
}

// On mount, locate the newest recognizable WAV by file number; check the target sample rate again at playback.
bool SdWavStore::scanRecordings() {
  File directory = SD_MMC.open(kRecordingDirectory);
  if (!directory || !directory.isDirectory()) {
    if (directory) directory.close();
    return fail("cannot open /recordings");
  }

  uint16_t highestNumber = 0;
  uint32_t highestDataBytes = 0;
  char highestPath[sizeof(lastPath_)] = {};

  while (true) {
    File entry = directory.openNextFile();
    if (!entry) break;
    if (!entry.isDirectory()) {
      uint16_t number = 0;
      const char *entryName = entry.name();
      char path[sizeof(lastPath_)];
      if (entryName[0] == '/') {
        snprintf(path, sizeof(path), "%s", entryName);
      } else {
        snprintf(path, sizeof(path), "%s/%s", kRecordingDirectory,
                 entryName);
      }

      uint32_t dataBytes = 0;
      if (parseRecordingNumber(path, &number) && number > highestNumber &&
          readWavHeader(path, 0, &dataBytes)) {
        highestNumber = number;
        highestDataBytes = dataBytes;
        snprintf(highestPath, sizeof(highestPath), "%s", path);
      }
    }
    entry.close();
  }
  directory.close();

  if (highestNumber > 0) {
    hasRecording_ = true;
    latestDataBytes_ = highestDataBytes;
    snprintf(lastPath_, sizeof(lastPath_), "%s", highestPath);
    snprintf(lastName_, sizeof(lastName_), "%s", baseName(highestPath));
    nextNumber_ = highestNumber < 9999 ? (uint16_t)(highestNumber + 1) : 0;
  }
  return true;
}

bool SdWavStore::startRecording(uint32_t sampleRate) {
  if (!ready_) return fail("SD is not ready");
  if (recording_ || playing_) return fail("audio file is already open");
  if (nextNumber_ == 0 || nextNumber_ > 9999) {
    return fail("recording number limit reached");
  }

  snprintf(currentPath_, sizeof(currentPath_), "%s/REC%04u.WAV",
           kRecordingDirectory, (unsigned)nextNumber_);
  // Skip existing numbers rather than overwriting recordings; assumes one writer and provides no concurrent-create protection.
  while (nextNumber_ <= 9999 && SD_MMC.exists(currentPath_)) {
    ++nextNumber_;
    if (nextNumber_ <= 9999) {
      snprintf(currentPath_, sizeof(currentPath_), "%s/REC%04u.WAV",
               kRecordingDirectory, (unsigned)nextNumber_);
    }
  }
  if (nextNumber_ > 9999) {
    nextNumber_ = 0;
    return fail("recording number limit reached");
  }
  s_audioFile = SD_MMC.open(currentPath_, FILE_WRITE);
  if (!s_audioFile) return fail("cannot create WAV file");

  // Reserve 44 bytes, then append PCM blocks; after BOOT release, fill the header using the actual length.
  // Power loss/card removal can invalidate the file; do not remove the card before recording finishes.
  uint8_t placeholder[kWavHeaderBytes] = {};
  if (s_audioFile.write(placeholder, sizeof(placeholder)) !=
      sizeof(placeholder)) {
    s_audioFile.close();
    SD_MMC.remove(currentPath_);
    return fail("cannot write WAV header");
  }

  recordSampleRate_ = sampleRate;
  recordDataBytes_ = 0;
  writeFailed_ = false;
  recording_ = true;
  lastError_[0] = '\0';
  Serial.printf("[sd] recording to %s\n", currentPath_);
  return true;
}

bool SdWavStore::writeMono(const int16_t *samples, size_t sampleCount) {
  if (!recording_ || !samples) return fail("WAV file is not recording");
  const size_t bytes = sampleCount * sizeof(int16_t);
  const size_t written = s_audioFile.write((const uint8_t *)samples, bytes);
  recordDataBytes_ += (uint32_t)written;
  if (written != bytes) {
    writeFailed_ = true;
    return fail("SD write failed or card is full");
  }
  return true;
}

bool SdWavStore::finishWavHeader() {
  uint8_t header[kWavHeaderBytes];
  buildWavHeader(header, recordSampleRate_, recordDataBytes_);
  if (!s_audioFile.seek(0)) return fail("cannot seek to WAV header");
  if (s_audioFile.write(header, sizeof(header)) != sizeof(header)) {
    return fail("cannot finalize WAV header");
  }
  s_audioFile.flush();
  return true;
}

// Try to add a header for data already written. A partial recording may survive a write failure, but the call returns false.
// If header writing fails, delete only this new file and leave earlier recordings untouched.
bool SdWavStore::stopRecording() {
  if (!recording_) return false;
  const bool headerOk = finishWavHeader();
  s_audioFile.close();
  recording_ = false;

  if (!headerOk) {
    SD_MMC.remove(currentPath_);
    return false;
  }

  hasRecording_ = true;
  latestDataBytes_ = recordDataBytes_;
  snprintf(lastPath_, sizeof(lastPath_), "%s", currentPath_);
  snprintf(lastName_, sizeof(lastName_), "%s", baseName(currentPath_));
  if (nextNumber_ < 9999) {
    ++nextNumber_;
  } else {
    nextNumber_ = 0;
  }
  Serial.printf("[sd] saved %s, %u bytes\n", lastName_,
                (unsigned)latestDataBytes_);
  return !writeFailed_;
}

bool SdWavStore::startPlayback(uint32_t expectedSampleRate) {
  if (!ready_) return fail("SD is not ready");
  if (!hasRecording_) return fail("no recording found");
  if (recording_ || playing_) return fail("audio file is already open");

  uint32_t dataBytes = 0;
  if (!readWavHeader(lastPath_, expectedSampleRate, &dataBytes)) {
    return fail("latest WAV format is invalid");
  }

  s_audioFile = SD_MMC.open(lastPath_, FILE_READ);
  if (!s_audioFile || !s_audioFile.seek(kWavHeaderBytes)) {
    if (s_audioFile) s_audioFile.close();
    return fail("cannot open latest WAV file");
  }

  playbackBytesRead_ = 0;
  playbackTotalBytes_ = dataBytes;
  playing_ = true;
  lastError_[0] = '\0';
  Serial.printf("[sd] playing %s, %u bytes\n", lastName_,
                (unsigned)playbackTotalBytes_);
  return true;
}

int SdWavStore::readMono(int16_t *samples, size_t maxSamples) {
  if (!playing_ || !samples) return -1;
  const uint32_t remaining = playbackTotalBytes_ - playbackBytesRead_;
  if (remaining == 0) return 0;

  size_t bytes = maxSamples * sizeof(int16_t);
  if (bytes > remaining) bytes = remaining;
  const int bytesRead = s_audioFile.read((uint8_t *)samples, bytes);
  if (bytesRead < 0 || (bytesRead & 1) != 0) return -1;
  playbackBytesRead_ += (uint32_t)bytesRead;
  return bytesRead / (int)sizeof(int16_t);
}

void SdWavStore::stopPlayback() {
  if (playing_) s_audioFile.close();
  playing_ = false;
}

const char *SdWavStore::lastFileName() const {
  return hasRecording_ ? lastName_ : "--";
}

const char *SdWavStore::activeFileName() const {
  return recording_ ? baseName(currentPath_) : lastFileName();
}
