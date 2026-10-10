#include "hardware/boot_button.h"

#include <Arduino.h>

#include "config.h"

namespace {

portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
volatile bool s_tap_pending = false;
volatile bool s_info_pending = false;
volatile bool s_is_down = false;
volatile unsigned long s_down_ms = 0;
bool s_interrupt_attached = false;

void IRAM_ATTR onBootButtonIsr() {
  const bool down = digitalRead(config::kBootPin) == LOW;
  const unsigned long now = millis();
  portENTER_CRITICAL_ISR(&s_mux);
  if (down) {
    if (!s_is_down) {
      s_is_down = true;
      s_down_ms = now;
    }
  } else if (s_is_down) {
    const unsigned long held = now - s_down_ms;
    if (held >= config::kBootTapMinMs) {
      if (held < config::kBootInfoHoldMs) {
        s_tap_pending = true;
      } else if (held < config::kBootSetupHoldMs) {
        s_info_pending = true;
      }
    }
    s_is_down = false;
  }
  portEXIT_CRITICAL_ISR(&s_mux);
}

bool consume(volatile bool* flag) {
  portENTER_CRITICAL(&s_mux);
  const bool was_set = *flag;
  *flag = false;
  portEXIT_CRITICAL(&s_mux);
  return was_set;
}

}  // namespace

void bootButtonInit() {
  pinMode(config::kBootPin, INPUT_PULLUP);
  if (s_interrupt_attached) {
    return;
  }
  attachInterrupt(digitalPinToInterrupt(static_cast<uint8_t>(config::kBootPin)),
                  onBootButtonIsr, CHANGE);
  s_interrupt_attached = true;
}

bool bootButtonPressed() { return digitalRead(config::kBootPin) == LOW; }

bool bootButtonConsumeTap() { return consume(&s_tap_pending); }

bool bootButtonConsumeInfoPress() { return consume(&s_info_pending); }

unsigned long bootButtonHeldMs() {
  if (!bootButtonPressed()) {
    // Contact bounce can swallow the release edge and leave the press
    // "running"; the next tap would then count as a long hold. Level wins.
    portENTER_CRITICAL(&s_mux);
    s_is_down = false;
    portEXIT_CRITICAL(&s_mux);
    return 0;
  }
  portENTER_CRITICAL(&s_mux);
  if (!s_is_down) {  // edge missed (held since before boot, say)
    s_is_down = true;
    s_down_ms = millis();
  }
  const unsigned long down_ms = s_down_ms;
  portEXIT_CRITICAL(&s_mux);
  const unsigned long held = millis() - down_ms;
  return held == 0 ? 1 : held;
}
