#include "interpolatingmovementtask.h"
#include "service/weblog.h"
#include <math.h>

const char* InterpolatingMovementTask::NAME = "InterpolatingMovementTask";

InterpolatingMovementTask::InterpolatingMovementTask(Movement* movement, Movement::Point target) {
    this->movement = movement;
    this->target = target;
}

void InterpolatingMovementTask::startRunning() {
    if (!movement) return;

    try {
        // Single direct move to target.
        // This keeps segment-level cornering from Movement/Runner effective.
        movement->beginLinearTravel(target.x, target.y, printSpeedSteps);
        started = true;
    } catch (const std::exception& e) {
        WebLog::error(String("InterpolatingMovementTask start error: ") + e.what());
        started = true; // avoid deadlock in runner
    } catch (...) {
        WebLog::error("InterpolatingMovementTask start unknown error");
        started = true; // avoid deadlock in runner
    }
}

bool InterpolatingMovementTask::isDone() {
    if (!started) return false;
    if (!movement) return true;

    if (movement->isMoving()) return false;

    // Use tolerance instead of exact floating compare
    Movement::Point p = movement->getCoordinatesLive();
    const double dx = p.x - target.x;
    const double dy = p.y - target.y;
    const double dist = sqrt(dx * dx + dy * dy);

    // 0.05 mm tolerance
    return dist <= 0.05;
}
