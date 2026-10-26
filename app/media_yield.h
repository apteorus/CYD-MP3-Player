#pragma once
#include <Arduino.h>
#ifdef ESP32
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

// Preparation can run for minutes. A plain yield only rotates ready tasks at
// this priority and need not run the lower-priority idle/watchdog task. Block
// for a real tick periodically; never disable the watchdog. The caller owns
// its timestamp so no permanent global buffer/state is needed.
inline void media_idle_tick() {
#ifdef ESP32
  vTaskDelay(1);
#else
  delay(1);
#endif
}
inline void media_yield(uint32_t& lastPause) {
  uint32_t now = millis();
  if (uint32_t(now - lastPause) >= 16) {
    media_idle_tick();
    lastPause = millis();
  } else yield();
}
