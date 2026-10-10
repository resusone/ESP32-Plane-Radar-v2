#pragma once

namespace ui {

/** Reserve the off-screen frame buffer. Call early in setup(), while the heap
 *  still has a large free block (before Wi-Fi and TLS fragment it). */
void radarDisplayPrepare();

/** Draw the static sonar/radar grid (black disc, green overlay, labels). */
void radarDisplayDraw();

/** Redraw aircraft only (blits cached grid; no full-screen clear). */
void radarDisplayRefreshAircraft();

/** Tell the radar whether Wi-Fi is up and how strong it is (dBm), for the
 *  status line. */
void radarDisplaySetLink(bool online, int rssi_dbm);

}  // namespace ui
