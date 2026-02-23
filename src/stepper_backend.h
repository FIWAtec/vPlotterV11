#ifndef STEPPER_BACKEND_H
#define STEPPER_BACKEND_H

#include <Arduino.h>

// Backend selector:
// - When USE_FAST_ACCELSTEPPER is defined (e.g. via build flag -DUSE_FAST_ACCELSTEPPER=1),
//   the project uses FastAccelStepper (high speed, hardware driven).
// - Otherwise it uses AccelStepper (existing behaviour).
#ifndef USE_FAST_ACCELSTEPPER
#define USE_FAST_ACCELSTEPPER 0
#endif

#if USE_FAST_ACCELSTEPPER
  #include <FastAccelStepper.h>
#else
  #include <AccelStepper.h>
#endif

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

  // optional enable pin wiring (recommended for FastAccelStepper)
  virtual void configureEnablePin(int enablePin, bool enableActiveLow) = 0;

  virtual void run() = 0;                                // called in loop for AccelStepper; no-op for FastAccelStepper
  virtual long distanceToGo() const = 0;
  virtual long currentPosition() const = 0;
  virtual void setCurrentPosition(long pos) = 0;

  // Pulse width (AccelStepper only). FastAccelStepper generates its own pulses (few µs) and does not expose setMinPulseWidth.
  virtual void setMinPulseWidth(unsigned int us) = 0;
};

#if USE_FAST_ACCELSTEPPER

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

  // We track target, because FastAccelStepper does not expose target position in a lightweight call.
  volatile long _target = 0;
};

#else

class AccelStepperBackend final : public StepperBackend {
public:
  AccelStepperBackend(uint8_t stepPin, uint8_t dirPin);

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

  void run() override;

  long distanceToGo() const override;
  long currentPosition() const override;
  void setCurrentPosition(long pos) override;

  void setMinPulseWidth(unsigned int us) override;

private:
  AccelStepper _stepper;
};

#endif

#endif // STEPPER_BACKEND_H
