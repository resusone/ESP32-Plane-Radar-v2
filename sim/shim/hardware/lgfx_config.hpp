// Desktop stand-in for the GC9A01 device: an off-screen 240x240 RGB565 canvas.
#pragma once

#include <Arduino.h>

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "config.h"

class LGFX : public LGFX_Sprite {
 public:
  void init() {
    setColorDepth(16);
    createSprite(config::kDisplayWidth, config::kDisplayHeight);
  }
  void setBrightness(int) {}
};
