#ifndef PenTask_h
#define PenTask_h
#include "pen.h"
#include "task.h"
class PenTask : public Task {
    private:
    const char* NAME = "PenTask";
    Pen *pen;
    bool up;
    int settleMs;
    public:
    PenTask(bool up, Pen *pen, int settleMs);
    bool isDone();
    void startRunning();
    const char* name() {
        return NAME;
    }
};
#endif