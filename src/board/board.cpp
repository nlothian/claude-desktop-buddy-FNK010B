#include "board.h"

M5_t M5;

namespace fnk {
  void touch_begin();
  void touch_poll(ButtonShim& btnA, ButtonShim& btnB);
  void power_begin();
}

// ---------- ButtonShim ----------
void ButtonShim::_set(bool pressed, uint32_t now_ms) {
  if (pressed && !_pressed) {
    _edge_pressed = true;
    _press_ms = now_ms;
  } else if (!pressed && _pressed) {
    _edge_released = true;
  }
  _pressed = pressed;
}

bool ButtonShim::wasPressed() {
  bool r = _edge_pressed;
  _edge_pressed = false;
  return r;
}

bool ButtonShim::wasReleased() {
  bool r = _edge_released;
  _edge_released = false;
  return r;
}

// ---------- M5_t ----------
void M5_t::begin() {
  Serial.begin(115200);

  fnk::power_begin();
  Lcd.init();
  Lcd.setRotation(0);
  Lcd.fillScreen(TFT_BLACK);
  fnk::touch_begin();
  Beep.begin();
}

void M5_t::update() {
  fnk::touch_poll(BtnA, BtnB);
  fnk::renderAttentionFrame();
}
