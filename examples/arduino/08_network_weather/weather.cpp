#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <math.h>
#include <string.h>
#include "weather.h"

namespace {
void resetWeather(WeatherData *data) {
  // Use NaN for missing floats and -1 for integers; missing fields do not mean actual zero temperature/precipitation.
  *data = {};
  data->temperatureC = NAN;
  data->apparentC = NAN;
  data->humidityPct = NAN;
  data->windSpeedKmh = NAN;
  data->windDirDeg = -1;
  data->pressureHpa = NAN;
  data->precipitationMm = NAN;
  data->weatherCode = -1;
  data->tempMaxC = NAN;
  data->tempMinC = NAN;
  data->rainChancePct = -1;
  data->uvIndexMax = NAN;
  snprintf(data->sunrise, sizeof(data->sunrise), "--:--");
  snprintf(data->sunset, sizeof(data->sunset), "--:--");
  snprintf(data->fetchedAt, sizeof(data->fetchedAt), "--:--");
}

void extractTime(const char *isoTime, char output[6]) {
  snprintf(output, 6, "--:--");
  if (!isoTime) return;
  const char *timePart = strchr(isoTime, 'T');
  if (timePart && strlen(timePart) >= 6) {
    snprintf(output, 6, "%.5s", timePart + 1);
  }
}

void drawCentered(RlcdPanel &panel, int16_t centerX, int16_t y,
                  const char *text, uint8_t scale) {
  drawString(panel, centerX - textWidth(text, scale) / 2, y,
             text, LCD_PIXEL_BLACK, scale);
}

void drawTodayCell(RlcdPanel &panel, int index,
                   const char *label, const char *value) {
  const int16_t centerX = 50 + index * 100;
  drawCentered(panel, centerX, 208, label, 1);
  drawCentered(panel, centerX, 228, value, 2);
}
}  // namespace

bool fetchWeather(float latitude, float longitude, WeatherData *out) {
  if (!out) return false;
  resetWeather(out);

  String url = "https://api.open-meteo.com/v1/forecast?latitude=";
  url += String(latitude, 4);
  url += "&longitude=";
  url += String(longitude, 4);
  url += "&current=temperature_2m,relative_humidity_2m,apparent_temperature,";
  url += "weather_code,wind_speed_10m,wind_direction_10m,";
  url += "surface_pressure,precipitation";
  url += "&daily=temperature_2m_max,temperature_2m_min,";
  url += "precipitation_probability_max,uv_index_max,sunrise,sunset";
  url += "&timezone=auto&forecast_days=1";

  // This example uses HTTPClient directly: download text, then parse it with ArduinoJson.
  HTTPClient http;
  http.setConnectTimeout(10000);
  http.setTimeout(15000);
  if (!http.begin(url)) {
    snprintf(out->error, sizeof(out->error), "request start failed");
    return false;
  }

  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    snprintf(out->error, sizeof(out->error), "HTTP %d", code);
    http.end();
    return false;
  }

  // Download fully before closing HTTP; payload and the JSON document briefly occupy memory together.
  const String payload = http.getString();
  http.end();

  JsonDocument document;
  const DeserializationError jsonError = deserializeJson(document, payload);
  if (jsonError) {
    snprintf(out->error, sizeof(out->error), "JSON: %s", jsonError.c_str());
    return false;
  }

  JsonObject current = document["current"];
  JsonObject daily = document["daily"];
  if (current.isNull() || daily.isNull()) {
    snprintf(out->error, sizeof(out->error), "weather data missing");
    return false;
  }

  out->temperatureC = current["temperature_2m"] | NAN;
  out->humidityPct = current["relative_humidity_2m"] | NAN;
  out->apparentC = current["apparent_temperature"] | NAN;
  out->weatherCode = current["weather_code"] | -1;
  out->windSpeedKmh = current["wind_speed_10m"] | NAN;
  out->windDirDeg = current["wind_direction_10m"] | -1;
  out->pressureHpa = current["surface_pressure"] | NAN;
  out->precipitationMm = current["precipitation"] | NAN;

  // forecast_days=1, so read only today's item at index 0 in the daily arrays.
  out->tempMaxC = daily["temperature_2m_max"][0] | NAN;
  out->tempMinC = daily["temperature_2m_min"][0] | NAN;
  out->rainChancePct = daily["precipitation_probability_max"][0] | -1;
  out->uvIndexMax = daily["uv_index_max"][0] | NAN;
  extractTime(daily["sunrise"][0].as<const char *>(), out->sunrise);
  extractTime(daily["sunset"][0].as<const char *>(), out->sunset);

  // Current temperature is required; other missing fields retain placeholders and are shown as --.
  if (!isfinite(out->temperatureC)) {
    snprintf(out->error, sizeof(out->error), "temperature missing");
    return false;
  }

  struct tm now = {};
  if (getLocalTime(&now, 10)) {
    strftime(out->fetchedAt, sizeof(out->fetchedAt), "%H:%M", &now);
  }
  out->error[0] = '\0';
  return true;
}

const char *weatherCodeText(int code) {
  if (code == 0) return "Clear";
  if (code >= 1 && code <= 2) return "Part cloudy";
  if (code == 3) return "Overcast";
  if (code == 45 || code == 48) return "Fog";
  if (code >= 51 && code <= 57) return "Drizzle";
  if ((code >= 61 && code <= 67) || (code >= 80 && code <= 82)) return "Rain";
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) return "Snow";
  if (code >= 95) return "Thunderstorm";
  return "Unknown";
}

const char *windDirText(int degrees) {
  static const char *directions[8] = {"N", "NE", "E", "SE",
                                      "S", "SW", "W", "NW"};
  if (degrees < 0) return "--";
  return directions[((degrees + 22) / 45) % 8];
}

void drawStatusPage(RlcdPanel &panel, const char *title, const char *detail) {
  panel.clearBuffer(LCD_PIXEL_WHITE);
  drawCentered(panel, panel.width() / 2, 110, title, 2);
  drawCentered(panel, panel.width() / 2, 150, detail, 1);
  panel.flush();
}

void drawWeatherPage(RlcdPanel &panel, const DashboardData &data,
                     const char *city) {
  static const char *weekdays[] = {"Sun", "Mon", "Tue", "Wed",
                                    "Thu", "Fri", "Sat"};
  panel.clearBuffer(LCD_PIXEL_WHITE);
  char text[80];

  snprintf(text, sizeof(text), "%s [%s]", city,
           data.wifiConnected ? "ONLINE" : "OFFLINE");
  drawString(panel, 12, 10, text, LCD_PIXEL_BLACK, 1);

  if (data.rtcReady) {
    snprintf(text, sizeof(text), "%04u-%02u-%02u %s",
             data.rtc.year, data.rtc.month, data.rtc.day,
             weekdays[data.rtc.weekday % 7]);
  } else {
    snprintf(text, sizeof(text), "----/--/--");
  }
  drawString(panel, panel.width() - 12 - textWidth(text), 10,
             text, LCD_PIXEL_BLACK, 1);
  drawLine(panel, 10, 24, panel.width() - 10, 24, LCD_PIXEL_BLACK);

  drawLine(panel, 205, 32, 205, 112, LCD_PIXEL_BLACK);
  if (data.rtcReady) {
    snprintf(text, sizeof(text), "%02u:%02u",
             data.rtc.hour, data.rtc.minute);
  } else {
    snprintf(text, sizeof(text), "--:--");
  }
  drawString(panel, 14, 38, text, LCD_PIXEL_BLACK, 4);

  if (data.indoorReady) {
    snprintf(text, sizeof(text), "IN %.1fC  %.0f%%",
             (double)data.indoor.temperatureC,
             (double)data.indoor.humidityPct);
  } else {
    snprintf(text, sizeof(text), "IN -- (no sensor)");
  }
  drawString(panel, 14, 84, text, LCD_PIXEL_BLACK, 1);

  if (data.weatherReady) {
    snprintf(text, sizeof(text), "%.1fC", (double)data.weather.temperatureC);
    drawString(panel, 222, 38, text, LCD_PIXEL_BLACK, 4);
    drawString(panel, 222, 78, weatherCodeText(data.weather.weatherCode),
               LCD_PIXEL_BLACK, 1);
    if (isfinite(data.weather.apparentC)) {
      snprintf(text, sizeof(text), "Feels %.0fC",
               (double)data.weather.apparentC);
      drawString(panel, 222, 96, text, LCD_PIXEL_BLACK, 1);
    }
  } else {
    drawString(panel, 222, 38, "--.-C", LCD_PIXEL_BLACK, 4);
    drawString(panel, 222, 84, "no weather yet", LCD_PIXEL_BLACK, 1);
  }

  drawLine(panel, 10, 120, panel.width() - 10, 120, LCD_PIXEL_BLACK);
  const int16_t columnX[3] = {16, 148, 280};
  const char *labels[6] = {"WIND km/h", "HUMIDITY", "PRESSURE",
                           "PRECIP", "HI/LO", "FEELS"};
  char values[6][20] = {"--", "--", "--", "--", "--", "--"};
  if (data.weatherReady) {
    if (isfinite(data.weather.windSpeedKmh)) {
      snprintf(values[0], 20, "%s %.0f",
               windDirText(data.weather.windDirDeg),
               (double)data.weather.windSpeedKmh);
    }
    if (isfinite(data.weather.humidityPct))
      snprintf(values[1], 20, "%.0f%%", (double)data.weather.humidityPct);
    if (isfinite(data.weather.pressureHpa))
      snprintf(values[2], 20, "%.0fhPa", (double)data.weather.pressureHpa);
    if (isfinite(data.weather.precipitationMm))
      snprintf(values[3], 20, "%.1fmm", (double)data.weather.precipitationMm);
    if (isfinite(data.weather.tempMaxC) && isfinite(data.weather.tempMinC)) {
      snprintf(values[4], 20, "%.0f/%.0fC",
               (double)data.weather.tempMaxC, (double)data.weather.tempMinC);
    }
    if (isfinite(data.weather.apparentC))
      snprintf(values[5], 20, "%.0fC", (double)data.weather.apparentC);
  }

  for (int index = 0; index < 6; index++) {
    const int row = index / 3;
    const int col = index % 3;
    const int16_t y = 128 + row * 34;
    drawString(panel, columnX[col], y, labels[index], LCD_PIXEL_BLACK, 1);
    drawString(panel, columnX[col], y + 11, values[index], LCD_PIXEL_BLACK, 2);
  }

  drawLine(panel, 10, 200, panel.width() - 10, 200, LCD_PIXEL_BLACK);
  drawLine(panel, 100, 204, 100, 254, LCD_PIXEL_BLACK);
  drawLine(panel, 200, 204, 200, 254, LCD_PIXEL_BLACK);
  drawLine(panel, 300, 204, 300, 254, LCD_PIXEL_BLACK);

  char today[4][12] = {"--", "--", "--", "--"};
  if (data.weatherReady) {
    if (data.weather.rainChancePct >= 0)
      snprintf(today[0], 12, "%d%%", data.weather.rainChancePct);
    if (isfinite(data.weather.uvIndexMax))
      snprintf(today[1], 12, "%.1f", (double)data.weather.uvIndexMax);
    snprintf(today[2], 12, "%s", data.weather.sunrise);
    snprintf(today[3], 12, "%s", data.weather.sunset);
  }
  drawTodayCell(panel, 0, "RAIN", today[0]);
  drawTodayCell(panel, 1, "UV MAX", today[1]);
  drawTodayCell(panel, 2, "RISE", today[2]);
  drawTodayCell(panel, 3, "SET", today[3]);

  const int16_t statusY = panel.height() - 34;
  drawRect(panel, 8, statusY, panel.width() - 16, 24, LCD_PIXEL_BLACK);
  snprintf(text, sizeof(text), "%.46s", data.message);
  drawString(panel, 14, statusY + 8, text, LCD_PIXEL_BLACK, 1);

  const char *source = "data: open-meteo.com";
  drawString(panel, panel.width() - 8 - textWidth(source),
             panel.height() - 8, source, LCD_PIXEL_BLACK, 1);
  panel.flush();
}
