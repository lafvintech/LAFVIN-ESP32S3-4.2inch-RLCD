#pragma once

#include <stdint.h>

// Strings are borrowed: audioUiUpdate copies them into LVGL labels immediately and does not retain these pointers.
// This struct is not an inter-task message; all UI calls and the handler must run in the same loop task.
struct AudioUiData {
  const char *state;
  const char *detail;
  const char *fileName;
  uint32_t elapsedMs;
  uint32_t totalMs;
  bool showTotal;
  uint8_t micLevelPercent;
  uint8_t playbackPercent;
};

// Call once after LVGL initialization to create the persistent single-page recording UI.
void audioUiBuild();

// Only updates objects; a later handler performs rendering. Percentages range from 0 to 100.
// Update infrequently during recording/playback to reduce full-frame rendering/SPI impact on audio in the same loop.
void audioUiUpdate(const AudioUiData &data);
