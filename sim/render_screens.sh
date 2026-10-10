#!/usr/bin/env bash
# Render the device screens on this computer (needs SDL2 headers and a prior
# `pio run`, which fetches LovyanGFX and ArduinoJson).
#
#   sim/render_screens.sh [out-dir] [busy-feed.json] [quiet-feed.json lat lon]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-${ROOT}/sim/out}"
BUILD="${SIM_BUILD_DIR:-${ROOT}/.pio/sim}"
LIBS="${ROOT}/.pio/libdeps/supermini"
LGFX="${LIBS}/LovyanGFX/src"
shift || true

if [[ ! -d "$LGFX" ]]; then
  echo "LovyanGFX not found; run 'pio run' once first." >&2
  exit 1
fi
mkdir -p "$OUT" "$BUILD"

CXXFLAGS=(-std=gnu++17 -O1 -w -DLGFX_SDL -DPLANE_RADAR_SIM
  $(sdl2-config --cflags)
  -I"${ROOT}/sim/shim" -I"${ROOT}/include" -I"$LGFX" -I"${LIBS}/ArduinoJson/src")

# LovyanGFX itself only needs compiling once.
if [[ ! -f "${BUILD}/liblgfx.a" ]]; then
  echo "Building LovyanGFX for desktop..."
  objs=()
  for src in "$LGFX"/lgfx/v1/*.cpp "$LGFX"/lgfx/utility/*.c \
             "$LGFX"/lgfx/Fonts/efont/*.c "$LGFX"/lgfx/Fonts/IPA/*.c; do
    obj="${BUILD}/$(basename "$src").o"
    if [[ "$src" == *.c ]]; then
      clang -O1 -w -DLGFX_SDL $(sdl2-config --cflags) -I"$LGFX" -c "$src" -o "$obj"
    else
      clang++ "${CXXFLAGS[@]}" -c "$src" -o "$obj"
    fi
    objs+=("$obj")
  done
  ar rcs "${BUILD}/liblgfx.a" "${objs[@]}"
fi

(cd "$ROOT" && clang++ "${CXXFLAGS[@]}" \
  sim/screens.cpp sim/font_blob.S \
  src/ui/status_screens.cpp src/ui/radar_display.cpp src/ui/radar_range.cpp \
  src/ui/runway_overlay.cpp src/data/large_airports_data.cpp \
  src/hardware/display.cpp src/hardware/display_font.cpp \
  src/services/radar_location.cpp src/services/settings.cpp \
  src/services/aircraft_parse.cpp \
  "${BUILD}/liblgfx.a" $(sdl2-config --libs) -o "${BUILD}/screens")

"${BUILD}/screens" "$OUT" "$@"

# BMP -> PNG where sips is available (macOS).
if command -v sips >/dev/null 2>&1; then
  for bmp in "$OUT"/*.bmp; do
    sips -s format png "$bmp" --out "${bmp%.bmp}.png" >/dev/null && rm "$bmp"
  done
fi
echo "Screens are in $OUT"
