#include "mks_dlc32_i2s.h"

#include <driver/i2s.h>

// We keep implementation intentionally small and deterministic.
// This is NOT a high-performance motion engine like FluidNC, but it is good enough
// to make the socketed TMC2209 drivers move with your existing code structure.

namespace {
  constexpr i2s_port_t I2S_PORT = I2S_NUM_0;

  // DLC32 typical pins (bitstream/shift-register):
  constexpr int PIN_BCK  = 16;
  constexpr int PIN_WS   = 17;
  constexpr int PIN_DATA = 21;

  // Output word (16 bits are enough for DLC32 shift register signals we use)
  volatile uint16_t g_word = 0;
  volatile bool g_alwaysEnabled = true;

  // Track active step pulses (up to 2 at same time is enough for X/Y)
  struct Pulse {
    bool active = false;
    uint8_t bit = 0;
    uint32_t endUs = 0;
  };

  Pulse g_pulses[4];

  inline void setBit(uint8_t bit, bool level) {
    if (bit > 15) return;
    uint16_t mask = (uint16_t)(1u << bit);
    if (level) g_word |= mask;
    else g_word &= (uint16_t)~mask;
  }

  inline void ensureEnabledBit() {
    // I2SO.0 is shared stepper disable. We keep outputs enabled forever.
    // Common mapping: 0 = enabled, 1 = disabled. We force it to 0.
    if (g_alwaysEnabled) {
      setBit(0, false);
    }
  }

  inline void i2sWriteWord(uint16_t w) {
    size_t written = 0;
    // send as 16-bit mono frame
    i2s_write(I2S_PORT, (const void*)&w, sizeof(w), &written, 0);
  }
}

void MksDlc32I2SBus::begin() {
  static bool started = false;
  if (started) return;
  started = true;

  i2s_config_t cfg{};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = 100000; // we manually push words, sample_rate not critical here
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = 0;
  cfg.dma_buf_count = 4;
  cfg.dma_buf_len = 64;
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = true;
  cfg.fixed_mclk = 0;

  i2s_pin_config_t pins{};
  pins.bck_io_num = PIN_BCK;
  pins.ws_io_num = PIN_WS;
  pins.data_out_num = PIN_DATA;
  pins.data_in_num = I2S_PIN_NO_CHANGE;

  i2s_driver_install(I2S_PORT, &cfg, 0, nullptr);
  i2s_set_pin(I2S_PORT, &pins);

  // Reset word and force enabled.
  g_word = 0;
  ensureEnabledBit();
  i2sWriteWord(g_word);
}

void MksDlc32I2SBus::setAlwaysEnabled(bool enabledAlways) {
  g_alwaysEnabled = enabledAlways;
  ensureEnabledBit();
  commit();
}

void MksDlc32I2SBus::commit() {
  ensureEnabledBit();
  uint16_t w = g_word;
  i2sWriteWord(w);
}

void MksDlc32I2SBus::setDirBit(uint8_t dirBit, bool level) {
  setBit(dirBit, level);
  commit();
}

void MksDlc32I2SBus::requestStepPulse(uint8_t stepBit, uint32_t pulseUs) {
  if (pulseUs < 1) pulseUs = 1;
  uint32_t now = micros();

  // Find a free pulse slot
  for (auto &p : g_pulses) {
    if (!p.active) {
      p.active = true;
      p.bit = stepBit;
      p.endUs = now + pulseUs;
      setBit(stepBit, true);
      commit();
      return;
    }
  }

  // If all slots used, just force the pulse (rare at our speeds)
  setBit(stepBit, true);
  commit();
}

void MksDlc32I2SBus::tick() {
  uint32_t now = micros();
  bool changed = false;

  for (auto &p : g_pulses) {
    if (p.active && (int32_t)(now - p.endUs) >= 0) {
      p.active = false;
      setBit(p.bit, false);
      changed = true;
    }
  }

  if (changed) commit();
}

// =====================
// Stepper
// =====================

MksDlc32I2SStepper::MksDlc32I2SStepper(uint8_t stepBit, uint8_t dirBit)
  : _stepBit(stepBit), _dirBit(dirBit) {
  MksDlc32I2SBus::begin();
  MksDlc32I2SBus::setAlwaysEnabled(true);
}

void MksDlc32I2SStepper::setMaxSpeed(float vStepsPerSec) {
  if (vStepsPerSec < 1.0f) vStepsPerSec = 1.0f;
  _maxSpeed = vStepsPerSec;
}

void MksDlc32I2SStepper::setAcceleration(float aStepsPerSec2) {
  _accel = aStepsPerSec2; // currently not used
}

void MksDlc32I2SStepper::setMinPulseWidth(uint32_t us) {
  if (us < 1) us = 1;
  if (us > 2000) us = 2000;
  _pulseUs = us;
}

void MksDlc32I2SStepper::setPinsInverted(bool dirInverted) {
  _dirInverted = dirInverted;
}

void MksDlc32I2SStepper::enableOutputs() {
  MksDlc32I2SBus::setAlwaysEnabled(true);
}

void MksDlc32I2SStepper::disableOutputs() {
  // intentionally ignored: user wants drivers always enabled.
  MksDlc32I2SBus::setAlwaysEnabled(true);
}

void MksDlc32I2SStepper::move(long relative) {
  moveTo(_current + relative);
}

void MksDlc32I2SStepper::moveTo(long absolute) {
  _target = absolute;
}

void MksDlc32I2SStepper::stop() {
  _target = _current;
}

void MksDlc32I2SStepper::setSpeed(float stepsPerSec) {
  if (stepsPerSec > _maxSpeed) stepsPerSec = _maxSpeed;
  if (stepsPerSec < -_maxSpeed) stepsPerSec = -_maxSpeed;
  _speed = stepsPerSec;
  updateInterval();
}

void MksDlc32I2SStepper::updateInterval() {
  float v = fabs(_speed);
  if (v < 0.01f) {
    _stepIntervalUs = 0;
    return;
  }
  _stepIntervalUs = (uint32_t)(1000000.0f / v);
  if (_stepIntervalUs < 1) _stepIntervalUs = 1;
}

void MksDlc32I2SStepper::runSpeedToPosition() {
  MksDlc32I2SBus::tick();

  long dist = _target - _current;
  if (dist == 0) return;
  if (_stepIntervalUs == 0) return;

  uint32_t now = micros();
  if ((uint32_t)(now - _lastStepUs) < _stepIntervalUs) return;
  _lastStepUs = now;

  int dir = (dist > 0) ? 1 : -1;
  bool dirLevel = (dir > 0);
  if (_dirInverted) dirLevel = !dirLevel;
  MksDlc32I2SBus::setDirBit(_dirBit, dirLevel);

  // Fire one non-blocking step pulse
  MksDlc32I2SBus::requestStepPulse(_stepBit, _pulseUs);

  _current += dir;
}

long MksDlc32I2SStepper::distanceToGo() const {
  return _target - _current;
}

long MksDlc32I2SStepper::currentPosition() const {
  return _current;
}

void MksDlc32I2SStepper::setCurrentPosition(long pos) {
  _current = pos;
  _target = pos;
}
