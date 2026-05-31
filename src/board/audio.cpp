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
static volatile uint32_t _sustain_ms = 0;   // requested hold time (excl. envelope)
static volatile bool _playing = false;
static float    _phase = 0.0f;   // audio-task-owned phase accumulator
static uint32_t _env   = 0;      // audio-task-owned: audible samples emitted so far
static volatile bool _retrigger = false;  // tone() arms; task restarts the envelope
static bool _ready = false;
static TaskHandle_t      _audioTask = nullptr;
static SemaphoreHandle_t _audioWake = nullptr;

// Tone shape. A clean sine pushed through a soft attack / sustain / release
// envelope — no click on/off, no harsh harmonics — with a gentle release tail
// so the beep rings off pleasantly rather than snapping silent. Loudness is set
// in the codec (es8311), not here, so the sine peak stays fixed near full scale
// for the best signal-to-noise; BeepShim::volume() attenuates downstream.
static constexpr uint32_t ATTACK_MS  = 12;
static constexpr uint32_t RELEASE_MS = 90;
static constexpr int16_t  PEAK       = 30000;

// Amp power gating. The external amp (AP_ENABLE, active-low) is held off while
// the screen is asleep — no quiescent draw, no idle hiss — and enabled lazily
// on the first beep. Because it needs tens of ms to ramp out of shutdown, a
// beep that finds the amp off plays a short silent lead-in (until _warmUntil)
// so the audible tone lands at full volume. Beeps with the amp already on play
// instantly. main.cpp calls amp(false) on each screen-off transition.
static volatile bool     _ampOn     = false;
static volatile uint32_t _warmUntil = 0;
static constexpr uint32_t AMP_WARMUP_MS = 100;
// Power the amp down this long after the last tone ends — reclaiming the
// quiescent draw/hiss the lazy-enable otherwise leaves on once a beep has run
// (notably on USB, where the screen may never sleep to trigger amp(false)).
static constexpr uint32_t IDLE_AMP_OFF_MS = 4000;

// Dedicated I²S feeder. Sleeps when no tone is queued; while playing, blocks
// on i2s_write so it self-paces to the codec's 44.1 kHz consumption rate
// regardless of how busy the render loop on core 1 is.
static void _audio_task(void*) {
  constexpr int CHUNK = 256;
  int16_t buf[CHUNK];
  for (;;) {
    if (!_playing) {
      // Idle. While the amp is still powered (left on after the last tone), wake
      // periodically so we can drop it after IDLE_AMP_OFF_MS of silence; once
      // it's off there's nothing to time out, so wait indefinitely.
      TickType_t wait = _ampOn ? (IDLE_AMP_OFF_MS / portTICK_PERIOD_MS) : portMAX_DELAY;
      if (xSemaphoreTake(_audioWake, wait) == pdFALSE) {
        if (!_playing && _ampOn) {   // still idle — power the amp down
          digitalWrite(AP_ENABLE, HIGH);
          _ampOn = false;
          _warmUntil = 0;
        }
        continue;
      }
      _phase = 0.0f;   // a tone arrived — fresh start
      _env   = 0;      // envelope begins at the attack
      _retrigger = false;
      continue;
    }

    if (_retrigger) {
      // tone() re-armed while a tone (or its release tail) was still playing —
      // restart the envelope from the attack so the new beep isn't clipped by
      // the previous one's position.
      _phase = 0.0f;
      _env   = 0;
      _retrigger = false;
    }

    if ((int32_t)(millis() - _warmUntil) < 0) {
      // Amp still ramping out of shutdown — feed silence (it keeps the codec
      // clocked and the amp settling) so the tone that follows lands cleanly.
      // _phase / _env stay at 0, so the envelope starts when the window ends.
      memset(buf, 0, sizeof(buf));
      size_t written = 0;
      i2s_write(I2S_PORT, buf, sizeof(buf), &written, portMAX_DELAY);
      continue;
    }

    // Envelope lengths in samples; total audible tone = attack + sustain + release.
    const uint32_t atk   = ATTACK_MS  * SAMPLE_RATE / 1000;
    const uint32_t rel   = RELEASE_MS * SAMPLE_RATE / 1000;
    const uint32_t sus   = _sustain_ms * SAMPLE_RATE / 1000;
    const uint32_t total = atk + sus + rel;

    if (_env >= total) {
      // Tone, including its release tail, has finished. Leave the amp enabled
      // (see begin) and push one chunk of silence so the codec settles cleanly,
      // then wait for the next tone.
      _playing = false;
      memset(buf, 0, sizeof(buf));
      size_t written = 0;
      i2s_write(I2S_PORT, buf, sizeof(buf), &written, 50 / portTICK_PERIOD_MS);
      continue;
    }

    const float step = 2.0f * (float)M_PI * (float)_freq / (float)SAMPLE_RATE;
    for (int i = 0; i < CHUNK; ++i) {
      // Attack / sustain / release gain in [0,1], smoothstep-rounded so the
      // ramps have no click and add no harsh harmonics.
      uint32_t s = _env;
      float g;
      if      (s < atk)         g = (float)s / (float)atk;
      else if (s < atk + sus)   g = 1.0f;
      else if (s < total)       g = (float)(total - s) / (float)rel;
      else                      g = 0.0f;
      g = g * g * (3.0f - 2.0f * g);   // smoothstep for rounded edges
      buf[i] = (int16_t)((float)PEAK * g * sinf(_phase));
      _phase += step;
      if (_phase > 2.0f * (float)M_PI) _phase -= 2.0f * (float)M_PI;
      _env++;
    }
    size_t written = 0;
    // Block until DMA has room — this is what paces us to 44.1 kHz exactly.
    i2s_write(I2S_PORT, buf, sizeof(buf), &written, portMAX_DELAY);
  }
}

void BeepShim::begin() {
  pinMode(AP_ENABLE, OUTPUT);
  digitalWrite(AP_ENABLE, HIGH);   // amp off (active-low) until the first beep
  _ampOn = false;

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
  _sustain_ms = dur_ms;   // hold time; the task adds the attack + release around it
  uint32_t now = millis();
  if (!_ampOn) {
    // Amp was off (screen asleep / first beep). Enable it and prepend a silent
    // warm-up window so the tone lands at full volume instead of fading in with
    // the amp's ramp. The audible envelope only starts once _warmUntil passes.
    digitalWrite(AP_ENABLE, LOW);
    _ampOn = true;
    _warmUntil = now + AMP_WARMUP_MS;
  } else {
    _warmUntil = now;   // already warm — no lead-in, play immediately
  }
  _retrigger = true;   // restart the envelope from attack (the task resets _env/_phase)
  if (!_playing) {
    _playing = true;
    if (_audioWake) xSemaphoreGive(_audioWake);
  }
}

// Gate the speaker power amp. main.cpp calls amp(false) when the screen sleeps
// so the amp draws nothing and can't hiss while idle; the next beep re-enables
// it (with the warm-up above). amp(true) is available for symmetry but unused —
// enabling eagerly would defeat the warm-up for a beep fired right afterward.
void BeepShim::amp(bool on) {
  if (!_ready) return;
  if (on) {
    digitalWrite(AP_ENABLE, LOW);
    _ampOn = true;
  } else {
    _playing = false;               // drop any tone in flight
    digitalWrite(AP_ENABLE, HIGH);
    _ampOn = false;
    _warmUntil = 0;
  }
}

void BeepShim::mute() {
  amp(false);
}

// Clean codec window for this speaker+amp. Slider 1..100 maps linearly across
// [MIN, MAX]; 0 is a true mute. MAX sits just under the distortion onset so the
// top of the slider stays clean; MIN is the quietest level we still want
// audible. Starting calibration — tune by ear.
static constexpr int VOL_CODEC_MIN = 45;
static constexpr int VOL_CODEC_MAX = 74;

// Set beep loudness as a 0..100 percentage, applied as real attenuation in the
// codec ahead of the amp (scaling the digital sample amplitude does nothing —
// the amp clips a full tone to the rails regardless).
void BeepShim::volume(uint8_t pct) {
  if (pct > 100) pct = 100;
  int codecVol = pct
    ? VOL_CODEC_MIN + (int)pct * (VOL_CODEC_MAX - VOL_CODEC_MIN) / 100
    : 0;
  es8311_codec_set_volume(codecVol);
}

void BeepShim::update() {
  // Audio is driven by the FreeRTOS task above; nothing to do per loop tick.
}
