#ifndef PEN_H
#define PEN_H

#include <ESP32Servo.h>

#define PEN_SERVO_PIN     32
#define PEN_START_POS     60

class Pen {
private:
    Servo *penServo;

    // Zero offset (logical angle -> raw servo angle). This allows UI "0" to match
    // the real mechanical zero even if the servo horn is mounted with an offset.
    // raw = logical + zeroOffsetDeg
    int zeroOffsetDeg;

    // Active angles
    int upAngle;
    int downAngle;

    // Pending angles (applied on next transition only)
    int pendingUpAngle;
    int pendingDownAngle;
    bool hasPendingUp;
    bool hasPendingDown;

    int slowSpeedDegPerSec;
    // Current logical position (UI space, 0..80)
    int currentPosition;

public:
    Pen();

    void setRawValue(int rawValue);

    // Zero offset API
    void setZeroOffsetDeg(int value);
    int  getZeroOffsetDeg() const;

    // Backward compatible: set down angle directly (used by calibration phase)
    void setPenDistance(int value);

    // Active angle API
    void setUpAngle(int value);
    void setDownAngle(int value);
    int  getUpAngle() const;
    int  getDownAngle() const;

    // Pending angle API
    void setPendingUpAngle(int value);
    void setPendingDownAngle(int value);
    int  getPendingUpAngle() const;
    int  getPendingDownAngle() const;
    bool pendingUp() const;
    bool pendingDown() const;

    // Motion
    void slowUp();
    void slowDown();

    bool isDown();
    int  currentAngle() const;
};

#endif
