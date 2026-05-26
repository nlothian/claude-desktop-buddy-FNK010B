#include "board.h"
#include <time.h>
#include <sys/time.h>

// newlib's mktime treats fields as the current TZ; with TZ unset (the ESP32
// default) it's effectively UTC, which is what we want here.

// Software RTC seeded by BLE time sync. Uses the ESP32-S3's internal monotonic
// clock plus a settable epoch offset, mirroring the M5StickC Plus RTC API.

static time_t _seed_epoch = 0;     // wall-clock epoch corresponding to _seed_ms
static uint32_t _seed_ms = 0;
static bool _seeded = false;

static time_t _now_epoch() {
  if (!_seeded) return 0;
  return _seed_epoch + (time_t)((millis() - _seed_ms) / 1000);
}

static void _to_tm(struct tm* out) {
  time_t t = _now_epoch();
  gmtime_r(&t, out);
}

void RtcShim::GetTime(RTC_TimeTypeDef* t) {
  struct tm lt; _to_tm(&lt);
  t->Hours   = (uint8_t)lt.tm_hour;
  t->Minutes = (uint8_t)lt.tm_min;
  t->Seconds = (uint8_t)lt.tm_sec;
}

void RtcShim::GetDate(RTC_DateTypeDef* d) {
  struct tm lt; _to_tm(&lt);
  d->WeekDay = (uint8_t)lt.tm_wday;
  d->Month   = (uint8_t)(lt.tm_mon + 1);
  d->Date    = (uint8_t)lt.tm_mday;
  d->Year    = (uint16_t)(lt.tm_year + 1900);
}

void RtcShim::SetTime(RTC_TimeTypeDef* t) {
  struct tm lt; _to_tm(&lt);  // preserve current date
  lt.tm_hour = t->Hours;
  lt.tm_min  = t->Minutes;
  lt.tm_sec  = t->Seconds;
  _seed_epoch = mktime(&lt);
  _seed_ms = millis();
  _seeded = true;
}

void RtcShim::SetDate(RTC_DateTypeDef* d) {
  struct tm lt; _to_tm(&lt);  // preserve current time
  lt.tm_wday = d->WeekDay;
  lt.tm_mon  = d->Month - 1;
  lt.tm_mday = d->Date;
  lt.tm_year = d->Year - 1900;
  _seed_epoch = mktime(&lt);
  _seed_ms = millis();
  _seeded = true;
}
