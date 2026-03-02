#ifndef Runner_h
#define Runner_h

#include <cstddef>
#include <deque>
#include <LittleFS.h>

#include "movement.h"
#include "tasks/task.h"
#include "pen.h"
#include "display.h"

class Runner {
private:
    struct QueuedCommand {
        enum Type { Pen, Move } type;
        bool penDown; // only for Pen
        Movement::Point p; // only for Move
        bool protect; // do not collinear-merge away (used for arc-expanded points)
        QueuedCommand(bool down) : type(Pen), penDown(down), p(0,0), protect(false) {}
        QueuedCommand(Movement::Point pt, bool protect=false) : type(Move), penDown(false), p(pt), protect(protect) {}
    };

    Movement *movement;
    Pen *pen;
    Display *display;

    void initTaskProvider();
    Task* getNextTask();

    bool fillLookaheadQueue();
    void optimizeLookaheadQueue();

    Task* currentTask = nullptr;
    bool currentTaskCountsDistance = false;

    // For distance split (pen down vs pen up)
    bool currentMoveIsDrawing = false;

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

    double jobDrawDistanceSoFar   = 0.0; // pen down movement only
    double jobTravelDistanceSoFar = 0.0; // pen up movement only

    double skippedDistance = 0.0;

    Movement::Point startPosition;
    Movement::Point targetPosition;

    int progress = 0;

    volatile bool abortRequested = false;

    std::deque<QueuedCommand> lookaheadQ;
    bool eofReached = false;

    // Track current pen state so we can select moveSpeedSteps vs printSpeedSteps.
    bool penIsDown = false;

    int penSettleMs = 0;

    // ---------- Time / speed metrics ----------
    uint32_t jobStartMs      = 0;
    uint32_t lastTickMs      = 0;
    uint32_t pauseStartMs    = 0;
    uint32_t totalPausedMs   = 0;
    uint32_t movingActiveMs  = 0; // time where steppers were actively moving (excludes pen delays)

    void tickTiming_();

    // ---------- Restart from scrubbed line ----------
    bool restartRequested = false;
    size_t restartLineAfterHeader = 0;

    // ---------- Pause "Park" (Grundstellung) ----------
    bool parkRequested = false;
    bool parked        = false;
    bool returnRequested = false;

    Movement::Point parkTarget = Movement::Point(0,0);
    Movement::Point parkReturn = Movement::Point(0,0);
    int parkStage = 0; // 0=idle, 1=goingToPark, 2=parked, 3=returning

    void handlePark_();

public:
    Runner(Movement *movement, Pen *pen, Display *display);

    void setPenSettleMs(int ms);
    int  getPenSettleMs() const;

    void setStartLine(size_t lineAfterHeader);
    size_t getStartLine() const;

    void start();
    void run();
    void dryRun();

    void pauseJob();
    void resumeJob();

    // Restart job from a specific commands line (line index after header d/h).
    // Used for "spooling" in pause UI: robot does not move while spooling, only after play.
    bool requestRestartFromLine(size_t lineAfterHeader);

    // Pause helper: move to a "base" position while staying paused.
    // Pressing resume will automatically return to the previous XY first, then continue.
    bool requestParkTo(double xMm, double yMm);
    bool requestParkToBase(double yMm);

    void abortAndGoHome();

    int getProgress() const;
    double getTotalDistance() const;
    double getDistanceSoFar() const;

    double getDrawDistanceSoFar() const;
    double getTravelDistanceSoFar() const;

    uint32_t getElapsedMs() const;     // excludes paused time
    uint32_t getMovingActiveMs() const; // excludes pen delays / dwell (movement only)

    double getAvgSpeedMmS_MovingOnly() const;

    bool isStopped() const;
    bool isPaused() const;
    bool isParked() const;
};

#endif
