// Desktop stand-in for the bits of Arduino.h the UI code touches.
#pragma once

#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>

unsigned long millis();
inline void delay(unsigned long) {}

struct SimSerial {
  void println(const char* s = "") { printf("%s\n", s); }
  void printf(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
  }
};
extern SimSerial Serial;

typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(mux) (void)(mux)
#define portEXIT_CRITICAL(mux) (void)(mux)
