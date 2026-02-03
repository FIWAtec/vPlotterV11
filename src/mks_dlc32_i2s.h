#ifndef MKS_DLC32_I2S_H
#define MKS_DLC32_I2S_H

#include <Arduino.h>

// Minimal I2S/bitstream stepper driver for MKS DLC32 V2.1 socket drivers.
// We output a 16-bit word through I2S pins (bck/ws/data) into the board shift-register.
// Bit mapping (common DLC32 mapping):
//   I2SO.0 = shared stepper disable (we keep it ALWAYS enabled)
//   I2SO.1 = X step
//   I2SO.2 = X dir
//   I2SO.5 = Y step
//   I2SO.6 = Y dir

class MksDlc32I2SBus {
  public:
    static void begin();
    static void setAlwaysEnabled(bool enabledAlways);

    // Request a STEP pulse for a specific bit. Non-blocking: the bus will drop the bit after pulse_us.
    static void requestStepPulse(uint8_t stepBit, uint32_t pulseUs);
    static void setDirBit(uint8_t dirBit, bool level);

    // Must be called frequently from loop to finish pulses.
    static void tick();

  private:
    static void commit();
};

class MksDlc32I2SStepper {
  public:
    // stepBit/dirBit are I2SO bit indices (0..15)
    MksDlc32I2SStepper(uint8_t stepBit, uint8_t dirBit);

    void setMaxSpeed(float vStepsPerSec);
    void setAcceleration(float aStepsPerSec2);
    void setMinPulseWidth(uint32_t us);
    void setPinsInverted(bool dirInverted);

    void enableOutputs();
    void disableOutputs();

    void move(long relative);
    void moveTo(long absolute);
    void stop();

    void setSpeed(float stepsPerSec);
    void runSpeedToPosition();

    long distanceToGo() const;
    long currentPosition() const;
    void setCurrentPosition(long pos);

  private:
    uint8_t _stepBit;
    uint8_t _dirBit;

    bool _dirInverted = false;
    uint32_t _pulseUs = 5;

    volatile long _current = 0;
    volatile long _target = 0;

    float _speed = 0.0f;      // steps/sec
    float _maxSpeed = 2000.0f;
    float _accel = 0.0f;      // unused (kept for API compatibility)

    uint32_t _stepIntervalUs = 0;
    uint32_t _lastStepUs = 0;

    void updateInterval();
};

#endif
