#pragma once

#include <cstdint>

#include <driver/gpio.h>

namespace config {

constexpr char kFirmwareVersion[] = "2.0.1";
/** Sent with every outbound HTTP request. adsb.lol rejects the stock
 *  "ESP32HTTPClient" agent with 403, so this must stay a custom string. */
constexpr char kUserAgent[] = "PlaneRadar/2.0 (ESP32-C3)";

// --- Wi-Fi portal ---
constexpr char kPortalApName[] = "PlaneRadar-Setup";
constexpr char kPortalIp[] = "192.168.4.1";
/** mDNS host (no ".local" suffix); browser: http://plane-radar.local */
constexpr char kPortalHostname[] = "plane-radar";
constexpr char kPortalHostUrl[] = "plane-radar.local";

/** Per-attempt STA connect wait (ms); retried kWifiConnectAttempts times. */
constexpr unsigned long kWifiConnectAttemptMs = 12000;
constexpr uint8_t kWifiConnectAttempts = 3;
constexpr unsigned long kWifiConnectingFrameMs = 50;
/** Minimum interval between background reconnect tries. */
constexpr unsigned long kWifiReconnectIntervalMs = 20000;
/** While the setup portal is open with a saved network, retry it this often
 *  (only when no phone is attached to the setup AP). */
constexpr unsigned long kPortalRetrySavedMs = 45000;
/** How long the "Connected" / "Can't connect" result screens stay up. */
constexpr unsigned long kWifiResultScreenMs = 3500;
/**
 * Transmit power, in the driver's quarter-dBm units. The Super Mini's antenna
 * matching is poor and full power often *prevents* it from associating, so
 * 8.5 dBm is the usual workaround -- but that is too quiet to reach a distant
 * router. "Auto" starts low and steps up when the signal is weak.
 */
constexpr int kWifiTxPowerLow = 34;     // 8.5 dBm
constexpr int kWifiTxPowerMedium = 52;  // 13 dBm
constexpr int kWifiTxPowerHigh = 68;    // 17 dBm
/** Auto: at or below these signal levels (dBm), use medium / high power. */
constexpr int kWifiRssiMedium = -67;
constexpr int kWifiRssiHigh = -75;
/** Signal at or below this is reported as the likely cause of feed errors. */
constexpr int kWifiRssiWeak = -78;
/** How often Auto re-checks the signal. */
constexpr unsigned long kWifiPowerCheckMs = 30000;

// --- BOOT button (ESP32-C3 Super Mini, active LOW) ---
constexpr gpio_num_t kBootPin = GPIO_NUM_9;
/** Ignore BOOT taps shorter than this (debounce). */
constexpr unsigned long kBootTapMinMs = 40UL;
/** Release before this = tap (cycle range); after = info screen. */
constexpr unsigned long kBootInfoHoldMs = 700UL;
/** Hold this long to open the setup portal (settings are kept). */
constexpr unsigned long kBootSetupHoldMs = 3000UL;
/** Keep holding this long to erase everything. */
constexpr unsigned long kBootFactoryHoldMs = 10000UL;
/** Info screen stays up this long (tap dismisses it sooner). */
constexpr unsigned long kInfoScreenMs = 12000UL;

// --- Display: GC9A01 1.28" round 240×240 (SPI) ---
constexpr gpio_num_t kDisplayPinRst = GPIO_NUM_0;
constexpr gpio_num_t kDisplayPinCs = GPIO_NUM_1;
constexpr gpio_num_t kDisplayPinDc = GPIO_NUM_10;
constexpr gpio_num_t kDisplayPinMosi = GPIO_NUM_3;  // display SDA
constexpr gpio_num_t kDisplayPinSclk = GPIO_NUM_4;  // display SCL

constexpr int kDisplayWidth = 240;
constexpr int kDisplayHeight = 240;

constexpr uint32_t kDisplaySpiWriteHz = 40000000;
// GC9A01 modules often need invert + BGR for correct black/green output
constexpr bool kDisplayInvert = true;
constexpr bool kDisplayRgbOrder = true;

// --- Radar center defaults (overridden via WiFi setup portal) ---
constexpr double kDefaultRadarLat = 52.3676;
constexpr double kDefaultRadarLon = 4.9041;

/** Pause between ADS-B polls (public API limit: 1 req/s). */
constexpr unsigned long kAdsbFetchIntervalMs = 5000;
/** Drop the aircraft list when no fetch has succeeded for this long, so stale
 *  targets are not dead-reckoned across the screen forever. */
constexpr unsigned long kAdsbStaleMs = 45000;
/** In "Auto" source mode, switch feeds after this many failures in a row. */
constexpr uint8_t kAdsbFailoverAfter = 3;
/** Redraw cadence; aircraft are dead-reckoned along their track/speed so motion
 *  is smooth. ~4 Hz = 250 ms (panel scanout caps at ~24 Hz; the full-frame
 *  recompose+present is the practical limit ~10-15 Hz). */
constexpr unsigned long kRadarRedrawIntervalMs = 250;

}  // namespace config
