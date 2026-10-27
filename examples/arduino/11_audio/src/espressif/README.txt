Vendored third-party components (unmodified copies)
====================================================

esp_codec_dev  v1.3.5
  - Source: Espressif official component (from esp-adf, also published on
    the ESP Component Registry as espressif/esp_codec_dev)
  - License: Apache-2.0 (see esp_codec_dev/LICENSE)

codec_board   v0.5.5
  - Source: Espressif official component (espressif/esp-codec-board)
  - License: Espressif Modified MIT (see codec_board/LICENSE)
  - Board profile used: "S3_RLCD_4_2" (I2S mclk=16 bclk=9 ws=45 din=10
    dout=8; ES8311 output with PA on GPIO46; ES7210 input)

These third-party components are vendored here so each example folder
stays self-contained. Each component retains its own license and copyright
notices; see the LICENSE file in that component's directory. They are NOT
course-authored code.
