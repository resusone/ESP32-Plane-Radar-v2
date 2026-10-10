// Renders every on-device screen to an image on the desktop, using the real
// UI code and LovyanGFX drawing into an off-screen canvas.
//
//   sim/render_screens.sh [output-dir]
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "config.h"
#include "hardware/display.h"
#include "services/aircraft_parse.h"
#include "services/radar_location.h"
#include "services/settings.h"
#include "ui/radar_display.h"
#include "ui/radar_range.h"
#include "ui/status_screens.h"

SimSerial Serial;

namespace {
unsigned long g_now_ms = 100000;
std::vector<services::adsb::Aircraft> g_aircraft;
services::adsb::Status g_status;
std::string g_out_dir = ".";
}  // namespace

unsigned long millis() { return g_now_ms; }

// --- stand-ins for the networked half of the ADS-B client -------------------
namespace services::adsb {

size_t snapshotAircraft(Aircraft* out, size_t max_out,
                        unsigned long* out_last_update_ms) {
  if (out_last_update_ms != nullptr) {
    *out_last_update_ms = g_status.last_success_ms;
  }
  const size_t n = g_aircraft.size() < max_out ? g_aircraft.size() : max_out;
  for (size_t i = 0; i < n; ++i) {
    out[i] = g_aircraft[i];
  }
  return n;
}

Status status() { return g_status; }

}  // namespace services::adsb

namespace {

class FileSource {
 public:
  explicit FileSource(const char* path) {
    FILE* f = fopen(path, "rb");
    if (f == nullptr) {
      fprintf(stderr, "cannot open %s\n", path);
      exit(2);
    }
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
      data_.insert(data_.end(), buf, buf + n);
    }
    fclose(f);
  }
  int read() {
    return pos_ < data_.size() ? static_cast<unsigned char>(data_[pos_++]) : -1;
  }

 private:
  std::vector<char> data_;
  size_t pos_ = 0;
};

/** Load a captured (plain, de-chunked) feed reply as the current traffic. */
void loadFeed(const char* path, float radius_km) {
  FileSource src(path);
  g_aircraft.assign(services::adsb::kMaxAircraft, services::adsb::Aircraft());
  services::adsb::ParseCounters c;
  services::adsb::parseFeed(src, services::location::lat(),
                            services::location::lon(),
                            services::settings::showGround(),
                            g_aircraft.data(), g_aircraft.size(), &c);
  g_aircraft.resize(c.kept);
  // The API was asked for more than the screen shows; keep what the device
  // would have been sent for this range.
  while (!g_aircraft.empty() && g_aircraft.back().dist_km > radius_km) {
    g_aircraft.pop_back();
  }
  g_status = services::adsb::Status();
  g_status.attempted = g_status.last_ok = g_status.ever_ok = true;
  g_status.http_code = 200;
  g_status.last_success_ms = g_now_ms - 1500;
  g_status.shown = static_cast<uint16_t>(g_aircraft.size());
}

/** Write the canvas as a 2x BMP; pixels off the round panel are greyed out. */
void save(const char* name) {
  constexpr int kScale = 2;
  const int w = config::kDisplayWidth * kScale;
  const int h = config::kDisplayHeight * kScale;
  const int row = (w * 3 + 3) & ~3;
  std::vector<uint8_t> px(static_cast<size_t>(row) * h, 0);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const int sx = x / kScale;
      const int sy = y / kScale;
      const float dx = sx - 119.5f;
      const float dy = sy - 119.5f;
      uint8_t r = 46, g = 46, b = 50;
      if (dx * dx + dy * dy <= 120.0f * 120.0f) {
        const uint16_t c = tft.readPixel(sx, sy);
        r = static_cast<uint8_t>(((c >> 11) & 0x1F) * 255 / 31);
        g = static_cast<uint8_t>(((c >> 5) & 0x3F) * 255 / 63);
        b = static_cast<uint8_t>((c & 0x1F) * 255 / 31);
      }
      uint8_t* p = &px[static_cast<size_t>(h - 1 - y) * row + x * 3];
      p[0] = b;
      p[1] = g;
      p[2] = r;
    }
  }
  const std::string path = g_out_dir + "/" + name + ".bmp";
  FILE* f = fopen(path.c_str(), "wb");
  const uint32_t size = 54 + static_cast<uint32_t>(px.size());
  uint8_t hdr[54] = {'B', 'M'};
  auto put32 = [&](int off, uint32_t v) { memcpy(hdr + off, &v, 4); };
  put32(2, size);
  put32(10, 54);
  put32(14, 40);
  put32(18, static_cast<uint32_t>(w));
  put32(22, static_cast<uint32_t>(h));
  hdr[26] = 1;
  hdr[28] = 24;
  put32(34, static_cast<uint32_t>(px.size()));
  fwrite(hdr, 1, 54, f);
  fwrite(px.data(), 1, px.size(), f);
  fclose(f);
  printf("wrote %s\n", path.c_str());
}

void radar(const char* name, uint8_t range_index) {
  ui::radar::rangeSet(range_index);
  ui::radarDisplayDraw();
  save(name);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: screens <out-dir> <busy-feed.json> [quiet-feed.json "
                    "lat lon]\n");
    return 2;
  }
  g_out_dir = argv[1];
  const char* busy_feed = argv[2];

  displaySetSwapRedBlue(false);  // the canvas is plain RGB
  displayInit();

  statusScreenSplash(config::kFirmwareVersion);
  save("01_splash");

  statusScreenPortal(config::kPortalApName, config::kPortalIp, nullptr);
  save("02_portal");
  statusScreenPortal(config::kPortalApName, config::kPortalIp,
                     "Can't join HomeNet");
  save("03_portal_note");

  statusScreenConnectingBegin("HomeNet-5G", 1, 3);
  for (int i = 0; i < 9; ++i) statusScreenConnectingTick();
  save("04_connecting");
  statusScreenConnectingBegin("A Very Long Network Name Indeed 2.4GHz", 2, 3);
  save("05_connecting_long");

  statusScreenConnected("HomeNet-5G", "192.168.1.137", "plane-radar.local");
  save("06_connected");
  statusScreenConnectFailed("HomeNet-5G", "Wrong password?");
  save("07_failed");

  statusScreenHoldBegin();
  statusScreenHoldProgress(0.2f);
  statusScreenHoldProgress(0.45f);
  save("08_hold");

  statusScreenMessage("Erased", "Restarting...", nullptr);
  save("09_message");
  statusScreenMessage("Finding you", "Looking up your", "approximate location");
  save("10_message2");

  InfoScreenData info;
  info.version = config::kFirmwareVersion;
  info.online = true;
  info.ssid = "HomeNet-5G";
  info.rssi_dbm = -61;
  info.ip = "192.168.1.137";
  info.host = "plane-radar.local";
  info.feed_headline = "12 aircraft";
  info.feed_ok = true;
  info.feed_detail = "adsb.fi, 3 s ago";
  info.location = "52.3676 N 4.9041 E";
  info.range = "Range 50 km";
  statusScreenInfo(info);
  save("11_info");
  info.online = false;
  info.feed_headline = "Connect failed";
  info.feed_ok = false;
  info.feed_detail = "adsb.lol, 2 min ago";
  statusScreenInfo(info);
  save("12_info_offline");

  // --- radar ---------------------------------------------------------------
  services::location::save(52.3676, 4.9041);
  loadFeed(busy_feed, 1e9f);
  radar("20_radar_busy_25km", 3);
  radar("21_radar_busy_50km", 4);
  radar("22_radar_busy_150km", 6);
  radar("23_radar_busy_10km", 1);

  services::settings::setMaxLabels(12);
  radar("24_radar_busy_50km_12labels", 4);
  services::settings::setMaxLabels(services::settings::kDefaultMaxLabels);

  ui::radar::setUseMiles(true);
  radar("25_radar_miles", 3);
  ui::radar::setUseMiles(false);

  if (argc >= 6) {
    services::location::save(atof(argv[4]), atof(argv[5]));
    loadFeed(argv[3], 1e9f);
    radar("30_radar_quiet_50km", 4);
    radar("31_radar_quiet_100km", 5);
  }

  g_aircraft.clear();
  g_status.shown = 0;
  radar("40_radar_empty", 1);
  radar("41_radar_empty_maxrange", 6);

  g_status.last_ok = false;
  g_status.ever_ok = false;
  snprintf(g_status.error, sizeof(g_status.error), "Connect failed");
  radar("42_radar_feed_error", 4);

  g_status = services::adsb::Status();
  radar("43_radar_loading", 4);

  g_status.attempted = true;
  snprintf(g_status.error, sizeof(g_status.error), "Connect failed");
  ui::radarDisplaySetLink(true, -83);
  radar("45_radar_weak_wifi", 4);

  ui::radarDisplaySetLink(false, 0);
  radar("44_radar_wifi_lost", 4);
  return 0;
}
