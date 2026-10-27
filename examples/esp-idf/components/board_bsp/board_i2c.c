#include "board_i2c.h"

#include "board_pins.h"

static i2c_master_bus_handle_t s_bus_handle;

esp_err_t board_i2c_init(void)
{
    if (s_bus_handle != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    const i2c_master_bus_config_t config = {
        .i2c_port = BOARD_I2C_PORT,
        .sda_io_num = BOARD_I2C_SDA_GPIO,
        .scl_io_num = BOARD_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&config, &s_bus_handle);
}

esp_err_t board_i2c_probe(uint8_t address)
{
    if (s_bus_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (address == 0 || address >= 0x7F) {
        return ESP_ERR_INVALID_ARG;
    }

    return i2c_master_probe(s_bus_handle, address, 50);
}

i2c_master_bus_handle_t board_i2c_get_handle(void)
{
    return s_bus_handle;
}

esp_err_t board_i2c_deinit(void)
{
    if (s_bus_handle == NULL) {
        return ESP_OK;
    }

    const esp_err_t err = i2c_del_master_bus(s_bus_handle);
    if (err == ESP_OK) {
        s_bus_handle = NULL;
    }
    return err;
}

