#include "wav_store.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "board_sdcard.h"
#include "esp_log.h"

/* VFS paths include the mount point; the on-card directory is /recordings. Enable FATFS long filenames in the project. */
#define RECORDING_DIRECTORY "/sdcard/recordings"
#define WAV_HEADER_BYTES 44

/* File state is distinct from application state: files may already be closed while the application is in ERROR/IDLE. */
typedef enum { FILE_IDLE, FILE_RECORDING, FILE_PLAYING } file_mode_t;
static const char *TAG = "wav_store";
static FILE *s_file;
static file_mode_t s_mode;
static wav_store_info_t s_info;
static uint32_t s_sample_rate;
static unsigned s_next_number = 1;
static char s_current_path[64];
static char s_latest_path[64];
static char s_error[80];

static esp_err_t fail(esp_err_t err, const char *message)
{
    snprintf(s_error, sizeof(s_error), "%s", message);
    ESP_LOGE(TAG, "%s", s_error);
    return err;
}

/* Encode little-endian bytes explicitly instead of writing C structures with padding/alignment directly to the file. */
static void put_u16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void put_u32(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) {
        p[i] = (uint8_t)(value >> (i * 8));
    }
}

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static const char *base_name(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash == NULL ? path : slash + 1;
}

static unsigned recording_number(const char *name)
{
    if (strlen(name) != 11 || memcmp(name, "REC", 3) != 0 ||
        strcmp(name + 7, ".WAV") != 0) {
        return 0;
    }
    unsigned number = 0;
    for (unsigned i = 3; i < 7; ++i) {
        if (name[i] < '0' || name[i] > '9') {
            return 0;
        }
        number = number * 10 + (unsigned)(name[i] - '0');
    }
    return number;
}

static void make_header(uint8_t header[WAV_HEADER_BYTES])
{
    memset(header, 0, WAV_HEADER_BYTES);
    memcpy(header, "RIFF", 4);
    put_u32(header + 4, 36 + s_info.recorded_bytes);
    memcpy(header + 8, "WAVEfmt ", 8);
    put_u32(header + 16, 16);
    put_u16(header + 20, 1);
    put_u16(header + 22, 1);
    put_u32(header + 24, s_sample_rate);
    put_u32(header + 28, s_sample_rate * 2);
    put_u16(header + 32, 2);
    put_u16(header + 34, 16);
    memcpy(header + 36, "data", 4);
    put_u32(header + 40, s_info.recorded_bytes);
}

/* Check fixed RIFF/fmt/data positions and lengths; do not skip extension chunks such as LIST or decode compressed audio.
  * sample_rate=0 accepts any sample rate during scanning; enforce the application's rate when starting playback. */
static bool valid_header(const char *path, uint32_t sample_rate, uint32_t *data_bytes)
{
    struct stat status;
    if (stat(path, &status) != 0 || !S_ISREG(status.st_mode) ||
        status.st_size < WAV_HEADER_BYTES) {
        return false;
    }
    FILE *probe = fopen(path, "rb");
    if (probe == NULL) {
        return false;
    }
    uint8_t header[WAV_HEADER_BYTES];
    const size_t read_bytes = fread(header, 1, sizeof(header), probe);
    const bool close_ok = fclose(probe) == 0;
    if (read_bytes != sizeof(header) || !close_ok ||
        memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVEfmt ", 8) != 0 ||
        memcmp(header + 36, "data", 4) != 0 || get_u32(header + 16) != 16 ||
        get_u16(header + 20) != 1 || get_u16(header + 22) != 1 ||
        get_u16(header + 32) != 2 || get_u16(header + 34) != 16) {
        return false;
    }
    const uint32_t rate = get_u32(header + 24);
    const uint32_t bytes = get_u32(header + 40);
    if (rate == 0 || rate > UINT32_MAX / 2 ||
        (sample_rate != 0 && rate != sample_rate) || get_u32(header + 28) != rate * 2 ||
        bytes > UINT32_MAX - 36 || get_u32(header + 4) != bytes + 36 ||
        bytes == 0 || (bytes & 1U) != 0 ||
        (uint64_t)bytes > (uint64_t)status.st_size - WAV_HEADER_BYTES) {
        return false;
    }
    *data_bytes = bytes;
    return true;
}

static esp_err_t scan_recordings(void)
{
    DIR *directory = opendir(RECORDING_DIRECTORY);
    if (directory == NULL) {
        return fail(ESP_FAIL, "cannot open /recordings");
    }
    /* Track the latest playable number separately from the highest occupied number: never play damaged files or overwrite them with new recordings. */
    unsigned latest_number = 0;
    unsigned highest_number = 0;
    struct dirent *entry;
    errno = 0;
    while ((entry = readdir(directory)) != NULL) {
        const unsigned number = recording_number(entry->d_name);
        if (number == 0) {
            continue;
        }
        if (number > highest_number) {
            highest_number = number;
        }
        char path[64];
        snprintf(path, sizeof(path), RECORDING_DIRECTORY "/REC%04u.WAV", number);
        uint32_t bytes = 0;
        if (number > latest_number && valid_header(path, 0, &bytes)) {
            latest_number = number;
            s_info.latest_bytes = bytes;
            snprintf(s_latest_path, sizeof(s_latest_path), "%s", path);
        }
        errno = 0;
    }
    const bool read_ok = errno == 0;
    const bool close_ok = closedir(directory) == 0;
    if (!read_ok || !close_ok) {
        return fail(ESP_FAIL, "cannot scan /recordings");
    }
    s_info.has_recording = latest_number != 0;
    s_next_number = highest_number + 1;
    return ESP_OK;
}

esp_err_t wav_store_init(void)
{
    if (s_info.ready) {
        return ESP_OK;
    }
    memset(&s_info, 0, sizeof(s_info));
    s_latest_path[0] = '\0';
    s_error[0] = '\0';
    const board_sdcard_config_t config = BOARD_SDCARD_CONFIG_DEFAULT();
    esp_err_t err = board_sdcard_is_mounted() ? ESP_OK : board_sdcard_mount(&config);
    if (err != ESP_OK) {
        return fail(err, "mount failed; insert a FAT32 card");
    }
    struct stat status;
    if (stat(RECORDING_DIRECTORY, &status) != 0) {
        if (errno != ENOENT || mkdir(RECORDING_DIRECTORY, 0775) != 0) {
            err = fail(ESP_FAIL, "cannot create /recordings");
        }
    } else if (!S_ISDIR(status.st_mode)) {
        err = fail(ESP_ERR_INVALID_STATE, "/recordings is not a directory");
    }
    if (err == ESP_OK) {
        err = scan_recordings();
    }
    if (err != ESP_OK) {
        board_sdcard_unmount();
        return err;
    }
    s_info.ready = true;
    const wav_store_info_t info = wav_store_get_info();
    ESP_LOGI(TAG, "SD ready, latest=%s", info.file_name);
    return ESP_OK;
}

esp_err_t wav_store_remount(void)
{
    wav_store_abort_recording();
    wav_store_stop_playback();
    s_info.ready = false;
    if (board_sdcard_is_mounted()) {
        const esp_err_t err = board_sdcard_unmount();
        if (err != ESP_OK) {
            return fail(err, "SD unmount failed");
        }
    }
    return wav_store_init();
}

wav_store_info_t wav_store_get_info(void)
{
    wav_store_info_t info = s_info;
    snprintf(info.file_name, sizeof(info.file_name), "%.15s",
             s_mode == FILE_RECORDING ? base_name(s_current_path) :
             (s_info.has_recording ? base_name(s_latest_path) : "--"));
    return info;
}

const char *wav_store_last_error(void)
{
    return s_error;
}

esp_err_t wav_store_start_recording(uint32_t sample_rate)
{
    if (!s_info.ready || s_mode != FILE_IDLE) {
        return fail(ESP_ERR_INVALID_STATE, "SD is not ready or file is busy");
    }
    if (sample_rate == 0 || sample_rate > UINT32_MAX / 2) {
        return fail(ESP_ERR_INVALID_ARG, "invalid sample rate");
    }
    int fd = -1;
    while (s_next_number <= 9999) {
        snprintf(s_current_path, sizeof(s_current_path),
                 RECORDING_DIRECTORY "/REC%04u.WAV", s_next_number);
        /* Create exclusively; if a file exists, try another number rather than truncating an old recording. */
        fd = open(s_current_path, O_CREAT | O_EXCL | O_RDWR, 0664);
        if (fd >= 0) {
            break;
        }
        if (errno != EEXIST) {
            return fail(ESP_FAIL, "cannot create WAV file");
        }
        ++s_next_number;
    }
    if (fd < 0) {
        return fail(ESP_ERR_INVALID_SIZE, "recording number limit reached");
    }
    s_file = fdopen(fd, "wb+");
    if (s_file == NULL) {
        close(fd);
        remove(s_current_path);
        return fail(ESP_FAIL, "cannot open WAV stream");
    }
    s_mode = FILE_RECORDING;
    s_sample_rate = sample_rate;
    s_info.recorded_bytes = 0;
    s_error[0] = '\0';
    /* Placeholder header -> append PCM blocks -> fill in lengths on stop; unfinished files may be invalid after power loss.
      * On runtime errors, the application uses abort to delete this file; do not remove the card while recording. */
    const uint8_t placeholder[WAV_HEADER_BYTES] = {0};
    if (fwrite(placeholder, 1, sizeof(placeholder), s_file) != sizeof(placeholder)) {
        wav_store_abort_recording();
        return fail(ESP_FAIL, "cannot write WAV header");
    }
    return ESP_OK;
}

esp_err_t wav_store_write_mono(const int16_t *samples, size_t count)
{
    if (s_mode != FILE_RECORDING || samples == NULL || count == 0) {
        return fail(ESP_ERR_INVALID_STATE, "WAV file is not recording");
    }
    /* RIFF lengths are 32-bit; reserve room for the header and prevent accumulated byte counts from wrapping. */
    if (count > (UINT32_MAX - 36U - s_info.recorded_bytes) / sizeof(int16_t)) {
        return fail(ESP_ERR_INVALID_SIZE, "WAV size limit reached");
    }
    if (fwrite(samples, sizeof(int16_t), count, s_file) != count) {
        return fail(ESP_FAIL, "SD write failed or card is full");
    }
    s_info.recorded_bytes += (uint32_t)(count * sizeof(int16_t));
    return ESP_OK;
}

void wav_store_abort_recording(void)
{
    if (s_mode != FILE_RECORDING) {
        return;
    }
    fclose(s_file);
    s_file = NULL;
    s_mode = FILE_IDLE;
    if (remove(s_current_path) != 0) {
        ESP_LOGW(TAG, "could not remove incomplete recording %s", s_current_path);
    }
}

esp_err_t wav_store_stop_recording(void)
{
    if (s_mode != FILE_RECORDING) {
        return fail(ESP_ERR_INVALID_STATE, "WAV file is not recording");
    }
    if (s_info.recorded_bytes == 0) {
        wav_store_abort_recording();
        return fail(ESP_ERR_INVALID_SIZE, "no audio was captured");
    }
    uint8_t header[WAV_HEADER_BYTES];
    make_header(header);
    if (fseek(s_file, 0, SEEK_SET) != 0 ||
        fwrite(header, 1, sizeof(header), s_file) != sizeof(header) ||
        fflush(s_file) != 0) {
        wav_store_abort_recording();
        return fail(ESP_FAIL, "cannot finalize WAV header");
    }
    const bool close_ok = fclose(s_file) == 0;
    s_file = NULL;
    s_mode = FILE_IDLE;
    if (!close_ok) {
        remove(s_current_path);
        return fail(ESP_FAIL, "WAV close failed");
    }
    /* Commit the new file as the latest recording only after header finalization, flush, and close all succeed. */
    s_info.has_recording = true;
    s_info.latest_bytes = s_info.recorded_bytes;
    snprintf(s_latest_path, sizeof(s_latest_path), "%s", s_current_path);
    ++s_next_number;
    return ESP_OK;
}

esp_err_t wav_store_start_playback(uint32_t sample_rate)
{
    if (!s_info.ready || !s_info.has_recording || s_mode != FILE_IDLE) {
        return fail(ESP_ERR_INVALID_STATE, "no recording or file is busy");
    }
    uint32_t bytes = 0;
    if (!valid_header(s_latest_path, sample_rate, &bytes)) {
        return fail(ESP_ERR_INVALID_RESPONSE, "latest WAV format is invalid");
    }
    s_file = fopen(s_latest_path, "rb");
    if (s_file == NULL) {
        return fail(ESP_FAIL, "cannot open latest WAV");
    }
    if (fseek(s_file, WAV_HEADER_BYTES, SEEK_SET) != 0) {
        fclose(s_file);
        s_file = NULL;
        return fail(ESP_FAIL, "cannot seek to WAV data");
    }
    s_info.played_bytes = 0;
    s_info.total_bytes = bytes;
    s_mode = FILE_PLAYING;
    s_error[0] = '\0';
    return ESP_OK;
}

esp_err_t wav_store_read_mono(int16_t *samples, size_t capacity, size_t *count)
{
    if (count != NULL) {
        *count = 0;
    }
    if (s_mode != FILE_PLAYING || samples == NULL || count == NULL || capacity == 0) {
        return fail(ESP_ERR_INVALID_STATE, "WAV file is not playing");
    }
    const uint32_t remaining = (s_info.total_bytes - s_info.played_bytes) / 2;
    const size_t wanted = capacity < remaining ? capacity : remaining;
    if (wanted == 0) {
        return ESP_OK;
    }
    const size_t actual = fread(samples, sizeof(int16_t), wanted, s_file);
    /* Missing data promised by the header indicates a read error/truncated file, not normal EOF. */
    if (actual != wanted || ferror(s_file)) {
        return fail(ESP_FAIL, "SD read failed or WAV was truncated");
    }
    s_info.played_bytes += (uint32_t)(actual * sizeof(int16_t));
    *count = actual;
    return ESP_OK;
}

esp_err_t wav_store_stop_playback(void)
{
    if (s_mode != FILE_PLAYING) {
        return ESP_OK;
    }
    const bool close_ok = fclose(s_file) == 0;
    s_file = NULL;
    s_mode = FILE_IDLE;
    return close_ok ? ESP_OK : fail(ESP_FAIL, "playback file close failed");
}
