// Minste mulige erstatning for Arduino-kjernen og FreeRTOS, saa .ino-filene kan
// kompileres og kjoeres paa PC. Bare det testene trenger er med.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdarg>
#include <string>
#include <map>
using std::isnan;
typedef std::string String;
#include <vector>

#define constrain(x, lo, hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))
#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define INPUT 0

inline size_t strlcpy(char *dst, const char *src, size_t n) {
  size_t len = strlen(src);
  if (n) { size_t k = len < n - 1 ? len : n - 1; memcpy(dst, src, k); dst[k] = 0; }
  return len;
}

// Tid styres av testen
extern uint32_t testMillis;
inline uint32_t millis() { return testMillis; }
inline int64_t esp_timer_get_time() { return (int64_t) testMillis * 1000; }
inline void delay(uint32_t) {}
inline void delayMicroseconds(uint32_t) {}
inline void pinMode(uint8_t, uint8_t) {}
inline void digitalWrite(uint8_t, uint8_t) {}

// pulseIn leser neste verdi fra lista testen fyller
extern std::vector<unsigned long> testEkko;
inline unsigned long pulseIn(uint8_t, uint8_t, unsigned long) {
  if (testEkko.empty()) return 0;
  unsigned long v = testEkko.front();
  testEkko.erase(testEkko.begin());
  return v;
}

struct SerialStub {
  bool vis = false;
  void begin(int) {}
  void printf(const char *f, ...) { if (!vis) return; va_list a; va_start(a, f); vprintf(f, a); va_end(a); }
  void println(const char *s = "") { if (vis) puts(s); }
};
extern SerialStub Serial;

// FreeRTOS
typedef uint32_t TickType_t;
typedef void *SemaphoreHandle_t;
typedef void *QueueHandle_t;
#define pdMS_TO_TICKS(ms) (ms)
#define portMAX_DELAY 0xffffffff
#define pdTRUE 1
inline void vTaskDelay(TickType_t) {}
inline void vTaskDelayUntil(TickType_t *, TickType_t) {}
inline TickType_t xTaskGetTickCount() { return testMillis; }
inline int xSemaphoreTake(SemaphoreHandle_t, TickType_t) { return pdTRUE; }
inline int xSemaphoreGive(SemaphoreHandle_t) { return pdTRUE; }
inline SemaphoreHandle_t xSemaphoreCreateMutex() { return nullptr; }
inline QueueHandle_t xQueueCreate(int, size_t) { return nullptr; }
inline int xQueueOverwrite(QueueHandle_t, const void *) { return pdTRUE; }
inline int xQueueReceive(QueueHandle_t, void *, TickType_t) { return 0; }
inline void xTaskCreatePinnedToCore(void (*)(void *), const char *, int, void *, int, void *, int) {}
inline void vTaskDelete(void *) {}
inline void configTzTime(const char *, const char *) {}
