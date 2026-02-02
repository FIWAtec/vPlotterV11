#include "runner.h"
#include "tasks/interpolatingmovementtask.h"
#include "tasks/pentask.h"

#include <stdexcept>
#include <math.h>

#include "service/weblog.h"

using namespace std;

Runner::Runner(Movement *movement, Pen *pen, Display *display) {
    this->movement = movement;
    this->pen = pen;
    this->display = display;
    stopped = true;
    paused = false;
}

void Runner::setStartLine(size_t lineAfterHeader) {
    startLine = lineAfterHeader;
}

size_t Runner::getStartLine() const {
    return startLine;
}

void Runner::initTaskProvider() {
    prefaceIx = 0;
    prefaceCount = 0;
    sequenceIx = 0;

    if (openedFile) {
        openedFile.close();
    }

    openedFile = LittleFS.open("/commands", "r");
    if (!openedFile) {
        throw std::invalid_argument("No File");
    }

    String line = openedFile.readStringUntil('\n');
    line.trim();
    if (line.length() < 2 || line.charAt(0) != 'd') {
        throw std::invalid_argument("bad file");
    }
    headerTotalDistance = line.substring(1).toDouble();

    String heightLine = openedFile.readStringUntil('\n');
    heightLine.trim();
    if (heightLine.length() < 2 || heightLine.charAt(0) != 'h') {
        throw std::invalid_argument("bad file");
    }

    startPosition = movement->getCoordinates();
    targetPosition = startPosition;

    skippedDistance = 0.0;
    bool penDown = false;
    Movement::Point virtualPos = startPosition;

    size_t consumed = 0;
    while (consumed < startLine && openedFile.available()) {
        String l = openedFile.readStringUntil('\n');
        l.trim();
        if (l.length() == 0) {
            continue; 
        }

        const char c0 = l.charAt(0);
        if (c0 == 'p') {
            const char c1 = (l.length() > 1) ? l.charAt(1) : '0';
            penDown = (c1 == '1');
            consumed++;
            continue;
        }

        int sep = l.indexOf(' ');
        if (sep < 0) {
            consumed++;
            continue;
        }

        double x = l.substring(0, sep).toDouble();
        double y = l.substring(sep + 1).toDouble();

        Movement::Point np(x, y);
        skippedDistance += Movement::distanceBetweenPoints(virtualPos, np);
        virtualPos = np;

        consumed++;
    }

    jobTotalDistance = headerTotalDistance - skippedDistance;
    if (jobTotalDistance < 0.0) jobTotalDistance = 0.0;
    jobDistanceSoFar = 0.0;

    progress = -1; 

    if (startLine > 0) {
        prefaceSequence[prefaceCount++] = new PenTask(true, pen);

        if (!(virtualPos.x == startPosition.x && virtualPos.y == startPosition.y)) {
            prefaceSequence[prefaceCount++] = new InterpolatingMovementTask(movement, virtualPos);
        }

        if (penDown) {
            prefaceSequence[prefaceCount++] = new PenTask(false, pen);
        }

        startPosition = virtualPos;
        targetPosition = virtualPos;
    }

    auto homeCoordinates = movement->getHomeCoordinates();
    finishingSequence[0] = new PenTask(true, pen);
    finishingSequence[1] = new InterpolatingMovementTask(movement, homeCoordinates);
}

void Runner::start() {
    paused = false;
    stopped = true;

    initTaskProvider();

    if (display) {
        display->displayText(String(0) + "%");
    }

    currentTask = getNextTask();
    if (!currentTask) {
        stopped = true;
        progress = 100;
        return;
    }

    currentTask->startRunning();
    stopped = false;

    WebLog::info("Runner started");
}

Task *Runner::getNextTask() {
    if (prefaceIx < prefaceCount) {
        currentTaskCountsDistance = false;
        return prefaceSequence[prefaceIx++];
    }

    if (!openedFile || !openedFile.available()) {
        const int finishingCount = 2;
        if (sequenceIx < finishingCount) {
            currentTaskCountsDistance = false;
            return finishingSequence[sequenceIx++];
        }

        if (openedFile) openedFile.close();
        progress = 100;
        stopped = true;
        paused = false;
        WebLog::info("Runner finished");
        return nullptr;
    }

    String line = openedFile.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) {
        return getNextTask();
    }

    const char c0 = line.charAt(0);
    if (c0 == 'p') {
        const char c1 = (line.length() > 1) ? line.charAt(1) : '0';
        currentTaskCountsDistance = false;
        if (c1 == '1') {
            return new PenTask(false, pen); 
        }
        return new PenTask(true, pen);     
    }

    int sep = line.indexOf(' ');
    if (sep < 0) {
        return getNextTask();
    }

    double x = line.substring(0, sep).toDouble();
    double y = line.substring(sep + 1).toDouble();

    targetPosition = Movement::Point(x, y);
    currentTaskCountsDistance = true;
    return new InterpolatingMovementTask(movement, targetPosition);
}

void Runner::run() {
    if (stopped) {
        return;
    }

    if (abortRequested) {
        if (movement && movement->isMoving()) {
            return; 
        }

        abortRequested = false;
        paused = false;

        if (openedFile) openedFile.close();
        openedFile = File();

        if (currentTask) {
            delete currentTask;
            currentTask = nullptr;
        }

        prefaceIx = 0;
        prefaceCount = 0;

        sequenceIx = 0;

        currentTask = getNextTask();
        if (currentTask) {
            currentTask->startRunning();
            stopped = false;
        } else {
            stopped = true;
        }
        return;
    }

    if (paused) {
        return;
    }

    if (!currentTask) {
        stopped = true;
        return;
    }

    if (currentTask->isDone()) {
        if (currentTask->name() == InterpolatingMovementTask::NAME && currentTaskCountsDistance) {
            const double distanceCovered = Movement::distanceBetweenPoints(startPosition, targetPosition);
            jobDistanceSoFar += distanceCovered;
            startPosition = targetPosition;

            int newProgress = 0;
            if (jobTotalDistance > 0.0) {
                newProgress = (int)floor((jobDistanceSoFar / jobTotalDistance) * 100.0);
            }

            if (newProgress > 100) newProgress = 100;
            if (newProgress < 0) newProgress = 0;

            if (progress != newProgress) {
                progress = newProgress;
                if (display) {
                    display->displayText(String(progress) + "%");
                }
            }
        }

        delete currentTask;
        currentTask = getNextTask();

        if (currentTask) {
            currentTask->startRunning();
        } else {
            stopped = true;
        }
    }
}

void Runner::dryRun() {
    paused = false;
    initTaskProvider();

    Task* task = getNextTask();
    while (task != nullptr) {
        delete task;
        task = getNextTask();
    }
}

void Runner::pauseJob() {
    if (stopped) return;
    paused = true;
    WebLog::info("Runner paused");
}

void Runner::resumeJob() {
    if (stopped) return;
    paused = false;
    WebLog::info("Runner resumed");
}

void Runner::abortAndGoHome() {
    if (stopped) {
        paused = false;
        stopped = false;
        abortRequested = false;

        if (openedFile) openedFile.close();
        openedFile = File();

        if (currentTask) {
            delete currentTask;
            currentTask = nullptr;
        }

        prefaceIx = 0;
        prefaceCount = 0;
        sequenceIx = 0;

        auto homeCoordinates = movement->getHomeCoordinates();
        finishingSequence[0] = new PenTask(true, pen);
        finishingSequence[1] = new InterpolatingMovementTask(movement, homeCoordinates);

        currentTaskCountsDistance = false;
        currentTask = getNextTask();
        if (currentTask) {
            currentTask->startRunning();
        } else {
            stopped = true;
        }
        WebLog::warn("Runner stop requested while idle");
        return;
    }

    abortRequested = true;
    paused = false;
    WebLog::warn("Runner abort requested");
}
int Runner::getProgress() const { return progress < 0 ? 0 : progress; }

double Runner::getTotalDistance() const { return jobTotalDistance; }

double Runner::getDistanceSoFar() const { return jobDistanceSoFar; }

bool Runner::isStopped() const { return stopped; }

bool Runner::isPaused() const { return paused; }
