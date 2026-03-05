#ifndef Runner_h
#define Runner_h

#include <cstddef>
#include <deque>
#include <stdint.h>   // uint32_t
#include <cstring>    // strcmp
#include <LittleFS.h>

#include "movement.h"
#include "tasks/task.h"
#include "pen.h"
#include "display.h"

class Runner {
private:
    struct QueuedCommand {
        enum Type { Pen, Move } type;
        bool penDown;
        Movement::Point p;
        bool protect;

        QueuedCommand(bool down) : type(Pen), penDown(down), p(0, 0), protect(false) {}
        QueuedCommand(Movement::Point pt, bool protect = false) : type(Move), penDown(false), p(pt), protect(protect) {}
    };

    Movement *movement;
    Pen *pen;
    Display *display;

    void initTaskProvider();
    Task* getNextTask();

    // Timing / stats helpers (used by HUD + diagnostics)
    void tickTiming_();

    uint32_t jobStartMs     = 0;
    uint32_t lastTickMs     = 0;
    uint32_t pauseStartMs   = 0;
    uint32_t totalPausedMs  = 0;
    uint32_t movingActiveMs = 0;

    bool parked = false;

    bool fillLookaheadQueue();
    void optimizeLookaheadQueue();

    Task* currentTask = nullptr;
    bool currentTaskCountsDistance = false;

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

    double jobDrawDistanceSoFar   = 0.0;
    double jobTravelDistanceSoFar = 0.0;

    double skippedDistance = 0.0;

    Movement::Point startPosition;
    Movement::Point targetPosition;

    int progress = 0;

    volatile bool abortRequested = false;

    std::deque<QueuedCommand> lookaheadQ;
    bool eofReached = false;

    bool penIsDown = false;

    int penSettleMs = 0;
    double penMergeMm = 0.0;

    bool pendingPenUp = false;
    bool pendingPenUpPrevDown = false;

    String pushbackLine;
    bool hasPushbackLine = false;

    // Pen move counters (count only real toggles)
    uint32_t penMovesTotal = 0;
    uint32_t penMovesUp    = 0;
    uint32_t penMovesDown  = 0;

    bool restartRequested = false;
    size_t restartLineAfterHeader = 0;

public:
    Runner(Movement *movement, Pen *pen, Display *display);

    void setPenSettleMs(int ms);
    int  getPenSettleMs() const;

    void setPenMergeMm(double mm);
    double getPenMergeMm() const;

    uint32_t getPenMovesTotal() const;
    uint32_t getPenMovesUp() const;
    uint32_t getPenMovesDown() const;

    void setStartLine(size_t lineAfterHeader);
    size_t getStartLine() const;

    void start();
    void run();
    void dryRun();

    void pauseJob();
    void resumeJob();

    bool requestRestartFromLine(size_t lineAfterHeader);

    void abortAndGoHome();

    int getProgress() const;
    double getTotalDistance() const;
    double getDistanceSoFar() const;

    double getDrawDistanceSoFar() const;
    double getTravelDistanceSoFar() const;

    // Runtime stats for UI
    uint32_t getElapsedMs() const;
    uint32_t getMovingActiveMs() const;
    double   getAvgSpeedMmS_MovingOnly() const;

    // Park / manual positioning while paused
    bool isParked() const;
    bool requestParkTo(double xMm, double yMm);
    bool requestParkToBase(double yMm);

    bool isStopped() const;
    bool isPaused() const;
};

#endif
