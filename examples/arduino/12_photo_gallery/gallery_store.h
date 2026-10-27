#pragma once
#include <stdint.h>
#include "gallery_config.h"

struct GalleryPhoto {
  bool occupied = false;
  bool valid = false;
  uint32_t crc = 0;
};

// Handles only album files on SD; if mounting fails, the application retains one temporary photo in RAM.
// No internal mutex; all calls execute sequentially in the Arduino loop.
class GalleryStore {
 public:
  // scratch must hold at least GALLERY_PACKET_BYTES; mount and scan valid photos at startup.
  bool begin(uint8_t *scratch);
  bool isPersistent() const { return mounted_; }
  // The caller must ensure id is in 1..GALLERY_MAX_PHOTOS, never 0.
  const GalleryPhoto &photo(uint8_t id) const { return photos_[id - 1]; }
  uint8_t count() const;
  uint8_t firstValid() const;
  bool read(uint8_t id, uint8_t *packet);
  bool add(const uint8_t *packet, uint8_t *idOut);
  bool remove(uint8_t id);
  const char *lastError() const { return error_; }

 private:
  bool fail(const char *message);
  bool readPath(const char *path, uint8_t *packet);
  void makePath(uint8_t id, char *path, size_t size);
  bool mounted_ = false;
  GalleryPhoto photos_[GALLERY_MAX_PHOTOS];
  char error_[128] = {};
};
