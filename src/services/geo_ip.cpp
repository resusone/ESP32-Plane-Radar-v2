#include "services/geo_ip.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>

#include "config.h"

namespace services::geo {

namespace {
// Plain HTTP: the free tier of this service does not offer TLS.
constexpr char kUrl[] =
    "http://ip-api.com/json/?fields=status,lat,lon,city,country";
constexpr int kTimeoutMs = 5000;
}  // namespace

bool lookup(double* lat, double* lon, char* place, size_t place_len) {
  if (place != nullptr && place_len > 0) {
    place[0] = '\0';
  }

  WiFiClient client;
  HTTPClient http;
  if (!http.begin(client, kUrl)) {
    return false;
  }
  http.setUserAgent(config::kUserAgent);
  http.setConnectTimeout(kTimeoutMs);
  http.setTimeout(kTimeoutMs);

  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("geo: HTTP %d\n", code);
    http.end();
    return false;
  }
  const String body = http.getString();
  http.end();

  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    Serial.println("geo: bad reply");
    return false;
  }
  const char* status = doc["status"] | "";
  if (strcmp(status, "success") != 0 || !doc["lat"].is<double>() ||
      !doc["lon"].is<double>()) {
    Serial.println("geo: lookup failed");
    return false;
  }

  *lat = doc["lat"].as<double>();
  *lon = doc["lon"].as<double>();
  if (place != nullptr && place_len > 0) {
    const char* city = doc["city"] | "";
    const char* country = doc["country"] | "";
    snprintf(place, place_len, "%s%s%s", city,
             (city[0] != '\0' && country[0] != '\0') ? ", " : "", country);
  }
  Serial.printf("geo: %.4f, %.4f (%s)\n", *lat, *lon,
                place != nullptr ? place : "");
  return true;
}

}  // namespace services::geo
