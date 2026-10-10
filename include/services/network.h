#pragma once

#include <Arduino.h>

/**
 * Wi-Fi: joins the saved network, or runs the setup access point with its
 * captive web portal. Connect attempts block (with an on-screen spinner);
 * everything else is serviced from loop().
 */
namespace net {

enum class Mode : uint8_t {
  kStation,  // on (or trying to rejoin) the saved network
  kPortal,   // setup access point is up
};

/** Boot: connect with the saved network, else open the setup portal. */
void begin();
/** Call every loop() iteration. */
void loop();

Mode mode();
/** Joined a network and holding an address. */
bool online();

bool hasSavedNetwork();
const String& savedSsid();
/** Why the portal opened ("Can't join HomeNet"), or "". */
const char* portalNote();

/** Open the setup portal on request (settings and saved Wi-Fi are kept). */
void openPortalByUser();
/** Leave a user-opened portal and go back to the saved network. */
void cancelPortal();

/**
 * Join `ssid` shortly (after the web reply has gone out). The credentials are
 * saved only once they work; on failure the portal reopens.
 */
void queueConnect(const String& ssid, const String& pass);

/** Restart shortly (after the web reply has gone out). */
void queueRestart();
/** Erase Wi-Fi, location and settings, then restart. */
void queueFactoryReset();
void factoryResetNow();

/** Current transmit power (dBm). */
float txPowerDbm();
/** Re-apply the Wi-Fi power setting after it changed. */
void applyPowerSetting();

/** Network scan for the portal: start one, then poll. */
void scanStart();
/** WiFi.scanComplete() semantics: >= 0 results, -1 running, -2 none. */
int scanState();

}  // namespace net
