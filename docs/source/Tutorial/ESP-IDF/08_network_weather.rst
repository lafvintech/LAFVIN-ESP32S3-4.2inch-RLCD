.. _idf-network-weather:

8. Online Weather Dashboard
===========================

Overview
--------

This lesson connects to Wi-Fi, retrieves Open-Meteo weather data, and attempts to set the RTC from network time.
The screen combines city weather, the date and time, and indoor temperature and humidity from the onboard SHTC3.
Indoor readings come from the sensor, while city weather comes from the network, so they may differ.

Network requests run in a separate task while the application continues to handle local readings, buttons, and display updates.

Setup
-----

Follow :ref:`esp-idf-tutorial` to prepare the environment and the complete example code,
then open the ``examples/esp-idf/08_network_weather`` project.
This example uses the network and JSON components included with ESP-IDF; no additional third-party components are required.
The project enables the HTTPS certificate bundle by default to verify the weather server's certificate.

Network and City
^^^^^^^^^^^^^^^^

Open ``examples/esp-idf/08_network_weather/main/app_config.h`` and enter the name and password
of a **2.4 GHz Wi-Fi** network with Internet access:

.. code-block:: c

   #define APP_WIFI_SSID       ""
   #define APP_WIFI_PASSWORD   ""

Replace the empty strings with your network details, keeping the double quotes.
The current implementation requires WPA2 or stronger security and does not connect to open hotspots without a password.
No weather API key is needed. Remove real Wi-Fi passwords before sharing configuration files or screenshots.

The default city is Shanghai. You can change it in the same file:

.. code-block:: c

   #define APP_WEATHER_CITY        "Shanghai"
   #define APP_WEATHER_LATITUDE    31.2304f
   #define APP_WEATHER_LONGITUDE   121.4737f

The city name is used only for display; use English or pinyin. Latitude and longitude determine the weather location.
Enter coordinates as numbers without double quotes. Also check ``APP_TIME_ZONE``: its default value, ``CST-8``, means UTC+8.
``APP_NTP_SERVER`` defaults to ``pool.ntp.org``.
After making changes, follow :ref:`idf-build-flash` to rebuild, flash, and view the serial output.

Operation and Controls
^^^^^^^^^^^^^^^^^^^^^^

* Weather is requested automatically after connecting, then every fifteen minutes by default.
* Indoor temperature and humidity are sampled every thirty seconds. The RTC is read every second, and the page clock shows hours and minutes.
* Press the KEY button to request weather while online. If offline and the Wi-Fi service has started successfully, it triggers a reconnection attempt without waiting for the normal retry interval.
* After a failed Wi-Fi connection or a disconnection, the default retry delay is 60 seconds.
* Pressing the KEY button while a weather request is running does not start another request.

If the network name is empty, the page shows ``set Wi-Fi in app_config.h``. Fill in the settings and flash again.
If a weather request fails, an error is shown and the last successful data is retained; that data may be out of date.

Code Walkthrough
----------------

Handling Network Waits in a Separate Task
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

In ``examples/esp-idf/08_network_weather/main/app_main.c``,
the application notifies the network task through ``request_weather()``.
That task waits for the network, attempts time synchronization, then retrieves the weather:

.. code-block:: c

   event.ntp_synced = ensure_time_synchronized();
   event.result = weather_client_fetch(
       &request_config, &event.weather);

The network task posts the result to the event queue, and the application updates its data and page after receiving it.
The network task does not access the RTC or draw the interface, so the application can still handle the clock,
indoor readings, and button events while it waits for a connection, time synchronization, or a weather response.

Retrieving and Checking Weather Data
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

``examples/esp-idf/components/weather_client/weather_client.c`` requests the weather over HTTPS
and passes the complete response to cJSON for parsing:

.. code-block:: c

   cJSON *document = cJSON_ParseWithLength(json, length);

The program reads the current weather and today's forecast. It replaces the stored weather only after the HTTP request,
JSON structure, and required temperature data pass their checks. Other missing fields are shown as placeholders.
The update time at the bottom is the local time when the request succeeded, rather than the weather observation time.

Time Synchronization and Display Updates
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

If the RTC time is valid at startup, the program uses it to initialize system time before attempting SNTP network synchronization.
Once network time synchronization is confirmed, later requests reuse that confirmed state.
When the application receives a successful synchronization result, it attempts to write system time back to the RTC;
a write failure is logged in the serial console.
Time synchronization and weather retrieval are checked separately, so the RTC may still be updated when a weather request fails.

The one-second event reads the RTC and checks network status. The clock display refreshes when the minute changes.
Indoor readings, network state changes, and weather results also trigger display updates.
The fifteen-minute interval controls when the program requests weather; the weather service determines its own data update frequency.

Example Output
--------------

The top of the screen shows the city, connection status, and date. The center shows the time and weather,
with wind speed, humidity, pressure, precipitation, and other information below. Indoor temperature and humidity appear below the clock.
Placeholders are shown until valid data is first obtained. Weather and sensor readings depend on the actual results.

.. figure:: ../img/idf/08_phen.png
   :alt: Online weather and indoor temperature and humidity display
   :width: 800px
   :align: center

   Online weather and indoor temperature and humidity display.

References
----------

* `Open-Meteo Weather API Documentation <https://open-meteo.com/en/docs>`_
* `ESP-IDF 5.4: ESP HTTP Client <https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/protocols/esp_http_client.html>`_
* :ref:`idf-rtc`
* :ref:`idf-shtc3`
