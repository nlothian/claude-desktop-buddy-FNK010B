#include "board.h"
#include <Wire.h>
#include <driver/i2s.h>
#include <math.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include "es8311/es8311.h"

// I2S / ES8311 pins (Sketch_07.2_Echo, FNK0104AB)
static constexpr int I2S_MCK   = 4;
static constexpr int I2S_BCK   = 5;
static constexpr int I2S_DINT  = 6;
static constexpr int I2S_DOUT  = 8;
static constexpr int I2S_WS    = 7;
static constexpr int AP_ENABLE = 1;
static constexpr int I2C_SDA   = 16;
static constexpr int I2C_SCL   = 15;

static constexpr i2s_port_t I2S_PORT = I2S_NUM_0;
static constexpr uint32_t SAMPLE_RATE = 44100;

static volatile uint16_t _freq = 0;
static volatile uint32_t _end_ms = 0;
static volatile bool _playing = false;
static float _phase = 0.0f;            // owned by the audio task only
static bool _ready = false;
static TaskHandle_t      _audioTask = nullptr;
static SemaphoreHandle_t _audioWake = nullptr;

// Dedicated I²S feeder. Sleeps when no tone is queued; while playing, blocks
// on i2s_write so it self-paces to the codec's 44.1 kHz consumption rate
// regardless of how busy the render loop on core 1 is.
static void _audio_task(void*) {
  constexpr int CHUNK = 256;
  int16_t buf[CHUNK];
  for (;;) {
    if (!_playing) {
      xSemaphoreTake(_audioWake, portMAX_DELAY);
      _phase = 0.0f;   // fresh start for the new tone
      continue;
    }

    if ((int32_t)(millis() - _end_ms) >= 0) {
      // Tone elapsed. Drop the amp, push one chunk of silence so the codec
      // settles cleanly, and go back to waiting.
      _playing = false;
      digitalWrite(AP_ENABLE, LOW);
      memset(buf, 0, sizeof(buf));
      size_t written = 0;
      i2s_write(I2S_PORT, buf, sizeof(buf), &written, 50 / portTICK_PERIOD_MS);
      continue;
    }

    // Square wave at full int16 amplitude — about 4-5 dB louder than a sine
    // at the same peak, since the square has more energy per cycle. Trade-off:
    // tone character is buzzer-like instead of a clean pip.
    const float step = 2.0f * (float)M_PI * (float)_freq / (float)SAMPLE_RATE;
    constexpr int16_t AMP = 32000;   // a hair below int16 max to dodge wrap-on-cast
    for (int i = 0; i < CHUNK; ++i) {
      buf[i] = (sinf(_phase) >= 0.0f) ? AMP : (int16_t)-AMP;
      _phase += step;
      if (_phase > 2.0f * (float)M_PI) _phase -= 2.0f * (float)M_PI;
    }
    size_t written = 0;
    // Block until DMA has room — this is what paces us to 44.1 kHz exactly.
    i2s_write(I2S_PORT, buf, sizeof(buf), &written, portMAX_DELAY);
  }
}

void BeepShim::begin() {
  pinMode(AP_ENABLE, OUTPUT);
  digitalWrite(AP_ENABLE, LOW);

  Wire.begin(I2C_SDA, I2C_SCL, 400000);

  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = SAMPLE_RATE;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 4;
  cfg.dma_buf_len = 256;
  cfg.use_apll = true;
  cfg.tx_desc_auto_clear = true;
  cfg.fixed_mclk = SAMPLE_RATE * 256;
  cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;

  if (i2s_driver_install(I2S_PORT, &cfg, 0, nullptr) != ESP_OK) return;

  i2s_pin_config_t pins = {};
  pins.mck_io_num = I2S_MCK;
  pins.bck_io_num = I2S_BCK;
  pins.ws_io_num = I2S_WS;
  pins.data_out_num = I2S_DOUT;
  pins.data_in_num = I2S_DINT;
  if (i2s_set_pin(I2S_PORT, &pins) != ESP_OK) return;

  if (es8311_codec_init() != ESP_OK) return;
  _ready = true;

  // Spawn the audio feeder on core 0 — the Arduino loop runs on core 1, so
  // beeps stay glitch-free even when rendering or BLE work spikes.
  _audioWake = xSemaphoreCreateBinary();
  xTaskCreatePinnedToCore(_audio_task, "audio", 4096, nullptr, 3, &_audioTask, 0);
}

void BeepShim::tone(uint16_t freq, uint16_t dur_ms) {
  if (!_ready) return;
  _freq = freq;
  _end_ms = millis() + dur_ms;
  if (!_playing) {
    digitalWrite(AP_ENABLE, HIGH);
    _playing = true;
    if (_audioWake) xSemaphoreGive(_audioWake);
  }
}

void BeepShim::mute() {
  if (!_ready) return;
  _playing = false;
  digitalWrite(AP_ENABLE, LOW);
}

void BeepShim::update() {
  // Audio is driven by the FreeRTOS task above; nothing to do per loop tick.
}
