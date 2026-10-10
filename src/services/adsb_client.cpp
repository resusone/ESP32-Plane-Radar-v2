#include "services/adsb_client.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <cmath>
#include <cstdio>
#include <cstring>

#include "config.h"
#include "services/aircraft_parse.h"
#include "services/http_body_framing.h"
#include "services/radar_location.h"
#include "services/settings.h"

namespace services::adsb {

namespace {

using settings::FeedSource;

constexpr float kKmPerNm = 1.852f;
/** Both feeds cap the query radius at 250 NM. */
constexpr float kMaxRadiusNm = 250.0f;
constexpr int kConnectTimeoutMs = 10000;
/** A handshake over a weak link needs several round trips with retries. */
constexpr unsigned long kTlsHandshakeTimeoutSec = 20;
/** Whole-reply budget. A wide query over busy airspace is >100 kB. */
constexpr unsigned long kRequestTimeoutMs = 20000;
/** Never poll faster than this, whatever asks for a refresh. */
constexpr unsigned long kMinRequestGapMs = 1500;
constexpr unsigned long kMaxBackoffMs = 30000;
constexpr uint32_t kTaskStackBytes = 16384;

Aircraft s_aircraft[kMaxAircraft];
size_t s_aircraft_count = 0;
unsigned long s_last_update_ms = 0;
Status s_status;
SemaphoreHandle_t s_mutex = nullptr;

TaskHandle_t s_task = nullptr;
RadiusFn s_radius_fn = nullptr;

/** Feed queried in Auto mode; flips after repeated failures. */
FeedSource s_auto_source = FeedSource::kAdsbFi;
/** Failures in a row on the current feed (drives failover). */
uint8_t s_failover_count = 0;
/** Failures in a row on any feed (drives back-off). */
uint8_t s_fail_streak = 0;

class Lock {
 public:
  Lock() {
    if (s_mutex != nullptr) {
      xSemaphoreTake(s_mutex, portMAX_DELAY);
    }
  }
  ~Lock() {
    if (s_mutex != nullptr) {
      xSemaphoreGive(s_mutex);
    }
  }
};

/**
 * Pumps the socket in blocks without buffering the whole response.
 *
 * getStreamPtr() hands back the raw socket, so on HTTP/1.1 the bytes still
 * carry chunk framing; BodyFramer unwraps that. Where the body ends is
 * BodyFramer's business, not this class's.
 */
class SocketSource {
 public:
  SocketSource(HTTPClient& http, WiFiClient& stream, unsigned long timeout_ms)
      : http_(&http), stream_(&stream), start_ms_(millis()),
        timeout_ms_(timeout_ms) {}

  /** Next raw byte, or -1 once the socket closes or the deadline passes. */
  int read() {
    if (pos_ >= len_ && !refill()) {
      return -1;
    }
    return static_cast<unsigned char>(buffer_[pos_++]);
  }

  bool timedOut() const { return timed_out_; }

 private:
  bool refill() {
    pos_ = 0;
    len_ = 0;
    while (millis() - start_ms_ < timeout_ms_) {
      const int available = stream_->available();
      if (available > 0) {
        const int to_read = available > static_cast<int>(sizeof(buffer_))
                                ? static_cast<int>(sizeof(buffer_))
                                : available;
        const int read_bytes = stream_->readBytes(buffer_, to_read);
        if (read_bytes > 0) {
          len_ = static_cast<size_t>(read_bytes);
          return true;
        }
      }
      if (!http_->connected() && stream_->available() <= 0) {
        return false;  // server closed and the socket is drained
      }
      delay(2);
    }
    timed_out_ = true;
    return false;
  }

  HTTPClient* http_;
  WiFiClient* stream_;
  unsigned long start_ms_;
  unsigned long timeout_ms_;
  char buffer_[512];
  size_t pos_ = 0;
  size_t len_ = 0;
  bool timed_out_ = false;
};

using BodyReader = services::http::BodyFramer<SocketSource>;

FeedSource otherSource(FeedSource s) {
  return s == FeedSource::kAdsbFi ? FeedSource::kAdsbLol : FeedSource::kAdsbFi;
}

FeedSource pickSource() {
  const FeedSource wanted = settings::feedSource();
  return wanted == FeedSource::kAuto ? s_auto_source : wanted;
}

String buildUrl(FeedSource source, double lat, double lon, float radius_km) {
  float nm = radius_km / kKmPerNm;
  if (nm > kMaxRadiusNm) nm = kMaxRadiusNm;
  if (nm < 1.0f) nm = 1.0f;

  char url[128];
  if (source == FeedSource::kAdsbLol) {
    // adsb.lol takes a whole number of nautical miles.
    snprintf(url, sizeof(url), "https://api.adsb.lol/v2/point/%.5f/%.5f/%d",
             lat, lon, static_cast<int>(ceilf(nm)));
  } else {
    snprintf(url, sizeof(url),
             "https://opendata.adsb.fi/api/v3/lat/%.5f/lon/%.5f/dist/%.1f", lat,
             lon, nm);
  }
  return String(url);
}

/** Everything one fetch attempt learned, folded into s_status at the end. */
struct Attempt {
  bool ok = false;
  int http_code = 0;
  char error[sizeof(Status::error)] = "";
  char detail[sizeof(Status::detail)] = "";
  uint32_t bytes = 0;
  ParseCounters counters;
};

void describeTransportError(int code, WiFiClientSecure& client, Attempt* a) {
  switch (code) {
    case HTTPC_ERROR_CONNECTION_REFUSED:
      // HTTPClient reports a failed TLS handshake or DNS lookup this way too.
      snprintf(a->error, sizeof(a->error), "Connect failed");
      break;
    case HTTPC_ERROR_READ_TIMEOUT:
      snprintf(a->error, sizeof(a->error), "Timed out");
      break;
    case HTTPC_ERROR_CONNECTION_LOST:
    case HTTPC_ERROR_NOT_CONNECTED:
      snprintf(a->error, sizeof(a->error), "Connection lost");
      break;
    default:
      snprintf(a->error, sizeof(a->error), "Net error %d", code);
      break;
  }

  char tls[sizeof(a->detail)] = "";
  if (client.lastError(tls, sizeof(tls)) != 0 && tls[0] != '\0') {
    snprintf(a->detail, sizeof(a->detail), "%s", tls);
  } else {
    snprintf(a->detail, sizeof(a->detail), "%s",
             HTTPClient::errorToString(code).c_str());
  }
}

void describeHttpError(int code, Attempt* a) {
  if (code == 429) {
    snprintf(a->error, sizeof(a->error), "Rate limited");
    snprintf(a->detail, sizeof(a->detail),
             "The feed is refusing requests for now (HTTP 429)");
  } else {
    snprintf(a->error, sizeof(a->error), "HTTP %d", code);
    snprintf(a->detail, sizeof(a->detail), "The feed answered HTTP %d", code);
  }
}

void runAttempt(const String& url, double lat, double lon, Aircraft* parsed,
                Attempt* a) {
  WiFiClientSecure client;
  client.setInsecure();
  client.setHandshakeTimeout(kTlsHandshakeTimeoutSec);

  HTTPClient http;
  if (!http.begin(client, url)) {
    snprintf(a->error, sizeof(a->error), "Bad URL");
    return;
  }
  http.setUserAgent(config::kUserAgent);
  http.setReuse(false);
  http.setConnectTimeout(kConnectTimeoutMs);
  http.setTimeout(kConnectTimeoutMs);

  // HTTPClient only records Transfer-Encoding in the collected headers when
  // it is asked for up front; _transferEncoding itself is private.
  static const char* kWantedHeaders[] = {"Transfer-Encoding"};
  http.collectHeaders(kWantedHeaders, 1);

  const int code = http.GET();
  a->http_code = code;
  if (code <= 0) {
    describeTransportError(code, client, a);
    http.end();
    return;
  }
  if (code != HTTP_CODE_OK) {
    describeHttpError(code, a);
    http.end();
    return;
  }

  WiFiClient* stream = http.getStreamPtr();
  if (stream == nullptr) {
    snprintf(a->error, sizeof(a->error), "No response");
    http.end();
    return;
  }

  const services::http::BodyFraming framing =
      http.header("Transfer-Encoding").equalsIgnoreCase("chunked")
          ? services::http::BodyFraming::kChunked
          : services::http::BodyFraming::kIdentity;

  SocketSource source(http, *stream, kRequestTimeoutMs);
  BodyReader body(source, framing, http.getSize());
  const ParseResult result =
      parseFeed(body, lat, lon, settings::showGround(), parsed, kMaxAircraft,
                &a->counters);
  a->bytes = static_cast<uint32_t>(body.bytesRead());
  // The reply is abandoned right after the array (and on any error); the
  // connection is not reused, so there is nothing to drain.
  http.end();

  switch (result) {
    case ParseResult::kOk:
      a->ok = true;
      break;
    case ParseResult::kNoList:
      snprintf(a->error, sizeof(a->error),
               a->bytes == 0 ? "Empty reply" : "Bad reply");
      snprintf(a->detail, sizeof(a->detail),
               "The reply had no aircraft list (%u bytes)",
               static_cast<unsigned>(a->bytes));
      break;
    case ParseResult::kTruncated:
      snprintf(a->error, sizeof(a->error),
               source.timedOut() ? "Timed out" : "Reply cut short");
      snprintf(a->detail, sizeof(a->detail),
               "Reply ended after %u bytes, %u aircraft",
               static_cast<unsigned>(a->bytes),
               static_cast<unsigned>(a->counters.reported));
      break;
    case ParseResult::kBadJson:
      snprintf(a->error, sizeof(a->error), "Bad reply");
      snprintf(a->detail, sizeof(a->detail),
               body.framingError() ? "Malformed chunked body"
                                   : "Aircraft list was not valid JSON");
      break;
  }
}

void fetchTask(void*) {
  unsigned long last_request_ms = 0;
  for (;;) {
    uint32_t wait_ms = 1000;
    const bool online = WiFi.status() == WL_CONNECTED &&
                        WiFi.localIP() != IPAddress(0, 0, 0, 0);
    if (online) {
      const unsigned long since = millis() - last_request_ms;
      if (last_request_ms != 0 && since < kMinRequestGapMs) {
        vTaskDelay(pdMS_TO_TICKS(kMinRequestGapMs - since));
      }
      const float radius = s_radius_fn != nullptr ? s_radius_fn() : 50.0f;
      const bool ok = fetchUpdate(location::lat(), location::lon(), radius);
      last_request_ms = millis();

      wait_ms = config::kAdsbFetchIntervalMs;
      if (!ok) {
        // Back off while the feed is unhappy; a 429 means "slow down".
        wait_ms = config::kAdsbFetchIntervalMs * s_fail_streak;
        if (status().http_code == 429 || wait_ms > kMaxBackoffMs) {
          wait_ms = kMaxBackoffMs;
        }
      }
    }
    // Sleeps for the interval, or until requestRefresh() pokes the task.
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wait_ms));
  }
}

}  // namespace

void init() {
  if (s_mutex == nullptr) {
    s_mutex = xSemaphoreCreateMutex();
  }
}

void startTask(RadiusFn radius_km) {
  s_radius_fn = radius_km;
  if (s_task == nullptr) {
    // The HTTPS request blocks for a second or more; its own task keeps the
    // radar redrawing (dead-reckoned) throughout.
    xTaskCreate(fetchTask, "adsb", kTaskStackBytes, nullptr, 1, &s_task);
  }
}

void requestRefresh() {
  if (s_task != nullptr) {
    xTaskNotifyGive(s_task);
  }
}

size_t snapshotAircraft(Aircraft* out, size_t max_out,
                        unsigned long* out_last_update_ms) {
  const Lock lock;
  if (out_last_update_ms != nullptr) {
    *out_last_update_ms = s_last_update_ms;
  }
  if (s_last_update_ms == 0 ||
      millis() - s_last_update_ms > config::kAdsbStaleMs) {
    return 0;
  }
  const size_t count = s_aircraft_count < max_out ? s_aircraft_count : max_out;
  for (size_t i = 0; i < count; ++i) {
    out[i] = s_aircraft[i];
  }
  return count;
}

Status status() {
  const Lock lock;
  return s_status;
}

const char* sourceName(uint8_t source) {
  return source == static_cast<uint8_t>(FeedSource::kAdsbLol) ? "adsb.lol"
                                                               : "adsb.fi";
}

bool fetchUpdate(double center_lat, double center_lon, float fetch_radius_km) {
  // Parsed off to the side, then published atomically so a reader on another
  // thread never sees a half-updated list. Static: too big for the task stack.
  static Aircraft parsed[kMaxAircraft];

  const FeedSource source = pickSource();
  const String url = buildUrl(source, center_lat, center_lon, fetch_radius_km);
  const unsigned long started = millis();

  Attempt attempt;
  runAttempt(url, center_lat, center_lon, parsed, &attempt);
  const uint32_t took_ms = millis() - started;

  {
    const Lock lock;
    s_status.attempted = true;
    s_status.last_ok = attempt.ok;
    s_status.http_code = attempt.http_code;
    s_status.last_attempt_ms = millis();
    s_status.fetch_ms = took_ms;
    s_status.bytes = attempt.bytes;
    s_status.radius_km = fetch_radius_km;
    s_status.source = static_cast<uint8_t>(source);
    memcpy(s_status.error, attempt.error, sizeof(s_status.error));
    memcpy(s_status.detail, attempt.detail, sizeof(s_status.detail));

    if (attempt.ok) {
      const size_t n = attempt.counters.kept;
      for (size_t i = 0; i < n; ++i) {
        s_aircraft[i] = parsed[i];
      }
      s_aircraft_count = n;
      s_last_update_ms = millis();  // base time for dead-reckoning
      s_status.ever_ok = true;
      s_status.last_success_ms = s_last_update_ms;
      s_status.reported = attempt.counters.reported;
      s_status.shown = attempt.counters.kept;
      s_status.hidden_ground = attempt.counters.hidden_ground;
      s_status.no_position = attempt.counters.no_position;
      s_status.nearest_km = attempt.counters.nearest_km;
      ++s_status.ok_count;
    } else {
      ++s_status.fail_count;
    }
  }

  if (attempt.ok) {
    s_failover_count = 0;
    s_fail_streak = 0;
    Serial.printf("adsb: %s %u aircraft (%u in reply, %u on ground hidden) "
                  "%u B in %u ms\n",
                  sourceName(static_cast<uint8_t>(source)),
                  static_cast<unsigned>(attempt.counters.kept),
                  static_cast<unsigned>(attempt.counters.reported),
                  static_cast<unsigned>(attempt.counters.hidden_ground),
                  static_cast<unsigned>(attempt.bytes),
                  static_cast<unsigned>(took_ms));
    return true;
  }

  Serial.printf("adsb: %s failed: %s (%s) heap %u/%u\n",
                sourceName(static_cast<uint8_t>(source)), attempt.error,
                attempt.detail, static_cast<unsigned>(ESP.getFreeHeap()),
                static_cast<unsigned>(ESP.getMaxAllocHeap()));
  if (s_fail_streak < 255) {
    ++s_fail_streak;
  }
  if (++s_failover_count >= config::kAdsbFailoverAfter) {
    s_failover_count = 0;
    if (settings::feedSource() == FeedSource::kAuto) {
      s_auto_source = otherSource(s_auto_source);
      Serial.printf("adsb: switching to %s\n",
                    sourceName(static_cast<uint8_t>(s_auto_source)));
    }
  }
  return false;
}

}  // namespace services::adsb
