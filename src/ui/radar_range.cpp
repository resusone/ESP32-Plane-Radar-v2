#include "ui/radar_range.h"

#include "ui/radar_theme.h"

#include <Arduino.h>
#include <Preferences.h>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace ui::radar {

namespace {

constexpr char kPrefsNamespace[] = "planeradar";
constexpr char kPrefsRangeKey[] = "rangeIdx";
constexpr char kPrefsMilesKey[] = "useMiles";
constexpr char kPrefsRunwaysKey[] = "showRwys";
constexpr uint8_t kDefaultRangeIndex = 4;  // 50 km ring
constexpr float kKmPerMile = 1.609344f;

uint8_t s_range_index = kDefaultRangeIndex;
bool s_use_miles = false;
bool s_show_runways = true;

void saveRangeIndex() {
  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, false)) {
    return;
  }
  prefs.putUChar(kPrefsRangeKey, s_range_index);
  prefs.end();
}

void saveUseMiles() {
  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, false)) {
    return;
  }
  prefs.putBool(kPrefsMilesKey, s_use_miles);
  prefs.end();
}

void saveShowRunways() {
  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, false)) {
    return;
  }
  prefs.putBool(kPrefsRunwaysKey, s_show_runways);
  prefs.end();
}

}  // namespace

void rangeInit() {
  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, true)) {
    return;
  }
  const uint8_t saved = prefs.getUChar(kPrefsRangeKey, kDefaultRangeIndex);
  s_range_index =
      (saved < kRangePresetCount) ? saved : kDefaultRangeIndex;
  s_use_miles = prefs.getBool(kPrefsMilesKey, false);
  s_show_runways = prefs.getBool(kPrefsRunwaysKey, true);
  prefs.end();
}

void rangeNext() {
  s_range_index = static_cast<uint8_t>((s_range_index + 1) % kRangePresetCount);
  saveRangeIndex();
}

bool rangeSet(uint8_t index) {
  if (index >= kRangePresetCount) {
    return false;
  }
  if (index != s_range_index) {
    s_range_index = index;
    saveRangeIndex();
  }
  return true;
}

const RangePreset& rangeCurrent() { return kRangePresets[s_range_index]; }

uint8_t rangeIndex() { return s_range_index; }

float fetchRadiusKm() {
  const float outer_km = rangeCurrent().outer_km;
  const float screen_r_px =
      static_cast<float>(kCenterX - kBeyondRingScreenMarginPx);
  return outer_km * (screen_r_px / static_cast<float>(kGridOuterRadius));
}

bool useMiles() { return s_use_miles; }

bool showRunways() { return s_show_runways; }

void setUseMiles(bool use_miles) {
  if (use_miles == s_use_miles) {
    return;
  }
  s_use_miles = use_miles;
  saveUseMiles();
}

void setShowRunways(bool show) {
  if (show == s_show_runways) {
    return;
  }
  s_show_runways = show;
  saveShowRunways();
}

void formatDistance(char* buf, size_t len, float km) {
  if (s_use_miles) {
    snprintf(buf, len, "%d mi", static_cast<int>(lroundf(km / kKmPerMile)));
  } else {
    snprintf(buf, len, "%d km", static_cast<int>(lroundf(km)));
  }
}

void formatRing3Label(char* buf, size_t len, float ring3_km, bool use_miles) {
  if (use_miles) {
    const int mi = static_cast<int>(lroundf(ring3_km / kKmPerMile));
    snprintf(buf, len, "%dmi", mi);
  } else {
    const int km = static_cast<int>(lroundf(ring3_km));
    snprintf(buf, len, "%dkm", km);
  }
}

void formatCurrentRing3Label(char* buf, size_t len) {
  formatRing3Label(buf, len, rangeCurrent().ring3_km, s_use_miles);
}

void unitsReset() {
  s_use_miles = false;
  s_show_runways = true;
  s_range_index = kDefaultRangeIndex;
  Preferences prefs;
  if (prefs.begin(kPrefsNamespace, false)) {
    prefs.remove(kPrefsRangeKey);
    prefs.remove(kPrefsMilesKey);
    prefs.remove(kPrefsRunwaysKey);
    prefs.end();
  }
}

}  // namespace ui::radar
