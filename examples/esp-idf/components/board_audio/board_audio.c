#include "board_audio.h"

#include <limits.h>
#include <math.h>

#include "board_i2c.h"
#include "board_pins.h"
#include "driver/i2s_tdm.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"

/* Expose only recording/playback operations; this layer owns the lifetimes of I2S, codec interfaces, and device handles.
  * Speaker/microphone share data_if and clocks; close sessions before releasing resources in dependency order. */
static const char *TAG = "board_audio";
static board_audio_config_t s_config;
static i2s_chan_handle_t s_tx;
static i2s_chan_handle_t s_rx;
static const audio_codec_data_if_t *s_data;
static const audio_codec_gpio_if_t *s_gpio;
static const audio_codec_ctrl_if_t *s_out_ctrl;
static const audio_codec_ctrl_if_t *s_in_ctrl;
static const audio_codec_if_t *s_out_codec;
static const audio_codec_if_t *s_in_codec;
static esp_codec_dev_handle_t s_speaker;
static esp_codec_dev_handle_t s_mic;
static bool s_speaker_open;
static bool s_mic_open;
/* open tracks the logical session; running tracks the stable I2S state between codec calls.
  * Codec configuration temporarily stops/starts shared-bus channels, so session flags alone cannot determine channel state. */
static bool s_tx_running;
static bool s_rx_running;

static esp_err_t set_channel_running(
    i2s_chan_handle_t channel, bool *running, bool enable)
{
    if (channel == NULL) {
        return enable ? ESP_ERR_INVALID_STATE : ESP_OK;
    }
    /* Avoid repeated enable/disable calls; update state only after the driver operation succeeds. */
    if (*running == enable) {
        return ESP_OK;
    }
    const esp_err_t err = enable ? i2s_channel_enable(channel)
                                 : i2s_channel_disable(channel);
    if (err == ESP_OK) {
        *running = enable;
    }
    return err;
}

static esp_err_t start_i2s(void)
{
    esp_err_t err = set_channel_running(s_tx, &s_tx_running, true);
    if (err == ESP_OK) {
        err = set_channel_running(s_rx, &s_rx_running, true);
    }
    return err;
}

static esp_err_t stop_i2s(void)
{
    /* TX supplies the shared clock: start TX before RX, and stop RX before TX. Do not reorder these steps. */
    const esp_err_t rx_err = set_channel_running(s_rx, &s_rx_running, false);
    const esp_err_t tx_err = set_channel_running(s_tx, &s_tx_running, false);
    return rx_err != ESP_OK ? rx_err : tx_err;
}

static void release_resources(void)
{
    ESP_ERROR_CHECK_WITHOUT_ABORT(board_audio_close());
    if (s_speaker != NULL) {
        esp_codec_dev_delete(s_speaker);
        s_speaker = NULL;
    }
    if (s_mic != NULL) {
        esp_codec_dev_delete(s_mic);
        s_mic = NULL;
    }
    if (s_out_codec != NULL) {
        audio_codec_delete_codec_if(s_out_codec);
        s_out_codec = NULL;
    }
    if (s_in_codec != NULL) {
        audio_codec_delete_codec_if(s_in_codec);
        s_in_codec = NULL;
    }
    if (s_out_ctrl != NULL) {
        audio_codec_delete_ctrl_if(s_out_ctrl);
        s_out_ctrl = NULL;
    }
    if (s_in_ctrl != NULL) {
        audio_codec_delete_ctrl_if(s_in_ctrl);
        s_in_ctrl = NULL;
    }
    if (s_data != NULL) {
        audio_codec_delete_data_if(s_data);
        s_data = NULL;
    }
    if (s_gpio != NULL) {
        audio_codec_delete_gpio_if(s_gpio);
        s_gpio = NULL;
    }
    if (s_tx != NULL) {
        i2s_del_channel(s_tx);
        s_tx = NULL;
    }
    if (s_rx != NULL) {
        i2s_del_channel(s_rx);
        s_rx = NULL;
    }
    s_speaker_open = false;
    s_mic_open = false;
    s_tx_running = false;
    s_rx_running = false;
    /* The shared I2C bus belongs to board_bsp and must not be deleted during audio retries. */
}

esp_err_t board_audio_init(const board_audio_config_t *config)
{
    if (config == NULL || config->sample_rate == 0 ||
        config->speaker_volume < 0 || config->speaker_volume > 100 ||
        !isfinite(config->mic_gain_db)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (board_audio_is_ready()) {
        return ESP_OK;
    }
    s_config = *config;
    esp_err_t err = ESP_OK;
    if (board_i2c_get_handle() == NULL) {
        err = board_i2c_init();
        if (err != ESP_OK) {
            return err;
        }
    }
    err = board_i2c_probe(BOARD_I2C_ADDR_ES8311);
    if (err == ESP_OK) {
        err = board_i2c_probe(BOARD_I2C_ADDR_ES7210);
    }
    if (err != ESP_OK) {
        return err;
    }

    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channel.auto_clear = true;
    /* The DMA queue absorbs brief SD/scheduling jitter, not arbitrarily long blocking operations. */
    channel.dma_desc_num = 8;
    err = i2s_new_channel(&channel, &s_tx, &s_rx);
    if (err != ESP_OK) {
        goto failed;
    }
    /* The initial 4-slot/32-bit format establishes clocks for board codecs; it is not the application's WAV format.
      * esp_codec_dev_open then selects stereo 16-bit at the requested sample rate. */
    const i2s_tdm_slot_mask_t slots =
        I2S_TDM_SLOT0 | I2S_TDM_SLOT1 | I2S_TDM_SLOT2 | I2S_TDM_SLOT3;
    i2s_tdm_config_t tdm = {
        .clk_cfg = I2S_TDM_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_TDM_PHILIPS_SLOT_DEFAULT_CONFIG(32, I2S_SLOT_MODE_STEREO, slots),
        .gpio_cfg = {
            .mclk = BOARD_AUDIO_MCLK_GPIO,
            .bclk = BOARD_AUDIO_BCLK_GPIO,
            .ws = BOARD_AUDIO_WS_GPIO,
            .dout = BOARD_AUDIO_DOUT_GPIO,
            .din = BOARD_AUDIO_DIN_GPIO,
        },
    };
    tdm.slot_cfg.total_slot = 4;
    err = i2s_channel_init_tdm_mode(s_tx, &tdm);
    if (err == ESP_OK) {
        err = i2s_channel_init_tdm_mode(s_rx, &tdm);
    }
    if (err == ESP_OK) {
        err = start_i2s();
    }
    if (err != ESP_OK) {
        goto failed;
    }

    audio_codec_i2s_cfg_t data_config = {
        .port = I2S_NUM_0, .tx_handle = s_tx, .rx_handle = s_rx,
    };
    s_data = audio_codec_new_i2s_data(&data_config);
    s_gpio = audio_codec_new_gpio();
    /* Codec control uses 8-bit address macros (0x30/0x80); board_i2c probing uses 7-bit addresses (0x18/0x40). */
    audio_codec_i2c_cfg_t i2c = {
        .port = BOARD_I2C_PORT,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = board_i2c_get_handle(),
    };
    s_out_ctrl = audio_codec_new_i2c_ctrl(&i2c);
    i2c.addr = ES7210_CODEC_DEFAULT_ADDR;
    s_in_ctrl = audio_codec_new_i2c_ctrl(&i2c);
    if (s_data == NULL || s_gpio == NULL || s_out_ctrl == NULL || s_in_ctrl == NULL) {
        err = ESP_ERR_NO_MEM;
        goto failed;
    }
    es8311_codec_cfg_t speaker_config = {
        .ctrl_if = s_out_ctrl, .gpio_if = s_gpio,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = BOARD_AUDIO_PA_GPIO, .use_mclk = true,
        .hw_gain.pa_gain = 6,
    };
    es7210_codec_cfg_t mic_config = {
        .ctrl_if = s_in_ctrl,
        .mic_selected = ES7120_SEL_MIC1 | ES7120_SEL_MIC2 |
                        ES7120_SEL_MIC3 | ES7120_SEL_MIC4,
    };
    s_out_codec = es8311_codec_new(&speaker_config);
    s_in_codec = es7210_codec_new(&mic_config);
    if (s_out_codec == NULL || s_in_codec == NULL) {
        err = ESP_FAIL;
        goto failed;
    }
    esp_codec_dev_cfg_t device = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT, .codec_if = s_out_codec, .data_if = s_data,
    };
    s_speaker = esp_codec_dev_new(&device);
    device.dev_type = ESP_CODEC_DEV_TYPE_IN;
    device.codec_if = s_in_codec;
    s_mic = esp_codec_dev_new(&device);
    if (s_speaker == NULL || s_mic == NULL) {
        err = ESP_ERR_NO_MEM;
        goto failed;
    }
    /* Codec construction needs MCLK; sessions are not open yet, so stop both channels afterward.
      * open_device later restores both channels before codec configuration to maintain the same initial conditions. */
    err = stop_i2s();
    if (err != ESP_OK) {
        goto failed;
    }
    ESP_LOGI(TAG, "ES8311 + ES7210 ready, %lu Hz / stereo / 16-bit",
             (unsigned long)s_config.sample_rate);
    return ESP_OK;

failed:
    ESP_LOGE(TAG, "init failed: %s", esp_err_to_name(err));
    release_resources();
    return err;
}

bool board_audio_is_ready(void)
{
    return s_speaker != NULL && s_mic != NULL;
}

void board_audio_deinit(void)
{
    release_resources();
}

bool board_audio_is_open(void)
{
    return s_speaker_open || s_mic_open;
}

esp_err_t board_audio_close(void)
{
    esp_err_t err = ESP_OK;
    if (s_speaker_open) {
        err = esp_codec_dev_close(s_speaker);
        if (err == ESP_OK) {
            s_speaker_open = false;
            /* The codec has already stopped OUT through data_if; mark TX stopped to avoid disabling it again. */
            s_tx_running = false;
        }
    }
    if (s_mic_open) {
        const esp_err_t mic_err = esp_codec_dev_close(s_mic);
        if (mic_err == ESP_OK) {
            s_mic_open = false;
            /* IN is stopped, so mark RX stopped; the TX shared clock may still run and is stopped by stop_i2s below. */
            s_rx_running = false;
        }
        if (err == ESP_OK) {
            err = mic_err;
        }
    }
    if (!s_speaker_open && !s_mic_open) {
        const esp_err_t stop_err = stop_i2s();
        if (err == ESP_OK) {
            err = stop_err;
        }
    }
    return err;
}

static esp_err_t open_device(bool speaker)
{
    if (!board_audio_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (speaker ? s_speaker_open : s_mic_open) {
        return ESP_OK;
    }
    esp_err_t err = board_audio_close();
    if (err != ESP_OK) {
        return err;
    }
    /* esp_codec_dev 1.3.5 set_fmt directly disables the selected direction; recording configuration also pauses TX.
      * After each idle resume or recording/playback switch, restore TX and RX before letting the codec configure the format.
      * This is not simultaneous recording/playback: it avoids disabling an already-stopped channel. Preserve this order. */
    err = start_i2s();
    if (err != ESP_OK) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(stop_i2s());
        return err;
    }
    esp_codec_dev_sample_info_t format = {
        .sample_rate = s_config.sample_rate, .channel = 2, .bits_per_sample = 16,
    };
    const esp_codec_dev_handle_t device = speaker ? s_speaker : s_mic;
    /* The codec marks itself open before configuration, so track that state here too. Even if format setup/enable fails,
      * use the common close + stop cleanup path to avoid leaving a half-open state. */
    s_speaker_open = speaker;
    s_mic_open = !speaker;
    err = esp_codec_dev_open(device, &format);
    if (err != ESP_OK) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(board_audio_close());
        return err;
    }
    if (speaker) {
        err = esp_codec_dev_set_out_vol(device, s_config.speaker_volume);
    } else {
        err = esp_codec_dev_set_in_gain(device, s_config.mic_gain_db);
    }
    if (err != ESP_OK) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(board_audio_close());
    }
    return err;
}

esp_err_t board_audio_open_speaker(void)
{
    return open_device(true);
}

esp_err_t board_audio_open_mic(void)
{
    return open_device(false);
}

esp_err_t board_audio_read(void *pcm, size_t bytes)
{
    if (!s_mic_open) {
        return ESP_ERR_INVALID_STATE;
    }
    if (pcm == NULL || bytes == 0 || bytes > INT_MAX || bytes % 4 != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    return esp_codec_dev_read(s_mic, pcm, (int)bytes);
}

esp_err_t board_audio_write(void *pcm, size_t bytes)
{
    if (!s_speaker_open) {
        return ESP_ERR_INVALID_STATE;
    }
    if (pcm == NULL || bytes == 0 || bytes > INT_MAX || bytes % 4 != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    return esp_codec_dev_write(s_speaker, pcm, (int)bytes);
}
