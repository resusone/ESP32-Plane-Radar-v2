#include "services/settings.h"

#include <Preferences.h>

#include "config.h"

namespace services::settings {

namespace {

constexpr char kPrefsNamespace[] = "planeradar";
constexpr char kKeyGround[] = "showGnd";
constexpr char kKeySource[] = "feedSrc";
constexpr char kKeyLabels[] = "maxLbl";
constexpr char kKeySwap[] = "swapRB";
constexpr char kKeyPower[] = "wifiPwr";

bool s_show_ground = false;
FeedSource s_source = FeedSource::kAuto;
uint8_t s_max_labels = kDefaultMaxLabels;
bool s_swap_red_blue = config::kDisplayRgbOrder;
WifiPower s_wifi_power = WifiPower::kAuto;

}  // namespace

void init() {
  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, true)) {
    return;
  }
  s_show_ground = prefs.getBool(kKeyGround, false);
  const uint8_t src = prefs.getUChar(kKeySource, 0);
  s_source = src < kFeedSourceCount ? static_cast<FeedSource>(src)
                                    : FeedSource::kAuto;
  const uint8_t labels = prefs.getUChar(kKeyLabels, kDefaultMaxLabels);
  s_max_labels = labels <= kMaxLabelsLimit ? labels : kDefaultMaxLabels;
  s_swap_red_blue = prefs.getBool(kKeySwap, config::kDisplayRgbOrder);
  const uint8_t power = prefs.getUChar(kKeyPower, 0);
  s_wifi_power = power < kWifiPowerCount ? static_cast<WifiPower>(power)
                                         : WifiPower::kAuto;
  prefs.end();
}

bool showGround() { return s_show_ground; }

void setShowGround(bool show) {
  if (show == s_show_ground) {
    return;
  }
  s_show_ground = show;
  Preferences prefs;
  if (prefs.begin(kPrefsNamespace, false)) {
    prefs.putBool(kKeyGround, show);
    prefs.end();
  }
}

FeedSource feedSource() { return s_source; }

void setFeedSource(FeedSource source) {
  if (source == s_source ||
      static_cast<uint8_t>(source) >= kFeedSourceCount) {
    return;
  }
  s_source = source;
  Preferences prefs;
  if (prefs.begin(kPrefsNamespace, false)) {
    prefs.putUChar(kKeySource, static_cast<uint8_t>(source));
    prefs.end();
  }
}

uint8_t maxLabels() { return s_max_labels; }

void setMaxLabels(uint8_t count) {
  if (count > kMaxLabelsLimit) {
    count = kMaxLabelsLimit;
  }
  if (count == s_max_labels) {
    return;
  }
  s_max_labels = count;
  Preferences prefs;
  if (prefs.begin(kPrefsNamespace, false)) {
    prefs.putUChar(kKeyLabels, count);
    prefs.end();
  }
}

bool swapRedBlue() { return s_swap_red_blue; }

void setSwapRedBlue(bool swap) {
  if (swap == s_swap_red_blue) {
    return;
  }
  s_swap_red_blue = swap;
  Preferences prefs;
  if (prefs.begin(kPrefsNamespace, false)) {
    prefs.putBool(kKeySwap, swap);
    prefs.end();
  }
}

WifiPower wifiPower() { return s_wifi_power; }

void setWifiPower(WifiPower power) {
  if (power == s_wifi_power ||
      static_cast<uint8_t>(power) >= kWifiPowerCount) {
    return;
  }
  s_wifi_power = power;
  Preferences prefs;
  if (prefs.begin(kPrefsNamespace, false)) {
    prefs.putUChar(kKeyPower, static_cast<uint8_t>(power));
    prefs.end();
  }
}

void reset() {
  s_show_ground = false;
  s_source = FeedSource::kAuto;
  s_max_labels = kDefaultMaxLabels;
  s_swap_red_blue = config::kDisplayRgbOrder;
  s_wifi_power = WifiPower::kAuto;
  Preferences prefs;
  if (prefs.begin(kPrefsNamespace, false)) {
    prefs.remove(kKeyPower);
    prefs.remove(kKeySwap);
    prefs.remove(kKeyGround);
    prefs.remove(kKeySource);
    prefs.remove(kKeyLabels);
    prefs.end();
  }
}

}  // namespace services::settings
