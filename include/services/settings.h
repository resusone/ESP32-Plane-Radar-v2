#pragma once

#include <cstdint>

/** Small persisted options that don't belong to range/location. */
namespace services::settings {

enum class FeedSource : uint8_t {
  kAuto = 0,     // adsb.fi, failing over to adsb.lol and back
  kAdsbFi = 1,
  kAdsbLol = 2,
};
constexpr uint8_t kFeedSourceCount = 3;

constexpr uint8_t kMaxLabelsLimit = 12;
constexpr uint8_t kDefaultMaxLabels = 6;

void init();

/** Show aircraft reporting "ground" (taxiing / parked). */
bool showGround();
void setShowGround(bool show);

FeedSource feedSource();
void setFeedSource(FeedSource source);

/** Most aircraft that get a callsign/type/altitude tag (nearest first). */
uint8_t maxLabels();
void setMaxLabels(uint8_t count);

enum class WifiPower : uint8_t {
  kAuto = 0,  // low, stepping up while the signal is weak
  kLow = 1,
  kHigh = 2,
};
constexpr uint8_t kWifiPowerCount = 3;

WifiPower wifiPower();
void setWifiPower(WifiPower power);

/** Exchange red and blue when drawing (see hardware/display.h). */
bool swapRedBlue();
void setSwapRedBlue(bool swap);

/** Back to defaults (factory reset). */
void reset();

}  // namespace services::settings
