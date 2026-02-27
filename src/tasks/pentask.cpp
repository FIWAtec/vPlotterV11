#include "pentask.h"
#include <Arduino.h>

PenTask::PenTask(bool up, Pen *pen, int settleMs)
: pen(pen), up(up), settleMs(settleMs) {
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

    if (settleMs > 0) {
        delay((unsigned long)settleMs);
    }

    Serial.println(F("Pen task ran"));
}

bool PenTask::isDone() {
    return true;
}
