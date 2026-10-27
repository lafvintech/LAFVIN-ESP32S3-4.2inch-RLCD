#pragma once
#include <Arduino.h>

// Pure browser-side image functions; no network, DOM or third-party library.
static const char kGalleryImageJs[] PROGMEM = R"JS(
(() => {
  'use strict';
  const width = 400, height = 300, frameBytes = width * height / 8;
  function crc32(bytes) {
    let crc = 0xffffffff;
    for (const value of bytes) {
      crc ^= value;
      for (let i = 0; i < 8; i++) crc = (crc >>> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
    }
    return (crc ^ 0xffffffff) >>> 0;
  }
  function monochrome(rgba, mode = 'dither', brightness = 0, contrast = 1) {
    if (rgba.length !== width * height * 4) throw new Error('预览尺寸不正确');
    const luminance = new Float32Array(width * height);
    const frame = new Uint8Array(frameBytes);
    for (let i = 0; i < luminance.length; i++) {
      const at = i * 4, alpha = rgba[at + 3] / 255;
      const gray = (rgba[at] * .299 + rgba[at + 1] * .587 + rgba[at + 2] * .114) * alpha + 255 * (1 - alpha);
      luminance[i] = Math.min(255, Math.max(0, (gray - 128) * contrast + 128 + brightness));
    }
    const diffuse = (x, y, error) => {
      if (x >= 0 && x < width && y < height) luminance[y * width + x] += error;
    };
    for (let y = 0; y < height; y++) {
      const direction = y % 2 ? -1 : 1;
      for (let step = 0; step < width; step++) {
        const x = direction === 1 ? step : width - 1 - step, index = y * width + x;
        const old = luminance[index], value = old < 128 ? 0 : 255;
        if (value === 0) frame[index >>> 3] |= 0x80 >>> (index & 7);
        if (mode === 'dither') {
          const error = old - value;
          diffuse(x + direction, y, error * 7 / 16);
          diffuse(x - direction, y + 1, error * 3 / 16);
          diffuse(x, y + 1, error * 5 / 16);
          diffuse(x + direction, y + 1, error / 16);
        }
      }
    }
    return frame;
  }
  function pixels(frame) {
    if (frame.length !== frameBytes) throw new Error('照片数据长度不正确');
    const rgba = new Uint8ClampedArray(width * height * 4);
    for (let i = 0; i < width * height; i++) {
      const color = frame[i >>> 3] & (0x80 >>> (i & 7)) ? 0 : 255;
      rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = color;
      rgba[i * 4 + 3] = 255;
    }
    return rgba;
  }
  function packet(frame) {
    if (frame.length !== frameBytes) throw new Error('照片数据长度不正确');
    const bytes = new Uint8Array(16 + frameBytes), view = new DataView(bytes.buffer);
    bytes.set([82, 76, 71, 49]);
    view.setUint16(4, width, true); view.setUint16(6, height, true);
    view.setUint32(8, frameBytes, true); view.setUint32(12, crc32(frame), true);
    bytes.set(frame, 16);
    return bytes;
  }
  function decode(buffer) {
    const bytes = buffer instanceof Uint8Array ? buffer : new Uint8Array(buffer);
    if (bytes.length !== 16 + frameBytes) throw new Error('设备照片长度不正确');
    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    if (bytes[0] !== 82 || bytes[1] !== 76 || bytes[2] !== 71 || bytes[3] !== 49 ||
        view.getUint16(4, true) !== width || view.getUint16(6, true) !== height ||
        view.getUint32(8, true) !== frameBytes) throw new Error('设备照片格式不正确');
    const frame = bytes.subarray(16);
    if (view.getUint32(12, true) !== crc32(frame)) throw new Error('照片校验失败，请重试');
    return frame;
  }
  globalThis.GalleryImage = { width, height, frameBytes, crc32, monochrome, pixels, packet, decode };
})();
)JS";
