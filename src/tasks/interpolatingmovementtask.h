#ifndef InterpolatingMovementTask_h
#define InterpolatingMovementTask_h

#include "movement.h"
#include "task.h"

class InterpolatingMovementTask : public Task {
private:
    Movement* movement;
    Movement::Point target;
    bool started = false;

public:
    static const char* NAME;

    InterpolatingMovementTask(Movement* movement, Movement::Point target);

    bool isDone() override;
    void startRunning() override;

    const char* name() override {
        return NAME;
    }
};

#endif
