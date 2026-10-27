#include "board_sdcard.h"

#include <stdio.h>
#include <string.h>

#include "board_pins.h"
#include "driver/sdmmc_host.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#define MOUNT_PATH_CAPACITY 32

/* SD OCR bit 30 is the Card Capacity Status (CCS) bit. ESP-IDF keeps the
 * matching helper macro private, so the board adapter uses the SD-defined
 * public bit value instead of depending on an internal header. */
#define SD_OCR_CARD_CAPACITY_STATUS (UINT32_C(1) << 30)

static sdmmc_card_t *s_card;
static char s_mount_path[MOUNT_PATH_CAPACITY];

static const char *card_type_name(const sdmmc_card_t *card)
{
    /* Classify by card protocol status only; this does not identify the filesystem as FAT32, exFAT, etc. */
    if (card->is_mmc) {
        return "MMC";
    }
    return (card->ocr & SD_OCR_CARD_CAPACITY_STATUS) != 0
               ? "SDHC"
               : "SDSC";
}

esp_err_t board_sdcard_mount(const board_sdcard_config_t *config)
{
    if (config == NULL || config->mount_path == NULL ||
        config->mount_path[0] != '/' || config->max_open_files == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_card != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (strlen(config->mount_path) >= sizeof(s_mount_path)) {
        return ESP_ERR_INVALID_SIZE;
    }

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1; /* This board connects only CLK/CMD/D0; do not simply switch to 4-bit mode. */
    slot.clk = BOARD_SD_CLK_GPIO;
    slot.cmd = BOARD_SD_CMD_GPIO;
    slot.d0 = BOARD_SD_D0_GPIO;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    const esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        /* Let the application report failures; do not use automatic formatting as recovery. */
        .format_if_mount_failed = false,
        .max_files = config->max_open_files,
        .allocation_unit_size = 16 * 1024,
    };

    esp_err_t err = esp_vfs_fat_sdmmc_mount(
        config->mount_path, &host, &slot, &mount_config, &s_card);
    if (err != ESP_OK) {
        s_card = NULL;
        return err;
    }

    snprintf(s_mount_path, sizeof(s_mount_path), "%s", config->mount_path);
    return ESP_OK;
}

esp_err_t board_sdcard_get_info(board_sdcard_info_t *info)
{
    if (s_card == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(info, 0, sizeof(*info));
    snprintf(info->card_type, sizeof(info->card_type), "%s",
             card_type_name(s_card));
    info->capacity_bytes =
        (uint64_t)s_card->csd.capacity * s_card->csd.sector_size;

    uint64_t free_bytes = 0;
    esp_err_t err = esp_vfs_fat_info(
        s_mount_path, &info->filesystem_bytes, &free_bytes);
    if (err != ESP_OK) {
        return err;
    }
    /* Filesystem usage and total card capacity are different; do not subtract free_bytes from card capacity. */
    info->used_bytes = info->filesystem_bytes - free_bytes;
    return ESP_OK;
}

esp_err_t board_sdcard_unmount(void)
{
    if (s_card == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = esp_vfs_fat_sdcard_unmount(s_mount_path, s_card);
    if (err == ESP_OK) {
        s_card = NULL;
        s_mount_path[0] = '\0';
    }
    return err;
}

bool board_sdcard_is_mounted(void)
{
    return s_card != NULL;
}

const char *board_sdcard_mount_path(void)
{
    return s_card == NULL ? NULL : s_mount_path;
}
