#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "gallery_config.h"

// Portable packet: RLG1, uint16 width/height, uint32 payload size/CRC32,
// then 400x300 row-major pixels, MSB first, bit 1 = black. All integers LE.
inline uint16_t galleryRead16(const uint8_t *p) { return p[0] | ((uint16_t)p[1] << 8); }
inline uint32_t galleryRead32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
inline void galleryWrite16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
inline void galleryWrite32(uint8_t *p, uint32_t v) {
  for (uint8_t i = 0; i < 4; ++i) p[i] = v >> (8 * i);
}
inline uint32_t galleryCrc32(const uint8_t *p, size_t n) {
  uint32_t crc = 0xFFFFFFFFU;
  while (n--) {
    crc ^= *p++;
    for (uint8_t bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320U : 0U);
  }
  return crc ^ 0xFFFFFFFFU;
}
inline void galleryMakeHeader(uint8_t *packet, const uint8_t *frame) {
  memcpy(packet, "RLG1", 4);
  galleryWrite16(packet + 4, GALLERY_WIDTH);
  galleryWrite16(packet + 6, GALLERY_HEIGHT);
  galleryWrite32(packet + 8, GALLERY_FRAME_BYTES);
  galleryWrite32(packet + 12, galleryCrc32(frame, GALLERY_FRAME_BYTES));
}
inline bool galleryValidate(const uint8_t *packet, size_t n) {
  return packet && n == GALLERY_PACKET_BYTES && memcmp(packet, "RLG1", 4) == 0 &&
    galleryRead16(packet + 4) == GALLERY_WIDTH && galleryRead16(packet + 6) == GALLERY_HEIGHT &&
    galleryRead32(packet + 8) == GALLERY_FRAME_BYTES &&
    galleryRead32(packet + 12) == galleryCrc32(packet + GALLERY_HEADER_BYTES, GALLERY_FRAME_BYTES);
}
