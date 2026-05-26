#pragma once

// FNK0104B HAL shim that re-exports the `M5StickCPlus` API surface used by the
// claude-desktop-buddy firmware. Application code includes this header instead
// of <M5StickCPlus.h>. See plan: i-want-to-port-noble-quill.md.

#include <Arduino.h>
#include <FS.h>
#include <TFT_eSPI.h>

// Color name aliases the source firmware inherited from M5StickCPlus.
#ifndef GREEN
#define GREEN TFT_GREEN
#endif
#ifndef RED
#define RED TFT_RED
#endif
#ifndef BLUE
#define BLUE TFT_BLUE
#endif
#ifndef WHITE
#define WHITE TFT_WHITE
#endif
#ifndef BLACK
#define BLACK TFT_BLACK
#endif
#ifndef YELLOW
#define YELLOW TFT_YELLOW
#endif
#ifndef ORANGE
#define ORANGE TFT_ORANGE
#endif
#ifndef CYAN
#define CYAN TFT_CYAN
#endif
#ifndef MAGENTA
#define MAGENTA TFT_MAGENTA
#endif

// ---------- RTC types (originally from M5StickCPlus) ----------
struct RTC_TimeTypeDef {
  uint8_t Hours;
  uint8_t Minutes;
  uint8_t Seconds;
};

struct RTC_DateTypeDef {
  uint8_t WeekDay;
  uint8_t Month;
  uint8_t Date;
  uint16_t Year;
};

// ---------- Component shims ----------
class ButtonShim {
public:
  // Called by touch driver after each scan.
  void _set(bool pressed, uint32_t now_ms);

  bool isPressed() const { return _pressed; }
  bool wasPressed();   // edge: pressed since last call
  bool wasReleased();  // edge: released since last call
  bool pressedFor(uint32_t ms) const {
    return _pressed && (millis() - _press_ms) >= ms;
  }

private:
  bool _pressed = false;
  bool _edge_pressed = false;
  bool _edge_released = false;
  uint32_t _press_ms = 0;
};

class BeepShim {
public:
  void begin();
  void tone(uint16_t freq, uint16_t dur_ms);
  void update();
  void mute();
};

class ImuShim {
public:
  void Init() {}
  void getAccelData(float* ax, float* ay, float* az);
};

class AxpShim {
public:
  // Brightness 20..100 mapped to PWM duty on TFT_BL.
  void ScreenBreath(int level);
  void SetLDO2(bool on);            // gate the backlight as "screen power"
  void PowerOff();                  // hard off (deep sleep)
  uint8_t GetBtnPress();            // always 0 (no AXP power button)
  float GetVBusVoltage();
  float GetBatVoltage();
  float GetBatCurrent();
  float GetTempInAXP192();
};

class RtcShim {
public:
  void GetTime(RTC_TimeTypeDef* t);
  void GetDate(RTC_DateTypeDef* d);
  void SetTime(RTC_TimeTypeDef* t);
  void SetDate(RTC_DateTypeDef* d);
};

// ---------- Aggregator ----------
struct M5_t {
  TFT_eSPI Lcd;        // direct subclass-of-Print TFT instance
  ImuShim Imu;
  BeepShim Beep;
  AxpShim Axp;
  RtcShim Rtc;
  ButtonShim BtnA;     // virtual: top half of screen
  ButtonShim BtnB;     // virtual: bottom half of screen

  void begin();
  void update();
};

extern M5_t M5;

// ---------- FNK0104B-specific extensions ----------
namespace fnk {
  // Physical panel dimensions (pre-rotation).
  static constexpr int PANEL_W = 240;
  static constexpr int PANEL_H = 320;
  // Application canvas (post setRotation(1) landscape).
  static constexpr int CANVAS_W = 320;
  static constexpr int CANVAS_H = 240;

  // Drives an on-screen attention indicator (red pulsing border around the
  // full canvas). Replaces the M5StickC Plus's GPIO 10 red LED.
  void setAttentionPulse(bool on);
  void renderAttentionFrame();

  // True while the touchscreen is being "covered" — used by the IMU stub to
  // report a face-down accel reading, which keeps source code's isFaceDown()
  // working unchanged.
  bool isCovered();
}
