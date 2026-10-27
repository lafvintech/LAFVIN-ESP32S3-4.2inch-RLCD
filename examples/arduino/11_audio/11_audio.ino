// ============================================================
// 11_audio -- Streaming recording/playback from SD with LVGL status
//
// Recording: ES7210 -> stereo I2S -> average left/right to mono -> SD WAV
// Playback: SD WAV -> mono PCM -> duplicate into stereo I2S -> ES8311
// Hold BOOT to record, release to save. Short-press KEY to play/stop or retry initialization after ERROR.
//
// Reading order: setup -> loop/handleButtons -> start/step/stop -> audio_bsp and sd_bsp.
// Each loop handles one audio block, then advances the low-frequency UI; no extra audio/UI tasks are created.
// This is sequential block processing, not asynchronous I/O: SD, I2S, and LVGL can still occupy the loop.
// ============================================================

#include <Arduino.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <lvgl.h>
#include "audio_bsp.h"
#include "board_pins.h"
#include "buttons_bsp.h"
#include "display_bsp.h"
#include "lvgl_port.h"
#include "sd_bsp.h"
#include "ui_bsp.h"

namespace {
RlcdPanel panel(PIN_LCD_MOSI, PIN_LCD_SCK, PIN_LCD_DC, PIN_LCD_CS, PIN_LCD_RST,
                LCD_WIDTH_LANDSCAPE, LCD_HEIGHT_LANDSCAPE);
AudioCodec codec;
SdWavStore wavStore;
DebouncedButton bootButton(PIN_KEY_BOOT);
DebouncedButton keyButton(PIN_KEY_USER);

// I2S is fixed at stereo 16-bit; WAV stores mono 16-bit at the same sample rate.
constexpr uint32_t kSampleRate = 16000;
constexpr int kCodecChannels = 2;
constexpr int kBitsPerSample = 16;
constexpr AudioFormat kCodecFormat = {
    kSampleRate, kCodecChannels, kBitsPerSample};
constexpr uint32_t kMonoBytesPerSecond =
    kSampleRate * (kBitsPerSample / 8);

constexpr int kSpeakerVolume = 100;
constexpr float kMicGainDb = 35.0f;
constexpr uint32_t kIdleCloseMs = 1500;
constexpr uint32_t kActiveUiRefreshMs = 500;

// Small static buffers use internal RAM rather than retaining the whole recording; display buffers still use PSRAM.
// One I2S frame contains L+R (4 bytes); one mono sample is 2 bytes.
// Record blocks contain up to 512 frames (32ms); playback blocks contain up to 1024 frames (64ms).
int16_t s_inputStereo[1024];
int16_t s_recordMono[512];
int16_t s_playMono[1024];
int16_t s_playStereo[2048];

// loop exclusively owns application state. IDLE does not imply a closed codec; sessions close after an idle interval.
enum class RecorderState : uint8_t {
  IDLE,
  RECORDING,
  PLAYING,
  ERROR,
};

RecorderState g_state = RecorderState::IDLE;
uint32_t g_lastAudioMs = 0;
uint32_t g_lastUiMs = 0;
uint32_t g_recordedBytes = 0;
uint64_t g_levelEnergy = 0;
uint32_t g_levelSamples = 0;
char g_idleDetail[64] = "SD ready - hold BOOT to record";
char g_errorDetail[64] = {};

void refreshUi(bool force);

// Pass PCM payload bytes only, excluding the 44-byte WAV header; 16kHz mono uses 32000 bytes/second.
uint32_t bytesToMillis(uint32_t bytes) {
  return (uint32_t)(((uint64_t)bytes * 1000ULL) / kMonoBytesPerSecond);
}

const char *stateName() {
  switch (g_state) {
    case RecorderState::IDLE: return "IDLE";
    case RecorderState::RECORDING: return "RECORDING";
    case RecorderState::PLAYING: return "PLAYING";
    case RecorderState::ERROR: return "ERROR";
  }
  return "ERROR";
}

void setIdleDetail(const char *message) {
  snprintf(g_idleDetail, sizeof(g_idleDetail), "%s", message);
}

void touchAudio() {
  g_lastAudioMs = millis();
}

void enterError(const char *message) {
  if (wavStore.isRecording()) wavStore.stopRecording();
  if (wavStore.isPlaying()) wavStore.stopPlayback();
  codec.closeIdle();
  snprintf(g_errorDetail, sizeof(g_errorDetail), "%s - TAP KEY to retry",
           message);
  g_state = RecorderState::ERROR;
  Serial.printf("[11] ERROR: %s\n", message);
  refreshUi(true);
}

void maybeCloseIdleCodec() {
  if (g_state != RecorderState::IDLE) return;
  if (!codec.speakerIsOpen() && !codec.micIsOpen()) return;
  if ((millis() - g_lastAudioMs) < kIdleCloseMs) return;
  codec.closeIdle();
  Serial.println(F("[audio] idle close"));
}

// Generate a two-tone startup sound to test the speaker path without relying on SD audio files.
size_t makeStartupBeep(uint8_t *dst, size_t maxBytes) {
  constexpr float kFirstHz = 660.0f;
  constexpr float kSecondHz = 990.0f;
  constexpr uint32_t kSegmentMs = 120;
  constexpr float kAmplitude = 25000.0f;

  const uint32_t samplesPerSegment = kSampleRate * kSegmentMs / 1000;
  const size_t totalFrames = samplesPerSegment * 2;
  if (totalFrames * kCodecChannels * sizeof(int16_t) > maxBytes) return 0;

  int16_t *output = (int16_t *)dst;
  for (uint32_t i = 0; i < totalFrames; ++i) {
    const bool secondSegment = i >= samplesPerSegment;
    const uint32_t index = secondSegment ? i - samplesPerSegment : i;
    const float frequency = secondSegment ? kSecondHz : kFirstHz;

    float envelope = 1.0f;
    const uint32_t fadeSamples = kSampleRate * 10 / 1000;
    if (index < fadeSamples) {
      envelope = (float)index / fadeSamples;
    } else if (index > samplesPerSegment - fadeSamples) {
      envelope = (float)(samplesPerSegment - index) / fadeSamples;
    }

    const float phase = 2.0f * PI * frequency * ((float)index / kSampleRate);
    const int16_t value = (int16_t)(sinf(phase) * kAmplitude * envelope);
    output[i * 2] = value;
    output[i * 2 + 1] = value;
  }
  return totalFrames * kCodecChannels * sizeof(int16_t);
}

void playStartupBeep() {
  static uint8_t beepBuffer[16 * 1024];
  const size_t bytes = makeStartupBeep(beepBuffer, sizeof(beepBuffer));
  if (!codec.ensureSpeaker(kCodecFormat)) {
    enterError("speaker open failed");
    return;
  }
  codec.setSpeakerVolume(kSpeakerVolume);
  if (codec.speakerWrite(beepBuffer, bytes) < 0) {
    enterError("startup beep failed");
    return;
  }
  touchAudio();
}

uint8_t consumeMicLevel() {
  if (g_levelSamples == 0) return 0;

  const double rms = sqrt((double)g_levelEnergy / g_levelSamples);
  g_levelEnergy = 0;
  g_levelSamples = 0;
  if (rms < 1.0) return 0;

  // Convert RMS to -60..0 dBFS, then map to 0..100; this is not a calibrated environmental sound-pressure level.
  const double dbFs = 20.0 * log10(rms / 32768.0);
  int level = (int)((dbFs + 60.0) * (100.0 / 60.0));
  if (level < 0) level = 0;
  if (level > 100) level = 100;
  return (uint8_t)level;
}

bool startRecording() {
  if (!wavStore.isReady()) {
    enterError("SD card is not ready");
    return false;
  }
  if (!codec.ensureMic(kCodecFormat)) {
    enterError("microphone open failed");
    return false;
  }
  codec.setMicGainDb(kMicGainDb);
  if (!wavStore.startRecording(kSampleRate)) {
    enterError(wavStore.lastError());
    return false;
  }

  g_recordedBytes = 0;
  g_levelEnergy = 0;
  g_levelSamples = 0;
  g_state = RecorderState::RECORDING;
  touchAudio();
  Serial.printf("[record] START %s\n", wavStore.activeFileName());
  refreshUi(true);
  return true;
}

void stopRecording() {
  if (g_state != RecorderState::RECORDING) return;
  const bool saved = wavStore.stopRecording();
  touchAudio();
  if (!saved) {
    enterError(wavStore.lastError());
    return;
  }

  g_state = RecorderState::IDLE;
  setIdleDetail("WAV saved - tap KEY to play");
  Serial.printf("[record] SAVED %s, %.1f s\n", wavStore.lastFileName(),
                (double)g_recordedBytes / kMonoBytesPerSecond);
  refreshUi(true);
}

void recordStep() {
  if (!bootButton.isDown()) {
    stopRecording();
    return;
  }

  const int bytesRead = codec.micRead(s_inputStereo, sizeof(s_inputStereo));
  if (bytesRead < 0 || (bytesRead % 4) != 0) {
    enterError("microphone read failed");
    return;
  }

  // Promote L/R to 32-bit before adding to prevent overflow; downmixing does not change the sample rate.
  const size_t frames = (size_t)bytesRead / 4;
  for (size_t i = 0; i < frames; ++i) {
    const int32_t mixed =
        ((int32_t)s_inputStereo[i * 2] + s_inputStereo[i * 2 + 1]) / 2;
    const int16_t mono = (int16_t)mixed;
    s_recordMono[i] = mono;
    g_levelEnergy += (uint64_t)((int64_t)mono * mono);
  }
  g_levelSamples += (uint32_t)frames;

  if (!wavStore.writeMono(s_recordMono, frames)) {
    enterError(wavStore.lastError());
    return;
  }
  g_recordedBytes += (uint32_t)(frames * sizeof(int16_t));
  touchAudio();
}

bool startPlayback() {
  if (!wavStore.hasRecording()) {
    setIdleDetail("No WAV yet - hold BOOT to record");
    Serial.println(F("[play] no recording found"));
    refreshUi(true);
    return false;
  }
  if (!wavStore.startPlayback(kSampleRate)) {
    enterError(wavStore.lastError());
    return false;
  }
  if (!codec.ensureSpeaker(kCodecFormat)) {
    wavStore.stopPlayback();
    enterError("speaker open failed");
    return false;
  }

  codec.setSpeakerVolume(kSpeakerVolume);
  g_state = RecorderState::PLAYING;
  touchAudio();
  Serial.printf("[play] START %s\n", wavStore.lastFileName());
  refreshUi(true);
  return true;
}

void stopPlayback(const char *detail) {
  wavStore.stopPlayback();
  g_state = RecorderState::IDLE;
  setIdleDetail(detail);
  touchAudio();
  Serial.println(F("[play] STOP"));
  refreshUi(true);
}

void playbackStep() {
  const int samplesRead =
      wavStore.readMono(s_playMono, sizeof(s_playMono) / sizeof(s_playMono[0]));
  if (samplesRead < 0) {
    enterError("SD read failed");
    return;
  }
  if (samplesRead == 0) {
    stopPlayback("Playback complete");
    return;
  }

  // Apply 10ms fade-in/out using positions across the whole file, not at every block boundary.
  const uint32_t totalSamples = wavStore.playbackTotalBytes() / 2;
  const uint32_t endSample = wavStore.playbackBytesRead() / 2;
  const uint32_t firstSample = endSample - (uint32_t)samplesRead;
  const uint32_t fadeSamples = kSampleRate * 10 / 1000;

  for (int i = 0; i < samplesRead; ++i) {
    const uint32_t absoluteSample = firstSample + (uint32_t)i;
    float envelope = 1.0f;
    if (absoluteSample < fadeSamples) {
      envelope = (float)absoluteSample / fadeSamples;
    } else {
      const uint32_t samplesRemaining =
          totalSamples > absoluteSample ? totalSamples - absoluteSample : 0;
      if (samplesRemaining < fadeSamples) {
        envelope = (float)samplesRemaining / fadeSamples;
      }
    }
    // Expand file mono to I2S stereo; the output array is writable so the codec can apply software volume.
    const int16_t value = (int16_t)(s_playMono[i] * envelope);
    s_playStereo[i * 2] = value;
    s_playStereo[i * 2 + 1] = value;
  }

  if (codec.speakerWrite(s_playStereo,
                         (size_t)samplesRead * 2 * sizeof(int16_t)) < 0) {
    enterError("speaker write failed");
    return;
  }
  touchAudio();
}

void retryAfterError() {
  if (!wavStore.remount()) {
    enterError(wavStore.lastError());
    return;
  }
  if (!codec.isReady() && !codec.begin()) {
    enterError("codec initialization failed");
    return;
  }
  g_state = RecorderState::IDLE;
  setIdleDetail("Recovered - hold BOOT to record");
  refreshUi(true);
}

// KEY short-press triggers on release; BOOT long-press triggers while held, with no later short-press event.
void handleButtons(ButtonEvent bootEvent, ButtonEvent keyEvent) {
  switch (g_state) {
    case RecorderState::IDLE:
      if (bootEvent == BTN_LONG) {
        startRecording();
      } else if (keyEvent == BTN_SHORT) {
        startPlayback();
      }
      break;

    case RecorderState::RECORDING:
      // recordStep checks the debounced BOOT release state; KEY is ignored while recording.
      break;

    case RecorderState::PLAYING:
      if (keyEvent == BTN_SHORT) {
        stopPlayback("Playback stopped");
      } else if (bootEvent == BTN_LONG) {
        stopPlayback("Starting a new recording");
        startRecording();
      }
      break;

    case RecorderState::ERROR:
      if (keyEvent == BTN_SHORT) retryAfterError();
      break;
  }
}

// Update objects every 500ms while active and force updates on state changes; rendering occurs in a later LVGL handler.
void refreshUi(bool force) {
  const uint32_t now = millis();
  const bool active = g_state == RecorderState::RECORDING ||
                      g_state == RecorderState::PLAYING;
  if (!force && !active) return;
  if (!force && (now - g_lastUiMs) < kActiveUiRefreshMs) return;
  g_lastUiMs = now;

  AudioUiData data = {};
  data.state = stateName();
  data.fileName = wavStore.activeFileName();
  data.detail = g_state == RecorderState::ERROR
                    ? g_errorDetail
                    : (g_state == RecorderState::RECORDING
                           ? "Writing mono WAV directly to SD"
                           : (g_state == RecorderState::PLAYING
                                  ? "Streaming latest WAV from SD"
                                  : g_idleDetail));

  if (g_state == RecorderState::RECORDING) {
    data.elapsedMs = bytesToMillis(g_recordedBytes);
    data.showTotal = false;
    data.micLevelPercent = consumeMicLevel();
  } else if (g_state == RecorderState::PLAYING) {
    // This is SD read progress, not the exact playback position in the speaker DMA buffer.
    const uint32_t played = wavStore.playbackBytesRead();
    const uint32_t total = wavStore.playbackTotalBytes();
    data.elapsedMs = bytesToMillis(played);
    data.totalMs = bytesToMillis(total);
    data.showTotal = true;
    data.playbackPercent = total > 0
                               ? (uint8_t)(((uint64_t)played * 100) / total)
                               : 0;
  } else {
    data.elapsedMs = 0;
    data.totalMs = bytesToMillis(wavStore.latestDataBytes());
    data.showTotal = wavStore.hasRecording();
  }

  audioUiUpdate(data);
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println(F("[11] SD WAV voice recorder"));

  bootButton.begin();
  keyButton.begin();

  if (!panel.begin()) {
    Serial.println(F("PANEL_INIT: FAIL"));
    while (true) delay(1000);
  }
  if (!lvglPortBegin(&panel)) {
    Serial.println(F("LVGL_INIT: FAIL"));
    while (true) delay(1000);
  }
  audioUiBuild();

  const bool sdReady = wavStore.begin();
  const bool codecReady = codec.begin();
  if (!sdReady) {
    enterError(wavStore.lastError());
  } else if (!codecReady) {
    enterError("codec initialization failed");
  } else {
    Serial.println(F("SD_MOUNT: PASS"));
    Serial.println(F("CODEC_INIT: PASS"));
    codec.setSpeakerVolume(kSpeakerVolume);
    codec.setMicGainDb(kMicGainDb);
    setIdleDetail(wavStore.hasRecording()
                      ? "SD ready - tap KEY to play latest"
                      : "SD ready - hold BOOT to record");
    refreshUi(true);
    playStartupBeep();
  }

  Serial.println(F("HOLD BOOT : record to SD / TAP KEY : play or stop"));
}

void loop() {
  // Sample buttons first, then process one PCM block for the current state; slower synchronous I/O delays button response.
  bootButton.update();
  keyButton.update();
  handleButtons(bootButton.consumeEvent(), keyButton.consumeEvent());

  if (g_state == RecorderState::RECORDING) {
    recordStep();
  } else if (g_state == RecorderState::PLAYING) {
    playbackStep();
  } else {
    maybeCloseIdleCodec();
  }

  refreshUi(false);
  if (g_state == RecorderState::RECORDING ||
      g_state == RecorderState::PLAYING) {
    lvglPortPoll();
  } else {
    lvglPortTask(20);
  }
}
