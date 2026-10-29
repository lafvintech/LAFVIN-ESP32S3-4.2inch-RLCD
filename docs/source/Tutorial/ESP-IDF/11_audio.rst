.. _idf-audio:

11. MicroSD Audio Recording and Playback
========================================

Overview
--------

This lesson combines audio, a MicroSD card, and an LVGL interface into a voice recorder.
Hold the BOOT button to record, release it to save a WAV file, and briefly press the KEY button to play or stop playback.
The interface shows the current state, filename, recording signal level, and playback progress.

The default recording format is 16 kHz, 16-bit, mono PCM, which stores sound as uncompressed digital data.
Files are saved in the card's /recordings directory with numbered names such as REC0001.WAV.

Setup
-----

No code changes are needed when using the default settings.

#. Follow :ref:`esp-idf-tutorial` to download the complete code and open the ``examples/esp-idf/11_audio`` project.
#. With power disconnected, insert a **FAT32 MicroSD card** and check that the speaker is correctly connected.
#. Check :ref:`idf-board-settings`, then follow :ref:`idf-build-flash` to build, flash, and view the serial output.

main/idf_component.yml pins **LVGL 9.3.0** and **esp_codec_dev 1.3.5**.
The first build requires Internet access to download them. Keep the shared components, default configuration,
and the project's partitions.csv file. You do not need to copy the Arduino lv_conf.h file or src/espressif directory.
This lesson also uses a 4 MB application partition, with a Flash size of 16 MB.

To adjust the settings, edit ``examples/esp-idf/11_audio/main/app_config.h``, then rebuild and flash:

* ``APP_SPEAKER_VOLUME``: Speaker volume, range 0–100; default 100.
* ``APP_MIC_GAIN_DB``: Microphone gain; default 35 dB.
* ``APP_RECORD_HOLD_MS``: Hold duration required to start recording; default 700 ms.
* ``APP_AUDIO_SAMPLE_RATE_HZ``: Sample rate; default 16000.
  Playback requires the WAV sample rate to match the current setting. Make a new recording after changing it.

Controls and Expected Results
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

After successful initialization, two short tones play. You can then record and play audio:

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Action
     - Function
   * - Hold the BOOT button
     - Recording starts after about 0.7 seconds; keep holding the button while speaking
   * - Release the BOOT button
     - Stop recording and save the WAV file
   * - Briefly press the KEY button while idle
     - Play the valid recording with the latest number
   * - Briefly press the KEY button during playback
     - Stop playback early
   * - Briefly press the KEY button in the ERROR state
     - Detect the MicroSD card again and initialize the audio device

A brief press of the BOOT button does not record. Holding the BOOT button during playback stops playback and starts a new recording.
If no recordings exist, the KEY button prompts you to record first. During recording, the KEY button does not start playback.

.. warning::
   After releasing the BOOT button, wait for ``record SAVED`` in the serial console or a WAV-saved message on the screen before disconnecting power and removing the card.
   Power loss during recording may leave the current file incomplete.
   This example does not automatically detect card insertion or removal while running. Disconnect power before inserting or removing the card.

Code Walkthrough
----------------

Processing Audio in Blocks
^^^^^^^^^^^^^^^^^^^^^^^^^^

``examples/esp-idf/11_audio/main/app_main.c`` creates a separate audio task
to manage recording, playback, the audio device, and WAV files.
The button service only queues actions. The audio task processes the buttons, then reads or outputs a block of audio.

During recording, the ES7210 provides left and right channels over I2S. The program averages them into a mono signal:

.. code-block:: c

   const int32_t mixed = ((int32_t)s_input_stereo[i * 2] +
                          s_input_stereo[i * 2 + 1]) / 2;
   s_record_mono[i] = (int16_t)mixed;

Recording and playback read and write small blocks instead of loading the entire recording into memory.
During playback, mono data is copied to both left and right channels and output through the ES8311.
MicroSD card and I2S operations still take time in the audio task, so button responses may need to wait for the current block to finish.

Saving Separate Recording Files
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

``examples/esp-idf/components/wav_store/wav_store.c`` uses file paths prefixed with /sdcard,
such as /sdcard/recordings/REC0001.WAV. The directory on the card is still /recordings.
Files are created with:

.. code-block:: c

   fd = open(s_current_path, O_CREAT | O_EXCL | O_RDWR, 0664);

``O_EXCL`` requires the file not to exist; if the name is already in use, the program tries the next number.
After recording stops, it fills in the WAV header with the format and length, then marks the file as the latest recording only after closing it successfully.
If a read, write, or save error occurs, it attempts to delete the incomplete recording from that session.
This example plays its own mono, 16-bit PCM WAV files with a fixed 44-byte header.

Passing UI Updates to the Display Task
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

During recording and playback, the audio task submits UI data every 500 ms by default, and immediately when the state changes.
``examples/esp-idf/11_audio/main/audio_ui.c`` retains only the latest update:

.. code-block:: c

   return xQueueOverwrite(s_updates, data) == pdPASS ? ESP_OK : ESP_FAIL;

An LVGL timer checks the queue at 100 ms intervals. When new data arrives, the display task updates the widgets and draws the interface.
Submitting data does not immediately update the screen, and the audio task does not wait for drawing or display transfer to finish.
The MIC bar indicates recording signal strength; it is not a decibel measurement of environmental noise.
Playback progress is calculated from the amount of data read from the MicroSD card, rather than the exact position of the sound leaving the speaker.

Example Output
--------------

.. figure:: ../img/idf/11_phen.png
   :alt: ESP-IDF voice recorder in the idle state
   :width: 800px
   :align: center

   The recorder's IDLE screen shows the latest recording file and control instructions.

References
----------

* :ref:`idf-sd-card`
* :ref:`idf-lvgl-ui`
