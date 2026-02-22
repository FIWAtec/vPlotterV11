#ifndef InterpolatingMovementTask_h
#define InterpolatingMovementTask_h

#include "movement.h"
#include "task.h"

class InterpolatingMovementTask : public Task {
private:
    Movement* movement;
    Movement::Point target;
    int speedSteps;

    bool started = false;

    // segmented straight-line planning in XY
    Movement::Point start;
    int segmentIndex = 0;
    int segmentCount = 0;

    void startNextSegment();

public:
    static const char* NAME;

    InterpolatingMovementTask(Movement* movement, Movement::Point target, int speedSteps);

    bool isDone() override;
    void startRunning() override;

    const char* name() override { return NAME; }
};

#endif
