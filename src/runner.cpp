#include "runner.h"
#include "tasks/interpolatingmovementtask.h"
#include "tasks/pentask.h"

#include <stdexcept>
#include <math.h>
#include <algorithm>

#include "service/weblog.h"
#include <SD.h>
#include "sd/sd_commands_bridge.h"

using namespace std;

static double angleDegBetween(const Movement::Point& a, const Movement::Point& b, const Movement::Point& c) {
    const double v1x = b.x - a.x;
    const double v1y = b.y - a.y;
    const double v2x = c.x - b.x;
    const double v2y = c.y - b.y;
    const double l1 = sqrt(v1x*v1x + v1y*v1y);
    const double l2 = sqrt(v2x*v2x + v2y*v2y);
    if (l1 < 1e-9 || l2 < 1e-9) return 180.0;
    double dot = (v1x*v2x + v1y*v2y) / (l1*l2);
    if (dot > 1.0) dot = 1.0;
    if (dot < -1.0) dot = -1.0;
    return acos(dot) * 180.0 / PI;
}

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
    lookaheadQ.clear();
    eofReached = false;
    penIsDown = false;

    if (openedFile) openedFile.close();

    if (!sdCommandsEnsureMounted()) throw std::invalid_argument("SD not mounted");

    openedFile = SD.open("/commands", FILE_READ);
    if (!openedFile) throw std::invalid_argument("No File");

    String line = openedFile.readStringUntil('\n');
    line.trim();
    if (line.length() < 2 || line.charAt(0) != 'd') throw std::invalid_argument("bad file");
    headerTotalDistance = line.substring(1).toDouble();

    String heightLine = openedFile.readStringUntil('\n');
    heightLine.trim();
    if (heightLine.length() < 2 || heightLine.charAt(0) != 'h') throw std::invalid_argument("bad file");

    startPosition = movement->getCoordinates();
    targetPosition = startPosition;

    skippedDistance = 0.0;
    bool penDown = false;
    Movement::Point virtualPos = startPosition;

    size_t consumed = 0;
    while (consumed < startLine && openedFile.available()) {
        String l = openedFile.readStringUntil('\n');
        l.trim();
        if (l.length() == 0) continue;

        const char c0 = l.charAt(0);
        if (c0 == 'p') {
            const char c1 = (l.length() > 1) ? l.charAt(1) : '0';
            penDown = (c1 == '1');
            (void)penDown;
            consumed++;
            continue;
        }

        int sep = l.indexOf(' ');
        if (sep < 0) { consumed++; continue; }

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
            prefaceSequence[prefaceCount++] = new InterpolatingMovementTask(movement, virtualPos, moveSpeedSteps);
            startPosition = virtualPos;
        }

        if (penDown) { prefaceSequence[prefaceCount++] = new PenTask(false, pen); penIsDown = true; }
    }

    Movement::Point home = movement->getHomeCoordinates();
    finishingSequence[0] = new PenTask(true, pen);
    finishingSequence[1] = new InterpolatingMovementTask(movement, home, moveSpeedSteps);
}

bool Runner::fillLookaheadQueue() {
    if (!openedFile) return false;
    const int maxSegments = movement->getPlannerConfig().lookaheadSegments;
    while (!eofReached && (int)lookaheadQ.size() < maxSegments && openedFile.available()) {
        String line = openedFile.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;

        const char c0 = line.charAt(0);
        if (c0 == 'p') {
            const char c1 = (line.length() > 1) ? line.charAt(1) : '0';
            lookaheadQ.emplace_back(c1 == '1');
            continue;
        }

        int sep = line.indexOf(' ');
        if (sep < 0) continue;

        double x = line.substring(0, sep).toDouble();
        double y = line.substring(sep + 1).toDouble();
        lookaheadQ.emplace_back(Movement::Point(x, y));
    }

    if (!openedFile.available()) eofReached = true;
    optimizeLookaheadQueue();
    return !lookaheadQ.empty();
}

void Runner::optimizeLookaheadQueue() {
    const auto cfg = movement->getPlannerConfig();

    // Remove too-short move segments (skip noise)
    Movement::Point prev = startPosition;
    for (auto it = lookaheadQ.begin(); it != lookaheadQ.end();) {
        if (it->type == QueuedCommand::Move) {
            const double d = Movement::distanceBetweenPoints(prev, it->p);
            if (d < cfg.minSegmentLenMM) {
                it = lookaheadQ.erase(it);
                continue;
            }
            prev = it->p;
        }
        ++it;
    }

    // Merge collinear points for consecutive Move-Move-Move blocks between pen commands
    bool changed = true;
    while (changed) {
        changed = false;
        Movement::Point anchor = startPosition;
        for (size_t i = 0; i + 2 < lookaheadQ.size(); ++i) {
            if (lookaheadQ[i].type != QueuedCommand::Move) {
                anchor = startPosition;
                continue;
            }
            if (i > 0 && lookaheadQ[i-1].type == QueuedCommand::Move) anchor = lookaheadQ[i-1].p;
            if (lookaheadQ[i+1].type != QueuedCommand::Move || lookaheadQ[i+2].type != QueuedCommand::Move) continue;

            const auto &a = anchor;
            const auto &b = lookaheadQ[i+1].p;
            const auto &c = lookaheadQ[i+2].p;
            const double ang = angleDegBetween(a, b, c);
            if (ang <= cfg.collinearDeg || fabs(180.0 - ang) <= cfg.collinearDeg) {
                lookaheadQ.erase(lookaheadQ.begin() + (long)(i+1));
                changed = true;
                break;
            }
        }
    }
}

Task *Runner::getNextTask() {
    if (prefaceIx < prefaceCount) {
        currentTaskCountsDistance = false;
        return prefaceSequence[prefaceIx++];
    }

    if (lookaheadQ.empty()) {
        fillLookaheadQueue();
    }

    if (lookaheadQ.empty() && eofReached) {
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

    if (lookaheadQ.empty()) return nullptr;

    QueuedCommand cmd = lookaheadQ.front();
    lookaheadQ.pop_front();

    if (cmd.type == QueuedCommand::Pen) {
        currentTaskCountsDistance = false;
        penIsDown = cmd.penDown;
        return cmd.penDown ? (Task*)new PenTask(false, pen) : (Task*)new PenTask(true, pen);
    }

    targetPosition = cmd.p;
    currentTaskCountsDistance = true;
    return new InterpolatingMovementTask(movement, targetPosition, penIsDown ? printSpeedSteps : moveSpeedSteps);
}

void Runner::run() {
    if (stopped) return;

    if (abortRequested) {
        if (movement && movement->isMoving()) return;

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
        lookaheadQ.clear();
        eofReached = false;

        currentTask = getNextTask();
        if (currentTask) {
            currentTask->startRunning();
            stopped = false;
        } else {
            stopped = true;
        }
        return;
    }

    if (paused) return;
    if (!currentTask) { stopped = true; return; }

    if (currentTask->isDone()) {
        if (currentTask->name() == InterpolatingMovementTask::NAME && currentTaskCountsDistance) {
            const double distanceCovered = Movement::distanceBetweenPoints(startPosition, targetPosition);
            jobDistanceSoFar += distanceCovered;
            startPosition = targetPosition;

            int newProgress = 0;
            if (jobTotalDistance > 0.0) newProgress = (int)floor((jobDistanceSoFar / jobTotalDistance) * 100.0);
            if (newProgress > 100) newProgress = 100;
            if (newProgress < 0) newProgress = 0;

            if (progress != newProgress) {
                progress = newProgress;
                if (display) display->displayText(String(progress) + "%");
            }
        }

        delete currentTask;
        currentTask = getNextTask();

        if (currentTask) currentTask->startRunning();
        else stopped = true;
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
    abortRequested = true;

    if (openedFile) {
        openedFile.close();
        openedFile = File();
    }

    if (currentTask) {
        delete currentTask;
        currentTask = nullptr;
    }

    prefaceIx = 0;
    prefaceCount = 0;
    sequenceIx = 0;
    lookaheadQ.clear();
    eofReached = false;

    Movement::Point home = movement->getHomeCoordinates();
    finishingSequence[0] = new PenTask(true, pen);
    finishingSequence[1] = new InterpolatingMovementTask(movement, home, moveSpeedSteps);

    sequenceIx = 0;
    stopped = false;
    paused = false;

    currentTask = getNextTask();
    if (currentTask) currentTask->startRunning();

    WebLog::warn("Abort requested. Going home.");
}

void Runner::start() {
    paused = false;
    abortRequested = false;
    initTaskProvider();

    if (currentTask) {
        delete currentTask;
        currentTask = nullptr;
    }

    currentTask = getNextTask();
    if (currentTask) {
        currentTask->startRunning();
        stopped = false;
        WebLog::info("Runner started");
    } else {
        stopped = true;
        WebLog::warn("Runner start: no task");
    }
}

bool Runner::isStopped() const { return stopped; }
bool Runner::isPaused() const { return paused; }

int Runner::getProgress() const {
    if (progress < 0) return 0;
    if (progress > 100) return 100;
    return progress;
}

double Runner::getTotalDistance() const { return jobTotalDistance; }
double Runner::getDistanceSoFar() const { return jobDistanceSoFar; }
