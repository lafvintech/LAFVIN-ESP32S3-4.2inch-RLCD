#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char card_type[8];
    uint64_t capacity_bytes;   /* Card-reported sector capacity: sector count x sector size. */
    uint64_t filesystem_bytes; /* Mounted filesystem capacity, which may differ from card capacity. */
    uint64_t used_bytes;       /* Filesystem space used, not just this example's files. */
} board_sdcard_info_t;

typedef struct {
    const char *mount_path;
    size_t max_open_files;
} board_sdcard_config_t;

#define BOARD_SDCARD_CONFIG_DEFAULT() \
    {                                  \
        .mount_path = "/sdcard",       \
        .max_open_files = 5,           \
    }

/* SDMMC 1-bit + FATFS/VFS singleton; mount_path is copied, and a mount failure never triggers formatting.
  * After mounting, use standard file paths with the mount prefix; file operations and hot-plug handling are outside this component.
  * Callers must serialize mounting/unmounting and file access; there is no internal task mutex. */
esp_err_t board_sdcard_mount(const board_sdcard_config_t *config);
esp_err_t board_sdcard_get_info(board_sdcard_info_t *info);
/* Close all FILE streams/directories and stop other I/O tasks before unmounting; do not remove a card still in use. */
esp_err_t board_sdcard_unmount(void);
bool board_sdcard_is_mounted(void);
/* Return an internal read-only string, or NULL when unmounted; do not free/modify it or use it after unmounting. */
const char *board_sdcard_mount_path(void);

#ifdef __cplusplus
}
#endif
