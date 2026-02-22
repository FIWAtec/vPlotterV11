#include "interpolatingmovementtask.h"
#include "service/weblog.h"
#include <math.h>

const char* InterpolatingMovementTask::NAME = "InterpolatingMovementTask";

InterpolatingMovementTask::InterpolatingMovementTask(Movement* movement, Movement::Point target, int speedSteps) {
    this->movement = movement;
    this->target = target;
    this->speedSteps = speedSteps;
}

void InterpolatingMovementTask::startNextSegment() {
    if (!movement) return;
    if (segmentCount <= 0) return;
    if (segmentIndex >= segmentCount) return;

    const double t = (double)(segmentIndex + 1) / (double)segmentCount;
    const double x = start.x + (target.x - start.x) * t;
    const double y = start.y + (target.y - start.y) * t;

    movement->beginLinearTravel(x, y, speedSteps);
    segmentIndex++;
}

void InterpolatingMovementTask::startRunning() {
    if (!movement) return;

    try {
        // IMPORTANT:
        // A V-plotter cannot create a straight XY line by commanding only final belt lengths.
        // We must interpolate in XY and call IK (getBeltLengths) per segment.
        start = movement->getCoordinatesLive();

        const double dx = target.x - start.x;
        const double dy = target.y - start.y;
        const double dist = sqrt(dx * dx + dy * dy);

        // Use planner config, but keep sane lower bound.
        const auto cfg = movement->getPlannerConfig();
        double segLen = cfg.minSegmentLenMM;
        if (segLen < 0.5) segLen = 0.5;      // visually straight
        if (segLen > 5.0) segLen = 5.0;      // do not get too coarse

        segmentCount = (dist <= 1e-6) ? 1 : (int)ceil(dist / segLen);
        if (segmentCount < 1) segmentCount = 1;

        segmentIndex = 0;
        started = true;

        startNextSegment();
    } catch (const std::exception& e) {
        WebLog::error(String("InterpolatingMovementTask start error: ") + e.what());
        started = true; // avoid deadlock in runner
        segmentCount = 0;
    } catch (...) {
        WebLog::error("InterpolatingMovementTask start unknown error");
        started = true; // avoid deadlock in runner
        segmentCount = 0;
    }
}

bool InterpolatingMovementTask::isDone() {
    if (!started) return false;
    if (!movement) return true;

    // If current segment still moving, task is not done.
    if (movement->isMoving()) return false;

    // If there are still segments left, start the next one.
    if (segmentIndex < segmentCount) {
        try {
            startNextSegment();
        } catch (const std::exception& e) {
            WebLog::error(String("InterpolatingMovementTask segment error: ") + e.what());
            // fall through, allow runner to continue
            segmentIndex = segmentCount;
        } catch (...) {
            WebLog::error("InterpolatingMovementTask segment unknown error");
            segmentIndex = segmentCount;
        }
        return false;
    }

    // Final tolerance check
    Movement::Point p = movement->getCoordinatesLive();
    const double dx = p.x - target.x;
    const double dy = p.y - target.y;
    const double d = sqrt(dx * dx + dy * dy);

    return d <= 0.05; // 0.05 mm tolerance
}
