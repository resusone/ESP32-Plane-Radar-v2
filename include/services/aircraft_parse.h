#pragma once

#include <ArduinoJson.h>

#include <cstddef>
#include <cstdint>

#include "services/adsb_client.h"
#include "services/aircraft_stream.h"

/**
 * Feed reply -> Aircraft list. Free of any network or board dependency so the
 * whole path can be exercised on a desktop against a captured reply
 * (sim/feed_test.cpp).
 */
namespace services::adsb {

struct ParseCounters {
  uint16_t reported = 0;
  uint16_t kept = 0;
  uint16_t hidden_ground = 0;
  uint16_t no_position = 0;
  float nearest_km = -1.0f;
};

enum class ParseResult {
  kOk,
  kNoList,     // reply has no "ac" array (an error document, say)
  kBadJson,
  kTruncated,  // stream ended inside the array
};

/** Fields kept from each aircraft object; everything else never reaches RAM. */
const JsonDocument& planeFilter();

/** Kilometres from the center to lat/lon (flat-earth, fine at radar scale). */
float distanceKm(double center_lat, double center_lon, float lat, float lon);

/** False when the object carries no position. */
bool planeFromJson(JsonObjectConst plane, double center_lat, double center_lon,
                   Aircraft* out);

/** Insert into `list`, evicting the farthest entry once it is full. */
void keepNearest(Aircraft* list, size_t max_count, size_t* count,
                 const Aircraft& plane);

void sortByDistance(Aircraft* list, size_t count);

/**
 * Parse a feed reply from `body` (anything with `int read()`), keeping the
 * `max_out` nearest aircraft in `out`, nearest first.
 */
template <typename Source>
ParseResult parseFeed(Source& body, double center_lat, double center_lon,
                      bool show_ground, Aircraft* out, size_t max_out,
                      ParseCounters* counters) {
  *counters = ParseCounters();
  PushbackReader<Source> in(body);
  if (!seekAircraftArray(in)) {
    return ParseResult::kNoList;
  }

  size_t count = 0;
  JsonDocument doc;
  for (;;) {
    const ArrayStep step = nextArrayElement(in);
    if (step == ArrayStep::kEnd) {
      break;
    }
    if (step == ArrayStep::kMalformed) {
      return ParseResult::kTruncated;
    }
    const DeserializationError err = deserializeJson(
        doc, in, DeserializationOption::Filter(planeFilter()));
    if (err) {
      return err == DeserializationError::IncompleteInput
                 ? ParseResult::kTruncated
                 : ParseResult::kBadJson;
    }
    ++counters->reported;

    Aircraft plane;
    if (!planeFromJson(doc.as<JsonObjectConst>(), center_lat, center_lon,
                       &plane)) {
      ++counters->no_position;
      continue;
    }
    if (plane.on_ground && !show_ground) {
      ++counters->hidden_ground;
      continue;
    }
    keepNearest(out, max_out, &count, plane);
  }

  sortByDistance(out, count);
  counters->kept = static_cast<uint16_t>(count);
  counters->nearest_km = count > 0 ? out[0].dist_km : -1.0f;
  return ParseResult::kOk;
}

}  // namespace services::adsb
