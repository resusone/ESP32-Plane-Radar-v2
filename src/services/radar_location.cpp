#include "services/radar_location.h"

#include <Arduino.h>
#include <Preferences.h>
#include <cstdlib>
#include <cstring>

#include "config.h"

namespace services::location {

namespace {

constexpr char kPrefsNamespace[] = "radar";
constexpr char kKeyLat[] = "lat";
constexpr char kKeyLon[] = "lon";
constexpr char kKeyApprox[] = "approx";

// Read by the ADS-B task while the web portal may write from the main loop; a
// double is two words on this MCU, so guard against a torn read.
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
double s_lat = config::kDefaultRadarLat;
double s_lon = config::kDefaultRadarLon;
bool s_is_set = false;
bool s_approx = false;

void setRuntime(double lat, double lon, bool is_set, bool approx) {
  portENTER_CRITICAL(&s_mux);
  s_lat = lat;
  s_lon = lon;
  s_is_set = is_set;
  s_approx = approx;
  portEXIT_CRITICAL(&s_mux);
}

bool parseCoord(const char* text, double* out) {
  if (text == nullptr || text[0] == '\0') {
    return false;
  }
  char* end = nullptr;
  const double v = strtod(text, &end);
  if (end == text) {
    return false;
  }
  while (end != nullptr && (*end == ' ' || *end == '\t')) {
    ++end;
  }
  if (end != nullptr && *end != '\0') {
    return false;
  }
  *out = v;
  return true;
}

bool validLatLon(double lat, double lon) {
  return lat >= -90.0 && lat <= 90.0 && lon >= -180.0 && lon <= 180.0;
}

}  // namespace

void init() {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, true);
  if (prefs.isKey(kKeyLat) && prefs.isKey(kKeyLon)) {
    const double lat = prefs.getDouble(kKeyLat, config::kDefaultRadarLat);
    const double lon = prefs.getDouble(kKeyLon, config::kDefaultRadarLon);
    if (validLatLon(lat, lon)) {
      setRuntime(lat, lon, true, prefs.getBool(kKeyApprox, false));
    }
  }
  prefs.end();
}

double lat() {
  portENTER_CRITICAL(&s_mux);
  const double v = s_lat;
  portEXIT_CRITICAL(&s_mux);
  return v;
}

double lon() {
  portENTER_CRITICAL(&s_mux);
  const double v = s_lon;
  portEXIT_CRITICAL(&s_mux);
  return v;
}

bool isSet() { return s_is_set; }

bool isApproximate() { return s_approx; }

bool save(double lat, double lon, bool approximate) {
  if (!validLatLon(lat, lon)) {
    return false;
  }
  Preferences prefs;
  prefs.begin(kPrefsNamespace, false);
  prefs.putDouble(kKeyLat, lat);
  prefs.putDouble(kKeyLon, lon);
  prefs.putBool(kKeyApprox, approximate);
  prefs.end();
  setRuntime(lat, lon, true, approximate);
  Serial.printf("Radar location saved: %.6f, %.6f%s\n", lat, lon,
                approximate ? " (approximate)" : "");
  return true;
}

bool saveFromStrings(const char* lat_str, const char* lon_str) {
  double lat = 0.0;
  double lon = 0.0;
  if (!parseCoord(lat_str, &lat) || !parseCoord(lon_str, &lon)) {
    return false;
  }
  return save(lat, lon, false);
}

void clear() {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, false);
  prefs.remove(kKeyLat);
  prefs.remove(kKeyLon);
  prefs.remove(kKeyApprox);
  prefs.end();
  setRuntime(config::kDefaultRadarLat, config::kDefaultRadarLon, false, false);
}

}  // namespace services::location
