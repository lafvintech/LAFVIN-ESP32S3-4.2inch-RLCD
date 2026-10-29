.. _arduino-sd-card:

9. Reading and Writing a MicroSD Card
=====================================

Overview
--------

This lesson uses the onboard MicroSD card slot to read card information, create
a directory, write and append text, read the file back to check its contents,
and list files in the directory. Test results appear on the RLCD and over serial.
Press the KEY button to run the test again.

Setup
-----

No code parameters need changing, and no third-party libraries are required.
Prepare a MicroSD card formatted as **FAT32**. With power disconnected, insert it
into the onboard slot, then power on the board. This example does not demonstrate hot swapping.

Complete :ref:`arduino-board-settings` and download the code as described in
:ref:`download-code`. Open ``examples/arduino/09_sd_card/09_sd_card.ino``,
compile it, and upload it. Set the Serial Monitor to **115200** baud.

.. warning::
   Each test overwrites the previous contents of ``/demo09/hello.txt`` on the card,
   then appends fixed text. The test does not format the card or modify other directories.

Expected Results
^^^^^^^^^^^^^^^^

The test runs automatically once at power-on. The screen lists Mount, Directory,
Write, Append, Read, and List in order, and shows PASS when all steps succeed.
The KEY button reruns the same test. The serial output reports the card type,
capacity, bytes read and written, and the file list.

After a successful test, the file should contain:

.. code-block:: text

   Hello from ESP32-S3 RLCD!
   Append test passed.

The file remains on the card after the test. Disconnect power before removing
the card to view it on a computer. If Mount fails, disconnect power, check that
the card is inserted and formatted as FAT32, then power on again.

Code Walkthrough
----------------

Detecting and Accessing the Card
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The onboard slot uses SDMMC 1-bit mode with three signal lines: CLK, CMD, and D0.
The main program sets the pins, then calls ``SD_MMC.begin()`` to make the card's files accessible:

.. code-block:: cpp

   if (!SD_MMC.begin("/sdcard", true, false)) {
     Serial.println(F("[sd] mount failed; insert a FAT32 card"));
     SD_MMC.end();
     return false;
   }

The second argument, ``true``, selects 1-bit mode. The third argument, ``false``,
disables automatic formatting if initialization fails. ``/sdcard`` is the system
mount point. When using the Arduino file interface, specify only the path within
the card, such as ``/demo09/hello.txt``.

Writing and Appending
^^^^^^^^^^^^^^^^^^^^^

Overwriting and appending use different file-opening modes:

.. code-block:: cpp

   File file = SD_MMC.open(path, FILE_WRITE);

``FILE_WRITE`` rewrites the file, while ``FILE_APPEND`` adds data at the end.
After writing, the program closes the file and checks that the number of bytes
written matches the expected count.

Reading Back and Checking the Contents
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The program compares the text read back with the expected contents:

.. code-block:: cpp

   result.readOk = strcmp(readBack, kExpectedText) == 0;

The Read check passes only if the file is read completely and its contents match.
Repeated tests still produce the same two lines because each run overwrites first,
then appends.

Example Output
--------------

.. figure:: ../img/09_phen.png
   :alt: Display after a successful MicroSD card read and write test
   :width: 800px
   :align: center

   Display after a successful MicroSD card read and write test.

References
----------

* The official **File → Examples → SD_MMC → SDMMC_Test** example in your installed ESP32 board package.
