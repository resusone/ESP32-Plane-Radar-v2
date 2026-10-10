#pragma once

#include <cstdint>

/**
 * Full-screen status pages, drawn straight to the panel. Every line is fitted
 * to the round display: text is shrunk (then clipped) to the width of the
 * circle at its row.
 */

void statusScreenSplash(const char* version);

/**
 * Setup portal: a QR code that joins the setup network, plus the network name
 * and address for doing it by hand. `note` (optional) replaces the heading,
 * e.g. why the portal opened.
 */
void statusScreenPortal(const char* ap_name, const char* ip, const char* note);

/** Saved-network connect animation (call Tick until connect finishes). */
void statusScreenConnectingBegin(const char* ssid, uint8_t attempt,
                                 uint8_t attempts);
void statusScreenConnectingTick();

void statusScreenConnected(const char* ssid, const char* ip, const char* host);
void statusScreenConnectFailed(const char* ssid, const char* reason);

/**
 * Shown while BOOT is held past the setup threshold. `erase_progress` runs
 * 0..1 toward the factory-reset threshold and fills a ring around the rim.
 */
void statusScreenHoldBegin();
void statusScreenHoldProgress(float erase_progress);

/** Title plus up to two lines ("Restarting...", "Finding location", ...). */
void statusScreenMessage(const char* title, const char* line1,
                         const char* line2);

struct InfoScreenData {
  const char* version = "";
  bool online = false;
  const char* ssid = "";
  int rssi_dbm = 0;
  const char* ip = "";
  const char* host = "";
  /** e.g. "12 aircraft" or the feed error. */
  const char* feed_headline = "";
  bool feed_ok = false;
  /** e.g. "adsb.fi, 3 s ago". */
  const char* feed_detail = "";
  /** e.g. "52.3676 N 4.9041 E". */
  const char* location = "";
  /** e.g. "Range 50 km". */
  const char* range = "";
};

/** Device summary (BOOT held ~1 s). */
void statusScreenInfo(const InfoScreenData& info);
