#pragma once
#include <stdint.h>

namespace fnk {

enum GestureKind : uint8_t {
  GESTURE_NONE = 0,
  GESTURE_TAP,
  GESTURE_SWIPE_LEFT,
  GESTURE_SWIPE_RIGHT,
  GESTURE_SWIPE_UP,
  GESTURE_SWIPE_DOWN,
  GESTURE_LONG_PRESS,
};

struct TouchEvent {
  GestureKind kind;
  int16_t x;   // canvas coords (post-rotation), gesture start point
  int16_t y;
};

// Pops the next completed gesture (if any). Returns true and fills `out` once
// per gesture. The driver still drives M5.BtnA / M5.BtnB for legacy paths
// that haven't been migrated yet (top-half = BtnA, bottom-half = BtnB).
bool touch_event(TouchEvent& out);

// True while a finger is currently in contact, filling x/y with the latest
// canvas position. Where touch_event() reports completed gestures, this exposes
// the live contact for direct-manipulation controls (e.g. dragging the volume
// slider). Leaves x/y untouched and returns false when no finger is down.
bool touch_active(int16_t& x, int16_t& y);

}  // namespace fnk
