.. _online_flasher:

Online Flasher
======================

The online flasher tool allows you to flash firmware to your ESP32 device directly from the browser, without installing any development environment.

Prerequisites
---------------------

- Chrome or Edge browser installed (Web Serial API support required)
- ESP32 device connected to your computer with a USB Type-C data cable
- Serial port device recognized in Device Manager

.. note::
   If the device is not found in Device Manager, try using a different USB port.

.. image:: img/olf1.png

Flashing Steps
---------------------

#. Open the online flasher: `LAFVIN Web Flasher <https://lafvintech.github.io/Lafvin_Web_Flasher/>`_

   .. image:: img/olf2.png

#. Select your kit or device model, for example, ``ESP32S3 RLCD``.

   .. image:: img/olf3.png

#. Select the firmware to flash. For this kit, select ``xiaozhi_rlcd``.

   .. image:: img/olf4.png

#. Select the firmware version. This guide uses ``v1.0``.

   .. image:: img/olf5.png

#. Click ``Connect``. A serial port selection window opens in the upper-left corner of the browser. Grant permission if prompted, select your device, and click ``Connect``. Once connected, the button changes to ``Disconnect``.

   .. image:: img/olf6.png

   .. image:: img/olf7.png

#. Select the ``Flash Baud Rate`` and whether to enable ``Erase Flash`` (recommended for first-time flashing). Confirm the settings and click ``Flash``. The tool erases flash if selected, then writes the selected firmware.

   .. image:: img/olf8.png

.. note::
   Erasing flash may take a while. Wait for it to finish.
