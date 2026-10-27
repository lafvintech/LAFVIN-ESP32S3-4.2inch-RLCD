// The phone generates a black-and-white bitmap; the device validates and stores it on SD, then converts to panel layout.
// Without SD, retain only one temporary photo in RAM, lost on restart; storage mode does not change at runtime.
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include "board_pins.h"
#include "buttons_bsp.h"
#include "display_bsp.h"
#include "draw_bsp.h"
#include "gallery_app.h"
#include "gallery_config.h"
#include "gallery_image.h"
#include "gallery_packet.h"
#include "gallery_store.h"
#include "gallery_web.h"

namespace {
RlcdPanel panel(PIN_LCD_MOSI, PIN_LCD_SCK, PIN_LCD_DC, PIN_LCD_CS, PIN_LCD_RST,
                GALLERY_WIDTH, GALLERY_HEIGHT);
DebouncedButton boot(PIN_KEY_BOOT), key(PIN_KEY_USER);
WebServer server(80);
DNSServer dns;
Preferences preferences;
GalleryStore store;
uint8_t activeFrame[GALLERY_FRAME_BYTES] = {};
uint8_t *scratch = nullptr;
bool displayReady = false, hasPhoto = false, helpVisible = true, preferencesReady = false;
uint8_t selected = 0;
uint32_t slideshowMs = 0, lastSlideAt = 0, revision = 0;

bool uploadStarted = false, uploadComplete = false;
uint8_t uploadFiles = 0;
size_t uploadBytes = 0;
uint32_t uploadAt = 0;
int uploadErrorCode = 0;
const char *uploadError = "";

String jsonString(const char *s) {
  String value = "\"";
  while (*s) {
    const unsigned char c = *s++;
    if (c == '"' || c == '\\') { value += '\\'; value += (char)c; }
    else if (c < 32) { char escape[7]; snprintf(escape, sizeof(escape), "\\u%04x", c); value += escape; }
    else value += (char)c;
  }
  value += '"';
  return value;
}
void errorResponse(int code, const char *message) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json; charset=utf-8", String("{\"error\":") + jsonString(message) + "}");
}
void okResponse() {
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json; charset=utf-8", "{\"ok\":true}");
}
void rememberSelection() {
  if (preferencesReady && preferences.getUChar("selected", 0) != selected)
    preferences.putUChar("selected", selected);
}
bool flushPanel() {
  if (displayReady && panel.flush()) return true;
  displayReady = false;
  Serial.println("[12] RLCD transfer failed; restart to retry");
  return false;
}
void showHelp() {
  helpVisible = true;
  if (!displayReady) return;
  panel.clearBuffer(LCD_PIXEL_WHITE);
  drawRect(panel, 6, 6, 388, 288, LCD_PIXEL_BLACK);
  drawString(panel, 20, 22, "RLCD GALLERY", LCD_PIXEL_BLACK, 2);
  drawLine(panel, 20, 50, 380, 50, LCD_PIXEL_BLACK);
  drawString(panel, 20, 66, "1. CONNECT TO WI-FI", LCD_PIXEL_BLACK);
  drawString(panel, 20, 88, GALLERY_AP_SSID, LCD_PIXEL_BLACK, 2);
  char line[48];
  snprintf(line, sizeof(line), "PASSWORD: %.32s", GALLERY_AP_PASSWORD);
  drawString(panel, 20, 116, line, LCD_PIXEL_BLACK);
  drawString(panel, 20, 142, "2. OPEN IN YOUR BROWSER", LCD_PIXEL_BLACK);
  snprintf(line, sizeof(line), "http://%s", WiFi.softAPIP().toString().c_str());
  drawString(panel, 20, 166, line, LCD_PIXEL_BLACK, 2);
  drawString(panel, 20, 194, "3. CHOOSE PHOTO, PREVIEW, UPLOAD", LCD_PIXEL_BLACK);
  drawString(panel, 20, 220, store.isPersistent() ? "SD: 24 PHOTO SLOTS / SAVED ON REBOOT" : "NO SD: ONE TEMPORARY PHOTO", LCD_PIXEL_BLACK);
  drawString(panel, 20, 244, "BOOT: PREV / KEY: NEXT / HOLD KEY: INFO", LCD_PIXEL_BLACK);
  drawString(panel, 20, 268, "NO INTERNET NEEDED", LCD_PIXEL_BLACK);
  flushPanel();
}
// Input is row-major, MSB-first 1bpp; convert through setPixel to the ST7305 layout rather than copying directly.
bool showFrame() {
  helpVisible = false;
  if (!displayReady || !hasPhoto) return false;
  for (uint16_t y = 0; y < GALLERY_HEIGHT; ++y) {
    const uint8_t *row = activeFrame + y * (GALLERY_WIDTH / 8);
    for (uint16_t x = 0; x < GALLERY_WIDTH; ++x)
      panel.setPixel(x, y, (row[x / 8] & (0x80U >> (x & 7))) ? LCD_PIXEL_BLACK : LCD_PIXEL_WHITE);
  }
  return flushPanel();
}
bool choosePhoto(uint8_t id) {
  if (id < 1 || id > (store.isPersistent() ? GALLERY_MAX_PHOTOS : 1)) return false;
  if (store.isPersistent()) {
    if (!store.photo(id).valid || !store.read(id, scratch)) { ++revision; return false; }
    memcpy(activeFrame, scratch + GALLERY_HEADER_BYTES, GALLERY_FRAME_BYTES);
  } else if (!hasPhoto || id != 1) return false;
  hasPhoto = true;
  selected = id;
  ++revision;
  lastSlideAt = millis();
  rememberSelection();
  Serial.printf("[12] Display photo %u\n", selected);
  return showFrame();
}
void nextPhoto(int direction) {
  if (!hasPhoto && !store.firstValid()) return;
  if (!store.isPersistent()) { if (hasPhoto && helpVisible) showFrame(); return; }
  for (uint8_t step = 1; step <= GALLERY_MAX_PHOTOS; ++step) {
    int id = (selected ? selected : 1) - 1 + direction * step;
    id = ((id % GALLERY_MAX_PHOTOS) + GALLERY_MAX_PHOTOS) % GALLERY_MAX_PHOTOS + 1;
    if (store.photo(id).valid) {
      if (id != selected || helpVisible) choosePhoto(id);
      lastSlideAt = millis();
      return;
    }
  }
}
void statusResponse() {
  String json; json.reserve(2500);
  json = "{\"ssid\":"; json += jsonString(GALLERY_AP_SSID);
  json += ",\"ip\":"; json += jsonString(WiFi.softAPIP().toString().c_str());
  json += ",\"persistent\":"; json += store.isPersistent() ? "true" : "false";
  json += ",\"displayReady\":"; json += displayReady ? "true" : "false";
  json += ",\"helpVisible\":"; json += helpVisible ? "true" : "false";
  json += ",\"capacity\":"; json += store.isPersistent() ? GALLERY_MAX_PHOTOS : 1;
  json += ",\"selected\":"; json += selected;
  json += ",\"slideshow\":"; json += slideshowMs / 1000;
  json += ",\"revision\":"; json += revision;
  json += ",\"items\":[";
  bool comma = false;
  if (store.isPersistent()) {
    for (uint8_t id = 1; id <= GALLERY_MAX_PHOTOS; ++id) {
      const GalleryPhoto &p = store.photo(id);
      if (!p.occupied) continue;
      if (comma) json += ',';
      json += "{\"id\":"; json += id;
      json += ",\"valid\":"; json += p.valid ? "true" : "false";
      json += ",\"crc\":"; json += p.crc; json += '}';
      comma = true;
    }
  } else if (hasPhoto) {
    json += "{\"id\":1,\"valid\":true,\"crc\":";
    json += galleryCrc32(activeFrame, GALLERY_FRAME_BYTES); json += '}';
  }
  json += "]}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json; charset=utf-8", json);
}
int requestId() {
  const String id = server.arg("id");
  if (!id.length() || id.length() > 2) return 0;
  for (size_t i = 0; i < id.length(); ++i) if (id[i] < '0' || id[i] > '9') return 0;
  const int value = id.toInt();
  return value >= 1 && value <= GALLERY_MAX_PHOTOS ? value : 0;
}
void frameResponse() {
  const int id = requestId();
  if (!id || !scratch) { errorResponse(400, "照片编号不正确"); return; }
  if (hasPhoto && id == selected) {
    galleryMakeHeader(scratch, activeFrame);
    memcpy(scratch + GALLERY_HEADER_BYTES, activeFrame, GALLERY_FRAME_BYTES);
  } else if (!store.read(id, scratch)) { errorResponse(404, "照片不存在或校验失败"); return; }
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(GALLERY_PACKET_BYTES);
  server.send(200, "application/octet-stream", "");
  server.sendContent((const char *)scratch, GALLERY_PACKET_BYTES);
}
void resetUpload() {
  uploadStarted = uploadComplete = false;
  uploadFiles = 0;
  uploadBytes = 0;
  uploadErrorCode = 0;
  uploadError = "";
}
// Upload callbacks append only to scratch; commit a photo only after complete reception and dimension/CRC validation.
void receiveUpload() {
  HTTPUpload &u = server.upload();
  uploadAt = millis();
  if (u.status == UPLOAD_FILE_START) {
    if (!uploadStarted) resetUpload();
    uploadStarted = true;
    ++uploadFiles;
    if (uploadFiles != 1 || u.name != "frame") {
      uploadErrorCode = 400; uploadError = "每次只能上传一张照片";
    } else if (!scratch) { uploadErrorCode = 503; uploadError = "设备图片缓冲区不可用"; }
    else if (!displayReady) { uploadErrorCode = 503; uploadError = "屏幕未就绪，请检查设备并重启"; }
  } else if (u.status == UPLOAD_FILE_WRITE && !uploadErrorCode) {
    if (u.currentSize > GALLERY_PACKET_BYTES - uploadBytes) {
      uploadErrorCode = 413; uploadError = "照片数据过大，请通过画廊页面上传";
    } else {
      memcpy(scratch + uploadBytes, u.buf, u.currentSize);
      uploadBytes += u.currentSize;
    }
  } else if (u.status == UPLOAD_FILE_END && !uploadErrorCode) {
    uploadComplete = uploadFiles == 1 && galleryValidate(scratch, uploadBytes);
    if (!uploadComplete) { uploadErrorCode = 400; uploadError = "照片长度、尺寸或校验不正确，请重新上传"; }
  } else if (u.status == UPLOAD_FILE_ABORTED) {
    Serial.println("[12] Upload aborted; current photo unchanged");
    resetUpload();
  }
}
// With SD, save the file before replacing the current photo; without SD, update activeFrame only, without claiming persistence.
void finishUpload() {
  if (uploadErrorCode || !uploadStarted || !uploadComplete || uploadFiles != 1) {
    errorResponse(uploadErrorCode ? uploadErrorCode : 400,
                  uploadErrorCode ? uploadError : "没有收到完整照片，请重新选择并上传");
    resetUpload(); return;
  }
  uint8_t id = 1;
  if (store.isPersistent() && !store.add(scratch, &id)) {
    errorResponse(store.count() >= GALLERY_MAX_PHOTOS ? 409 : 500, store.lastError());
    resetUpload(); return;
  }
  memcpy(activeFrame, scratch + GALLERY_HEADER_BYTES, GALLERY_FRAME_BYTES);
  hasPhoto = true;
  selected = id;
  slideshowMs = 0;
  lastSlideAt = millis();
  ++revision;
  rememberSelection();
  const bool shown = showFrame();
  resetUpload();
  if (!shown) { errorResponse(503, "照片已接收，但屏幕刷新失败，请重启检查显示连接"); return; }
  Serial.printf("[12] Received photo %u (%s)\n", selected, store.isPersistent() ? "SD saved" : "RAM only");
  okResponse();
}
void selectResponse() {
  const int id = requestId();
  slideshowMs = 0;
  if (!id || (!store.isPersistent() && id != 1)) { errorResponse(400, "照片编号不正确"); return; }
  if (!choosePhoto(id)) { errorResponse(displayReady ? 404 : 503, "照片读取或屏幕刷新失败"); return; }
  okResponse();
}
void deleteResponse() {
  const int id = requestId();
  if (!id || (!store.isPersistent() && (id != 1 || !hasPhoto))) { errorResponse(404, "照片不存在"); return; }
  if (store.isPersistent() && !store.remove(id)) { errorResponse(500, store.lastError()); return; }
  ++revision; slideshowMs = 0;
  if (selected == id) {
    selected = 0; hasPhoto = false;
    const uint8_t replacement = store.isPersistent() ? store.firstValid() : 0;
    if (replacement) choosePhoto(replacement);
    else { rememberSelection(); showHelp(); }
  }
  okResponse();
}
void slideshowResponse() {
  const String value = server.arg("seconds");
  if (value != "0" && value != "10" && value != "30" && value != "60") {
    errorResponse(400, "请选择手动、10秒、30秒或60秒切换"); return;
  }
  slideshowMs = value.toInt() * 1000UL;
  lastSlideAt = millis(); ++revision;
  if (hasPhoto && helpVisible && slideshowMs) showFrame();
  okResponse();
}
void beginServer() {
  server.on("/", HTTP_GET, [] { server.send_P(200, "text/html; charset=utf-8", kGalleryHtml); });
  server.on("/image.js", HTTP_GET, [] { server.send_P(200, "application/javascript; charset=utf-8", kGalleryImageJs); });
  server.on("/favicon.ico", HTTP_GET, [] { server.send(204, "image/x-icon", ""); });
  server.on("/api/status", HTTP_GET, statusResponse);
  server.on("/api/frame", HTTP_GET, frameResponse);
  server.on("/api/upload", HTTP_POST, finishUpload, receiveUpload);
  server.on("/api/select", HTTP_POST, selectResponse);
  server.on("/api/delete", HTTP_POST, deleteResponse);
  server.on("/api/slideshow", HTTP_POST, slideshowResponse);
  server.on("/api/info", HTTP_POST, [] { slideshowMs = 0; showHelp(); ++revision; okResponse(); });
  server.onNotFound([] {
    if (server.uri().startsWith("/api/")) { errorResponse(404, "接口不存在"); return; }
    server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
    server.send(302, "text/plain", "Open the gallery home page");
  });
  server.begin();
}
} // namespace

void gallerySetup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n[12] RLCD local photo gallery / 16MB Flash / 8MB PSRAM");
  boot.begin();
  key.begin();
  scratch = (uint8_t *)heap_caps_malloc(GALLERY_PACKET_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!scratch) scratch = (uint8_t *)malloc(GALLERY_PACKET_BYTES);
  displayReady = panel.begin();
  if (!displayReady) Serial.println("[12] PANEL_INIT: FAIL");
  if (scratch) store.begin(scratch);
  if (store.isPersistent()) preferencesReady = preferences.begin("gallery13", false);
  WiFi.mode(WIFI_AP);
  const IPAddress ip(192, 168, 4, 1), subnet(255, 255, 255, 0);
  if (!WiFi.softAPConfig(ip, ip, subnet) || !WiFi.softAP(GALLERY_AP_SSID, GALLERY_AP_PASSWORD)) {
    Serial.println("[12] AP_INIT: FAIL; restart after checking configuration");
    while (true) delay(1000);
  }
  dns.start(53, "*", WiFi.softAPIP());
  beginServer();
  Serial.printf("[12] SSID=%s password=%s URL=http://%s\n", GALLERY_AP_SSID, GALLERY_AP_PASSWORD,
                WiFi.softAPIP().toString().c_str());
  uint8_t saved = preferencesReady ? preferences.getUChar("selected", 0) : 0;
  if (saved < 1 || saved > GALLERY_MAX_PHOTOS || !store.photo(saved).valid) saved = store.firstValid();
  if (saved && displayReady && scratch) { if (!choosePhoto(saved)) showHelp(); }
  else showHelp();
}
// DNS, HTTP, buttons, and slideshow share one loop; synchronous request handling can delay button response and auto-advance.
void galleryLoop() {
  dns.processNextRequest();
  server.handleClient();
  boot.update();
  key.update();
  const ButtonEvent bootEvent = boot.consumeEvent(), keyEvent = key.consumeEvent();
  if (keyEvent == BTN_LONG) { slideshowMs = 0; showHelp(); ++revision; }
  else if (keyEvent == BTN_SHORT || bootEvent == BTN_SHORT) {
    slideshowMs = 0;
    if (hasPhoto && helpVisible) { showFrame(); ++revision; }
    else nextPhoto(keyEvent == BTN_SHORT ? 1 : -1);
  }
  if (uploadStarted && millis() - uploadAt > GALLERY_UPLOAD_TIMEOUT_MS) resetUpload();
  if (slideshowMs && !helpVisible && !uploadStarted && millis() - lastSlideAt >= slideshowMs)
    nextPhoto(1);
  delay(2);
}
