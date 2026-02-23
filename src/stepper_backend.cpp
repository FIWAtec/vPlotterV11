#include "stepper_backend.h"

#if USE_FAST_ACCELSTEPPER

static FastAccelStepperEngine* g_engine = nullptr;

FastAccelStepperEngine& FastStepperBackend::engine() {
  if (!g_engine) {
    g_engine = new FastAccelStepperEngine();
    // default init() is fine. For ESP32 you can optionally pin to core: init(0/1).
    g_engine->init();
  }
  return *g_engine;
}

FastStepperBackend::FastStepperBackend(uint8_t stepPin, uint8_t dirPin, int enablePin, bool enableActiveLow) {
  _dirPin = dirPin;

  _stepper = engine().stepperConnectToPin(stepPin);
  if (_stepper) {
    // dirHighCountsUp is the inversion control
    _stepper->setDirectionPin(dirPin, true);
    if (enablePin >= 0) {
      _stepper->setEnablePin((uint8_t)enablePin, enableActiveLow);
      _stepper->setAutoEnable(true);
      // Avoid enable delay to keep two-motor sync tight
      _stepper->setDelayToEnable(0);
      _stepper->setDelayToDisable(0);
    }
    // safe defaults (will be overwritten by Movement::setSpeeds/setMotionTuning)
    _stepper->setSpeedInHz(1000);
    _stepper->setAcceleration(10000);
    _target = _stepper->getCurrentPosition();
  }
}

void FastStepperBackend::setPinsInverted(bool dirInvert) {
  _dirInvert = dirInvert;
  if (_stepper) {
    // dirHighCountsUp = true means: DIR high => positive count
    // If we want to invert, set dirHighCountsUp=false.
    _stepper->setDirectionPin(_dirPin, !dirInvert);
  }
}

void FastStepperBackend::setMaxSpeed(float stepsPerSecond) {
  if (!_stepper) return;
  uint32_t hz = (stepsPerSecond < 1.0f) ? 1u : (uint32_t)stepsPerSecond;
  _stepper->setSpeedInHz(hz);
}

void FastStepperBackend::setAcceleration(float stepsPerSecond2) {
  if (!_stepper) return;
  uint32_t a = (stepsPerSecond2 < 1.0f) ? 1u : (uint32_t)stepsPerSecond2;
  _stepper->setAcceleration(a);
}

void FastStepperBackend::setSpeed(float stepsPerSecond) {
  // For FastAccelStepper, setSpeedInHz defines the max speed used for next move/runForward/runBackward
  setMaxSpeed(stepsPerSecond);
}

void FastStepperBackend::move(long relativeSteps) {
  if (!_stepper) return;
  _target = _stepper->getCurrentPosition() + relativeSteps;
  _stepper->move(relativeSteps);
}

void FastStepperBackend::moveTo(long absoluteSteps) {
  if (!_stepper) return;
  _target = absoluteSteps;
  _stepper->moveTo((int32_t)absoluteSteps);
}

void FastStepperBackend::stop() {
  if (!_stepper) return;
  _stepper->stopMove();
  // target becomes current after stop
  _target = _stepper->getCurrentPosition();
}

void FastStepperBackend::enableOutputs() {
  if (_stepper) _stepper->enableOutputs();
}

void FastStepperBackend::disableOutputs() {
  if (_stepper) _stepper->disableOutputs();
}

void FastStepperBackend::configureEnablePin(int enablePin, bool enableActiveLow) {
  if (!_stepper) return;
  if (enablePin < 0) return;
  _stepper->setEnablePin((uint8_t)enablePin, enableActiveLow);
  _stepper->setAutoEnable(true);
  _stepper->setDelayToEnable(0);
  _stepper->setDelayToDisable(0);
}

long FastStepperBackend::distanceToGo() const {
  if (!_stepper) return 0;
  return (long)_target - (long)_stepper->getCurrentPosition();
}

long FastStepperBackend::currentPosition() const {
  if (!_stepper) return 0;
  return (long)_stepper->getCurrentPosition();
}

void FastStepperBackend::setCurrentPosition(long pos) {
  if (!_stepper) return;
  _stepper->setCurrentPosition((int32_t)pos);
  _target = pos;
}

#else

AccelStepperBackend::AccelStepperBackend(uint8_t stepPin, uint8_t dirPin)
  : _stepper(AccelStepper::DRIVER, stepPin, dirPin) {}

void AccelStepperBackend::setPinsInverted(bool dirInvert) {
  _stepper.setPinsInverted(dirInvert);
}

void AccelStepperBackend::setMaxSpeed(float stepsPerSecond) {
  _stepper.setMaxSpeed(stepsPerSecond);
}

void AccelStepperBackend::setAcceleration(float stepsPerSecond2) {
  _stepper.setAcceleration(stepsPerSecond2);
}

void AccelStepperBackend::setSpeed(float stepsPerSecond) {
  _stepper.setSpeed(stepsPerSecond);
}

void AccelStepperBackend::move(long relativeSteps) {
  _stepper.move(relativeSteps);
}

void AccelStepperBackend::moveTo(long absoluteSteps) {
  _stepper.moveTo(absoluteSteps);
}

void AccelStepperBackend::stop() {
  _stepper.stop();
}

void AccelStepperBackend::enableOutputs() {
  _stepper.enableOutputs();
}

void AccelStepperBackend::disableOutputs() {
  _stepper.disableOutputs();
}

void AccelStepperBackend::configureEnablePin(int enablePin, bool enableActiveLow) {
  if (enablePin < 0) return;
  // AccelStepper enable pin: LOW disables by default unless inverted.
  _stepper.setEnablePin(enablePin);
  // If enable is active LOW, we invert the enable logic.
  _stepper.setPinsInverted(false, false, enableActiveLow);
}

void AccelStepperBackend::run() {
  _stepper.run();
}

long AccelStepperBackend::distanceToGo() const {
  return _stepper.distanceToGo();
}

long AccelStepperBackend::currentPosition() const {
  return _stepper.currentPosition();
}

void AccelStepperBackend::setCurrentPosition(long pos) {
  _stepper.setCurrentPosition(pos);
}

void AccelStepperBackend::setMinPulseWidth(unsigned int us) {
  _stepper.setMinPulseWidth(us);
}

#endif
