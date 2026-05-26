#include "board.h"
#include <esp_sleep.h>

// FNK0104B analog/PWM
static constexpr int BAT_ADC_PIN = 9;
static constexpr int TFT_BL_PIN  = 45;
static constexpr int BL_PWM_CH   = 7;
static constexpr int BL_PWM_FREQ = 5000;
static constexpr int BL_PWM_BITS = 8;

static int _lastDuty = 200;   // remembered while LDO2 "off"
static bool _ldo2On = true;

namespace fnk {
void power_begin() {
  ledcSetup(BL_PWM_CH, BL_PWM_FREQ, BL_PWM_BITS);
  ledcAttachPin(TFT_BL_PIN, BL_PWM_CH);
  ledcWrite(BL_PWM_CH, _lastDuty);
  analogReadResolution(12);
}
}

void AxpShim::ScreenBreath(int level) {
  // Source passes 20, 40, 60, 80, 100. Map linearly to 8-bit PWM duty.
  if (level < 0) level = 0;
  if (level > 100) level = 100;
  _lastDuty = (level * 255) / 100;
  if (_ldo2On) ledcWrite(BL_PWM_CH, _lastDuty);
}

void AxpShim::SetLDO2(bool on) {
  _ldo2On = on;
  ledcWrite(BL_PWM_CH, on ? _lastDuty : 0);
}

void AxpShim::PowerOff() {
  ledcWrite(BL_PWM_CH, 0);
  esp_deep_sleep_start();
}

uint8_t AxpShim::GetBtnPress() {
  return 0;  // no physical power button on this board
}

float AxpShim::GetVBusVoltage() {
  // No dedicated VBus sense pin. Approximate "on USB" as battery voltage
  // above the typical full-charge plateau, which is what the application
  // actually tests (`> 4.0f`).
  float vbat = AxpShim::GetBatVoltage();
  return (vbat > 4.10f) ? 5.0f : 0.0f;
}

float AxpShim::GetBatVoltage() {
  // From Sketch_05.1_Battery_Voltage: 2:1 divider on GPIO 9.
  return analogReadMilliVolts(BAT_ADC_PIN) * 2.0f / 1000.0f;
}

float AxpShim::GetBatCurrent() {
  return 0.0f;  // no fuel-gauge IC on this board
}

float AxpShim::GetTempInAXP192() {
  return temperatureRead();  // ESP32-S3 internal temp sensor
}
