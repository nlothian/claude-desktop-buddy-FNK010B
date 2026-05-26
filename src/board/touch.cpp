#include "board.h"
#include "touch.h"
#include <Wire.h>
#include <FT6336U.h>

// FNK0104B touch pins (Sketch_12.1_TFT_Touch_Draw)
static constexpr int TOUCH_SDA = 16;
static constexpr int TOUCH_SCL = 15;
static constexpr int TOUCH_RST = 18;
static constexpr int TOUCH_INT = 17;

static FT6336U ft(TOUCH_SDA, TOUCH_SCL, TOUCH_RST, TOUCH_INT);

namespace fnk { void _reportTouchCount(int count); }

namespace fnk {

// Gesture thresholds. Tap distance is small so a steady finger registers; the
// swipe distance is well clear of jitter on a 320x240 panel.
static constexpr uint32_t TAP_MAX_MS    = 400;
static constexpr uint32_t LONG_PRESS_MS = 700;
static constexpr int      TAP_MAX_DIST  = 12;
static constexpr int      SWIPE_DIST    = 40;

// FT6336U reports raw panel coordinates (240w x 320h). The application canvas
// is 320w x 240h after setRotation(3) — landscape, flipped 180° from rotation 1.
//   raw (rx, ry) with rx ∈ [0, PANEL_W), ry ∈ [0, PANEL_H)
//   canvas (cx, cy):  cx = PANEL_H - 1 - ry,  cy = rx
static inline void rawToCanvas(uint16_t rx, uint16_t ry, int16_t& cx, int16_t& cy) {
  cx = (int16_t)(PANEL_H - 1 - ry);
  cy = (int16_t)rx;
}

enum State : uint8_t { S_IDLE, S_DOWN, S_LONG_FIRED };

static State    _state = S_IDLE;
static int16_t  _x0 = 0, _y0 = 0;
static int16_t  _xLast = 0, _yLast = 0;
static uint32_t _t0 = 0;

// Single-slot event queue. If a gesture completes before main consumes the
// previous one, the new one overwrites — gestures should drain each frame.
static TouchEvent _pending = { GESTURE_NONE, 0, 0 };
static bool       _hasPending = false;

static void emit(GestureKind k, int16_t x, int16_t y) {
  _pending = { k, x, y };
  _hasPending = true;
}

bool touch_event(TouchEvent& out) {
  if (!_hasPending) return false;
  out = _pending;
  _hasPending = false;
  return true;
}

void touch_begin() {
  ft.begin();
}

void touch_poll(ButtonShim& btnA, ButtonShim& btnB) {
  uint8_t n = ft.read_touch_number();
  uint32_t now = millis();

  if (n == 0) {
    // Release edge
    if (_state == S_DOWN) {
      int dx = _xLast - _x0;
      int dy = _yLast - _y0;
      int adx = dx < 0 ? -dx : dx;
      int ady = dy < 0 ? -dy : dy;
      uint32_t dt = now - _t0;
      if (adx >= SWIPE_DIST || ady >= SWIPE_DIST) {
        if (adx > ady) emit(dx > 0 ? GESTURE_SWIPE_RIGHT : GESTURE_SWIPE_LEFT, _x0, _y0);
        else           emit(dy > 0 ? GESTURE_SWIPE_DOWN  : GESTURE_SWIPE_UP,   _x0, _y0);
      } else if (dt <= TAP_MAX_MS && adx <= TAP_MAX_DIST && ady <= TAP_MAX_DIST) {
        emit(GESTURE_TAP, _x0, _y0);
      }
      // else: held without moving past LONG_PRESS — that event already fired.
    }
    _state = S_IDLE;
    btnA._set(false, now);
    btnB._set(false, now);
    _reportTouchCount(0);
    return;
  }

  // n >= 1: read first point and translate to canvas coords.
  uint16_t rx = ft.read_touch1_x();
  uint16_t ry = ft.read_touch1_y();
  int16_t cx, cy;
  rawToCanvas(rx, ry, cx, cy);

  if (_state == S_IDLE) {
    _x0 = _xLast = cx;
    _y0 = _yLast = cy;
    _t0 = now;
    _state = S_DOWN;
  } else {
    _xLast = cx;
    _yLast = cy;
    // Fire long-press once if the finger has been roughly stationary past
    // the long-press window without converting into a swipe.
    if (_state == S_DOWN && (now - _t0) >= LONG_PRESS_MS) {
      int dx = _xLast - _x0, dy = _yLast - _y0;
      int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
      if (adx <= TAP_MAX_DIST && ady <= TAP_MAX_DIST) {
        emit(GESTURE_LONG_PRESS, _x0, _y0);
        _state = S_LONG_FIRED;
      }
    }
  }

  // Legacy ButtonShim driver: top half = BtnA, bottom half = BtnB. Keeps the
  // existing menu/page navigation working until Phase D rewires them.
  bool topHalf = (cy < (CANVAS_H / 2));
  btnA._set(topHalf, now);
  btnB._set(!topHalf, now);

  _reportTouchCount(n);
}

}  // namespace fnk
