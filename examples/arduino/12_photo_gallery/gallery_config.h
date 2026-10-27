#pragma once
#include <stdint.h>
#include <stddef.h>

constexpr char GALLERY_AP_SSID[] = "RLCD-Gallery";
constexpr char GALLERY_AP_PASSWORD[] = "12345678"; // At least eight ASCII characters.
constexpr uint16_t GALLERY_WIDTH = 400;
constexpr uint16_t GALLERY_HEIGHT = 300;
constexpr size_t GALLERY_FRAME_BYTES = GALLERY_WIDTH * GALLERY_HEIGHT / 8;
constexpr size_t GALLERY_HEADER_BYTES = 16;
constexpr size_t GALLERY_PACKET_BYTES = GALLERY_HEADER_BYTES + GALLERY_FRAME_BYTES;
// SD mode holds up to 24 photos; without a card, only one temporary photo is retained in RAM.
constexpr uint8_t GALLERY_MAX_PHOTOS = 24;
constexpr uint32_t GALLERY_UPLOAD_TIMEOUT_MS = 15000;
static_assert(GALLERY_WIDTH % 8 == 0, "Rows must contain a whole number of bytes");
static_assert(GALLERY_MAX_PHOTOS < 100, "Pxx filenames allow at most 99 slots");
