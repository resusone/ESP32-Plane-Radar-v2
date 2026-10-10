#include "services/aircraft_parse.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace services::adsb {

namespace {

constexpr float kKmPerDeg = 111.0f;
constexpr float kDegToRad = 0.01745329252f;

bool readJsonFloat(JsonObjectConst obj, const char* key, float* out) {
  if (obj[key].is<float>()) {
    *out = obj[key].as<float>();
    return true;
  }
  return false;
}

/** First of `keys` present as a number, else 0. */
float pickFirst(JsonObjectConst plane, std::initializer_list<const char*> keys) {
  float v = 0.0f;
  for (const char* key : keys) {
    if (readJsonFloat(plane, key, &v)) {
      return v;
    }
  }
  return 0.0f;
}

bool isOnGround(JsonObjectConst plane) {
  if (!plane["alt_baro"].is<const char*>()) {
    return false;
  }
  return strcmp(plane["alt_baro"].as<const char*>(), "ground") == 0;
}

void copyJsonStringTrimmed(JsonObjectConst obj, const char* key, char* out,
                           size_t out_len) {
  out[0] = '\0';
  if (out_len == 0 || !obj[key].is<const char*>()) {
    return;
  }
  const char* s = obj[key].as<const char*>();
  size_t n = strnlen(s, out_len - 1);
  while (n > 0 && s[n - 1] == ' ') {
    --n;
  }
  memcpy(out, s, n);
  out[n] = '\0';
}

void formatAltitudeTag(JsonObjectConst plane, bool on_ground, char* out,
                       size_t out_len) {
  out[0] = '\0';
  if (on_ground) {
    snprintf(out, out_len, "GND");
    return;
  }
  float alt = 0.0f;
  if (readJsonFloat(plane, "alt_baro", &alt) ||
      readJsonFloat(plane, "alt_geom", &alt)) {
    snprintf(out, out_len, "%d ft", static_cast<int>(lroundf(alt)));
  }
}

}  // namespace

const JsonDocument& planeFilter() {
  static JsonDocument filter;
  if (filter.isNull()) {
    for (const char* key :
         {"lat", "lon", "true_heading", "mag_heading", "track", "dir", "gs",
          "tas", "ias", "alt_baro", "alt_geom", "seen_pos", "flight", "hex",
          "t"}) {
      filter[key] = true;
    }
  }
  return filter;
}

float distanceKm(double center_lat, double center_lon, float lat, float lon) {
  const float dx = static_cast<float>(lon - center_lon) * kKmPerDeg *
                   cosf(static_cast<float>(center_lat) * kDegToRad);
  const float dy = static_cast<float>(lat - center_lat) * kKmPerDeg;
  return sqrtf(dx * dx + dy * dy);
}

bool planeFromJson(JsonObjectConst plane, double center_lat, double center_lon,
                   Aircraft* out) {
  if (!plane["lat"].is<float>() || !plane["lon"].is<float>()) {
    return false;
  }

  out->lat = plane["lat"].as<float>();
  out->lon = plane["lon"].as<float>();
  out->nose_deg =
      pickFirst(plane, {"true_heading", "mag_heading", "track", "dir"});
  out->track_deg =
      pickFirst(plane, {"track", "true_heading", "mag_heading", "dir"});
  out->gs_knots = pickFirst(plane, {"gs", "tas", "ias"});
  out->dist_km = distanceKm(center_lat, center_lon, out->lat, out->lon);
  out->on_ground = isOnGround(plane);

  // seen_pos: seconds since this position was measured. Use it as the
  // dead-reckoning age offset, capped so a very stale fix isn't flung far.
  float seen_pos = 0.0f;
  readJsonFloat(plane, "seen_pos", &seen_pos);
  if (seen_pos < 0.0f) seen_pos = 0.0f;
  if (seen_pos > 30.0f) seen_pos = 30.0f;
  out->pos_age_ms = static_cast<uint32_t>(seen_pos * 1000.0f);

  copyJsonStringTrimmed(plane, "flight", out->callsign, sizeof(out->callsign));
  if (out->callsign[0] == '\0') {
    copyJsonStringTrimmed(plane, "hex", out->callsign, sizeof(out->callsign));
  }
  copyJsonStringTrimmed(plane, "t", out->type, sizeof(out->type));
  formatAltitudeTag(plane, out->on_ground, out->alt, sizeof(out->alt));
  return true;
}

void keepNearest(Aircraft* list, size_t max_count, size_t* count,
                 const Aircraft& plane) {
  if (max_count == 0) {
    return;
  }
  if (*count < max_count) {
    list[(*count)++] = plane;
    return;
  }
  size_t farthest = 0;
  for (size_t i = 1; i < max_count; ++i) {
    if (list[i].dist_km > list[farthest].dist_km) {
      farthest = i;
    }
  }
  if (plane.dist_km < list[farthest].dist_km) {
    list[farthest] = plane;
  }
}

void sortByDistance(Aircraft* list, size_t count) {
  for (size_t i = 1; i < count; ++i) {
    const Aircraft key = list[i];
    size_t j = i;
    while (j > 0 && list[j - 1].dist_km > key.dist_km) {
      list[j] = list[j - 1];
      --j;
    }
    list[j] = key;
  }
}

}  // namespace services::adsb
