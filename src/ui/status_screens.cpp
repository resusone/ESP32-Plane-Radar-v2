#include "ui/status_screens.h"

#include <lgfx/v1/lgfx_fonts.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include "config.h"
#include "hardware/display.h"
#include "hardware/display_font.h"

namespace {

const int kCenterX = config::kDisplayWidth / 2;
const int kCenterY = config::kDisplayHeight / 2;
const int kScreenRadius = config::kDisplayWidth / 2;

/** Keep text this far inside the bezel. */
constexpr int kTextInsetPx = 9;
/** Smallest scale drawRow() shrinks to before it starts clipping. */
constexpr float kMinTextSize = 0.62f;

constexpr int kRimOuterRadius = 119;
constexpr int kRimInnerRadius = 116;

constexpr int kSpinnerDotCount = 10;
constexpr int kSpinnerRadius = 111;
constexpr int kSpinnerDotRadius = 3;
constexpr int kSpinnerEraseRadius = 5;
constexpr float kSpinnerStepDeg = 6.0f;

struct SpinnerDot {
  int x = 0;
  int y = 0;
  bool drawn = false;
};

float s_spinner_angle_deg = -90.0f;
SpinnerDot s_spinner_dots[kSpinnerDotCount];
float s_hold_progress_drawn = 0.0f;

// Palette. Resolved per call: the red/blue swap can change at runtime.
uint16_t colBackground() { return displayColor(3, 8, 20); }
uint16_t colText() { return displayColor(255, 255, 255); }
uint16_t colDim() { return displayColor(140, 160, 180); }
uint16_t colRing() { return displayColor(16, 100, 32); }
uint16_t colOk() { return displayColor(70, 225, 120); }
uint16_t colAccent() { return displayColor(255, 190, 40); }
uint16_t colError() { return displayColor(255, 85, 70); }
uint16_t colInfo() { return displayColor(90, 200, 255); }

void setTextStyle(float size) {
  if (displayFontIsSmooth()) {
    displayFontSetSmoothSize(tft, size);
  } else {
    displayFontSetBitmap(tft, size >= 1.1f ? &fonts::FreeSansBold12pt7b
                                           : &fonts::FreeSansBold9pt7b);
  }
}

/** Width of the round screen across the rows y_top..y_bottom, less the inset. */
int chordWidth(int y_top, int y_bottom) {
  const int dy = std::max(std::abs(y_top - kCenterY), std::abs(y_bottom - kCenterY));
  const int r = kScreenRadius - kTextInsetPx;
  if (dy >= r) {
    return 0;
  }
  return 2 * static_cast<int>(std::sqrt(static_cast<float>(r * r - dy * dy)));
}

/**
 * One centred line at row `y`, at scale `size` or smaller: shrunk until it
 * fits the circle at that row, then clipped with ".." if it still doesn't.
 */
void drawRow(const char* text, int y, float size, uint16_t fg) {
  if (text == nullptr || text[0] == '\0') {
    return;
  }
  char buf[48];
  snprintf(buf, sizeof(buf), "%s", text);

  float s = size;
  int avail = 0;
  for (;;) {
    setTextStyle(s);
    const int h = tft.fontHeight();
    avail = chordWidth(y - h / 2, y + h / 2);
    if (tft.textWidth(buf) <= avail || s <= kMinTextSize ||
        !displayFontIsSmooth()) {
      break;
    }
    s -= 0.04f;
  }

  for (size_t keep = strlen(text); tft.textWidth(buf) > avail && keep > 1;) {
    --keep;
    snprintf(buf, sizeof(buf), "%.*s..", static_cast<int>(keep), text);
  }

  tft.setTextDatum(textdatum_t::middle_center);
  tft.setTextColor(fg, colBackground());
  tft.drawString(buf, kCenterX, y);
}

void drawRim(uint16_t color) {
  tft.fillArc(kCenterX, kCenterY, kRimOuterRadius, kRimInnerRadius, 0.0f,
              360.0f, color);
}

void beginScreen(uint16_t rim_color) {
  tft.fillScreen(colBackground());
  drawRim(rim_color);
}

void drawCheckIcon(int cx, int cy, uint16_t color) {
  tft.fillSmoothCircle(cx, cy, 17, color);
  const uint16_t bg = colBackground();
  tft.drawWideLine(cx - 8, cy, cx - 2, cy + 6, 2.0f, bg);
  tft.drawWideLine(cx - 2, cy + 6, cx + 8, cy - 6, 2.0f, bg);
}

void drawCrossIcon(int cx, int cy, uint16_t color) {
  tft.fillSmoothCircle(cx, cy, 17, color);
  const uint16_t bg = colBackground();
  tft.drawWideLine(cx - 6, cy - 6, cx + 6, cy + 6, 2.0f, bg);
  tft.drawWideLine(cx - 6, cy + 6, cx + 6, cy - 6, 2.0f, bg);
}

/** Small radar glyph: rings, a sweep and a target blip. */
void drawRadarIcon(int cx, int cy) {
  const uint16_t ring = colRing();
  tft.drawCircle(cx, cy, 26, ring);
  tft.drawCircle(cx, cy, 25, ring);
  tft.drawCircle(cx, cy, 16, ring);
  tft.drawCircle(cx, cy, 7, ring);
  tft.drawWideLine(cx, cy, cx + 17, cy - 19, 1.0f, colOk());
  tft.fillSmoothCircle(cx - 10, cy - 8, 3, colError());
  tft.fillSmoothCircle(cx, cy, 2, colText());
}

void eraseSpinnerDots() {
  for (int i = 0; i < kSpinnerDotCount; ++i) {
    if (!s_spinner_dots[i].drawn) {
      continue;
    }
    tft.fillCircle(s_spinner_dots[i].x, s_spinner_dots[i].y,
                   kSpinnerEraseRadius, colBackground());
    s_spinner_dots[i].drawn = false;
  }
}

void drawSpinnerDots() {
  constexpr float kDegToRad = 0.01745329252f;
  const float head_rad = s_spinner_angle_deg * kDegToRad;

  for (int i = 0; i < kSpinnerDotCount; ++i) {
    const float a =
        head_rad - static_cast<float>(i) * (6.283185307f / kSpinnerDotCount);
    const int x =
        kCenterX + static_cast<int>(std::lround(std::cos(a) * kSpinnerRadius));
    const int y =
        kCenterY + static_cast<int>(std::lround(std::sin(a) * kSpinnerRadius));

    const int fade = 255 - i * 22;
    tft.fillSmoothCircle(x, y, kSpinnerDotRadius,
                         displayColor(0, static_cast<uint8_t>(fade), 0));

    s_spinner_dots[i].x = x;
    s_spinner_dots[i].y = y;
    s_spinner_dots[i].drawn = true;
  }
}

/** Clockwise arc from 12 o'clock covering `fraction` of the rim. */
void drawRimProgress(float from_fraction, float to_fraction, uint16_t color) {
  // LovyanGFX angles: 0 deg = 3 o'clock, clockwise; 12 o'clock is 270.
  float a0 = 270.0f + from_fraction * 360.0f;
  float a1 = 270.0f + to_fraction * 360.0f;
  if (a1 <= a0) {
    return;
  }
  if (a0 >= 360.0f) {
    a0 -= 360.0f;
    a1 -= 360.0f;
  }
  if (a1 > 360.0f) {
    tft.fillArc(kCenterX, kCenterY, kRimOuterRadius, kRimInnerRadius - 3, a0,
                360.0f, color);
    a0 = 0.0f;
    a1 -= 360.0f;
  }
  tft.fillArc(kCenterX, kCenterY, kRimOuterRadius, kRimInnerRadius - 3, a0, a1,
              color);
}

}  // namespace

void statusScreenSplash(const char* version) {
  beginScreen(colRing());
  drawRadarIcon(kCenterX, 72);
  drawRow("Plane Radar", 128, 1.45f, colText());
  char line[24];
  snprintf(line, sizeof(line), "v%s", version != nullptr ? version : "");
  drawRow(line, 156, 0.85f, colDim());
  drawRow("Starting...", 190, 0.8f, colDim());
}

void statusScreenPortal(const char* ap_name, const char* ip,
                        const char* note) {
  beginScreen(colAccent());

  const bool has_note = note != nullptr && note[0] != '\0';
  drawRow(has_note ? note : "Scan to set up", 23, 0.85f,
          has_note ? colError() : colText());

  // Version-3 code (29 modules) at 4 px per module, on a white card that
  // doubles as the quiet zone phones need to lock on.
  constexpr int kQrPx = 116;
  constexpr int kCardPad = 8;
  constexpr int kCardSize = kQrPx + kCardPad * 2;
  constexpr int kCardTop = 36;
  const int card_left = kCenterX - kCardSize / 2;
  tft.fillSmoothRoundRect(card_left, kCardTop, kCardSize, kCardSize, 8,
                          tft.color565(255, 255, 255));

  char payload[80];
  snprintf(payload, sizeof(payload), "WIFI:T:nopass;S:%s;;", ap_name);
  tft.qrcode(payload, card_left + kCardPad, kCardTop + kCardPad, kQrPx, 3);

  drawRow(ap_name, 183, 0.95f, colAccent());
  char line[40];
  snprintf(line, sizeof(line), "then open %s", ip);
  drawRow(line, 203, 0.78f, colDim());
}

void statusScreenConnectingBegin(const char* ssid, uint8_t attempt,
                                 uint8_t attempts) {
  tft.fillScreen(colBackground());
  drawRow("Connecting to", 96, 0.85f, colDim());
  drawRow((ssid != nullptr && ssid[0] != '\0') ? ssid : "Wi-Fi", 122, 1.15f,
          colText());
  if (attempt > 1) {
    char line[24];
    snprintf(line, sizeof(line), "attempt %u of %u", attempt, attempts);
    drawRow(line, 150, 0.8f, colDim());
  }

  s_spinner_angle_deg = -90.0f;
  for (auto& dot : s_spinner_dots) {
    dot.drawn = false;
  }
  drawSpinnerDots();
}

void statusScreenConnectingTick() {
  eraseSpinnerDots();
  s_spinner_angle_deg += kSpinnerStepDeg;
  if (s_spinner_angle_deg >= 270.0f) {
    s_spinner_angle_deg -= 360.0f;
  }
  drawSpinnerDots();
}

void statusScreenConnected(const char* ssid, const char* ip,
                           const char* host) {
  beginScreen(colOk());
  drawCheckIcon(kCenterX, 46, colOk());
  drawRow("Connected", 88, 1.2f, colOk());
  drawRow(ssid, 114, 0.9f, colText());
  drawRow("Settings page:", 144, 0.75f, colDim());
  drawRow(ip, 166, 1.1f, colAccent());
  drawRow(host, 190, 0.8f, colDim());
}

void statusScreenConnectFailed(const char* ssid, const char* reason) {
  beginScreen(colError());
  drawCrossIcon(kCenterX, 46, colError());
  drawRow("Can't connect", 88, 1.15f, colError());
  drawRow(ssid, 114, 0.9f, colText());
  drawRow(reason, 142, 0.9f, colAccent());
  drawRow("Opening setup...", 176, 0.8f, colDim());
}

void statusScreenHoldBegin() {
  beginScreen(colRing());
  drawRow("Setup mode", 72, 1.25f, colText());
  drawRow("Release now", 100, 0.95f, colOk());
  drawRow("to change Wi-Fi", 120, 0.8f, colDim());
  drawRow("Or keep holding", 156, 0.8f, colDim());
  drawRow("to erase everything", 175, 0.8f, colError());
  s_hold_progress_drawn = 0.0f;
}

void statusScreenHoldProgress(float erase_progress) {
  const float p = std::max(0.0f, std::min(1.0f, erase_progress));
  if (p <= s_hold_progress_drawn) {
    return;
  }
  drawRimProgress(s_hold_progress_drawn, p, colError());
  s_hold_progress_drawn = p;
}

void statusScreenMessage(const char* title, const char* line1,
                         const char* line2) {
  beginScreen(colRing());
  const bool two = line2 != nullptr && line2[0] != '\0';
  drawRow(title, two ? 92 : 104, 1.2f, colText());
  drawRow(line1, two ? 124 : 136, 0.9f, colDim());
  drawRow(line2, 146, 0.9f, colDim());
}

void statusScreenInfo(const InfoScreenData& info) {
  beginScreen(info.online ? colRing() : colError());

  char line[48];
  snprintf(line, sizeof(line), "Plane Radar %s", info.version);
  drawRow(line, 25, 0.72f, colDim());

  if (info.online) {
    snprintf(line, sizeof(line), "%s  %d dBm", info.ssid, info.rssi_dbm);
    drawRow(line, 47, 0.85f, colText());
    drawRow(info.ip, 71, 1.1f, colAccent());
    drawRow(info.host, 92, 0.78f, colDim());
  } else {
    drawRow("Wi-Fi not connected", 58, 0.9f, colError());
    drawRow(info.ssid, 82, 0.85f, colText());
  }

  drawRow(info.feed_headline, 121, 1.15f,
          info.feed_ok ? colOk() : colError());
  drawRow(info.feed_detail, 144, 0.8f, colDim());

  drawRow(info.location, 172, 0.8f, colInfo());
  drawRow(info.range, 194, 0.8f, colDim());
}
