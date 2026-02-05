#ifndef MOVEMENT_H
#define MOVEMENT_H

#include <AccelStepper.h>
#include <Arduino.h>
#include <math.h>
#include "display.h"

extern int printSpeedSteps;
extern int moveSpeedSteps;

constexpr bool USE_GT2_PULLEY = false;

constexpr double GT2_PITCH_MM = 2.0;
constexpr int GT2_TEETH = 20;

constexpr double LEGACY_DIAMETER_MM = 12.69;

constexpr int stepsPerRotation = 200 * 32;

static inline double travelPerRotationMM() { return USE_GT2_PULLEY ? (GT2_TEETH * GT2_PITCH_MM) : (LEGACY_DIAMETER_MM * PI); }

static inline int mmToSteps(double mm) { return int((mm / travelPerRotationMM()) * stepsPerRotation); }

static inline double stepsToMM(int steps) { return (double(steps) / double(stepsPerRotation)) * travelPerRotationMM(); }

constexpr double midPulleyToWall = 41.0;
constexpr float homedStepOffsetMM = 40.0;

static inline int homedStepsOffsetSteps() { return mmToSteps(homedStepOffsetMM); }

constexpr double mass_bot = 1.0;
constexpr double g_constant = 9.81;

constexpr double d_t = 76.027;
constexpr double d_p = 4.4866;
constexpr double d_m = 10.0 + d_p;

constexpr double belt_elongation_coefficient = 5e-5;

constexpr int HOME_Y_OFFSET_MM = 340;
constexpr double safeYFraction = 0.2;
constexpr double safeXFraction = 0.2;

constexpr int LEFT_STEP_PIN = 26;
constexpr int LEFT_DIR_PIN = 33;

constexpr int RIGHT_STEP_PIN = 27;
constexpr int RIGHT_DIR_PIN = 25;

class Movement {
   public:

    void setEnablePins(int leftEnablePin, int rightEnablePin);
    int  getLeftEnablePin() const;
    int  getRightEnablePin() const;

    void setPulseWidths(int leftUs, int rightUs);
    int  getLeftPulseWidthUs() const;
    int  getRightPulseWidthUs() const;

    explicit Movement(Display* display);

    struct MotionTuning {
        long infiniteSteps;
        long acceleration;
        MotionTuning(long inf = 999999999L, long acc = 999999999L) : infiniteSteps(inf), acceleration(acc) {}
    };

    MotionTuning getMotionTuning() const;
    void setMotionTuning(long infiniteSteps, long acceleration);

    struct Point {
        double x;
        double y;
        Point(double x, double y) : x(x), y(y) {}
        Point() : x(0), y(0) {}
    };

    static double distanceBetweenPoints(Point p1, Point p2) { return sqrt(pow(p2.x - p1.x, 2) + pow(p2.y - p1.y, 2)); }

    bool isMoving();
    bool hasStartedHoming();
    double getWidth();

    Point getCoordinatesLive();
    Point getCoordinates();

    void setTopDistance(int distance);
    void resumeTopDistance(int distance);
    int getTopDistance();

    void leftStepper(int dir);
    void rightStepper(int dir);

    int extendToHome();
    void runSteppers();

    float beginLinearTravel(double x, double y, int speed);

    void setSpeeds(int newPrintSpeed, int newMoveSpeed);

    void extend1000mm();
    Point getHomeCoordinates();
    void disableMotors();

   private:

    static constexpr int FIXED_PULSE_US = 5;

    int _leftEnablePin  = -1;
    int _rightEnablePin = -1;
    
    int _leftPulseWidthUs  = FIXED_PULSE_US;
    int _rightPulseWidthUs = FIXED_PULSE_US;
    struct Lengths {
        int left;
        int right;
        Lengths(int left, int right) : left(left), right(right) {}
        Lengths() : left(0), right(0) {}
    };

    int topDistance;
    double minSafeY;
    double minSafeXOffset;
    double width;

    volatile bool moving;
    bool homed;
    bool startedHoming;

    double X = -1;
    double Y = -1;

    AccelStepper* leftMotor;
    AccelStepper* rightMotor;
    Display* display;

    void setOrigin();

    long infiniteStepsSteps = 999999999L;
    long accelerationSteps = 999999999L;

    Lengths getBeltLengths(double x, double y);

    double gamma_last_position = 0.0;

    inline void getLeftTangentPoint(double frameX, double frameY, double gamma, double& x_PL, double& y_PL) const;
    inline void getRightTangentPoint(double frameX, double frameY, double gamma, double& x_PR, double& y_PR) const;
    void getBeltAngles(double frameX, double frameY, double gamma, double& phi_L, double& phi_R) const;
    void getBeltForces(double phi_L, double phi_R, double& F_L, double& F_R) const;
    double solveTorqueEquilibrium(double phi_L, double phi_R, double F_L, double F_R, double gamma_start) const;
    double getDilationCorrectedBeltLength(double belt_length_mm, double F_belt) const;
};

#endif