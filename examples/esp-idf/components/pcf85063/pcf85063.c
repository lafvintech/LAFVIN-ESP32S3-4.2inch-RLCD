#include "pcf85063.h"

#include <stddef.h>

#define PCF85063_I2C_ADDRESS      0x51
#define PCF85063_I2C_FREQUENCY_HZ 300000
#define PCF85063_TIMEOUT_MS       200

#define PCF85063_REG_CONTROL_1 0x00
#define PCF85063_REG_SECONDS   0x04
/* OS is the oscillator-stop flag, not a battery-level flag; see NXP PCF85063A datasheet section 7.3.1.1.
 * https://www.nxp.com/docs/en/data-sheet/PCF85063A.pdf */
#define PCF85063_OS_FLAG       0x80

static i2c_master_dev_handle_t s_device;

static uint8_t from_bcd(uint8_t value)
{
    /* The high nibble holds tens and the low nibble units: 0x59 means 59, not decimal 89. */
    return (uint8_t)((value >> 4) * 10 + (value & 0x0F));
}

static uint8_t to_bcd(uint8_t value)
{
    return (uint8_t)(((value / 10) << 4) | (value % 10));
}

static bool is_leap_year(uint16_t year)
{
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

static uint8_t days_in_month(uint16_t year, uint8_t month)
{
    static const uint8_t days[] = {
        31, 28, 31, 30, 31, 30,
        31, 31, 30, 31, 30, 31,
    };
    if (month < 1 || month > 12) {
        return 0;
    }
    if (month == 2 && is_leap_year(year)) {
        return 29;
    }
    return days[month - 1];
}

bool pcf85063_datetime_is_valid(const pcf85063_datetime_t *datetime)
{
    return datetime != NULL &&
           datetime->year >= 2000 && datetime->year <= 2099 &&
           datetime->month >= 1 && datetime->month <= 12 &&
           datetime->day >= 1 &&
           datetime->day <= days_in_month(datetime->year, datetime->month) &&
           datetime->weekday <= 6 &&
           datetime->hour <= 23 &&
           datetime->minute <= 59 &&
           datetime->second <= 59;
}

uint8_t pcf85063_calculate_weekday(
    uint16_t year, uint8_t month, uint8_t day)
{
    static const uint8_t month_offsets[] = {
        0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4,
    };
    if (year < 2000 || year > 2099 || month < 1 || month > 12 ||
        day < 1 || day > days_in_month(year, month)) {
        return 0;
    }

    /* Sakamoto's algorithm: January/February belong to the previous year; 0=Sunday. */
    uint16_t adjusted_year = year;
    if (month < 3) {
        --adjusted_year;
    }
    return (uint8_t)((adjusted_year + adjusted_year / 4 -
                      adjusted_year / 100 + adjusted_year / 400 +
                      month_offsets[month - 1] + day) % 7);
}

static esp_err_t read_registers(
    uint8_t register_address, uint8_t *data, size_t data_size)
{
    /* Write the register address, then read with a repeated START in the same transaction; do not split it. */
    return i2c_master_transmit_receive(
        s_device, &register_address, 1,
        data, data_size, PCF85063_TIMEOUT_MS);
}

static esp_err_t write_registers(
    uint8_t register_address, const uint8_t *data, size_t data_size)
{
    if (data_size > 7) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t transfer[8] = {register_address};
    for (size_t i = 0; i < data_size; ++i) {
        transfer[i + 1] = data[i];
    }
    return i2c_master_transmit(
        s_device, transfer, data_size + 1, PCF85063_TIMEOUT_MS);
}

esp_err_t pcf85063_init(i2c_master_bus_handle_t bus)
{
    if (bus == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_device != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    const i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PCF85063_I2C_ADDRESS,
        .scl_speed_hz = PCF85063_I2C_FREQUENCY_HZ,
    };
    esp_err_t err = i2c_master_bus_add_device(
        bus, &device_config, &s_device);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t control_1 = 0;
    err = read_registers(PCF85063_REG_CONTROL_1, &control_1, 1);
    if (err != ESP_OK) {
        return err;
    }

    /* Control_1: clear STOP (bit 5) and 12_24 (bit 1) to run in 24-hour mode.
      * Preserve other configuration bits; do not change the date or clear OS here. */
    const uint8_t wanted =
        control_1 & (uint8_t)~((1U << 5) | (1U << 1));
    if (wanted == control_1) {
        return ESP_OK;
    }
    return write_registers(PCF85063_REG_CONTROL_1, &wanted, 1);
}

esp_err_t pcf85063_read(
    pcf85063_datetime_t *datetime, bool *oscillator_stopped)
{
    if (s_device == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (datetime == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Read seconds, minutes, hours, day, weekday, month, and year together from 0x04 to avoid split reads across a second boundary. */
    uint8_t raw[7] = {0};
    const esp_err_t err = read_registers(
        PCF85063_REG_SECONDS, raw, sizeof(raw));
    if (err != ESP_OK) {
        return err;
    }

    /* Seconds bit 7 is OS; mask it before BCD conversion and return OS separately for the application to check. */
    datetime->second = from_bcd(raw[0] & 0x7F);
    datetime->minute = from_bcd(raw[1] & 0x7F);
    datetime->hour = from_bcd(raw[2] & 0x3F);
    datetime->day = from_bcd(raw[3] & 0x3F);
    datetime->weekday = raw[4] & 0x07;
    datetime->month = from_bcd(raw[5] & 0x1F);
    datetime->year = (uint16_t)(2000 + from_bcd(raw[6]));

    if (oscillator_stopped != NULL) {
        *oscillator_stopped = (raw[0] & PCF85063_OS_FLAG) != 0;
    }
    return ESP_OK;
}

esp_err_t pcf85063_write(const pcf85063_datetime_t *datetime)
{
    if (s_device == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!pcf85063_datetime_is_valid(datetime)) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t registers[] = {
        to_bcd(datetime->second) & 0x7F, /* Clear OS while preserving the requested seconds value. */
        to_bcd(datetime->minute),
        to_bcd(datetime->hour),
        to_bcd(datetime->day),
        datetime->weekday,
        to_bcd(datetime->month),
        to_bcd((uint8_t)(datetime->year % 100)),
    };
    return write_registers(
        PCF85063_REG_SECONDS, registers, sizeof(registers));
}
