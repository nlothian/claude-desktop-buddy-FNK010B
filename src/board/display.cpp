#include "board.h"

namespace fnk {

// Landscape canvas (CANVAS_W x CANVAS_H). The application sprite is the same
// size as the panel post-rotation; drawing uses canvas coordinates throughout.

static bool _attentionPulse = false;
static uint32_t _lastPulseMs = 0;
static bool _pulseOn = false;
static bool _covered = false;
static int _coveredCount = 0;
static uint32_t _coveredSince = 0;

void setAttentionPulse(bool on) {
  _attentionPulse = on;
  if (!on && _pulseOn) {
    // Clear the border immediately
    M5.Lcd.drawRect(0, 0, CANVAS_W, CANVAS_H, TFT_BLACK);
    M5.Lcd.drawRect(1, 1, CANVAS_W - 2, CANVAS_H - 2, TFT_BLACK);
    _pulseOn = false;
  }
}

void renderAttentionFrame() {
  if (!_attentionPulse) return;
  uint32_t now = millis();
  if (now - _lastPulseMs < 400) return;
  _lastPulseMs = now;
  _pulseOn = !_pulseOn;
  uint16_t col = _pulseOn ? TFT_RED : TFT_BLACK;
  M5.Lcd.drawRect(0, 0, CANVAS_W, CANVAS_H, col);
  M5.Lcd.drawRect(1, 1, CANVAS_W - 2, CANVAS_H - 2, col);
}

// Touch driver pokes this each scan: count of simultaneous touch points (0..2).
void _reportTouchCount(int count) {
  if (count >= 2) {
    if (_coveredCount < 2) {
      _coveredCount = 2;
      _coveredSince = millis();
    } else if (!_covered && millis() - _coveredSince > 1500) {
      _covered = true;
    }
  } else {
    _coveredCount = count;
    _covered = false;
  }
}

bool isCovered() { return _covered; }

}  // namespace fnk
