#include "shtc3.h"

#include <stddef.h>

#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SHTC3_I2C_ADDRESS       0x70
#define SHTC3_I2C_FREQUENCY_HZ 400000
#define SHTC3_TIMEOUT_MS        200

#define SHTC3_CMD_WAKEUP  0x3517
#define SHTC3_CMD_MEASURE 0x7866 /* Normal mode, temperature first, no clock stretching. */
#define SHTC3_CMD_SLEEP   0xB098
#define SHTC3_CMD_READ_ID 0xEFC8

static i2c_master_dev_handle_t s_device;

static uint8_t crc8(const uint8_t *data, size_t data_size)
{
    /* SHTC3 CRC: polynomial 0x31, initial value 0xFF, no input/output reflection, MSB-first shifting.
      * Each 16-bit data word has its own CRC; do not compute a single CRC across all four temperature/humidity bytes.
      * Reference: https://sensirion.com/file/datasheet_shtc3 */
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < data_size; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x80) != 0
                      ? (uint8_t)((crc << 1) ^ 0x31)
                      : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

static esp_err_t send_command(uint16_t command)
{
    /* Send the 16-bit command MSB first; this is not an 8-bit register address followed by a register value. */
    const uint8_t bytes[] = {
        (uint8_t)(command >> 8),
        (uint8_t)(command & 0xFF),
    };
    return i2c_master_transmit(
        s_device, bytes, sizeof(bytes), SHTC3_TIMEOUT_MS);
}

esp_err_t shtc3_init(i2c_master_bus_handle_t bus)
{
    if (bus == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_device != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    const i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SHTC3_I2C_ADDRESS,
        .scl_speed_hz = SHTC3_I2C_FREQUENCY_HZ,
    };
    return i2c_master_bus_add_device(
        bus, &device_config, &s_device);
}

esp_err_t shtc3_read_id(uint16_t *chip_id)
{
    if (s_device == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (chip_id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = send_command(SHTC3_CMD_READ_ID);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t response[3] = {0};
    err = i2c_master_receive(
        s_device, response, sizeof(response), SHTC3_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }
    if (crc8(response, 2) != response[2]) {
        return ESP_ERR_INVALID_CRC;
    }

    *chip_id = (uint16_t)((response[0] << 8) | response[1]);
    return ESP_OK;
}

esp_err_t shtc3_measure(shtc3_sample_t *sample)
{
    if (s_device == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (sample == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = send_command(SHTC3_CMD_WAKEUP);
    if (err != ESP_OK) {
        return err;
    }
    /* Allow 1 ms to settle after wakeup before sending the next command. */
    esp_rom_delay_us(1000);

    err = send_command(SHTC3_CMD_MEASURE);
    if (err != ESP_OK) {
        (void)send_command(SHTC3_CMD_SLEEP);
        return err;
    }
    /* No clock stretching: wait for conversion before reading; yield the task rather than busy-waiting. */
    vTaskDelay(pdMS_TO_TICKS(15));

    uint8_t response[6] = {0};
    err = i2c_master_receive(
        s_device, response, sizeof(response), SHTC3_TIMEOUT_MS);
    /* Try to sleep after receiving data, even if CRC validation later fails, rather than leaving the sensor active.
      * Also check the sleep command result; on failure, preserve the caller's previous sample. */
    const esp_err_t sleep_err = send_command(SHTC3_CMD_SLEEP);
    if (err != ESP_OK) {
        return err;
    }
    if (sleep_err != ESP_OK) {
        return sleep_err;
    }
    if (crc8(&response[0], 2) != response[2] ||
        crc8(&response[3], 2) != response[5]) {
        return ESP_ERR_INVALID_CRC;
    }

    const uint16_t raw_temperature =
        (uint16_t)((response[0] << 8) | response[1]);
    const uint16_t raw_humidity =
        (uint16_t)((response[3] << 8) | response[4]);
    sample->temperature_c =
        -45.0f + 175.0f * (float)raw_temperature / 65536.0f;
    sample->humidity_percent =
        100.0f * (float)raw_humidity / 65536.0f;
    return ESP_OK;
}
