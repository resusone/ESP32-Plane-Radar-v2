#pragma once

#include "hardware/lgfx_config.hpp"

extern LGFX tft;

void displayInit();

/**
 * With kDisplayRgbOrder set the usual GC9A01 module shows red and blue
 * exchanged, so every UI colour goes through displayColor(), which swaps the
 * two channels back. The swap can be turned off from the web portal for the
 * odd panel that is wired the other way.
 */
void displaySetSwapRedBlue(bool swap);
bool displaySwapRedBlue();
uint16_t displayColor(uint8_t r, uint8_t g, uint8_t b);
