#include "hardware/display.h"

#include "hardware/display_font.h"

LGFX tft;

namespace {
bool s_swap_red_blue = config::kDisplayRgbOrder;
}

void displayInit() {
  tft.init();
  tft.setRotation(0);
  tft.setBrightness(255);
  tft.setTextWrap(false);
  displayFontInit();
}

void displaySetSwapRedBlue(bool swap) { s_swap_red_blue = swap; }

bool displaySwapRedBlue() { return s_swap_red_blue; }

uint16_t displayColor(uint8_t r, uint8_t g, uint8_t b) {
  return s_swap_red_blue ? tft.color565(b, g, r) : tft.color565(r, g, b);
}
