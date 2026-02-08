#include <math.h>
#include <stdexcept>
#include <algorithm>

#include "display.h"
#include "movement.h"
#include "service/weblog.h"

HardwareSerial TMCSerial(1);

int printSpeedSteps = 1200;
int moveSpeedSteps = 2000;

Movement::Movement(Display* display) {
    this->display = display;

    // Init shared UART bus for TMC2209
    TMCSerial.begin(115200, SERIAL_8N1, TMC_UART_PIN, TMC_UART_PIN);

    // Init TMC2209 via UART (addresses from MS1/MS2 hardware state)
    driverX = new TMC2209Stepper(&TMCSerial, R_SENSE, 0);
    driverY = new TMC2209Stepper(&TMCSerial, R_SENSE, 1);

    driverX->begin();
    driverY->begin();



// UART-Check direkt nach begin()

    driverX = new TMC2209Stepper(&TMCSerial, R_SENSE, 0); // X addr 0
    driverY = new TMC2209Stepper(&TMCSerial, R_SENSE, 1); // Y addr 1
    const uint32_t ioinX = driverX->IOIN();
    const uint32_t ioinY = driverY->IOIN();
    WebLog::info("UART X IOIN=0x" + String(ioinX, HEX));
    WebLog::info("UART Y IOIN=0x" + String(ioinY, HEX));





    driverX->rms_current(1200); // 1.2A
    driverY->rms_current(1200); // 1.2A

    driverX->microsteps(16);
    driverY->microsteps(16);

    driverX->en_spreadCycle(false);
    driverY->en_spreadCycle(false);

    driverX->pwm_autoscale(true);
    driverY->pwm_autoscale(true);

    // Init FastAccelStepper
    engine.init();

    leftMotor = engine.stepperConnectToPin(LEFT_STEP_PIN);
    rightMotor = engine.stepperConnectToPin(RIGHT_STEP_PIN);

    if (leftMotor) {
        leftMotor->setDirectionPin(LEFT_DIR_PIN);

        // EN is hardwired LOW on hardware -> no software EN pin control
        leftMotor->setAutoEnable(false);

        leftMotor->setSpeedInHz(moveSpeedSteps);
        leftMotor->setAcceleration(accelerationSteps);
        leftMotor->setCurrentPosition(0);
    }

    if (rightMotor) {
        // Right direction inversion kept (as in your earlier working variant)
        rightMotor->setDirectionPin(RIGHT_DIR_PIN, false);

        // EN is hardwired LOW on hardware -> no software EN pin control
        rightMotor->setAutoEnable(false);

        rightMotor->setSpeedInHz(moveSpeedSteps);
        rightMotor->setAcceleration(accelerationSteps);
        rightMotor->setCurrentPosition(0); 
    }

    topDistance = -1;
    moving = false;
    homed = false;
    startedHoming = false;
}

void Movement::setPlannerConfig(const PlannerConfig& cfg) {
    plannerCfg = cfg;

    if (plannerCfg.junctionDeviationMM < 0.001) plannerCfg.junctionDeviationMM = 0.001;
    if (plannerCfg.junctionDeviationMM > 2.0) plannerCfg.junctionDeviationMM = 2.0;
    if (plannerCfg.lookaheadSegments < 1) plannerCfg.lookaheadSegments = 1;
    if (plannerCfg.lookaheadSegments > 128) plannerCfg.lookaheadSegments = 128;
    if (plannerCfg.minSegmentTimeMs < 0) plannerCfg.minSegmentTimeMs = 0;
    if (plannerCfg.minSegmentTimeMs > 100) plannerCfg.minSegmentTimeMs = 100;
    if (plannerCfg.cornerSlowdown < 0.05) plannerCfg.cornerSlowdown = 0.05;
    if (plannerCfg.cornerSlowdown > 1.0) plannerCfg.cornerSlowdown = 1.0;
    if (plannerCfg.minCornerFactor < 0.05) plannerCfg.minCornerFactor = 0.05;
    if (plannerCfg.minCornerFactor > 1.0) plannerCfg.minCornerFactor = 1.0;
    if (plannerCfg.sCurveFactor < 0.0) plannerCfg.sCurveFactor = 0.0;
    if (plannerCfg.sCurveFactor > 1.0) plannerCfg.sCurveFactor = 1.0;
    if (plannerCfg.minSegmentLenMM < 0.0) plannerCfg.minSegmentLenMM = 0.0;
    if (plannerCfg.collinearDeg < 0.1) plannerCfg.collinearDeg = 0.1;
    if (plannerCfg.collinearDeg > 20.0) plannerCfg.collinearDeg = 20.0;
}

Movement::PlannerConfig Movement::getPlannerConfig() const {
    return plannerCfg;
}

void Movement::setTopDistance(const int distance) {
    WebLog::info("TopDistance | set to " + String(distance));
    topDistance = distance;
    minSafeY = safeYFraction * topDistance;
    minSafeXOffset = safeXFraction * topDistance;
    width = topDistance - 2 * minSafeXOffset;
}

void Movement::resumeTopDistance(int distance) {
    setTopDistance(distance);
    homed = true;

    const Point homeCoordinates = getHomeCoordinates();
    X = homeCoordinates.x;
    Y = homeCoordinates.y;
    lastSegmentDX = 0.0;
    lastSegmentDY = 0.0;
    lastDirX = 0;
    lastDirY = 0;

    const Lengths lengths = getBeltLengths(homeCoordinates.x, homeCoordinates.y);
    if (leftMotor) leftMotor->setCurrentPosition(lengths.left);
    if (rightMotor) rightMotor->setCurrentPosition(lengths.right);

    moving = false;
}

void Movement::setOrigin() {
    const int hs = homedStepsOffsetSteps();
    if (leftMotor) leftMotor->setCurrentPosition(hs);
    if (rightMotor) rightMotor->setCurrentPosition(hs);
    homed = true;
}

void Movement::leftStepper(const int dir) {
    if (!leftMotor) return;

    if (dir > 0) {
        leftMotor->setAcceleration(accelerationSteps);
        leftMotor->setSpeedInHz(printSpeedSteps);
        leftMotor->move(infiniteStepsSteps);
    } else if (dir < 0) {
        leftMotor->setAcceleration(accelerationSteps);
        leftMotor->setSpeedInHz(printSpeedSteps);
        leftMotor->move(-infiniteStepsSteps);
    } else {
        leftMotor->setAcceleration(accelerationSteps);
        leftMotor->stopMove();
    }
    moving = true;
}

void Movement::rightStepper(const int dir) {
    if (!rightMotor) return;

    if (dir > 0) {
        rightMotor->setAcceleration(accelerationSteps);
        rightMotor->setSpeedInHz(printSpeedSteps);
        rightMotor->move(infiniteStepsSteps);
    } else if (dir < 0) {
        rightMotor->setAcceleration(accelerationSteps);
        rightMotor->setSpeedInHz(printSpeedSteps);
        rightMotor->move(-infiniteStepsSteps);
    } else {
        rightMotor->setAcceleration(accelerationSteps);
        rightMotor->stopMove();
    }
    moving = true;
}

Movement::Point Movement::getHomeCoordinates() {
    if (topDistance == -1) {
        return Point(0, 0);
    }
    return Point(width / 2.0, HOME_Y_OFFSET_MM);
}

int Movement::extendToHome() {
    setOrigin();
    auto homeCoordinates = getHomeCoordinates();
    startedHoming = true;
    auto moveTime = beginLinearTravel(homeCoordinates.x, homeCoordinates.y, moveSpeedSteps);
    return int(ceil(moveTime));
}

void Movement::runSteppers() {
    if (!moving) return;

    bool leftRunning = leftMotor ? leftMotor->isRunning() : false;
    bool rightRunning = rightMotor ? rightMotor->isRunning() : false;

    if (!leftRunning && !rightRunning) {
        moving = false;
    }
}

inline void Movement::getLeftTangentPoint(const double frameX, const double frameY, const double gamma, double& x_PL, double& y_PL) const {
    const double s_L = d_t / 2.0;
    const double P_LX = s_L * cos(gamma) - d_p * sin(gamma);
    const double P_LY = s_L * sin(gamma) + d_p * cos(gamma);
    x_PL = frameX - P_LX;
    y_PL = frameY - P_LY;
}

inline void Movement::getRightTangentPoint(const double frameX, const double frameY, const double gamma, double& x_PR, double& y_PR) const {
    const double s_R = d_t / 2.0;
    const double P_RX = s_R * cos(gamma) + d_p * sin(gamma);
    const double P_RY = s_R * sin(gamma) - d_p * cos(gamma);
    x_PR = frameX + P_RX;
    y_PR = frameY + P_RY;
}

void Movement::getBeltAngles(const double frameX, const double frameY, const double gamma, double& phi_L, double& phi_R) const {
    double x_PL, y_PL;
    getLeftTangentPoint(frameX, frameY, gamma, x_PL, y_PL);
    phi_L = atan2(y_PL, x_PL);

    double x_PR, y_PR;
    getRightTangentPoint(frameX, frameY, gamma, x_PR, y_PR);
    phi_R = atan2(y_PR, topDistance - x_PR);
}

void Movement::getBeltForces(const double phi_L, const double phi_R, double& F_L, double& F_R) const {
    const double F_G = mass_bot * g_constant;
    F_R = F_G * cos(phi_L) / sin(phi_L + phi_R);
    F_L = F_G * cos(phi_R) / sin(phi_L + phi_R);
}

double Movement::solveTorqueEquilibrium(const double phi_L, const double phi_R, const double F_L, const double F_R, const double gamma_init) const {
    const double s_L = d_t / 2.0;
    const double s_R = d_t / 2.0;

    double gamma_best = 99999999;
    double T_delta_best = 99999999;

    constexpr double gamma_step = 0.20 * PI / 180.0;
    constexpr double gamma_min = -90.0 * PI / 180.0;
    constexpr double gamma_max = 90.0 * PI / 180.0;
    constexpr double gamma_search_window = 2.0 * PI / 180.0;

    for (double gamma = gamma_init - gamma_search_window; gamma > gamma_min && gamma < gamma_max && gamma <= gamma_init + gamma_search_window; gamma += gamma_step) {
        const double alpha = phi_L - gamma;
        const double beta = phi_R + gamma;

        const double T_L = s_L * sin(alpha) * F_L;
        const double T_R = s_R * sin(beta) * F_R;

        const double s_m = d_m * tan(gamma);
        const double F_G = mass_bot * g_constant;
        const double F_m = F_G * cos(gamma);
        const double T_m = s_m * F_m;

        const double T_delta = T_R - T_L + T_m;

        if (abs(T_delta) < abs(T_delta_best)) {
            T_delta_best = T_delta;
            gamma_best = gamma;
        } else {
            return gamma_best;
        }
    }
    return gamma_best;
}

inline double Movement::getDilationCorrectedBeltLength(double belt_length_mm, double F_belt) const {
    const double elongation_factor = 1.0 + belt_elongation_coefficient * F_belt;
    return belt_length_mm / elongation_factor;
}

Movement::Lengths Movement::getBeltLengths(const double x, const double y) {
    const double frameX = x + minSafeXOffset;
    const double frameY = y + minSafeY;

    double gamma = gamma_last_position;
    double phi_L = 0.0, phi_R = 0.0;
    double F_L = 0.0, F_R = 0.0;

    constexpr int solver_max_iterations = 20;
    constexpr double gamma_delta_termination = 0.25 / 180.0 * PI;

    for (int i = 0; i < solver_max_iterations; i++) {
        getBeltAngles(frameX, frameY, gamma, phi_L, phi_R);
        getBeltForces(phi_L, phi_R, F_L, F_R);

        const double gamma_last = gamma;
        gamma = solveTorqueEquilibrium(phi_L, phi_R, F_L, F_R, gamma);

        if (abs(gamma_last - gamma) < gamma_delta_termination) break;
    }

    gamma_last_position = gamma;

    double leftX, leftY, rightX, rightY;
    getLeftTangentPoint(frameX, frameY, gamma, leftX, leftY);
    getRightTangentPoint(frameX, frameY, gamma, rightX, rightY);

    const double leftLegFlat = sqrt(pow(leftX, 2) + pow(leftY, 2));
    const double rightLegFlat = sqrt(pow(topDistance - rightX, 2) + pow(rightY, 2));

    double leftLeg = sqrt(pow(leftLegFlat, 2) + pow(midPulleyToWall, 2));
    double rightLeg = sqrt(pow(rightLegFlat, 2) + pow(midPulleyToWall, 2));

    leftLeg = getDilationCorrectedBeltLength(leftLeg, F_L);
    rightLeg = getDilationCorrectedBeltLength(rightLeg, F_R);

    return Lengths(mmToSteps(leftLeg), mmToSteps(rightLeg));
}

double Movement::computeCornerFactor(double dx, double dy) const {
    const double len = sqrt(dx * dx + dy * dy);
    const double prevLen = sqrt(lastSegmentDX * lastSegmentDX + lastSegmentDY * lastSegmentDY);
    if (len < 1e-6 || prevLen < 1e-6) return 1.0;

    double dot = (dx * lastSegmentDX + dy * lastSegmentDY) / (len * prevLen);
    dot = std::max(-1.0, std::min(1.0, dot));
    const double angle = acos(dot);
    const double sharpness = angle / PI;

    double f = 1.0 - sharpness * plannerCfg.cornerSlowdown;
    if (f < plannerCfg.minCornerFactor) f = plannerCfg.minCornerFactor;
    if (f > 1.0) f = 1.0;
    return f;
}

float Movement::beginLinearTravel(double x, double y, int speed) {
    if (topDistance == -1 || !homed) throw std::invalid_argument("not ready");
    if (x < 0 || (x - 1) > width) throw std::invalid_argument("Invalid x");
    if (y < 0) throw std::invalid_argument("Invalid y");
    if (speed <= 0) throw std::invalid_argument("Invalid speed");

    double tx = x;
    double ty = y;
    const double dx = tx - X;
    const double dy = ty - Y;
    int dirX = (dx > 1e-6) ? 1 : ((dx < -1e-6) ? -1 : 0);
    int dirY = (dy > 1e-6) ? 1 : ((dy < -1e-6) ? -1 : 0);

    if (lastDirX != 0 && dirX != 0 && dirX != lastDirX) tx += dirX * plannerCfg.backlashXmm;
    if (lastDirY != 0 && dirY != 0 && dirY != lastDirY) ty += dirY * plannerCfg.backlashYmm;

    tx = std::max(0.0, std::min(width, tx));
    if (ty < 0.0) ty = 0.0;

    const auto lengths = getBeltLengths(tx, ty);
    const int leftLegSteps = lengths.left;
    const int rightLegSteps = lengths.right;

    const int curL = leftMotor ? leftMotor->getCurrentPosition() : 0;
    const int curR = rightMotor ? rightMotor->getCurrentPosition() : 0;
    const int deltaLeft = abs(curL - leftLegSteps);
    const int deltaRight = abs(curR - rightLegSteps);

    const int maxDelta = (deltaLeft >= deltaRight) ? deltaLeft : deltaRight;
    if (maxDelta == 0) {
        moving = false;
        X = tx;
        Y = ty;
        return 0.0f;
    }

    double cornerFactor = computeCornerFactor(dx, dy);
    double targetSpeed = speed * cornerFactor;

    if (plannerCfg.minSegmentTimeMs > 0) {
        const double minTimeS = (double)plannerCfg.minSegmentTimeMs / 1000.0;
        const double maxAllowedByTime = (double)maxDelta / minTimeS;
        if (targetSpeed > maxAllowedByTime) targetSpeed = maxAllowedByTime;
    }

    if (targetSpeed < 1.0) targetSpeed = 1.0;

    double accelScale = 1.0 - ((1.0 - cornerFactor) * plannerCfg.sCurveFactor);
    if (accelScale < 0.2) accelScale = 0.2;
    const uint32_t localAccel = (uint32_t)std::max(1.0, (double)accelerationSteps * accelScale);
    if (leftMotor) leftMotor->setAcceleration(localAccel);
    if (rightMotor) rightMotor->setAcceleration(localAccel);

    const float moveTime = (float)maxDelta / (float)targetSpeed;
    float leftSpeed = (deltaLeft > 0) ? ((float)deltaLeft / moveTime) : 0.0f;
    float rightSpeed = (deltaRight > 0) ? ((float)deltaRight / moveTime) : 0.0f;

    const float maxHz = (float)targetSpeed;

    // upper clamp
    if (leftSpeed > maxHz) leftSpeed = maxHz;
    if (rightSpeed > maxHz) rightSpeed = maxHz;

    // lower clamp (avoid 0 Hz on active axis)
    if (deltaLeft > 0 && leftSpeed < 1.0f) leftSpeed = 1.0f;
    if (deltaRight > 0 && rightSpeed < 1.0f) rightSpeed = 1.0f;

    if (leftMotor) {
        leftMotor->setSpeedInHz((uint32_t)leftSpeed);
        leftMotor->moveTo(leftLegSteps);
    }

    if (rightMotor) {
        rightMotor->setSpeedInHz((uint32_t)rightSpeed);
        rightMotor->moveTo(rightLegSteps);
    }

    X = tx;
    Y = ty;
    lastSegmentDX = dx;
    lastSegmentDY = dy;
    lastDirX = dirX;
    lastDirY = dirY;

    moving = true;
    return moveTime;
}

double Movement::getWidth() {
    if (topDistance == -1) throw std::invalid_argument("not ready");
    return width;
}

Movement::Point Movement::getCoordinates() {
    if (X == -1 || Y == -1) throw std::invalid_argument("not ready");
    if (moving) throw std::invalid_argument("not ready");
    return Point(X, Y);
}

Movement::Point Movement::getCoordinatesLive() {
    if (X == -1 || Y == -1) return Point(0, 0);
    return Point(X, Y);
}

void Movement::setSpeeds(int newPrintSpeed, int newMoveSpeed) {
    if (newPrintSpeed <= 0 || newMoveSpeed <= 0) return;

    printSpeedSteps = newPrintSpeed;
    moveSpeedSteps = newMoveSpeed;

    if (leftMotor) leftMotor->setSpeedInHz(moveSpeedSteps);
    if (rightMotor) rightMotor->setSpeedInHz(moveSpeedSteps);

    Serial.printf("setSpeeds: print=%d, move=%d\n", printSpeedSteps, moveSpeedSteps);
}

void Movement::extend1000mm() {
    const int steps = mmToSteps(1000.0);

    if (leftMotor) {
        leftMotor->setSpeedInHz(moveSpeedSteps);
        leftMotor->move(steps);
    }

    if (rightMotor) {
        rightMotor->setSpeedInHz(moveSpeedSteps);
        rightMotor->move(steps);
    }

    moving = true;
}

void Movement::disableMotors() {
    // EN is hardwired LOW -> cannot disable by EN pin in software
    if (leftMotor) leftMotor->stopMove();
    if (rightMotor) rightMotor->stopMove();
}

bool Movement::isMoving() { return moving; }
bool Movement::hasStartedHoming() { return startedHoming; }
int Movement::getTopDistance() { return topDistance; }

Movement::MotionTuning Movement::getMotionTuning() const { return MotionTuning(infiniteStepsSteps, accelerationSteps); }

void Movement::setMotionTuning(long infiniteSteps, long acceleration) {
    if (infiniteSteps < 1000L) infiniteSteps = 1000L;
    if (infiniteSteps > 2000000000L) infiniteSteps = 2000000000L;

    if (acceleration < 1L) acceleration = 1L;
    if (acceleration > 2000000000L) acceleration = 2000000000L;

    infiniteStepsSteps = infiniteSteps;
    accelerationSteps = acceleration;

    if (leftMotor) leftMotor->setAcceleration((uint32_t)accelerationSteps);
    if (rightMotor) rightMotor->setAcceleration((uint32_t)accelerationSteps);

    WebLog::info("Tuning updated: infiniteSteps=" + String(infiniteStepsSteps) + " acceleration=" + String(accelerationSteps));
}

void Movement::setEnablePins(int leftEnablePin, int rightEnablePin) {
    _leftEnablePin = leftEnablePin;
    _rightEnablePin = rightEnablePin;

    // EN is hardwired LOW on your setup -> keep values only for UI/config compatibility.
    // Intentionally no setEnablePin() calls here.
}

int Movement::getLeftEnablePin() const { return _leftEnablePin; }
int Movement::getRightEnablePin() const { return _rightEnablePin; }

void Movement::setPulseWidths(int leftUs, int rightUs) {
    if (leftUs < 1) leftUs = 1;
    if (rightUs < 1) rightUs = 1;
    if (leftUs > 1000) leftUs = 1000;
    if (rightUs > 1000) rightUs = 1000;

    _leftPulseWidthUs = leftUs;
    _rightPulseWidthUs = rightUs;

    // FastAccelStepper does not expose per-motor min pulse width like AccelStepper.
    // Kept values for config compatibility/UI persistence.
    WebLog::info("Pulse widths updated (stored): left=" + String(_leftPulseWidthUs) + "us right=" + String(_rightPulseWidthUs) + "us");
}

int Movement::getLeftPulseWidthUs() const { return _leftPulseWidthUs; }
int Movement::getRightPulseWidthUs() const { return _rightPulseWidthUs; }
