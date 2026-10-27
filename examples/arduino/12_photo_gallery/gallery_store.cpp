#include <Arduino.h>
#include <SD_MMC.h>
#include "board_pins.h"
#include "gallery_packet.h"
#include "gallery_store.h"

namespace {
// Retain the existing SD photo path regardless of the example number.
constexpr char kDirectory[] = "/gallery13";
constexpr char kTemporary[] = "/gallery13/upload.tmp";
}  // namespace

bool GalleryStore::fail(const char *message) {
  snprintf(error_, sizeof(error_), "%s", message);
  Serial.printf("[gallery SD] %s\n", error_);
  return false;
}

void GalleryStore::makePath(uint8_t id, char *path, size_t size) {
  snprintf(path, size, "%s/P%02u.rlcd", kDirectory, (unsigned)id);
}

bool GalleryStore::readPath(const char *path, uint8_t *packet) {
  File f = SD_MMC.open(path, FILE_READ);
  const bool ok = f && f.size() == GALLERY_PACKET_BYTES &&
                  f.read(packet, GALLERY_PACKET_BYTES) == GALLERY_PACKET_BYTES &&
                  galleryValidate(packet, GALLERY_PACKET_BYTES);
  f.close();
  return ok;
}

// Fail without formatting when SD is unavailable; the application then enters single-photo temporary RAM mode.
bool GalleryStore::begin(uint8_t *scratch) {
  if (!scratch) return fail("No packet buffer");
  if (!SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0) ||
      !SD_MMC.begin("/sdcard", true, false) || SD_MMC.cardType() == CARD_NONE) {
    SD_MMC.end();
    return fail("No FAT32 SD card; using temporary single-photo mode");
  }
  if (!SD_MMC.exists(kDirectory) && !SD_MMC.mkdir(kDirectory)) {
    SD_MMC.end();
    return fail("Cannot create /gallery13; using temporary mode");
  }
  File dir = SD_MMC.open(kDirectory);
  const bool directoryOk = dir && dir.isDirectory();
  dir.close();
  if (!directoryOk) {
    SD_MMC.end();
    return fail("/gallery13 is not a directory");
  }
  mounted_ = true;
  for (uint8_t id = 1; id <= GALLERY_MAX_PHOTOS; ++id) {
    char path[40];
    makePath(id, path, sizeof(path));
    GalleryPhoto &p = photos_[id - 1];
    p.occupied = SD_MMC.exists(path);
    p.valid = p.occupied && readPath(path, scratch);
    if (p.valid) p.crc = galleryRead32(scratch + 12);
    if (p.occupied && !p.valid) Serial.printf("[gallery SD] Corrupt photo: %s\n", path);
    delay(1);
  }
  Serial.printf("[gallery SD] %u slots occupied\n", count());
  return true;
}

uint8_t GalleryStore::count() const {
  uint8_t count = 0;
  for (const GalleryPhoto &p : photos_) if (p.occupied) ++count;
  return count;
}

uint8_t GalleryStore::firstValid() const {
  for (uint8_t id = 1; id <= GALLERY_MAX_PHOTOS; ++id) if (photos_[id - 1].valid) return id;
  return 0;
}

bool GalleryStore::read(uint8_t id, uint8_t *packet) {
  if (!mounted_ || !packet || id < 1 || id > GALLERY_MAX_PHOTOS || !photos_[id - 1].occupied) {
    return fail("Photo does not exist");
  }
  char path[40];
  makePath(id, path, sizeof(path));
  if (!readPath(path, packet)) {
    photos_[id - 1].valid = false;
    return fail("Photo read failed / CRC mismatch; power off before checking SD card");
  }
  photos_[id - 1].valid = true;
  photos_[id - 1].crc = galleryRead32(packet + 12);
  return true;
}

bool GalleryStore::add(const uint8_t *packet, uint8_t *idOut) {
  if (!mounted_ || !idOut || !galleryValidate(packet, GALLERY_PACKET_BYTES)) {
    return fail("Invalid upload");
  }
  uint8_t id = 0;
  for (uint8_t i = 1; i <= GALLERY_MAX_PHOTOS; ++i) {
    if (!photos_[i - 1].occupied) {
      id = i;
      break;
    }
  }
  if (!id) return fail("Gallery full; delete a photo first");
  // Write a temporary file, read it back byte-for-byte, then rename it to the final photo. Do not remove power/card while saving.
  File f = SD_MMC.open(kTemporary, FILE_WRITE);
  bool ok = f && f.write(packet, GALLERY_PACKET_BYTES) == GALLERY_PACKET_BYTES;
  f.flush();
  f.close();
  if (ok) {
    uint8_t block[512];
    f = SD_MMC.open(kTemporary, FILE_READ);
    ok = f && f.size() == GALLERY_PACKET_BYTES;
    for (size_t at = 0; ok && at < GALLERY_PACKET_BYTES; at += sizeof(block)) {
      const size_t bytes = min(sizeof(block), GALLERY_PACKET_BYTES - at);
      ok = f.read(block, bytes) == bytes && memcmp(block, packet + at, bytes) == 0;
    }
    f.close();
  }
  char path[40];
  makePath(id, path, sizeof(path));
  // Do not overwrite same-name files added externally after startup; on failure, remove only this attempt's temporary file.
  if (ok) ok = !SD_MMC.exists(path) && SD_MMC.rename(kTemporary, path);
  if (!ok) {
    SD_MMC.remove(kTemporary);
    return fail("SD save / verification failed; previous photos preserved");
  }
  photos_[id - 1].occupied = photos_[id - 1].valid = true;
  photos_[id - 1].crc = galleryRead32(packet + 12);
  *idOut = id;
  return true;
}

bool GalleryStore::remove(uint8_t id) {
  if (!mounted_ || id < 1 || id > GALLERY_MAX_PHOTOS || !photos_[id - 1].occupied) {
    return fail("Photo does not exist");
  }
  char path[40];
  makePath(id, path, sizeof(path));
  if (!SD_MMC.remove(path)) return fail("SD delete failed");
  photos_[id - 1] = GalleryPhoto{};
  return true;
}
