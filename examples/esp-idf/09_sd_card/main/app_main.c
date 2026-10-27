/* 09 SD self-test: board_sdcard handles mounting/card information only; file I/O uses the standard VFS API.
  * app_main runs one test, then waits on the KEY queue; button callbacks do not access the SD card or display.
  * Each test overwrites /demo09/hello.txt; it does not format the card or modify other directories. */
#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "board_display.h"
#include "board_sdcard.h"
#include "button_service.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "mono_graphics.h"

#define BYTES_PER_MEGABYTE (1024ULL * 1024ULL)

static const char *TAG = "sd_card_demo";
/* C stdio/POSIX uses VFS paths including the mount point; the directory on the card is still /demo09.
  * If board_sdcard's mount point changes, update both path prefixes here as well. */
static const char *DEMO_DIRECTORY = "/sdcard/demo09";
static const char *DEMO_FILE = "/sdcard/demo09/hello.txt";
static const char *DISPLAY_FILE = "/demo09/hello.txt";
static const char *FIRST_TEXT = "Hello from ESP32-S3 RLCD!\n";
static const char *APPEND_TEXT = "Append test passed.\n";
static const char *EXPECTED_TEXT =
    "Hello from ESP32-S3 RLCD!\n"
    "Append test passed.\n";

typedef struct {
    bool mount_ok;
    bool directory_ok;
    bool write_ok;
    bool append_ok;
    bool read_ok;
    bool list_ok;
    char card_type[8];
    uint64_t capacity_mb;
    uint64_t used_mb;
    uint16_t item_count;
} sd_test_result_t;

static QueueHandle_t s_button_queue;
static bool s_display_ready;

static bool all_steps_passed(const sd_test_result_t *result)
{
    return result->mount_ok && result->directory_ok &&
           result->write_ok && result->append_ok &&
           result->read_ok && result->list_ok;
}

static const char *step_text(bool attempted, bool passed)
{
    if (!attempted) {
        return "--";
    }
    return passed ? "PASS" : "FAIL";
}

static void draw_step(
    int16_t y, const char *name, bool attempted, bool passed)
{
    mono_gfx_draw_text(20, y, name, BOARD_DISPLAY_BLACK, 1);
    mono_gfx_draw_text(
        124, y, step_text(attempted, passed), BOARD_DISPLAY_BLACK, 1);
}

static void draw_running_screen(void)
{
    if (!s_display_ready) {
        return;
    }
    board_display_clear(BOARD_DISPLAY_WHITE);
    mono_gfx_draw_text(16, 16, "SD CARD TEST", BOARD_DISPLAY_BLACK, 2);
    mono_gfx_draw_text(
        20, 84, "Running basic file test...", BOARD_DISPLAY_BLACK, 1);
    mono_gfx_draw_text(20, 110, "Please wait.", BOARD_DISPLAY_BLACK, 1);
    ESP_ERROR_CHECK_WITHOUT_ABORT(board_display_flush());
}

static void draw_result_screen(const sd_test_result_t *result)
{
    if (!s_display_ready) {
        return;
    }

    board_display_clear(BOARD_DISPLAY_WHITE);
    mono_gfx_draw_text(16, 12, "SD CARD TEST", BOARD_DISPLAY_BLACK, 2);
    mono_gfx_draw_text(
        304, 16, all_steps_passed(result) ? "PASS" : "FAIL",
        BOARD_DISPLAY_BLACK, 1);
    mono_gfx_draw_line(16, 40, 383, 40, BOARD_DISPLAY_BLACK);

    draw_step(54, "Mount", true, result->mount_ok);
    draw_step(78, "Directory", result->mount_ok, result->directory_ok);
    draw_step(102, "Write", result->directory_ok, result->write_ok);
    draw_step(126, "Append", result->write_ok, result->append_ok);
    draw_step(150, "Read", result->append_ok, result->read_ok);
    draw_step(174, "List", result->directory_ok, result->list_ok);

    char text[48];
    if (result->mount_ok) {
        snprintf(text, sizeof(text), "Type: %s", result->card_type);
        mono_gfx_draw_text(200, 54, text, BOARD_DISPLAY_BLACK, 1);
        snprintf(text, sizeof(text), "Size: %" PRIu64 " MB",
                 result->capacity_mb);
        mono_gfx_draw_text(200, 78, text, BOARD_DISPLAY_BLACK, 1);
        snprintf(text, sizeof(text), "Used: %" PRIu64 " MB",
                 result->used_mb);
        mono_gfx_draw_text(200, 102, text, BOARD_DISPLAY_BLACK, 1);
        snprintf(text, sizeof(text), "Items: %u", result->item_count);
        mono_gfx_draw_text(200, 126, text, BOARD_DISPLAY_BLACK, 1);
        mono_gfx_draw_text(
            20, 218, "File kept on card:", BOARD_DISPLAY_BLACK, 1);
        mono_gfx_draw_text(
            20, 240, DISPLAY_FILE, BOARD_DISPLAY_BLACK, 1);
    } else {
        mono_gfx_draw_text(
            20, 218, "SD card was not mounted.", BOARD_DISPLAY_BLACK, 1);
    }

    mono_gfx_draw_rect(
        8, 270, board_display_width() - 16, 22, BOARD_DISPLAY_BLACK);
    mono_gfx_draw_text(
        16, 276,
        result->mount_ok ? "KEY: RUN AGAIN"
                         : "Power off, insert FAT32, reboot",
        BOARD_DISPLAY_BLACK, 1);
    ESP_ERROR_CHECK_WITHOUT_ABORT(board_display_flush());
}

static bool ensure_directory(const char *path)
{
    struct stat status = {0};
    if (stat(path, &status) == 0) {
        return S_ISDIR(status.st_mode);
    }

    const bool ok = mkdir(path, 0775) == 0;
    ESP_LOGI(TAG, "mkdir %s: %s", path, ok ? "OK" : strerror(errno));
    return ok;
}

static bool write_text(const char *path, const char *mode, const char *text)
{
    /* wb truncates/overwrites; ab appends at the end. A fclose failure also counts as a write failure. */
    FILE *file = fopen(path, mode);
    if (file == NULL) {
        ESP_LOGE(TAG, "open %s failed: %s", path, strerror(errno));
        return false;
    }

    const size_t expected = strlen(text);
    const size_t written = fwrite(text, 1, expected, file);
    const bool close_ok = fclose(file) == 0;
    ESP_LOGI(TAG, "%s %s: %u/%u bytes",
             mode[0] == 'a' ? "append" : "write", path,
             (unsigned)written, (unsigned)expected);
    return written == expected && close_ok;
}

static bool read_text(const char *path, char *buffer, size_t capacity)
{
    if (buffer == NULL || capacity < 2) {
        return false;
    }

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        ESP_LOGE(TAG, "open %s failed: %s", path, strerror(errno));
        return false;
    }

    const size_t length = fread(buffer, 1, capacity - 1, file);
    /* Require actual EOF with no I/O error; filling the buffer alone does not mean the whole file was read. */
    const bool complete = feof(file) != 0;
    const bool read_ok = ferror(file) == 0;
    fclose(file);
    buffer[length] = '\0';
    return complete && read_ok;
}

static bool list_directory(const char *path, uint16_t *item_count)
{
    /* List entries non-recursively; read directory/entry information only, with no cleanup operations. */
    DIR *directory = opendir(path);
    if (directory == NULL) {
        ESP_LOGE(TAG, "opendir %s failed: %s", path, strerror(errno));
        return false;
    }

    uint16_t count = 0;
    ESP_LOGI(TAG, "directory %s:", path);
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char full_path[160];
        const int length = snprintf(
            full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
        if (length < 0 || (size_t)length >= sizeof(full_path)) {
            ESP_LOGW(TAG, "path too long: %s", entry->d_name);
            continue;
        }

        struct stat status = {0};
        if (stat(full_path, &status) == 0) {
            ESP_LOGI(TAG, "  %s  %s  %ld bytes",
                     S_ISDIR(status.st_mode) ? "[DIR ]" : "[FILE]",
                     entry->d_name, (long)status.st_size);
        } else {
            ESP_LOGI(TAG, "  [????]  %s", entry->d_name);
        }
        count++;
    }

    const bool close_ok = closedir(directory) == 0;
    *item_count = count;
    return close_ok;
}

static bool mount_and_read_info(sd_test_result_t *result)
{
    /* Reuse the mounted card when rerunning; hot-plug detection is not provided. A card-info failure does not imply a mount failure. */
    if (!board_sdcard_is_mounted()) {
        const board_sdcard_config_t config = BOARD_SDCARD_CONFIG_DEFAULT();
        const esp_err_t err = board_sdcard_mount(&config);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "mount failed: %s", esp_err_to_name(err));
            return false;
        }
    }

    board_sdcard_info_t info = {0};
    const esp_err_t err = board_sdcard_get_info(&info);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "card info failed: %s", esp_err_to_name(err));
        snprintf(result->card_type, sizeof(result->card_type), "--");
        return true;
    }

    snprintf(result->card_type, sizeof(result->card_type), "%s",
             info.card_type);
    result->capacity_mb = info.capacity_bytes / BYTES_PER_MEGABYTE;
    result->used_mb = info.used_bytes / BYTES_PER_MEGABYTE;
    ESP_LOGI(TAG, "type=%s, capacity=%" PRIu64 " MB, used=%" PRIu64 " MB",
             result->card_type, result->capacity_mb, result->used_mb);
    return true;
}

static void run_sd_card_test(void)
{
    /* Called only by the application task, preventing concurrent reruns from overwriting the test file through two FILE streams. */
    draw_running_screen();
    sd_test_result_t result = {0};
    snprintf(result.card_type, sizeof(result.card_type), "--");

    ESP_LOGI(TAG, "========== SD CARD TEST ==========");
    result.mount_ok = mount_and_read_info(&result);
    if (result.mount_ok) {
        result.directory_ok = ensure_directory(DEMO_DIRECTORY);
        if (result.directory_ok) {
            /* Start each test by overwriting, then append and compare the complete text; do not accumulate test logs. */
            result.write_ok = write_text(DEMO_FILE, "wb", FIRST_TEXT);
            if (result.write_ok) {
                result.append_ok = write_text(DEMO_FILE, "ab", APPEND_TEXT);
            }

            char read_back[160] = {0};
            if (result.append_ok &&
                read_text(DEMO_FILE, read_back, sizeof(read_back))) {
                ESP_LOGI(TAG, "read back:\n%s", read_back);
                result.read_ok = strcmp(read_back, EXPECTED_TEXT) == 0;
                ESP_LOGI(TAG, "content check: %s",
                         result.read_ok ? "PASS" : "FAIL");
            }
            result.list_ok = list_directory(
                DEMO_DIRECTORY, &result.item_count);

            board_sdcard_info_t info = {0};
            if (board_sdcard_get_info(&info) == ESP_OK) {
                result.used_mb = info.used_bytes / BYTES_PER_MEGABYTE;
            }
        }
    }

    ESP_LOGI(TAG, "[09] result: %s",
             all_steps_passed(&result) ? "PASS" : "FAIL");
    draw_result_screen(&result);
}

static void button_callback(button_service_event_t event, void *context)
{
    /* Runs in the button task; post without waiting and ignore extra rerun requests if the queue is full. */
    (void)context;
    if (event == BUTTON_SERVICE_EVENT_USER_PRESSED) {
        const uint8_t signal = 1;
        xQueueSend(s_button_queue, &signal, 0);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "[09] basic SD card read/write");

    const board_display_config_t display_config =
        BOARD_DISPLAY_CONFIG_DEFAULT();
    esp_err_t err = board_display_init(&display_config);
    s_display_ready = err == ESP_OK;
    if (!s_display_ready) {
        ESP_LOGE(TAG, "display init failed: %s", esp_err_to_name(err));
    }

    s_button_queue = xQueueCreate(4, sizeof(uint8_t));
    ESP_ERROR_CHECK(s_button_queue == NULL ? ESP_ERR_NO_MEM : ESP_OK);
    const button_service_config_t button_config =
        BUTTON_SERVICE_CONFIG_DEFAULT(button_callback, NULL);
    ESP_ERROR_CHECK(button_service_start(&button_config));

    run_sd_card_test();
    for (;;) {
        /* Block when no button event is available; queued requests run sequentially after the previous test finishes. */
        uint8_t signal;
        if (xQueueReceive(s_button_queue, &signal, portMAX_DELAY) == pdTRUE) {
            run_sd_card_test();
        }
    }
}
