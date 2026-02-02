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
    currentPosition = PEN_START_POS;
    penDistance     = 80;  // 
    slowSpeedDegPerSec = 80;
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
    penDistance = constrain(value, 0, 180);
}

void Pen::slowUp()
{
    doSlowMove(this, currentPosition, PEN_START_POS, slowSpeedDegPerSec);
    currentPosition = PEN_START_POS;
}

void Pen::slowDown()
{
    doSlowMove(this, currentPosition, penDistance, slowSpeedDegPerSec);
    currentPosition = penDistance;
}

bool Pen::isDown()
{
    return currentPosition == penDistance;
}

int Pen::currentAngle() const
{
    return currentPosition;
}
