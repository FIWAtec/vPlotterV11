#ifndef Runner_h
#define Runner_h

#include <cstddef>
#include <LittleFS.h>

#include "movement.h"
#include "tasks/task.h"
#include "pen.h"
#include "display.h"

class Runner {
private:
    Movement *movement;
    Pen *pen;
    Display *display;

    void initTaskProvider();
    Task* getNextTask();

    Task* currentTask = nullptr;
    bool currentTaskCountsDistance = false;

    bool stopped = true;
    bool paused  = false;

    size_t startLine = 0; 

    Task* prefaceSequence[3];
    int   prefaceIx    = 0;
    int   prefaceCount = 0;

    Task* finishingSequence[2];
    int   sequenceIx   = 0;

    File openedFile;

    double headerTotalDistance = 0.0; 

    double jobTotalDistance = 0.0; 
    double jobDistanceSoFar = 0.0;

    double skippedDistance = 0.0;

    Movement::Point startPosition;
    Movement::Point targetPosition;

    int progress = 0;

    volatile bool abortRequested = false;

public:
    Runner(Movement *movement, Pen *pen, Display *display);

    void setStartLine(size_t lineAfterHeader);
    size_t getStartLine() const;

    void start();
    void run();
    void dryRun();

    void pauseJob();
    void resumeJob();

    void abortAndGoHome();

    int getProgress() const;
    double getTotalDistance() const;   
    double getDistanceSoFar() const;  

    bool isStopped() const;
    bool isPaused() const;
};

#endif
