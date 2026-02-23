#ifndef STEPPER_BACKEND_H
#define STEPPER_BACKEND_H

#include <Arduino.h>
#include <FastAccelStepper.h>

// Minimal interface required by Movement.
// This keeps Movement logic intact while swapping stepper libraries.
class StepperBackend {
public:
  virtual ~StepperBackend() = default;

  virtual void setPinsInverted(bool dirInvert) = 0;

  virtual void setMaxSpeed(float stepsPerSecond) = 0;
  virtual void setAcceleration(float stepsPerSecond2) = 0;

  virtual void setSpeed(float stepsPerSecond) = 0;       // used for manual jog
  virtual void move(long relativeSteps) = 0;             // used for manual jog
  virtual void moveTo(long absoluteSteps) = 0;           // path moves
  virtual void stop() = 0;

  virtual void enableOutputs() = 0;
  virtual void disableOutputs() = 0;

  // optional enable pin wiring
  virtual void configureEnablePin(int enablePin, bool enableActiveLow) = 0;

  // called from loop; FastAccelStepper runs autonomously (no-op)
  virtual void run() = 0;

  virtual long distanceToGo() const = 0;
  virtual long currentPosition() const = 0;
  virtual void setCurrentPosition(long pos) = 0;

  // Pulse width is not configurable with FastAccelStepper. Keep API as no-op for compatibility.
  virtual void setMinPulseWidth(unsigned int us) = 0;
};

// FastAccelStepper uses a global engine
class FastStepperBackend final : public StepperBackend {
public:
  FastStepperBackend(uint8_t stepPin, uint8_t dirPin, int enablePin = -1, bool enableActiveLow = true);

  void setPinsInverted(bool dirInvert) override;

  void setMaxSpeed(float stepsPerSecond) override;
  void setAcceleration(float stepsPerSecond2) override;

  void setSpeed(float stepsPerSecond) override;
  void move(long relativeSteps) override;
  void moveTo(long absoluteSteps) override;
  void stop() override;

  void enableOutputs() override;
  void disableOutputs() override;

  void configureEnablePin(int enablePin, bool enableActiveLow) override;

  void run() override {} // FastAccelStepper runs in its own task/ISR

  long distanceToGo() const override;
  long currentPosition() const override;
  void setCurrentPosition(long pos) override;

  void setMinPulseWidth(unsigned int) override {} // not supported

private:
  static FastAccelStepperEngine& engine();

  FastAccelStepper* _stepper = nullptr;
  uint8_t _dirPin = 255;
  bool _dirInvert = false;

  volatile long _target = 0;
};

#endif // STEPPER_BACKEND_H
