.. _idf-hello-rlcd:

4. Drawing Graphics and Text
============================

Overview
--------

This lesson uses the display driver to draw lines, rectangles, circles, and text,
then combines them into a static ``PIXEL STUDIO`` interface.
The page includes geometric shapes, text at different sizes, white-on-black labels, and a progress bar.

This example does not require LVGL or read the clock, sensors, or network.
The progress bar shows a fixed value from the program, rather than battery charge or the progress of a real task.

Setup
-----

No code settings need to be changed, and no additional third-party components are required.
Complete the preparation in :ref:`esp-idf-tutorial` and open the ``examples/esp-idf/04_hello_rlcd`` project directory.
Check :ref:`idf-board-settings`, then follow :ref:`idf-build-flash` to build, flash, and view the serial output.

This example uses a fixed 400 × 300 landscape layout.
If you change the display orientation, update the graphics and text coordinates as well.

Code Walkthrough
----------------

Combining Graphics into an Interface
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

``examples/esp-idf/components/mono_graphics/mono_graphics.c`` provides functions for drawing lines,
rectangles, circles, and text. They all use the display driver to change pixels.
``examples/esp-idf/04_hello_rlcd/main/app_main.c`` clears the background, then draws each area:

.. code-block:: c

   board_display_clear(BOARD_DISPLAY_WHITE);
   mono_gfx_draw_rect(8, 8, 384, 284, BOARD_DISPLAY_BLACK);
   draw_header();
   draw_geometry_card();
   draw_typography_card();
   draw_footer();

These functions draw the image in memory. Once the complete page is ready,
the main program calls ``board_display_flush()`` once to send it to the display.

White Text on a Black Background
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The text function draws only the character pixels. To create white text on a black background, draw the background first:

.. code-block:: c

   mono_gfx_fill_rect(12, 12, 376, 36, BOARD_DISPLAY_BLACK);
   mono_gfx_draw_text(
       24, 26, "04 PIXEL STUDIO", BOARD_DISPLAY_WHITE, 2);

The last argument of ``mono_gfx_draw_text()`` sets the text scale.
Each character is an 8 × 8 pixel pattern. At a scale of 2, each pixel becomes a 2 × 2 block.
This ASCII font supports English characters, numbers, and common symbols, but does not include Chinese characters.

Progress Bar and Drawing Time
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The program uses the fixed value ``PROGRESS_VALUE = 72`` to calculate the fill width.
``draw_progress_bar()`` draws a rectangular border, then fills the inside according to the percentage.

In the serial output, ``draw page`` measures the time spent drawing the page in memory.
``flush`` measures the display refresh call, including the wait for SPI transfer completion.
These timings describe software drawing and data transfer; they do not identify the exact moment when the LCD finishes its visible change.

Example Output
--------------

The screen shows a white-on-black title at the top, geometric shapes made from lines and circles on the left,
and text at scales of 1, 2, and 3 with labels on the right. The progress bar at the bottom shows 72%.
The program draws the page once; the progress bar does not advance automatically.

.. figure:: ../img/idf/04_phen.png
   :alt: ESP-IDF display showing graphics, text, and a progress bar
   :width: 800px
   :align: center

   The PIXEL STUDIO page combines graphics, text, and a fixed progress bar.

References
----------

* :ref:`idf-rlcd-driver`
