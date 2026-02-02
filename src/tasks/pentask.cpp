#include "pentask.h"
#include <Arduino.h>

PenTask::PenTask(bool up, Pen *pen)
: pen(pen), up(up) {
}

void PenTask::startRunning() {
    Serial.print(F("Starting pen task: "));
    Serial.println(up ? F("UP") : F("DOWN"));

    if (!pen) {
        Serial.println(F("PenTask error: pen is null"));
        return;
    }

    if (up) {
        Serial.println(F("Pen is going up"));
        pen->slowUp();
    } else {
        Serial.println(F("Pen is going down"));
        pen->slowDown();
    }

    Serial.println(F("Pen task ran"));
}

bool PenTask::isDone() {
    return true;
}
