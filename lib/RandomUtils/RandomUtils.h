#pragma once
#include <Arduino.h>

// --- Init (optional, aber sauber)
inline void randomInit() {
  randomSeed(esp_random() ^ micros());
}

// --- Integer: [min, max] (inklusive, ohne Bias)
inline int randomRange(int min, int max) {
  uint32_t range = (uint32_t)(max - min + 1);
  uint32_t limit = (UINT32_MAX / range) * range;

  uint32_t r;
  do {
    r = esp_random();
  } while (r >= limit);

  return min + (r % range);
}
