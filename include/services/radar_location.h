#pragma once

namespace services::location {

/** Load saved lat/lon from NVS, or use config defaults. Call once before WiFi setup. */
void init();

double lat();
double lon();

/** False until a location has been saved (the config.h default is in use). */
bool isSet();
/** True when the saved location came from IP geolocation (city-level only). */
bool isApproximate();

/** Validate, persist to NVS, update runtime values. */
bool save(double lat, double lon, bool approximate = false);
/** Parse decimal-degree strings, then save(). */
bool saveFromStrings(const char* lat_str, const char* lon_str);

/** Clear stored coordinates (factory reset). */
void clear();

}  // namespace services::location
