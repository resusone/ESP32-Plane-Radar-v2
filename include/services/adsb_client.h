#pragma once

#include <cstddef>
#include <cstdint>

namespace services::adsb {

struct Aircraft {
  float lat;
  float lon;
  float nose_deg;
  float track_deg;
  float gs_knots;
  /** Age of the position fix at fetch time (ms), from the feed's seen_pos.
   *  Added to the elapsed time when dead-reckoning so the drawn position
   *  reflects the estimated current location, not the already-stale fix. */
  uint32_t pos_age_ms;
  /** Distance from the radar center at fetch time. */
  float dist_km;
  char callsign[9];
  char type[5];
  char alt[12];
  bool on_ground;
};

/** Aircraft kept per fetch -- the nearest ones when the feed returns more. */
constexpr size_t kMaxAircraft = 64;

/** Outcome of the most recent fetch plus running totals; for the on-screen
 *  status line, the info screen and the web portal's diagnostics. */
struct Status {
  bool attempted = false;
  bool last_ok = false;
  bool ever_ok = false;
  /** HTTP status, or a negative HTTPClient error code. 0 before any attempt. */
  int http_code = 0;
  /** Short reason ("HTTP 429", "Timed out", ...); empty when last_ok. */
  char error[24] = "";
  /** Longer detail for the web portal (e.g. the TLS library's message). */
  char detail[72] = "";
  uint32_t last_attempt_ms = 0;
  uint32_t last_success_ms = 0;
  uint32_t fetch_ms = 0;
  uint32_t bytes = 0;
  /** Aircraft objects in the feed's reply (before any filtering). */
  uint16_t reported = 0;
  /** Of those: published to the display. */
  uint16_t shown = 0;
  uint16_t hidden_ground = 0;
  uint16_t no_position = 0;
  /** Nearest published aircraft (km), or < 0 when there are none. */
  float nearest_km = -1.0f;
  float radius_km = 0.0f;
  /** settings::FeedSource value actually queried (never kAuto). */
  uint8_t source = 1;
  uint32_t ok_count = 0;
  uint32_t fail_count = 0;
};

/** Create the internal lock. Call once before any fetch/snapshot. */
void init();

/** Fetch radius provider, queried before every poll. */
using RadiusFn = float (*)();
/** Start the background polling task (idles while Wi-Fi is down). */
void startTask(RadiusFn radius_km);
/** Poll again right away (after a range/location/source change). */
void requestRefresh();

/**
 * Copy the current aircraft into `out` (up to `max_out`), nearest first, and
 * report the fetch timestamp, all under a lock -- safe to call from a
 * different thread than the fetch task. Returns the number copied; 0 once the
 * data has gone stale.
 */
size_t snapshotAircraft(Aircraft* out, size_t max_out,
                        unsigned long* out_last_update_ms);

Status status();

/** "adsb.fi" / "adsb.lol" for a Status::source value. */
const char* sourceName(uint8_t source);

/** Fetch aircraft within fetch_radius_km of center_lat/lon. */
bool fetchUpdate(double center_lat, double center_lon, float fetch_radius_km);

}  // namespace services::adsb
