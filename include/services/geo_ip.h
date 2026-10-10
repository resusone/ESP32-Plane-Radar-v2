#pragma once

#include <cstddef>

namespace services::geo {

/**
 * Rough location from the public IP address (ip-api.com, city-level at best,
 * sometimes the ISP's hub in another city). Needs internet. Blocks for up to
 * a few seconds. `place` receives e.g. "Amsterdam, Netherlands".
 */
bool lookup(double* lat, double* lon, char* place, size_t place_len);

}  // namespace services::geo
