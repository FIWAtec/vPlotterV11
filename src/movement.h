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

constexpr int stepsPerRotation = 200 * 64;

static inline double travelPerRotationMM() { return USE_GT2_PULLEY ? (GT2_TEETH * GT2_PITCH_MM) : (LEGACY_DIAMETER_MM * PI); }

static inline int mmToSteps(double mm) { return int((mm / travelPerRotationMM()) * stepsPerRotation); }

static inline double stepsToMM(int steps) { return (double(steps) / double(stepsPerRotation)) * travelPerRotationMM(); }

constexpr double midPulleyToWall = 41.0;
constexpr float homedStepOffsetMM = 40.0;

static inline int homedStepsOffsetSteps() { return mmToSteps(homedStepOffsetMM); }

constexpr double mass_bot = 1.5;
constexpr double g_constant = 9.81;

constexpr double d_t = 76.027;
constexpr double d_p = 4.4866;
constexpr double d_m = 10.0 + d_p;

// constexpr double belt_elongation_coefficient = 5e-5;
constexpr double belt_elongation_coefficient = 0.0;

constexpr int HOME_Y_OFFSET_MM = 340;
constexpr double safeYFraction = 0.2;
constexpr double safeXFraction = 0.2;

constexpr int LEFT_STEP_PIN = 26;
constexpr int LEFT_DIR_PIN = 33;

constexpr int RIGHT_STEP_PIN = 27;
constexpr int RIGHT_DIR_PIN = 25;

class Movement {
   public:

    struct PlannerConfig {
        /*
        ================================================================================
        PlannerConfig – Default-Werte + Effekt: kleiner vs. größer
        ================================================================================

        Diese Werte bestimmen, wie der Motion-Planner Segmente fährt, Ecken behandelt,
        kurze Segmente bändigt und Bewegungen glättet. Es geht um “Fahrverhalten”:
        Genauigkeit vs. Geschwindigkeit vs. Ruhe.

        Merke: “Kleiner” heißt nicht automatisch besser. Es heißt oft:
        - präziser, aber langsamer/ruckeliger
        und “Größer” heißt oft:
        - schneller/flüssiger, aber runder/ungenauer

        --------------------------------------------------------------------------------
        junctionDeviationMM (z.B. 0.08)
        --------------------------------------------------------------------------------
        KLEINER:
        - stärkere Abbremsung an Ecken
        - schärfere Ecken, weniger Rundung
        - langsamere Gesamtdurchfahrt, mehr Stop-and-go möglich

        GRÖßER:
        - schneller durch Ecken, weniger Bremsen
        - Ecken werden runder, 90° wirkt “abgeschnitten”
        - bei Trägheit/Spiel: mehr Risiko für “zieht die Ecke”

        Typisch: 0.02 .. 0.20

        --------------------------------------------------------------------------------
        lookaheadSegments (z.B. 48)
        --------------------------------------------------------------------------------
        KLEINER:
        - weniger Vorausplanung
        - Speed kann schwanken (“pumpen”), mehr Ruckeln bei vielen Segmenten
        - weniger RAM/CPU

        GRÖßER:
        - gleichmäßigere Geschwindigkeit, smoother
        - bessere Verteilung von Beschleunigen/Bremsen
        - mehr RAM/CPU (zu groß kann irgendwann instabil/träge werden)

        Typisch: 16 .. 96

        --------------------------------------------------------------------------------
        minSegmentTimeMs (z.B. 3)
        --------------------------------------------------------------------------------
        KLEINER (bis 0):
        - weniger Begrenzung für ultrakurze Segmente
        - bei vielen Mini-Segmenten: Speed-Spikes möglich -> Zittern/Unruhe
        - schneller bei detailreichen SVGs

        GRÖßER:
        - zwingt kurze Segmente langsamer zu fahren
        - ruhiger/smoother bei vielen kleinen Segmenten
        - kann Details stark verlangsamen (mehr “Dauerbremsen”)

        Typisch: 0 .. 10 (häufig 2..5)

        --------------------------------------------------------------------------------
        cornerSlowdown (z.B. 0.55)
        --------------------------------------------------------------------------------
        KLEINER:
        - weniger Abbremsung in Ecken
        - schneller, aber Ecken werden ungenauer/runder
        - bei Spiel/Trägheit eher “zieht” der Plotter in der Ecke

        GRÖßER:
        - mehr Abbremsung in Ecken
        - präzisere Ecken, weniger Überschwingen
        - langsamer, kann bei vielen Ecken “stotteriger” wirken

        Typisch: 0.4 .. 0.9

        --------------------------------------------------------------------------------
        minCornerFactor (z.B. 0.30)
        --------------------------------------------------------------------------------
        KLEINER:
        - darf in harten Ecken sehr langsam werden
        - sehr präzise Ecken möglich
        - kann ruckelig wirken (starkes Abbremsen)

        GRÖßER:
        - Mindestgeschwindigkeit in Ecken bleibt hoch
        - flüssiger/schneller
        - Ecken werden runder, weniger exakt (besonders 90°)

        Typisch: 0.10 .. 0.50

        --------------------------------------------------------------------------------
        minSegmentLenMM (z.B. 0.20)
        --------------------------------------------------------------------------------
        KLEINER:
        - mehr Mini-Segmente werden “voll” gefahren
        - mehr Detailtreue
        - kann mehr ruckeln (viele kurze Segmente)

        GRÖßER:
        - mehr Mini-Segmente werden gemerged/ignoriert/limitiert (je nach Code)
        - ruhiger, weniger Jitter
        - Details gehen verloren / Konturen werden grober

        Typisch: 0.10 .. 1.00

        --------------------------------------------------------------------------------
        collinearDeg (z.B. 3.0)
        --------------------------------------------------------------------------------
        KLEINER:
        - streng: nur wirklich gerade gilt als kollinear
        - erkennt mehr “Ecken” -> mehr Corner-Slowdown
        - präziser, aber evtl. langsamer/ruckeliger

        GRÖßER:
        - tolerant: auch leicht abweichend wird als “gerade” behandelt
        - weniger unnötige Eck-Bremsen -> smoother/schneller
        - kleine Knicke können “verschluckt” werden

        Typisch: 1.0 .. 10.0

        --------------------------------------------------------------------------------
        backlashXmm / backlashYmm (z.B. 0.0)
        --------------------------------------------------------------------------------
        KLEINER (Richtung 0):
        - weniger/keine Backlash-Kompensation
        - gut fürs Debuggen, keine künstlichen Versätze
        - wenn Mechanik Spiel hat: Ecken können “offen” wirken / nicht schließen

        GRÖßER:
        - mehr Kompensation bei Richtungswechseln
        - kann Ecken/Schließen verbessern, WENN korrekt kalibriert
        - wenn falsch: Ecken springen, Quadrat schließt nicht, Kanten wirken versetzt

        Typisch: 0.0 .. 0.50 (sehr abhängig von Mechanik)

        --------------------------------------------------------------------------------
        sCurveFactor (z.B. 0.35)
        --------------------------------------------------------------------------------
        KLEINER (bis 0):
        - weniger “weiche” Beschleunigungs-Formung
        - direkter, ggf. “härterer” Ruck
        - kann Linien gerader machen, wenn vorher Rampenverhältnis/Timing krumm war

        GRÖßER:
        - weichere Beschleunigung/Abbremsung (Pseudo S-Curve)
        - weniger Ruck, ruhiger
        - kann bei ungünstiger Abstimmung Linien am Anfang/Ende “bananig” machen

        Typisch: 0.0 .. 0.6

        --------------------------------------------------------------------------------
        Praktische Quick-Tuning-Idee (für sauberes Viereck)
        --------------------------------------------------------------------------------
        Wenn Ecken zu rund:
        - junctionDeviationMM kleiner
        - cornerSlowdown größer
        - minCornerFactor kleiner

        Wenn Linien “bananig” am Anfang/Ende:
        - sCurveFactor kleiner
        - (und sicherstellen, dass Accel proportional zur Achs-MaxSpeed gesetzt wird)

        Wenn Ecken versetzt / Quadrat schließt nicht:
        - backlashXmm/backlashYmm erstmal auf 0 (Debug)
        - dann sauber kalibrieren statt raten

        ================================================================================
        */
       
        // Cornering and lookahead related knobs
        double junctionDeviationMM;   // 0.01 .. 1.0 typical
        int lookaheadSegments;        // 1 .. 128
        int minSegmentTimeMs;         // 0 .. 50
        double cornerSlowdown;        // 0..1 , lower => stronger corner slow-down
        double minCornerFactor;       // minimal speed factor in very sharp corners

        // Geometry based dynamic feed and filters
        double minSegmentLenMM;       // tiny segments can be skipped/merged by runner
        double collinearDeg;          // merge threshold in degrees

        // Backlash compensation in mm
        double backlashXmm;
        double backlashYmm;

        // S-curve style soften factor (0..1), practical approximation on top of AccelStepper
        double sCurveFactor;

        PlannerConfig() :
            junctionDeviationMM(0.04),
            lookaheadSegments(48),
            minSegmentTimeMs(8),
            cornerSlowdown(0.8),
            minCornerFactor(0.05),
            minSegmentLenMM(0.05),
            collinearDeg(1.0),
            backlashXmm(0.0),
            backlashYmm(0.0),
            sCurveFactor(0.0) {}
    };

    void setPlannerConfig(const PlannerConfig& cfg);
    PlannerConfig getPlannerConfig() const;

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

    static constexpr int FIXED_PULSE_US = 10;

    int _leftEnablePin  = -1;
    int _rightEnablePin = -1;

    int _leftPulseWidthUs  = FIXED_PULSE_US;
    int _rightPulseWidthUs = FIXED_PULSE_US;

    PlannerConfig plannerCfg{};

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
    bool useAccelMode; // This flag tell if we use acceleration ramp profile for current travel
    bool homed;
    bool startedHoming;

    double X = -1;
    double Y = -1;

    // for cornering/backlash
    double lastSegmentDX = 0.0;
    double lastSegmentDY = 0.0;
    int lastDirX = 0;
    int lastDirY = 0;

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

    double computeCornerFactor(double dx, double dy) const;
};

#endif
