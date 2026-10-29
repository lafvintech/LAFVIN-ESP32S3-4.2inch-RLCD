.. _arduino-local-dashboard:

7. Local Dashboard
==================

Overview
--------

This lesson combines the RTC, SHTC3, battery sensing, and buttons into a two-page
dashboard. The overview page shows the clock, temperature, humidity, and battery
information; the details page shows more data and drawing times. At startup,
the program also compares two drawing methods: calculating each pixel location
as needed, or calculating the locations in advance and looking them up in tables.

Setup
-----

No code parameters need changing, and no network connection or third-party libraries are required.
Complete :ref:`arduino-board-settings` and make sure OPI PSRAM is enabled.
Download the code as described in :ref:`download-code`, then open
``examples/arduino/07_local_dashboard/07_local_dashboard.ino``, compile it, and upload it.
To display an actual battery voltage, correctly connect a compatible battery.

Running and Using the Example
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Open the Serial Monitor at **115200** baud. The screen first shows the overview page.
Use the buttons as follows:

.. list-table::
   :header-rows: 1
   :widths: 25 75

   * - Action
     - Function
   * - Press the KEY button
     - Switch between the overview and details pages
   * - Press the BOOT button
     - Redraw the current page using the stored readings, without reading the sensors again

If you see ``RTC TIME UNRELIABLE``, send the current date and time over serial
with a newline, using the format in :ref:`arduino-rtc`. This example also supports
setting the RTC over serial. Failed temperature and humidity readings appear as
placeholders; battery percentage is estimated roughly from voltage.

The serial output reports the startup timing comparison and statistics for each
refresh in the following format. The ellipses stand for the measured times in milliseconds:

.. code-block:: text

   benchmark full-frame: direct=... ms, LUT=... ms
   refresh: draw=... ms flush=... ms

On the details page, last draw and last flush show statistics from the previous refresh.
The flush value measures the time spent submitting the SPI transfer, not the time
needed to finish displaying the image.

Code Walkthrough
----------------

Scheduling Separate Read Intervals
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The main program reads data at different intervals. This section reads the time
and battery information once per second:

.. code-block:: cpp

   if (now - g_lastFastUpdate >= 1000) {
     g_lastFastUpdate = now;
     readFastInputs();
     if (!g_page2) needRedraw = true;
   }

Time and battery information are read every second; temperature and humidity are
measured every ten seconds. Readings are stored in variables and used directly
when drawing the page, avoiding extra sensor reads. These operations run in
sequence within the same main loop, rather than simultaneously.

Refreshing Only When Needed
^^^^^^^^^^^^^^^^^^^^^^^^^^^

Page changes, the BOOT button, time setting, and updated readings set ``needRedraw``.
The main loop draws the page and submits a refresh only when needed. The details
page does not refresh with each one-second time and battery read, but it does
refresh after temperature and humidity updates or button actions.

Precalculating Pixel Locations
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

At startup, ``examples/arduino/07_local_dashboard/display_bsp.cpp`` calculates
the byte and bit used to store each pixel and saves them in two tables.
Drawing code then looks up the values directly:

.. code-block:: cpp

   size_t slot = (size_t)y * width_ + x;
   uint16_t byteIndex = lutByteIndex_[slot];
   uint8_t mask = lutBitMask_[slot];

The two tables use about 360000 bytes of PSRAM (the board's additional memory)
to reduce repeated calculations. LUT in the serial output refers to this lookup-table
method. The startup comparison measures only drawing in memory, excluding SPI
transfers. Check the serial output to see whether the lookup method is faster.

Example Output
--------------

.. figure:: ../img/07_phen.png
   :alt: Overview page of the local dashboard
   :width: 800px
   :align: center

   Overview page of the local dashboard.

References
----------

* :ref:`arduino-rtc`
* :ref:`arduino-shtc3`
* :ref:`arduino-adc-battery`
