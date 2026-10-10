/**
 * Plane Radar — Wi-Fi setup, then a live ADS-B radar on the round GC9A01.
 */

#include <Arduino.h>
#include <WiFi.h>

#include <cmath>

#include "config.h"
#include "hardware/boot_button.h"
#include "hardware/display.h"
#include "services/adsb_client.h"
#include "services/geo_ip.h"
#include "services/network.h"
#include "services/radar_location.h"
#include "services/settings.h"
#include "ui/radar_display.h"
#include "ui/radar_range.h"
#include "ui/status_screens.h"

namespace {

/** The info screen repaints whole, so keep refreshes sparse (no flicker). */
constexpr unsigned long kInfoRedrawMs = 4000;

unsigned long g_last_redraw_ms = 0;
/** Info screen is up while millis() - g_info_since_ms < kInfoScreenMs. */
bool g_info_visible = false;
unsigned long g_info_since_ms = 0;
unsigned long g_last_info_draw_ms = 0;
bool g_auto_locate_tried = false;

float fetchRadiusKm() { return ui::radar::fetchRadiusKm(); }

void drawInfoScreen() {
  const services::adsb::Status st = services::adsb::status();
  const bool online = net::online();

  const String ssid = online ? WiFi.SSID() : net::savedSsid();
  const String ip = online ? WiFi.localIP().toString() : String();

  char headline[28];
  char detail[40];
  const bool feed_ok = st.ever_ok && (st.last_ok ||
                                      millis() - st.last_success_ms < 15000UL);
  if (!st.attempted) {
    snprintf(headline, sizeof(headline), "Waiting for data");
  } else if (feed_ok) {
    snprintf(headline, sizeof(headline), "%u aircraft",
             static_cast<unsigned>(st.shown));
  } else {
    snprintf(headline, sizeof(headline), "%s",
             st.error[0] != '\0' ? st.error : "No data");
  }
  const char* source = services::adsb::sourceName(st.source);
  if (st.ever_ok) {
    const unsigned long age_s = (millis() - st.last_success_ms) / 1000UL;
    if (age_s < 120) {
      snprintf(detail, sizeof(detail), "%s, %lu s ago", source, age_s);
    } else {
      snprintf(detail, sizeof(detail), "%s, %lu min ago", source, age_s / 60);
    }
  } else {
    snprintf(detail, sizeof(detail), "%s, no reply yet", source);
  }

  const double lat = services::location::lat();
  const double lon = services::location::lon();
  char location[40];
  if (!services::location::isSet()) {
    snprintf(location, sizeof(location), "Location not set");
  } else {
    snprintf(location, sizeof(location),
             services::location::isApproximate() ? "~%.2f %c %.2f %c"
                                                 : "%.4f %c %.4f %c",
             fabs(lat), lat < 0 ? 'S' : 'N', fabs(lon), lon < 0 ? 'W' : 'E');
  }

  char ring[12];
  ui::radar::formatDistance(ring, sizeof(ring),
                            ui::radar::rangeCurrent().ring3_km);
  char range[24];
  snprintf(range, sizeof(range), "Range %s", ring);

  InfoScreenData info;
  info.version = config::kFirmwareVersion;
  info.online = online;
  info.ssid = ssid.c_str();
  info.rssi_dbm = online ? WiFi.RSSI() : 0;
  info.ip = ip.c_str();
  info.host = config::kPortalHostUrl;
  info.feed_headline = headline;
  info.feed_ok = feed_ok || !st.attempted;
  info.feed_detail = detail;
  info.location = location;
  info.range = range;
  statusScreenInfo(info);
}

/** BOOT held past the setup threshold: setup portal, or erase if held on. */
void handleLongHold() {
  statusScreenHoldBegin();
  const float span = static_cast<float>(config::kBootFactoryHoldMs -
                                        config::kBootSetupHoldMs);
  while (bootButtonPressed()) {
    const unsigned long held = bootButtonHeldMs();
    if (held >= config::kBootFactoryHoldMs) {
      net::factoryResetNow();  // restarts; does not return
    }
    if (held > config::kBootSetupHoldMs) {
      statusScreenHoldProgress(
          static_cast<float>(held - config::kBootSetupHoldMs) / span);
    }
    delay(20);
  }
  // The release interrupt saw a long press, so it latched nothing; clear any
  // tap that predates the hold.
  bootButtonConsumeTap();
  bootButtonConsumeInfoPress();
  g_info_visible = false;
  net::openPortalByUser();
}

/**
 * First connection with no location saved: start from a rough IP-based fix so
 * the radar shows the right part of the world straight away. The settings
 * page flags it as approximate.
 */
void autoLocateOnce() {
  g_auto_locate_tried = true;
  statusScreenMessage("Finding you", "Looking up your", "approximate location");
  double lat = 0.0;
  double lon = 0.0;
  char place[48];
  if (services::geo::lookup(&lat, &lon, place, sizeof(place)) &&
      services::location::save(lat, lon, true)) {
    statusScreenMessage("Near", place[0] != '\0' ? place : "your area",
                        "Fine-tune on the web page");
    delay(2500);
    services::adsb::requestRefresh();
  }
}

void redrawRadarNow() {
  g_last_redraw_ms = millis();
  ui::radarDisplayRefreshAircraft();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.printf("Plane Radar %s\n", config::kFirmwareVersion);

  bootButtonInit();
  services::settings::init();
  displaySetSwapRedBlue(services::settings::swapRedBlue());
  displayInit();
  statusScreenSplash(config::kFirmwareVersion);

  ui::radarDisplayPrepare();
  services::location::init();
  ui::radar::rangeInit();
  services::adsb::init();
  delay(900);  // let the splash register

  net::begin();
  services::adsb::startTask(fetchRadiusKm);
}

void loop() {
  if (bootButtonHeldMs() >= config::kBootSetupHoldMs) {
    handleLongHold();
  }
  const bool tap = bootButtonConsumeTap();
  const bool info_press = bootButtonConsumeInfoPress();

  net::loop();

  if (net::mode() == net::Mode::kPortal) {
    // The setup screen is static. A tap backs out to the saved network.
    g_info_visible = false;
    if (tap && net::hasSavedNetwork()) {
      net::cancelPortal();
    }
    delay(5);
    return;
  }

  const bool online = net::online();
  ui::radarDisplaySetLink(online, online ? WiFi.RSSI() : 0);

  if (online && !services::location::isSet() && !g_auto_locate_tried) {
    autoLocateOnce();
  }

  if (info_press) {
    g_info_visible = true;
    g_info_since_ms = millis();
    g_last_info_draw_ms = 0;
  }
  if (tap) {
    if (g_info_visible) {
      g_info_visible = false;
    } else {
      ui::radar::rangeNext();
      char label[12];
      ui::radar::formatCurrentRing3Label(label, sizeof(label));
      Serial.printf("Range: %s (fetch %.0f km)\n", label, fetchRadiusKm());
      services::adsb::requestRefresh();
    }
    redrawRadarNow();
  }
  if (g_info_visible && millis() - g_info_since_ms >= config::kInfoScreenMs) {
    g_info_visible = false;
    redrawRadarNow();
  }

  if (g_info_visible) {
    if (g_last_info_draw_ms == 0 ||
        millis() - g_last_info_draw_ms >= kInfoRedrawMs) {
      g_last_info_draw_ms = millis();
      drawInfoScreen();
    }
  } else if (millis() - g_last_redraw_ms >= config::kRadarRedrawIntervalMs) {
    // 4 Hz with dead-reckoned positions; fetching happens on its own task.
    redrawRadarNow();
  }

  delay(5);
}
