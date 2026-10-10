#pragma once

#include <cstddef>
#include <cstdint>

namespace ui::radar {

/**
 * Range presets (label on ring 3 = ¾ of outer radius).
 *
 *   5 km  — pattern / very local (airfield vicinity)
 *  10 km  — neighborhood spotting
 *  15 km  — wider local area
 *  25 km  — metro picture
 *  50 km  — default; enough to catch traffic unless you live at an airport
 * 100 km  — regional
 * 150 km  — en-route traffic (≈ 220 km fetch radius, inside the API's 250 NM cap)
 *
 * Indices 0-3 match the v1 firmware, so a saved v1 preset keeps its meaning.
 *
 * Outer radius (for aircraft math) is ring-3 distance ÷ 0.75.
 */
struct RangePreset {
  /** Distance shown on ring 3 (¾ of outer radius), always stored in km. */
  float ring3_km;
  float outer_km;
};

constexpr float kRing3ToOuterKm = 4.0f / 3.0f;

constexpr RangePreset kRangePresets[] = {
    {5.0f, 5.0f * kRing3ToOuterKm},
    {10.0f, 10.0f * kRing3ToOuterKm},
    {15.0f, 15.0f * kRing3ToOuterKm},
    {25.0f, 25.0f * kRing3ToOuterKm},
    {50.0f, 50.0f * kRing3ToOuterKm},
    {100.0f, 100.0f * kRing3ToOuterKm},
    {150.0f, 150.0f * kRing3ToOuterKm},
};

constexpr size_t kRangePresetCount =
    sizeof(kRangePresets) / sizeof(kRangePresets[0]);

/** Load saved range and distance units from flash. Call once after boot. */
void rangeInit();
/** Cycle preset and save to flash. */
void rangeNext();
/** Select a preset by index and save to flash. False if out of bounds. */
bool rangeSet(uint8_t index);
const RangePreset& rangeCurrent();
uint8_t rangeIndex();
/** ADSB fetch radius (km): scaled to screen edge so beyond-ring dots have data. */
float fetchRadiusKm();

bool useMiles();
bool showRunways();
void setUseMiles(bool use_miles);
void setShowRunways(bool show);
/** Format a distance in the user's units, e.g. "37 km" / "23 mi". */
void formatDistance(char* buf, size_t len, float km);
void formatRing3Label(char* buf, size_t len, float ring3_km, bool use_miles);
void formatCurrentRing3Label(char* buf, size_t len);
/** Reset range, units and runway overlay to defaults (factory reset). */
void unitsReset();

}  // namespace ui::radar
