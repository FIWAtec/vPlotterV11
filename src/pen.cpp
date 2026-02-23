#include "pen.h"
#include <Arduino.h>

static bool shouldStop(int currentDegree, int targetDegree, bool positive)
{
    return positive ? currentDegree >= targetDegree : currentDegree <= targetDegree;
}

static void doSlowMove(Pen *pen, int startDegree, int targetDegree, int speedDegPerSec)
{
    if (startDegree == targetDegree) return;

    unsigned long startTime = millis();
    bool positive = (targetDegree > startDegree);
    int currentDegree = startDegree;

    while (!shouldStop(currentDegree, targetDegree, positive))
    {
        pen->setRawValue(currentDegree);
        delay(2);

        unsigned long delta = millis() - startTime;
        int progress = (int)((double)delta / 1000.0 * speedDegPerSec);
        if (!positive) progress = -progress;
        currentDegree = startDegree + progress;
    }

    pen->setRawValue(targetDegree);
    delay(20);
}

Pen::Pen()
{
    penServo = new Servo();
    penServo->attach(PEN_SERVO_PIN);

    upAngle = PEN_START_POS;
    downAngle = 80;

    pendingUpAngle = upAngle;
    pendingDownAngle = downAngle;
    hasPendingUp = false;
    hasPendingDown = false;

    slowSpeedDegPerSec = 80;
    currentPosition = upAngle;
    penServo->write(currentPosition);
}

void Pen::setRawValue(int rawValue)
{
    rawValue = constrain(rawValue, 0, 180);
    penServo->write(rawValue);
    currentPosition = rawValue;
}

void Pen::setPenDistance(int value)
{
    // Keep old behavior for calibration route compatibility.
    // Requested hard limits: 0..80
    downAngle = constrain(value, 0, 70);
}

void Pen::setUpAngle(int value)
{
    upAngle = constrain(value, 0, 70);
}

void Pen::setDownAngle(int value)
{
    downAngle = constrain(value, 0, 70);
}

int Pen::getUpAngle() const
{
    return upAngle;
}

int Pen::getDownAngle() const
{
    return downAngle;
}

void Pen::setPendingUpAngle(int value)
{
    pendingUpAngle = constrain(value, 0, 70);
    hasPendingUp = true;
}

void Pen::setPendingDownAngle(int value)
{
    pendingDownAngle = constrain(value, 0, 70);
    hasPendingDown = true;
}

int Pen::getPendingUpAngle() const
{
    return pendingUpAngle;
}

int Pen::getPendingDownAngle() const
{
    return pendingDownAngle;
}

bool Pen::pendingUp() const
{
    return hasPendingUp;
}

bool Pen::pendingDown() const
{
    return hasPendingDown;
}

void Pen::slowUp()
{
    // Apply staged UP angle on next UP transition
    if (hasPendingUp) {
        upAngle = pendingUpAngle;
        hasPendingUp = false;
    }

    doSlowMove(this, currentPosition, upAngle, slowSpeedDegPerSec);
    currentPosition = upAngle;
}

void Pen::slowDown()
{
    // Apply staged DOWN angle on next DOWN transition
    if (hasPendingDown) {
        downAngle = pendingDownAngle;
        hasPendingDown = false;
    }

    doSlowMove(this, currentPosition, downAngle, slowSpeedDegPerSec);
    currentPosition = downAngle;
}

bool Pen::isDown()
{
    return currentPosition == downAngle;
}

int Pen::currentAngle() const
{
    return currentPosition;
}
