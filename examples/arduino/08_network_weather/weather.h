#pragma once

#include <stdint.h>
#include "board.h"

// ============================================================
// weather.h -- weather data, page data and drawing interfaces
//
// Example request:
//   https://api.open-meteo.com/v1/forecast
//       ?latitude=31.23&longitude=121.47
//       &current=temperature_2m,relative_humidity_2m,apparent_temperature,
//               weather_code,wind_speed_10m,wind_direction_10m,
//               surface_pressure,precipitation
//       &daily=temperature_2m_max,temperature_2m_min,
//              precipitation_probability_max,uv_index_max,sunrise,sunset
//       &timezone=auto&forecast_days=1
//
// Errors go in the error field; the main program shows them at the page bottom and prints them to Serial.
// ============================================================

struct WeatherData {
  float temperatureC;     // Current temperature.
  float apparentC;        // Apparent temperature.
  float humidityPct;      // Relative humidity.
  float windSpeedKmh;     // Wind speed, km/h.
  int windDirDeg;         // Wind direction in degrees, clockwise from north=0.
  float pressureHpa;      // Surface pressure, hPa.
  float precipitationMm;  // Current precipitation, mm.
  int weatherCode;        // WMO weather code.
  float tempMaxC;         // Today's high/low temperature.
  float tempMinC;
  int rainChancePct;      // Today's maximum precipitation probability (%).
  float uvIndexMax;       // Today's maximum UV index.
  char sunrise[6];        // Today's sunrise, "HH:MM".
  char sunset[6];         // Today's sunset, "HH:MM".
  char fetchedAt[20];     // Local time of a successful fetch, "HH:MM", not observation time; "--:--" if system time is unavailable.
  char error[80];         // Request failure reason.
};

// The page only needs these data fields, without timers or retry state.
struct DashboardData {
  RtcDateTime rtc = {};
  bool rtcReady = false;
  Shtc3Sample indoor = {};
  bool indoorReady = false;
  WeatherData weather = {};
  bool weatherReady = false;
  bool wifiConnected = false;
  char message[80] = "starting...";
};

// Perform and parse one synchronous HTTPS request; failures go in out->error, with no internal retry.
// out is cleared first. Use a temporary result and replace the page cache only on success to preserve old data on failure.
bool fetchWeather(float lat, float lon, WeatherData *out);

// Unlike basic drawing primitives, these page functions clear, draw and flush the buffer directly.
// Do not unconditionally flush again after return or draw concurrently from other tasks.
void drawStatusPage(RlcdPanel &panel, const char *title, const char *detail);
void drawWeatherPage(RlcdPanel &panel, const DashboardData &data,
                     const char *city);

// WMO weather code -> short English description (the display font supports ASCII only).
const char *weatherCodeText(int code);

// Wind direction in degrees -> eight compass abbreviations ("N"/"NE"/...).
const char *windDirText(int deg);
