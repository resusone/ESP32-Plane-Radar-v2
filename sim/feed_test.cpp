// Desktop check of the feed parser: runs a captured adsb.fi / adsb.lol reply
// (optionally still chunk-framed, as `curl --raw` saves it) through the same
// BodyFramer + parseFeed path the firmware uses.
//
//   clang++ -std=gnu++17 -Iinclude -I.pio/libdeps/supermini/ArduinoJson/src \
//     sim/feed_test.cpp src/services/aircraft_parse.cpp -o feed_test
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "services/aircraft_parse.h"
#include "services/http_body_framing.h"

using namespace services::adsb;

namespace {

/** Hands the capture out in awkward little pieces, like a socket would. */
class FileSource {
 public:
  explicit FileSource(std::vector<char> data) : data_(std::move(data)) {}
  int read() {
    if (pos_ >= data_.size()) {
      return -1;
    }
    return static_cast<unsigned char>(data_[pos_++]);
  }

 private:
  std::vector<char> data_;
  size_t pos_ = 0;
};

std::vector<char> slurp(const char* path) {
  std::vector<char> out;
  FILE* f = fopen(path, "rb");
  if (f == nullptr) {
    fprintf(stderr, "cannot open %s\n", path);
    exit(2);
  }
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
    out.insert(out.end(), buf, buf + n);
  }
  fclose(f);
  return out;
}

const char* resultName(ParseResult r) {
  switch (r) {
    case ParseResult::kOk: return "ok";
    case ParseResult::kNoList: return "no-list";
    case ParseResult::kBadJson: return "bad-json";
    case ParseResult::kTruncated: return "truncated";
  }
  return "?";
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 6) {
    fprintf(stderr,
            "usage: feed_test <file> <chunked|plain> <lat> <lon> <ground 0|1> "
            "[max] [truncate_bytes]\n");
    return 2;
  }
  std::vector<char> data = slurp(argv[1]);
  const bool chunked = strcmp(argv[2], "chunked") == 0;
  const double lat = atof(argv[3]);
  const double lon = atof(argv[4]);
  const bool ground = atoi(argv[5]) != 0;
  const size_t max_out = argc > 6 ? static_cast<size_t>(atoi(argv[6])) : kMaxAircraft;
  if (argc > 7) {
    data.resize(static_cast<size_t>(atoi(argv[7])));
  }

  FileSource source(std::move(data));
  services::http::BodyFramer<FileSource> body(
      source,
      chunked ? services::http::BodyFraming::kChunked
              : services::http::BodyFraming::kIdentity,
      -1);

  std::vector<Aircraft> out(max_out);
  ParseCounters c;
  const ParseResult r =
      parseFeed(body, lat, lon, ground, out.data(), max_out, &c);

  printf("result=%s reported=%u kept=%u ground_hidden=%u no_pos=%u "
         "nearest=%.1f body_bytes=%zu framing_error=%d\n",
         resultName(r), c.reported, c.kept, c.hidden_ground, c.no_position,
         c.nearest_km, body.bytesRead(), body.framingError());
  for (size_t i = 0; i < c.kept; ++i) {
    const Aircraft& a = out[i];
    printf("%-8s|%-4s|%-9s|%.1f|%.4f|%.4f|%.0f|%.0f|%d\n", a.callsign, a.type,
           a.alt, a.dist_km, a.lat, a.lon, a.track_deg, a.gs_knots,
           a.on_ground);
  }
  return r == ParseResult::kOk ? 0 : 1;
}
