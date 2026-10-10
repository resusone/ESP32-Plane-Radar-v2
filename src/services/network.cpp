#include "services/network.h"

#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFi.h>

#include <esp_system.h>
#include <esp_wifi.h>

#include <cstdio>
#include <cstring>

#include "config.h"
#include "hardware/boot_button.h"
#include "services/radar_location.h"
#include "services/settings.h"
#include "services/web_portal.h"
#include "ui/radar_range.h"
#include "ui/status_screens.h"

namespace net {

namespace {

constexpr char kPrefsNamespace[] = "wifi";
constexpr char kKeySsid[] = "ssid";
constexpr char kKeyPass[] = "pass";

/** Let the HTTP reply reach the browser before the radio is reconfigured. */
constexpr unsigned long kDeferredActionDelayMs = 900;
/** How long one background retry of the saved network may run. */
constexpr unsigned long kPortalRetryWindowMs = 12000;

Mode s_mode = Mode::kPortal;
String s_ssid;
String s_pass;
char s_note[40] = "";

/** Reason code of the latest STA disconnect (set from the Wi-Fi event task). */
volatile uint8_t s_disconnect_reason = 0;
volatile uint8_t s_auth_failures = 0;
bool s_events_registered = false;

enum class Deferred : uint8_t { kNone, kConnect, kRestart, kFactoryReset };
Deferred s_deferred = Deferred::kNone;
unsigned long s_deferred_at_ms = 0;
String s_pending_ssid;
String s_pending_pass;

unsigned long s_last_reconnect_ms = 0;

/** Portal opened because the saved network failed: keep retrying it. */
bool s_portal_auto_retry = false;
bool s_portal_retry_running = false;
unsigned long s_portal_retry_started_ms = 0;
unsigned long s_portal_last_retry_ms = 0;

bool linkUp() {
  return WiFi.status() == WL_CONNECTED &&
         WiFi.localIP() != IPAddress(0, 0, 0, 0);
}

bool isAuthFailure(uint8_t reason) {
  return reason == WIFI_REASON_AUTH_FAIL ||
         reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
         reason == WIFI_REASON_HANDSHAKE_TIMEOUT ||
         reason == WIFI_REASON_AUTH_EXPIRE;
}

void onWifiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    const uint8_t reason = info.wifi_sta_disconnected.reason;
    s_disconnect_reason = reason;
    if (isAuthFailure(reason) && s_auth_failures < 255) {
      s_auth_failures = s_auth_failures + 1;
    }
  }
}

void registerEvents() {
  if (!s_events_registered) {
    WiFi.onEvent(onWifiEvent);
    s_events_registered = true;
  }
}

const char* failureReason() {
  const uint8_t reason = s_disconnect_reason;
  if (reason == WIFI_REASON_NO_AP_FOUND) {
    return "Network not found";
  }
  if (isAuthFailure(reason)) {
    return "Wrong password?";
  }
  return "No response";
}

int s_tx_power = config::kWifiTxPowerLow;
unsigned long s_last_power_check_ms = 0;

void setTxPower(int quarter_dbm) {
  s_tx_power = quarter_dbm;
  WiFi.setTxPower(static_cast<wifi_power_t>(quarter_dbm));
}

/** Power for the user's setting; Auto picks by how well the router is heard. */
int txPowerForSignal(int rssi_dbm) {
  switch (services::settings::wifiPower()) {
    case services::settings::WifiPower::kLow:
      return config::kWifiTxPowerLow;
    case services::settings::WifiPower::kHigh:
      return config::kWifiTxPowerHigh;
    case services::settings::WifiPower::kAuto:
      break;
  }
  if (rssi_dbm <= config::kWifiRssiHigh) {
    return config::kWifiTxPowerHigh;
  }
  if (rssi_dbm <= config::kWifiRssiMedium) {
    return config::kWifiTxPowerMedium;
  }
  return config::kWifiTxPowerLow;
}

/**
 * Power while still trying to join, when the signal isn't known yet. Auto
 * starts quiet (what the Super Mini's antenna likes) and gets louder with
 * each attempt, in case the router simply can't hear it.
 */
int txPowerForAttempt(uint8_t attempt) {
  switch (services::settings::wifiPower()) {
    case services::settings::WifiPower::kLow:
      return config::kWifiTxPowerLow;
    case services::settings::WifiPower::kHigh:
      return config::kWifiTxPowerHigh;
    case services::settings::WifiPower::kAuto:
      break;
  }
  return attempt <= 1   ? config::kWifiTxPowerLow
         : attempt == 2 ? config::kWifiTxPowerMedium
                        : config::kWifiTxPowerHigh;
}

/** Follow the signal (and the setting) while connected. */
void adjustTxPower() {
  const int wanted = txPowerForSignal(WiFi.RSSI());
  if (wanted != s_tx_power) {
    Serial.printf("WiFi: signal %d dBm, transmit power -> %.1f dBm\n",
                  WiFi.RSSI(), wanted / 4.0f);
    setTxPower(wanted);
  }
}

void saveCredentials(const String& ssid, const String& pass) {
  s_ssid = ssid;
  s_pass = pass;
  Preferences prefs;
  if (prefs.begin(kPrefsNamespace, false)) {
    prefs.putString(kKeySsid, ssid);
    prefs.putString(kKeyPass, pass);
    prefs.end();
  }
}

/**
 * The v1 firmware left its credentials in the Wi-Fi driver's own storage.
 * Pick them up so an upgrade doesn't ask for the password again.
 */
bool importDriverCredentials() {
  WiFi.mode(WIFI_STA);
  delay(50);
  wifi_config_t conf = {};
  if (esp_wifi_get_config(WIFI_IF_STA, &conf) != ESP_OK ||
      conf.sta.ssid[0] == '\0') {
    return false;
  }
  // Fixed-size fields: a maximum-length value has no terminator.
  char ssid[sizeof(conf.sta.ssid) + 1] = {};
  memcpy(ssid, conf.sta.ssid, sizeof(conf.sta.ssid));
  char pass[sizeof(conf.sta.password) + 1] = {};
  memcpy(pass, conf.sta.password, sizeof(conf.sta.password));
  saveCredentials(String(ssid), String(pass));
  Serial.printf("WiFi: imported saved network \"%s\"\n", ssid);
  return true;
}

void loadCredentials() {
  Preferences prefs;
  if (prefs.begin(kPrefsNamespace, true)) {
    s_ssid = prefs.getString(kKeySsid, "");
    s_pass = prefs.getString(kKeyPass, "");
    prefs.end();
  }
  if (s_ssid.length() == 0) {
    importDriverCredentials();
  }
}

void startMdns() {
  MDNS.end();
  if (MDNS.begin(config::kPortalHostname)) {
    MDNS.addService("http", "tcp", 80);
  }
}

void stopServices() {
  web::stop();
  MDNS.end();
}

void beginStation(const String& ssid, const String& pass, int tx_power) {
  WiFi.setHostname(config::kPortalHostname);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid.c_str(), pass.length() > 0 ? pass.c_str() : nullptr);
  setTxPower(tx_power);
}

/**
 * Join with the spinner on screen. Returns once connected, out of attempts,
 * clearly rejected (bad password), or BOOT is held for setup.
 */
bool connectBlocking(const String& ssid, const String& pass) {
  stopServices();
  WiFi.softAPdisconnect(true);

  for (uint8_t attempt = 1; attempt <= config::kWifiConnectAttempts; ++attempt) {
    Serial.printf("WiFi: connecting to \"%s\" (%u/%u)\n", ssid.c_str(), attempt,
                  config::kWifiConnectAttempts);
    statusScreenConnectingBegin(ssid.c_str(), attempt,
                                config::kWifiConnectAttempts);
    s_disconnect_reason = 0;
    s_auth_failures = 0;
    if (attempt > 1) {
      WiFi.disconnect(true);
      delay(300);
    }
    beginStation(ssid, pass, txPowerForAttempt(attempt));

    const unsigned long started = millis();
    while (millis() - started < config::kWifiConnectAttemptMs) {
      if (linkUp()) {
        return true;
      }
      if (s_auth_failures >= 2) {
        Serial.println("WiFi: rejected (password?)");
        return false;  // retrying a wrong password only wastes time
      }
      if (bootButtonHeldMs() >= config::kBootSetupHoldMs) {
        return false;
      }
      statusScreenConnectingTick();
      delay(config::kWifiConnectingFrameMs);
    }
  }
  return linkUp();
}

void enterStation() {
  s_mode = Mode::kStation;
  s_note[0] = '\0';
  s_portal_auto_retry = false;
  s_portal_retry_running = false;
  s_last_reconnect_ms = millis();
  s_last_power_check_ms = millis();
  WiFi.setAutoReconnect(true);
  adjustTxPower();
  startMdns();
  web::start(false);

  const String ip = WiFi.localIP().toString();
  Serial.printf("WiFi: connected to \"%s\", http://%s or http://%s\n",
                WiFi.SSID().c_str(), ip.c_str(), config::kPortalHostUrl);
  statusScreenConnected(WiFi.SSID().c_str(), ip.c_str(),
                        config::kPortalHostUrl);
  delay(config::kWifiResultScreenMs);
}

void openPortal(const char* note, bool auto_retry) {
  stopServices();
  s_mode = Mode::kPortal;
  snprintf(s_note, sizeof(s_note), "%s", note != nullptr ? note : "");
  s_portal_auto_retry = auto_retry && s_ssid.length() > 0;
  s_portal_retry_running = false;
  s_portal_last_retry_ms = millis();

  // AP+STA so the page can scan for networks while a phone is attached. The
  // station side stays idle: a radio hunting for a missing network hops
  // channels and takes the access point with it.
  WiFi.setAutoReconnect(false);
  WiFi.disconnect();
  WiFi.mode(WIFI_AP_STA);
  const IPAddress ip(192, 168, 4, 1);
  WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0));
  WiFi.softAP(config::kPortalApName);
  delay(100);
  // The phone doing setup is right beside the radar; quiet is plenty.
  setTxPower(config::kWifiTxPowerLow);

  startMdns();
  web::start(true);
  Serial.printf("WiFi: setup portal \"%s\" at http://%s%s%s\n",
                config::kPortalApName, config::kPortalIp,
                s_note[0] != '\0' ? " - " : "", s_note);
  statusScreenPortal(config::kPortalApName, config::kPortalIp, s_note);
}

/** Connect; on success save the credentials, on failure reopen the portal. */
void connectOrPortal(const String& ssid, const String& pass, bool save) {
  if (connectBlocking(ssid, pass)) {
    if (save) {
      saveCredentials(ssid, pass);
    }
    enterStation();
    return;
  }

  char note[sizeof(s_note)];
  if (bootButtonHeldMs() >= config::kBootSetupHoldMs) {
    note[0] = '\0';  // the user asked for setup; nothing went wrong
  } else {
    const char* reason = failureReason();
    Serial.printf("WiFi: could not join \"%s\" (%s)\n", ssid.c_str(), reason);
    statusScreenConnectFailed(ssid.c_str(), reason);
    delay(config::kWifiResultScreenMs);
    snprintf(note, sizeof(note), "Can't join %s", ssid.c_str());
  }
  openPortal(note, true);
}

/** While the portal waits, quietly retry the saved network now and then. */
void servicePortalRetry() {
  if (!s_portal_auto_retry) {
    return;
  }
  const bool phone_attached = WiFi.softAPgetStationNum() > 0;

  if (s_portal_retry_running) {
    if (linkUp()) {
      Serial.println("WiFi: saved network is back");
      stopServices();
      WiFi.softAPdisconnect(true);
      WiFi.mode(WIFI_STA);
      enterStation();
      return;
    }
    if (phone_attached ||
        millis() - s_portal_retry_started_ms > kPortalRetryWindowMs) {
      WiFi.disconnect();  // station side only; the AP stays up
      s_portal_retry_running = false;
      s_portal_last_retry_ms = millis();
    }
    return;
  }

  if (!phone_attached && WiFi.scanComplete() != WIFI_SCAN_RUNNING &&
      millis() - s_portal_last_retry_ms > config::kPortalRetrySavedMs) {
    WiFi.begin(s_ssid.c_str(), s_pass.length() > 0 ? s_pass.c_str() : nullptr);
    s_portal_retry_running = true;
    s_portal_retry_started_ms = millis();
  }
}

void serviceStation() {
  if (linkUp()) {
    s_last_reconnect_ms = millis();
    if (millis() - s_last_power_check_ms > config::kWifiPowerCheckMs) {
      s_last_power_check_ms = millis();
      adjustTxPower();
    }
    return;
  }
  // The driver reconnects by itself; this is the nudge for when it gives up.
  if (millis() - s_last_reconnect_ms > config::kWifiReconnectIntervalMs) {
    s_last_reconnect_ms = millis();
    Serial.println("WiFi: lost, reconnecting");
    WiFi.disconnect();
    WiFi.begin(s_ssid.c_str(), s_pass.length() > 0 ? s_pass.c_str() : nullptr);
    // Dropping off is itself a sign the router can't hear us.
    if (services::settings::wifiPower() == services::settings::WifiPower::kAuto) {
      setTxPower(config::kWifiTxPowerHigh);
    }
  }
}

void runDeferred() {
  if (s_deferred == Deferred::kNone ||
      static_cast<long>(millis() - s_deferred_at_ms) < 0) {
    return;
  }
  const Deferred action = s_deferred;
  s_deferred = Deferred::kNone;

  switch (action) {
    case Deferred::kConnect: {
      const String ssid = s_pending_ssid;
      const String pass = s_pending_pass;
      s_pending_ssid = "";
      s_pending_pass = "";
      connectOrPortal(ssid, pass, true);
      break;
    }
    case Deferred::kRestart:
      statusScreenMessage("Restarting", "Back in a moment", nullptr);
      delay(600);
      esp_restart();
      break;
    case Deferred::kFactoryReset:
      factoryResetNow();
      break;
    case Deferred::kNone:
      break;
  }
}

void defer(Deferred action) {
  s_deferred = action;
  s_deferred_at_ms = millis() + kDeferredActionDelayMs;
}

}  // namespace

void begin() {
  registerEvents();
  loadCredentials();

  if (s_ssid.length() == 0) {
    Serial.println("WiFi: nothing saved, opening setup portal");
    openPortal(nullptr, false);
    return;
  }
  connectOrPortal(s_ssid, s_pass, false);
}

void loop() {
  runDeferred();
  if (s_mode == Mode::kPortal) {
    servicePortalRetry();
  } else {
    serviceStation();
  }
}

float txPowerDbm() { return s_tx_power / 4.0f; }

void applyPowerSetting() {
  if (s_mode == Mode::kStation && linkUp()) {
    adjustTxPower();
  }
}

Mode mode() { return s_mode; }

bool online() { return linkUp(); }

bool hasSavedNetwork() { return s_ssid.length() > 0; }

const String& savedSsid() { return s_ssid; }

const char* portalNote() { return s_note; }

void openPortalByUser() { openPortal(nullptr, false); }

void cancelPortal() {
  if (s_mode != Mode::kPortal || s_ssid.length() == 0) {
    return;
  }
  connectOrPortal(s_ssid, s_pass, false);
}

void queueConnect(const String& ssid, const String& pass) {
  s_pending_ssid = ssid;
  s_pending_pass = pass;
  defer(Deferred::kConnect);
}

void queueRestart() { defer(Deferred::kRestart); }

void queueFactoryReset() { defer(Deferred::kFactoryReset); }

void factoryResetNow() {
  Serial.println("Factory reset");
  stopServices();
  Preferences prefs;
  if (prefs.begin(kPrefsNamespace, false)) {
    prefs.clear();
    prefs.end();
  }
  services::location::clear();
  services::settings::reset();
  ui::radar::unitsReset();
  WiFi.disconnect(true, true);  // also wipes the driver's own copy
  statusScreenMessage("Erased", "Restarting...", nullptr);
  delay(1200);
  esp_restart();
}

void scanStart() {
  if (WiFi.scanComplete() == WIFI_SCAN_RUNNING) {
    return;
  }
  if (s_mode == Mode::kPortal && s_portal_retry_running) {
    WiFi.disconnect();  // a scan cannot start while the station is joining
    s_portal_retry_running = false;
    s_portal_last_retry_ms = millis();
  }
  WiFi.scanDelete();
  WiFi.scanNetworks(true /* async */, false /* hidden */);
}

int scanState() { return WiFi.scanComplete(); }

}  // namespace net
