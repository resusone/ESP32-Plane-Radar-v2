#include "services/web_portal.h"

#include <ArduinoJson.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <cmath>
#include <cstdlib>
#include <cstring>

#include "config.h"
#include "hardware/display.h"
#include "services/adsb_client.h"
#include "services/geo_ip.h"
#include "services/network.h"
#include "services/radar_location.h"
#include "services/settings.h"
#include "ui/radar_range.h"

extern "C" {
// data/portal.html, gzipped at build time by scripts/gzip_portal.py.
extern const uint8_t _binary_data_portal_html_gz_start[] asm(
    "_binary_data_portal_html_gz_start");
extern const uint8_t _binary_data_portal_html_gz_end[] asm(
    "_binary_data_portal_html_gz_end");
}

namespace web {

namespace {

constexpr uint16_t kDnsPort = 53;
/** Aircraft rows in /api/status (nearest first). */
constexpr size_t kStatusAircraftMax = 30;

WebServer s_server(80);
DNSServer s_dns;
bool s_routes_ready = false;
bool s_running = false;
bool s_captive = false;

constexpr uint32_t kTaskStackBytes = 8192;
TaskHandle_t s_task = nullptr;
/** Held while a request is being served, and by start()/stop(). */
SemaphoreHandle_t s_lock = nullptr;

void sendJson(int code, const JsonDocument& doc) {
  String body;
  serializeJson(doc, body);
  s_server.sendHeader("Cache-Control", "no-store");
  s_server.send(code, "application/json", body);
}

void sendError(int code, const char* message) {
  JsonDocument doc;
  doc["ok"] = false;
  doc["error"] = message;
  sendJson(code, doc);
}

void sendOk() {
  JsonDocument doc;
  doc["ok"] = true;
  sendJson(200, doc);
}

float roundTo(float v, float step) { return roundf(v / step) * step; }

/** Compass bearing (deg) from the radar center to lat/lon. */
int bearingDeg(float lat, float lon) {
  const double clat = services::location::lat();
  const double clon = services::location::lon();
  const float dx = static_cast<float>(lon - clon) *
                   cosf(static_cast<float>(clat) * 0.01745329252f);
  const float dy = static_cast<float>(lat - clat);
  int deg = static_cast<int>(lroundf(atan2f(dx, dy) * 57.29578f));
  if (deg < 0) {
    deg += 360;
  }
  return deg % 360;
}

void fillSettings(JsonObject out) {
  out["lat"] = services::location::lat();
  out["lon"] = services::location::lon();
  out["loc_set"] = services::location::isSet();
  out["loc_approx"] = services::location::isApproximate();
  out["range"] = ui::radar::rangeIndex();
  JsonArray ranges = out["ranges_km"].to<JsonArray>();
  for (size_t i = 0; i < ui::radar::kRangePresetCount; ++i) {
    ranges.add(ui::radar::kRangePresets[i].ring3_km);
  }
  out["miles"] = ui::radar::useMiles();
  out["runways"] = ui::radar::showRunways();
  out["ground"] = services::settings::showGround();
  out["source"] = static_cast<uint8_t>(services::settings::feedSource());
  out["labels"] = services::settings::maxLabels();
  out["labels_max"] = services::settings::kMaxLabelsLimit;
  out["swap_rb"] = services::settings::swapRedBlue();
  out["wifi_power"] = static_cast<uint8_t>(services::settings::wifiPower());
}

void handleRoot() {
  s_server.sendHeader("Cache-Control", "no-store");
  s_server.sendHeader("Content-Encoding", "gzip");
  s_server.send_P(200, "text/html; charset=utf-8",
                  reinterpret_cast<PGM_P>(_binary_data_portal_html_gz_start),
                  static_cast<size_t>(_binary_data_portal_html_gz_end -
                                      _binary_data_portal_html_gz_start));
}

void handleStatus() {
  JsonDocument doc;
  const bool online = net::online();
  doc["fw"] = config::kFirmwareVersion;
  doc["mode"] = net::mode() == net::Mode::kPortal ? "portal" : "station";
  doc["online"] = online;
  doc["ssid"] = online ? WiFi.SSID() : String();
  doc["saved_ssid"] = net::savedSsid();
  doc["ip"] = online ? WiFi.localIP().toString() : String();
  doc["rssi"] = online ? WiFi.RSSI() : 0;
  doc["rssi_weak"] = config::kWifiRssiWeak;
  doc["tx_dbm"] = roundTo(net::txPowerDbm(), 0.5f);
  doc["host"] = config::kPortalHostUrl;
  doc["note"] = net::portalNote();
  doc["uptime_s"] = millis() / 1000UL;
  doc["heap"] = ESP.getFreeHeap();
  doc["heap_min"] = ESP.getMinFreeHeap();
  doc["heap_block"] = ESP.getMaxAllocHeap();

  JsonObject loc = doc["loc"].to<JsonObject>();
  loc["lat"] = services::location::lat();
  loc["lon"] = services::location::lon();
  loc["set"] = services::location::isSet();
  loc["approx"] = services::location::isApproximate();

  const services::adsb::Status st = services::adsb::status();
  JsonObject feed = doc["feed"].to<JsonObject>();
  feed["attempted"] = st.attempted;
  feed["ok"] = st.last_ok;
  feed["ever_ok"] = st.ever_ok;
  feed["http"] = st.http_code;
  feed["error"] = st.error;
  feed["detail"] = st.detail;
  feed["age_s"] = st.ever_ok
                      ? static_cast<long>((millis() - st.last_success_ms) / 1000UL)
                      : -1L;
  feed["fetch_ms"] = st.fetch_ms;
  feed["bytes"] = st.bytes;
  feed["reported"] = st.reported;
  feed["shown"] = st.shown;
  feed["ground"] = st.hidden_ground;
  feed["nopos"] = st.no_position;
  feed["nearest_km"] = roundTo(st.nearest_km, 0.1f);
  feed["radius_km"] = roundTo(ui::radar::fetchRadiusKm(), 0.1f);
  feed["source"] = services::adsb::sourceName(st.source);
  feed["ok_count"] = st.ok_count;
  feed["fail_count"] = st.fail_count;

  static services::adsb::Aircraft planes[services::adsb::kMaxAircraft];
  const size_t n = services::adsb::snapshotAircraft(
      planes, services::adsb::kMaxAircraft, nullptr);
  doc["aircraft_total"] = n;
  JsonArray list = doc["aircraft"].to<JsonArray>();
  for (size_t i = 0; i < n && i < kStatusAircraftMax; ++i) {
    JsonObject a = list.add<JsonObject>();
    a["cs"] = planes[i].callsign;
    a["t"] = planes[i].type;
    a["alt"] = planes[i].alt;
    a["km"] = roundTo(planes[i].dist_km, 0.1f);
    a["brg"] = bearingDeg(planes[i].lat, planes[i].lon);
    a["gs"] = static_cast<int>(lroundf(planes[i].gs_knots));
  }
  sendJson(200, doc);
}

void handleSettingsGet() {
  JsonDocument doc;
  fillSettings(doc.to<JsonObject>());
  sendJson(200, doc);
}

bool argBool(const char* name) {
  const String v = s_server.arg(name);
  return v == "1" || v == "true" || v == "on";
}

/** Whole string as a number within [lo, hi]. */
bool parseNumber(const String& text, double lo, double hi, double* out) {
  if (text.length() == 0) {
    return false;
  }
  char* end = nullptr;
  const double v = strtod(text.c_str(), &end);
  if (end == text.c_str() || *end != '\0' || !(v >= lo && v <= hi)) {
    return false;
  }
  *out = v;
  return true;
}

void handleSettingsPost() {
  // Validate everything first, so a bad field doesn't half-apply the form.
  const bool has_loc = s_server.hasArg("lat") || s_server.hasArg("lon");
  double lat = 0.0;
  double lon = 0.0;
  if (has_loc) {
    if (!parseNumber(s_server.arg("lat"), -90.0, 90.0, &lat)) {
      sendError(400, "Latitude must be a number between -90 and 90.");
      return;
    }
    if (!parseNumber(s_server.arg("lon"), -180.0, 180.0, &lon)) {
      sendError(400, "Longitude must be a number between -180 and 180.");
      return;
    }
  }
  double range = 0.0;
  if (s_server.hasArg("range") &&
      !parseNumber(s_server.arg("range"), 0,
                   ui::radar::kRangePresetCount - 1, &range)) {
    sendError(400, "Unknown range.");
    return;
  }
  double source = 0.0;
  if (s_server.hasArg("source") &&
      !parseNumber(s_server.arg("source"), 0,
                   services::settings::kFeedSourceCount - 1, &source)) {
    sendError(400, "Unknown data source.");
    return;
  }
  double labels = 0.0;
  if (s_server.hasArg("labels") &&
      !parseNumber(s_server.arg("labels"), 0,
                   services::settings::kMaxLabelsLimit, &labels)) {
    sendError(400, "Labels must be between 0 and 12.");
    return;
  }

  double wifi_power = 0.0;
  if (s_server.hasArg("wifi_power") &&
      !parseNumber(s_server.arg("wifi_power"), 0,
                   services::settings::kWifiPowerCount - 1, &wifi_power)) {
    sendError(400, "Unknown Wi-Fi power setting.");
    return;
  }

  if (has_loc) {
    // Re-saving the auto-detected spot untouched keeps it marked approximate.
    const bool unchanged =
        services::location::isSet() &&
        fabs(lat - services::location::lat()) < 1e-6 &&
        fabs(lon - services::location::lon()) < 1e-6;
    const bool approx = s_server.hasArg("loc_approx")
                            ? argBool("loc_approx")
                            : (unchanged && services::location::isApproximate());
    if (!unchanged || approx != services::location::isApproximate()) {
      services::location::save(lat, lon, approx);
    }
  }
  if (s_server.hasArg("range")) {
    ui::radar::rangeSet(static_cast<uint8_t>(range));
  }
  if (s_server.hasArg("miles")) {
    ui::radar::setUseMiles(argBool("miles"));
  }
  if (s_server.hasArg("runways")) {
    ui::radar::setShowRunways(argBool("runways"));
  }
  if (s_server.hasArg("ground")) {
    services::settings::setShowGround(argBool("ground"));
  }
  if (s_server.hasArg("source")) {
    services::settings::setFeedSource(
        static_cast<services::settings::FeedSource>(static_cast<uint8_t>(source)));
  }
  if (s_server.hasArg("labels")) {
    services::settings::setMaxLabels(static_cast<uint8_t>(labels));
  }
  if (s_server.hasArg("swap_rb")) {
    services::settings::setSwapRedBlue(argBool("swap_rb"));
    displaySetSwapRedBlue(services::settings::swapRedBlue());
  }
  if (s_server.hasArg("wifi_power")) {
    services::settings::setWifiPower(static_cast<services::settings::WifiPower>(
        static_cast<uint8_t>(wifi_power)));
    net::applyPowerSetting();
  }
  services::adsb::requestRefresh();

  JsonDocument doc;
  fillSettings(doc.to<JsonObject>());
  doc["ok"] = true;
  sendJson(200, doc);
}

void handleScan() {
  if (s_server.hasArg("start")) {
    net::scanStart();
  }
  JsonDocument doc;
  const int n = net::scanState();
  if (n == WIFI_SCAN_RUNNING) {
    doc["state"] = "scanning";
  } else if (n < 0) {
    doc["state"] = "idle";
  } else {
    doc["state"] = "done";
    JsonArray nets = doc["networks"].to<JsonArray>();
    // Strongest first, one row per name (mesh systems repeat theirs).
    constexpr int kMaxListed = 20;
    int order[kMaxListed];
    int count = 0;
    for (int i = 0; i < n; ++i) {
      const String ssid = WiFi.SSID(i);
      if (ssid.length() == 0) {
        continue;
      }
      int dup = -1;
      for (int k = 0; k < count; ++k) {
        if (WiFi.SSID(order[k]) == ssid) {
          dup = k;
          break;
        }
      }
      if (dup >= 0) {
        if (WiFi.RSSI(i) > WiFi.RSSI(order[dup])) {
          order[dup] = i;
        }
      } else if (count < kMaxListed) {
        order[count++] = i;
      }
    }
    for (int a = 1; a < count; ++a) {
      const int key = order[a];
      int b = a;
      while (b > 0 && WiFi.RSSI(order[b - 1]) < WiFi.RSSI(key)) {
        order[b] = order[b - 1];
        --b;
      }
      order[b] = key;
    }
    for (int k = 0; k < count; ++k) {
      JsonObject o = nets.add<JsonObject>();
      o["ssid"] = WiFi.SSID(order[k]);
      o["rssi"] = WiFi.RSSI(order[k]);
      o["open"] = WiFi.encryptionType(order[k]) == WIFI_AUTH_OPEN;
    }
  }
  sendJson(200, doc);
}

void handleWifiPost() {
  String ssid = s_server.arg("ssid");
  const String pass = s_server.arg("pass");
  ssid.trim();
  if (ssid.length() == 0 || ssid.length() > 32) {
    sendError(400, "Choose a network or type its name.");
    return;
  }
  if (pass.length() != 0 && (pass.length() < 8 || pass.length() > 63)) {
    sendError(400, "Wi-Fi passwords are 8 to 63 characters.");
    return;
  }
  net::queueConnect(ssid, pass);
  sendOk();
}

void handleLocate() {
  if (!net::online()) {
    sendError(409, "The radar needs to be on Wi-Fi first.");
    return;
  }
  double lat = 0.0;
  double lon = 0.0;
  char place[64];
  if (!services::geo::lookup(&lat, &lon, place, sizeof(place))) {
    sendError(502, "The location lookup didn't answer. Try again in a moment.");
    return;
  }
  JsonDocument doc;
  doc["ok"] = true;
  doc["lat"] = lat;
  doc["lon"] = lon;
  doc["place"] = place;
  sendJson(200, doc);
}

void handleRestart() {
  net::queueRestart();
  sendOk();
}

void handleFactoryReset() {
  net::queueFactoryReset();
  sendOk();
}

/** True when the request was aimed at this device rather than a probe URL. */
bool hostIsUs() {
  const String host = s_server.hostHeader();
  return host.length() == 0 || host == config::kPortalIp ||
         host.equalsIgnoreCase(config::kPortalHostUrl) ||
         host == WiFi.localIP().toString();
}

void handleNotFound() {
  // Phones probe a well-known URL after joining; answering with a redirect is
  // what makes them pop the "sign in to network" sheet with our page in it.
  if (s_captive && !hostIsUs()) {
    s_server.sendHeader("Location",
                        String("http://") + config::kPortalIp + "/", true);
    s_server.send(302, "text/plain", "");
    return;
  }
  sendError(404, "Not found");
}

void registerRoutes() {
  if (s_routes_ready) {
    return;
  }
  s_server.on("/", HTTP_GET, handleRoot);
  s_server.on("/api/status", HTTP_GET, handleStatus);
  s_server.on("/api/settings", HTTP_GET, handleSettingsGet);
  s_server.on("/api/settings", HTTP_POST, handleSettingsPost);
  s_server.on("/api/scan", HTTP_GET, handleScan);
  s_server.on("/api/wifi", HTTP_POST, handleWifiPost);
  s_server.on("/api/locate", HTTP_POST, handleLocate);
  s_server.on("/api/restart", HTTP_POST, handleRestart);
  s_server.on("/api/factory-reset", HTTP_POST, handleFactoryReset);
  s_server.onNotFound(handleNotFound);
  s_routes_ready = true;
}

void serverTask(void*) {
  for (;;) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool running = s_running;
    if (running) {
      if (s_captive) {
        s_dns.processNextRequest();
      }
      s_server.handleClient();
    }
    xSemaphoreGive(s_lock);
    vTaskDelay(pdMS_TO_TICKS(running ? 2 : 50));
  }
}

void ensureTask() {
  if (s_lock == nullptr) {
    s_lock = xSemaphoreCreateMutex();
  }
  if (s_task == nullptr) {
    xTaskCreate(serverTask, "web", kTaskStackBytes, nullptr, 1, &s_task);
  }
}

}  // namespace

void start(bool captive) {
  stop();
  ensureTask();
  registerRoutes();
  xSemaphoreTake(s_lock, portMAX_DELAY);
  s_captive = captive;
  if (captive) {
    s_dns.setErrorReplyCode(DNSReplyCode::NoError);
    s_dns.start(kDnsPort, "*", IPAddress(192, 168, 4, 1));
  }
  s_server.begin();
  s_running = true;
  xSemaphoreGive(s_lock);
}

void stop() {
  if (s_lock == nullptr) {
    return;
  }
  xSemaphoreTake(s_lock, portMAX_DELAY);
  if (s_running) {
    s_server.stop();
    if (s_captive) {
      s_dns.stop();
    }
    s_captive = false;
    s_running = false;
  }
  xSemaphoreGive(s_lock);
}

}  // namespace web
