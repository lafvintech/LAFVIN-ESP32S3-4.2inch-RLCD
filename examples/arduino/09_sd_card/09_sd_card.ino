// ============================================================
// 09_sd_card -- basic SD card read/write
//
// Learning sequence: mount SD -> inspect card info -> create directory -> overwrite file
//                    -> append -> read and verify -> list directory.
//
// All test files stay in /demo09; the card is not formatted and other directories are untouched.
// Keep hello.txt after the test so it can be inspected on a computer.
// KEY reruns the complete test.
// Warning: each run overwrites /demo09/hello.txt; this is not a read-only check.
// Reading order: setup()/loop() -> runSdCardTest() -> file steps -> drawResultScreen().
// ============================================================

#include <Arduino.h>
#include <FS.h>
#include <SD_MMC.h>
#include <string.h>
#include "board.h"

namespace {
RlcdPanel panel(PIN_LCD_MOSI, PIN_LCD_SCK, PIN_LCD_DC,
                PIN_LCD_CS, PIN_LCD_RST, LCD_WIDTH, LCD_HEIGHT);
DebouncedButton keyButton(PIN_KEY);

constexpr char kDemoDirectory[] = "/demo09";
// SD_MMC file paths are relative to the card root; do not add the lower-level /sdcard mount prefix.
constexpr char kDemoFile[] = "/demo09/hello.txt";
constexpr char kFirstText[] = "Hello from ESP32-S3 RLCD!\n";
constexpr char kAppendText[] = "Append test passed.\n";
constexpr char kExpectedText[] =
    "Hello from ESP32-S3 RLCD!\n"
    "Append test passed.\n";

struct SdTestResult {
  bool mountOk = false;
  bool directoryOk = false;
  bool writeOk = false;
  bool appendOk = false;
  bool readOk = false;
  bool listOk = false;
  const char *cardType = "--";
  uint64_t capacityMB = 0;
  uint64_t usedMB = 0;
  uint16_t itemCount = 0;
};

SdTestResult result;
// This only tracks whether the program mounted the card, not live removal; hot-swapping a mounted card is unsupported.
bool sdMounted = false;

const char *cardTypeName(uint8_t type) {
  switch (type) {
    case CARD_MMC: return "MMC";
    case CARD_SD: return "SDSC";
    case CARD_SDHC: return "SDHC";
    case CARD_NONE: return "NONE";
    default: return "UNKNOWN";
  }
}

bool mountSdCard(SdTestResult *out) {
  if (!sdMounted) {
    // The board uses SDMMC 1-bit: only CLK, CMD and D0 signals are needed.
    if (!SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0)) {
      Serial.println(F("[sd] setPins failed"));
      return false;
    }

    // true selects SDMMC 1-bit; false disables formatting on mount failure. This is neither SPI nor 4-bit mode.
    if (!SD_MMC.begin("/sdcard", true, false)) {
      Serial.println(F("[sd] mount failed; insert a FAT32 card"));
      SD_MMC.end();
      return false;
    }

    if (SD_MMC.cardType() == CARD_NONE) {
      Serial.println(F("[sd] no card detected"));
      SD_MMC.end();
      return false;
    }
    sdMounted = true;
  }

  const uint8_t type = SD_MMC.cardType();
  out->cardType = cardTypeName(type);
  out->capacityMB = SD_MMC.cardSize() / (1024ULL * 1024ULL);
  out->usedMB = SD_MMC.usedBytes() / (1024ULL * 1024ULL);

  Serial.printf("[sd] type=%s, capacity=%llu MB, used=%llu MB\n",
                out->cardType,
                (unsigned long long)out->capacityMB,
                (unsigned long long)out->usedMB);
  return true;
}

bool ensureDirectory(const char *path) {
  if (SD_MMC.exists(path)) {
    File dir = SD_MMC.open(path);
    const bool isDirectory = dir && dir.isDirectory();
    dir.close();
    return isDirectory;
  }

  const bool ok = SD_MMC.mkdir(path);
  Serial.printf("[sd] mkdir %s: %s\n", path, ok ? "OK" : "FAIL");
  return ok;
}

bool writeTextFile(const char *path, const char *text) {
  // FILE_WRITE truncates an existing file; every test starts with the same initial text.
  File file = SD_MMC.open(path, FILE_WRITE);
  if (!file) {
    Serial.printf("[sd] open for write failed: %s\n", path);
    return false;
  }

  const size_t expected = strlen(text);
  const size_t written = file.write((const uint8_t *)text, expected);
  file.close();
  Serial.printf("[sd] write %s: %u/%u bytes\n", path,
                (unsigned)written, (unsigned)expected);
  return written == expected;
}

bool appendTextFile(const char *path, const char *text) {
  File file = SD_MMC.open(path, FILE_APPEND);
  if (!file) {
    Serial.printf("[sd] open for append failed: %s\n", path);
    return false;
  }

  const size_t expected = strlen(text);
  const size_t written = file.write((const uint8_t *)text, expected);
  file.close();
  Serial.printf("[sd] append %s: %u/%u bytes\n", path,
                (unsigned)written, (unsigned)expected);
  return written == expected;
}

bool readTextFile(const char *path, char *buffer, size_t bufferSize) {
  // Reserve a string terminator; reject truncated reads to avoid treating incomplete content as a pass.
  if (bufferSize < 2) return false;

  File file = SD_MMC.open(path, FILE_READ);
  if (!file) {
    Serial.printf("[sd] open for read failed: %s\n", path);
    return false;
  }

  const size_t fileSize = file.size();
  if (fileSize >= bufferSize) {
    Serial.printf("[sd] file is too large: %u bytes\n", (unsigned)fileSize);
    file.close();
    return false;
  }

  const size_t bytesRead = file.readBytes(buffer, fileSize);
  file.close();
  buffer[bytesRead] = '\0';
  return bytesRead == fileSize;
}

bool listDirectory(const char *path, uint16_t *itemCount) {
  // List only the current directory, without recursion or deleting existing entries.
  File directory = SD_MMC.open(path);
  if (!directory || !directory.isDirectory()) {
    Serial.printf("[sd] open directory failed: %s\n", path);
    directory.close();
    return false;
  }

  uint16_t count = 0;
  Serial.printf("[sd] directory %s:\n", path);
  while (true) {
    File entry = directory.openNextFile();
    if (!entry) break;

    Serial.printf("  %s  %s  %lu bytes\n",
                  entry.isDirectory() ? "[DIR ]" : "[FILE]",
                  entry.name(), (unsigned long)entry.size());
    count++;
    entry.close();
  }
  directory.close();
  *itemCount = count;
  return true;
}

bool allStepsPassed(const SdTestResult &r) {
  return r.mountOk && r.directoryOk && r.writeOk && r.appendOk &&
         r.readOk && r.listOk;
}

const char *stepText(bool attempted, bool passed) {
  if (!attempted) return "--";
  return passed ? "PASS" : "FAIL";
}

void drawStep(int16_t y, const char *name, bool attempted, bool passed) {
  drawString(panel, 20, y, name, LCD_PIXEL_BLACK, 1);
  drawString(panel, 124, y, stepText(attempted, passed), LCD_PIXEL_BLACK, 1);
}

void drawRunningScreen() {
  panel.clearBuffer(LCD_PIXEL_WHITE);
  drawString(panel, 16, 16, "SD CARD TEST", LCD_PIXEL_BLACK, 2);
  drawString(panel, 20, 84, "Running basic file test...",
             LCD_PIXEL_BLACK, 1);
  drawString(panel, 20, 110, "Please wait.", LCD_PIXEL_BLACK, 1);
  panel.flush();
}

void drawResultScreen(const SdTestResult &r) {
  panel.clearBuffer(LCD_PIXEL_WHITE);
  drawString(panel, 16, 12, "SD CARD TEST", LCD_PIXEL_BLACK, 2);
  drawString(panel, 304, 16, allStepsPassed(r) ? "PASS" : "FAIL",
             LCD_PIXEL_BLACK, 1);
  drawLine(panel, 16, 40, 383, 40, LCD_PIXEL_BLACK);

  drawStep(54, "Mount", true, r.mountOk);
  drawStep(78, "Directory", r.mountOk, r.directoryOk);
  drawStep(102, "Write", r.directoryOk, r.writeOk);
  drawStep(126, "Append", r.writeOk, r.appendOk);
  drawStep(150, "Read", r.appendOk, r.readOk);
  drawStep(174, "List", r.directoryOk, r.listOk);

  char line[48];
  if (r.mountOk) {
    snprintf(line, sizeof(line), "Type: %s", r.cardType);
    drawString(panel, 200, 54, line, LCD_PIXEL_BLACK, 1);
    snprintf(line, sizeof(line), "Size: %llu MB",
             (unsigned long long)r.capacityMB);
    drawString(panel, 200, 78, line, LCD_PIXEL_BLACK, 1);
    snprintf(line, sizeof(line), "Used: %llu MB",
             (unsigned long long)r.usedMB);
    drawString(panel, 200, 102, line, LCD_PIXEL_BLACK, 1);
    snprintf(line, sizeof(line), "Items: %u", (unsigned)r.itemCount);
    drawString(panel, 200, 126, line, LCD_PIXEL_BLACK, 1);
    drawString(panel, 20, 218, "File kept on card:", LCD_PIXEL_BLACK, 1);
    drawString(panel, 20, 240, kDemoFile, LCD_PIXEL_BLACK, 1);
  } else {
    drawString(panel, 20, 218, "SD card was not mounted.",
               LCD_PIXEL_BLACK, 1);
  }

  drawRect(panel, 8, 270, panel.width() - 16, 22, LCD_PIXEL_BLACK);
  drawString(panel, 16, 276,
             r.mountOk ? "KEY: RUN AGAIN" : "Power off, insert FAT32, reboot",
             LCD_PIXEL_BLACK, 1);
  panel.flush();
}

void runSdCardTest() {
  // The whole test runs sequentially here; new KEY input is ignored until polling resumes afterward.
  drawRunningScreen();
  result = SdTestResult{};

  Serial.println(F("\n========== SD CARD TEST =========="));
  result.mountOk = mountSdCard(&result);
  if (result.mountOk) {
    result.directoryOk = ensureDirectory(kDemoDirectory);
    if (result.directoryOk) {
      result.writeOk = writeTextFile(kDemoFile, kFirstText);
      // Append/verify only after their prerequisite steps succeed. Listing only needs the directory, so it can run after a write failure.
      if (result.writeOk) {
        result.appendOk = appendTextFile(kDemoFile, kAppendText);
      }

      char readBack[160] = {};
      if (result.appendOk && readTextFile(kDemoFile, readBack, sizeof(readBack))) {
        Serial.println(F("[sd] read back:"));
        Serial.print(readBack);
        result.readOk = strcmp(readBack, kExpectedText) == 0;
        Serial.printf("[sd] content check: %s\n", result.readOk ? "PASS" : "FAIL");
      }

      result.listOk = listDirectory(kDemoDirectory, &result.itemCount);
      result.usedMB = SD_MMC.usedBytes() / (1024ULL * 1024ULL); // Displayed MB use 1024 squared bytes.
    }
  }

  Serial.printf("[09] result: %s\n",
                allStepsPassed(result) ? "PASS" : "FAIL");
  drawResultScreen(result);
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println(F("[09] basic SD card read/write"));

  keyButton.begin();
  if (!panel.begin()) {
    Serial.println(F("[panel] init failed"));
    while (true) delay(1000);
  }

  runSdCardTest();
}

void loop() {
  keyButton.update();
  if (keyButton.consumePress()) {
    runSdCardTest();
  }
  delay(10);
}
