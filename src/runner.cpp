#include "runner.h"
#include "tasks/interpolatingmovementtask.h"
#include "tasks/pentask.h"

#include <Arduino.h>

#include <stdexcept>
#include <math.h>
#include <algorithm>
#include <deque>
#include <vector>

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

// GRBL-style junction deviation limit (approx) -> max junction speed in mm/s.
// thetaRad: angle between segments (0=straight)
static double junctionSpeedMmS(double thetaRad, double accelMmS2, double junctionDeviationMm) {
    if (thetaRad < 1e-6) return 1e9;
    // Use GRBL formula: v = sqrt( (a * jd * sin(theta/2)) / (1 - sin(theta/2)) )
    const double sinHalf = sin(thetaRad * 0.5);
    if (sinHalf < 1e-9) return 1e9;
    const double denom = (1.0 - sinHalf);
    if (denom < 1e-9) return 1e9;
    const double v2 = (accelMmS2 * junctionDeviationMm * sinHalf) / denom;
    if (v2 <= 0.0) return 0.0;
    return sqrt(v2);
}

static int clampi(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

Runner::Runner(Movement *movement, Pen *pen, Display *display) {
    this->movement = movement;
    this->pen = pen;
    this->display = display;
    stopped = true;
    paused = false;
}

void Runner::setPenSettleMs(int ms) {
    if (ms < 0) ms = 0;
    if (ms > 500) ms = 500;
    penSettleMs = ms;
}

int Runner::getPenSettleMs() const {
    return penSettleMs;
}



void Runner::setPenMergeMm(double mm) {
    if (!(mm >= 0.0)) mm = 0.0;
    if (mm > 20.0) mm = 20.0;
    penMergeMm = mm;
}

double Runner::getPenMergeMm() const {
    return penMergeMm;
}
static bool parseG2G3(const String& in, bool& outCw, double& outX, double& outY, double& outI, double& outJ) {
    String s = in;
    s.trim();
    s.toLowerCase();
    if (!(s.startsWith("g2") || s.startsWith("g3"))) return false;
    outCw = s.startsWith("g2");

    s.replace('\t', ' ');
    while (s.indexOf("  ") >= 0) s.replace("  ", " ");

    std::vector<String> toks;
    int idx = 0;
    while (idx < (int)s.length()) {
        int sp = s.indexOf(' ', idx);
        if (sp < 0) sp = s.length();
        const String t = s.substring(idx, sp);
        if (t.length() > 0) toks.push_back(t);
        idx = sp + 1;
    }
    if (toks.size() < 2) return false;

    bool hasLabel = false;
    for (size_t i = 1; i < toks.size(); i++) {
        if (toks[i].length() >= 2) {
            const char k = toks[i].charAt(0);
            if (k == 'x' || k == 'y' || k == 'i' || k == 'j') { hasLabel = true; break; }
        }
    }

    if (hasLabel) {
        bool hx=false, hy=false, hi=false, hj=false;
        for (size_t i = 1; i < toks.size(); i++) {
            const String t = toks[i];
            if (t.length() < 2) continue;
            const char k = t.charAt(0);
            const double v = t.substring(1).toDouble();
            if (k == 'x') { outX = v; hx=true; }
            if (k == 'y') { outY = v; hy=true; }
            if (k == 'i') { outI = v; hi=true; }
            if (k == 'j') { outJ = v; hj=true; }
        }
        return hx && hy && hi && hj;
    }

    if (toks.size() < 5) return false;
    outX = toks[1].toDouble();
    outY = toks[2].toDouble();
    outI = toks[3].toDouble();
    outJ = toks[4].toDouble();
    return true;
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
    jobDrawDistanceSoFar = 0.0;
    jobTravelDistanceSoFar = 0.0;

    // reset timing (real start happens in start()/restart)
    lastTickMs = 0;

    progress = -1;

    // Always force pen UP at (re)start to avoid "pen down while travel" situations.
    prefaceSequence[prefaceCount++] = new PenTask(true, pen, penSettleMs);

    if (startLine > 0) {

        if (!(virtualPos.x == startPosition.x && virtualPos.y == startPosition.y)) {
            prefaceSequence[prefaceCount++] = new InterpolatingMovementTask(movement, virtualPos, moveSpeedSteps);
            startPosition = virtualPos;
        }

        if (penDown) { prefaceSequence[prefaceCount++] = new PenTask(false, pen, penSettleMs); penIsDown = true; }
    }

    Movement::Point home = movement->getHomeCoordinates();
    finishingSequence[0] = new PenTask(true, pen, penSettleMs);
    finishingSequence[1] = new InterpolatingMovementTask(movement, home, moveSpeedSteps);
}

bool Runner::fillLookaheadQueue() {
    if (!openedFile) return false;
    const int maxSegments = movement->getPlannerConfig().lookaheadSegments;

    Movement::Point virtualPos = startPosition;
    for (auto it = lookaheadQ.rbegin(); it != lookaheadQ.rend(); ++it) {
        if (it->type == QueuedCommand::Move) { virtualPos = it->p; break; }
    }

    while (!eofReached && (int)lookaheadQ.size() < maxSegments && openedFile.available()) {
        String line = openedFile.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;

        const char c0 = line.charAt(0);
        if (c0 == 'p') {
            const char c1 = (line.length() > 1) ? line.charAt(1) : '0';
            const bool down = (c1 == '1');

            // Skip redundant pen commands to avoid unnecessary servo churn.
            if (!lookaheadQ.empty()) {
                const auto& last = lookaheadQ.back();
                if (last.type == QueuedCommand::Pen && last.penDown == down) {
                    continue;
                }
            }

            lookaheadQ.emplace_back(down);
            continue;
        }

        bool cw = false;
        double ax = 0.0, ay = 0.0, ai = 0.0, aj = 0.0;
        if (parseG2G3(line, cw, ax, ay, ai, aj)) {
            const auto cfg = movement->getPlannerConfig();
            const Movement::Point end(ax, ay);

            const double cx = virtualPos.x + ai;
            const double cy = virtualPos.y + aj;
            const double rs = hypot(virtualPos.x - cx, virtualPos.y - cy);
            const double re = hypot(end.x - cx, end.y - cy);
            if (rs < 1e-6 || fabs(rs - re) > 0.25) {
                lookaheadQ.emplace_back(end);
                virtualPos = end;
                continue;
            }

            double a0 = atan2(virtualPos.y - cy, virtualPos.x - cx);
            double a1 = atan2(end.y - cy, end.x - cx);

            double da = a1 - a0;
            if (cw) {
                if (da >= 0) da -= 2.0 * PI;
            } else {
                if (da <= 0) da += 2.0 * PI;
            }

            const double sweep = da;
            const double sweepAbs = fabs(sweep);
            if (sweepAbs < 1e-6) {
                lookaheadQ.emplace_back(end);
                virtualPos = end;
                continue;
            }

            const double chordErr = std::max(0.02, std::min(0.50, std::max(cfg.minSegmentLenMM * 0.15, cfg.junctionDeviationMM * 0.50)));
            double maxStepByErr = 2.0 * acos(std::max(-1.0, std::min(1.0, 1.0 - (chordErr / rs))));
            if (!(maxStepByErr > 1e-6)) maxStepByErr = (2.0 * PI) / 360.0;

            double step = maxStepByErr;
            if (cfg.minSegmentLenMM > 1e-6) {
                const double minStepByLen = cfg.minSegmentLenMM / rs;
                if (minStepByLen > step) step = minStepByLen;
            }

            int n = (int)ceil(sweepAbs / step);
            if (n < 1) n = 1;
            if (n > 4096) n = 4096;

            for (int k = 1; k <= n; k++) {
                const double t = (double)k / (double)n;
                const double a = a0 + sweep * t;
                const double x = cx + cos(a) * rs;
                const double y = cy + sin(a) * rs;
                lookaheadQ.emplace_back(Movement::Point(x, y), true);
            }

            virtualPos = end;
            continue;
        }

        int sep = line.indexOf(' ');
        if (sep < 0) continue;

        double x = line.substring(0, sep).toDouble();
        double y = line.substring(sep + 1).toDouble();
        lookaheadQ.emplace_back(Movement::Point(x, y));
        virtualPos = Movement::Point(x, y);
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
            if (!it->protect && d < cfg.minSegmentLenMM) {
                it = lookaheadQ.erase(it);
                continue;
            }
            prev = it->p;
        }
        ++it;
    }

    // ------------------------------------------------------------------
    // NEW: Reduce pen up/down churn safely (no geometry change):
    // - Buffer pen state changes and only emit them right before the next real MOVE.
    // - Drop zero-length moves.
    // - Drop pen toggles that are never followed by a move.
    // ------------------------------------------------------------------
    {
        std::deque<QueuedCommand> out;
        Movement::Point cur = startPosition;

        bool penDown = false;          // assume start is UP
        bool pending = false;
        bool pendingState = false;

        auto flushPendingIfNeeded = [&]() {
            if (pending) {
                out.emplace_back(pendingState); // Pen command
                penDown = pendingState;
                pending = false;
            }
        };

        const double eps = 1e-6;

        for (const auto &cmd : lookaheadQ) {
            if (cmd.type == QueuedCommand::Pen) {
                // ignore redundant state
                if (cmd.penDown == penDown) continue;

                // buffer state change (overwrite if multiple toggles happen without a move)
                pending = true;
                pendingState = cmd.penDown;
                continue;
            }

            // Move
            const double d = Movement::distanceBetweenPoints(cur, cmd.p);
            if (d < eps) {
                // no-op move => drop
                continue;
            }

            // there is a real move: apply pending pen state right before it
            flushPendingIfNeeded();

            out.push_back(cmd);
            cur = cmd.p;
        }

        // If pending is still set here, it means a pen change at end without movement -> drop it.
        lookaheadQ.swap(out);
    }

// ------------------------------------------------------------------
// NEW: Merge short pen-up travels (p0 -> short move -> p1) ON THE FLY.
// This reduces pen up/down cycles but draws a short connector line.
// Enabled when penMergeMm > 0.
// ------------------------------------------------------------------
if (penMergeMm > 0.0 && !lookaheadQ.empty()) {
    std::deque<QueuedCommand> out;
    Movement::Point cur = startPosition;
    bool penDown = false; // start UP

    for (size_t i = 0; i < lookaheadQ.size(); ++i) {
        const auto &cmd = lookaheadQ[i];

        // Detect: PenUp, Move, PenDown
        if (cmd.type == QueuedCommand::Pen && cmd.penDown == false && penDown == true) {
            if (i + 2 < lookaheadQ.size()
                && lookaheadQ[i+1].type == QueuedCommand::Move
                && lookaheadQ[i+2].type == QueuedCommand::Pen
                && lookaheadQ[i+2].penDown == true) {

                const Movement::Point& np = lookaheadQ[i+1].p;
                const double d = Movement::distanceBetweenPoints(cur, np);
                if (d <= penMergeMm) {
                    // Skip PenUp + PenDown, draw through the short move.
                    out.emplace_back(np, lookaheadQ[i+1].protect);
                    cur = np;
                    // penDown stays true
                    i += 2;
                    continue;
                }
            }
        }

        // Default: copy command and update state/cur
        out.push_back(cmd);
        if (cmd.type == QueuedCommand::Pen) {
            penDown = cmd.penDown;
        } else {
            cur = cmd.p;
        }
    }

    lookaheadQ.swap(out);
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

            if (lookaheadQ[i].protect || lookaheadQ[i+1].protect || lookaheadQ[i+2].protect) continue;

            const auto &a = anchor;
            const auto &b = lookaheadQ[i].p;
            const auto &c = lookaheadQ[i+1].p;
            const auto &d = lookaheadQ[i+2].p;

            const double ang = angleDegBetween(a, b, c);
            if (fabs(ang) <= cfg.collinearDeg || fabs(180.0 - ang) <= cfg.collinearDeg) {
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
        return cmd.penDown ? (Task*)new PenTask(false, pen, penSettleMs) : (Task*)new PenTask(true, pen, penSettleMs);
    }

    targetPosition = cmd.p;
    currentTaskCountsDistance = true;
    currentMoveIsDrawing = penIsDown;

    // -------- Lookahead-based speed planning (task-level) --------
    int baseSpeedSteps = penIsDown ? printSpeedSteps : moveSpeedSteps;
    int plannedSpeedSteps = baseSpeedSteps;

    try {
        const auto cfg = movement->getPlannerConfig();

        // Find next move point in lookaheadQ (skip pen commands)
        Movement::Point nextMove;
        bool hasNext = false;
        for (const auto& c : lookaheadQ) {
            if (c.type == QueuedCommand::Move) {
                nextMove = c.p;
                hasNext = true;
                break;
            }
        }

        const double dist = Movement::distanceBetweenPoints(startPosition, targetPosition);

        if (hasNext && dist > 1e-6) {
            const int maxDelta = movement->estimateMaxDeltaSteps(targetPosition.x, targetPosition.y);
            if (maxDelta > 0) {
                // Convert belt accel (steps/s^2) into mm/s^2 using mm per step.
                const auto tuning = movement->getMotionTuning();
                const double mmPerStep = stepsToMM(1);
                const double accelMmS2 = std::max(1.0, (double)tuning.acceleration * mmPerStep);

                // Nominal XY speed (mm/s) implied by the requested step rate.
                const double vNomMmS = (dist * (double)baseSpeedSteps) / (double)maxDelta;

                // Corner angle (0=straight).
                const double angDeg = angleDegBetween(startPosition, targetPosition, nextMove);
                const double theta = (angDeg * PI) / 180.0;

                // Angle-based slowdown (existing UI tuning).
                const double sharpness = theta / PI; // 0..1
                double f = 1.0 - sharpness * cfg.cornerSlowdown;
                if (f < cfg.minCornerFactor) f = cfg.minCornerFactor;
                if (f > 1.0) f = 1.0;
                const double vAngleMmS = vNomMmS * f;

                // Physics-ish junction limit.
                const double vJuncMmS = junctionSpeedMmS(theta, accelMmS2, cfg.junctionDeviationMM);

                double vPlannedMmS = vNomMmS;
                vPlannedMmS = std::min(vPlannedMmS, vAngleMmS);
                vPlannedMmS = std::min(vPlannedMmS, vJuncMmS);

                if (vPlannedMmS < 1e-3) vPlannedMmS = 1e-3;

                const int steps = (int)floor((vPlannedMmS * (double)maxDelta) / dist);
                plannedSpeedSteps = clampi(steps, 1, baseSpeedSteps);
            }
        }
    } catch (...) {
        plannedSpeedSteps = baseSpeedSteps;
    }

    return new InterpolatingMovementTask(movement, targetPosition, plannedSpeedSteps);
}

void Runner::run() {
    if (stopped) return;

    tickTiming_();

    // Restart requested from UI scrubbing (pause spool)
    if (restartRequested) {
        if (movement && movement->isMoving()) return;

        restartRequested = false;
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

        // reset metrics
        jobDistanceSoFar = 0.0;
        jobDrawDistanceSoFar = 0.0;
        jobTravelDistanceSoFar = 0.0;
        progress = -1;

        // restart timing
        jobStartMs = millis();
        lastTickMs = jobStartMs;
        totalPausedMs = 0;
        movingActiveMs = 0;

        setStartLine(restartLineAfterHeader);
        initTaskProvider();

        currentTask = getNextTask();
        if (currentTask) {
            currentTask->startRunning();
            stopped = false;
        } else {
            stopped = true;
        }
        WebLog::info(String("Runner restarted from line ") + restartLineAfterHeader);
        return;
    }

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
            if (currentMoveIsDrawing) jobDrawDistanceSoFar += distanceCovered;
            else jobTravelDistanceSoFar += distanceCovered;
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
    finishingSequence[0] = new PenTask(true, pen, penSettleMs);
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

    // reset timing
    jobStartMs = millis();
    lastTickMs = jobStartMs;
    pauseStartMs = 0;
    totalPausedMs = 0;
    movingActiveMs = 0;

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

double Runner::getDrawDistanceSoFar() const { return jobDrawDistanceSoFar; }
double Runner::getTravelDistanceSoFar() const { return jobTravelDistanceSoFar; }

uint32_t Runner::getElapsedMs() const {
    if (jobStartMs == 0) return 0;
    const uint32_t now = millis();
    uint32_t elapsed = (now >= jobStartMs) ? (now - jobStartMs) : 0;
    uint32_t pausedExtra = totalPausedMs;
    if (paused && pauseStartMs > 0 && now >= pauseStartMs) pausedExtra += (now - pauseStartMs);
    if (elapsed >= pausedExtra) elapsed -= pausedExtra;
    else elapsed = 0;
    return elapsed;
}

uint32_t Runner::getMovingActiveMs() const { return movingActiveMs; }

double Runner::getAvgSpeedMmS_MovingOnly() const {
    const double t = (double)getMovingActiveMs() / 1000.0;
    if (t <= 1e-6) return 0.0;
    return getDistanceSoFar() / t;
}

bool Runner::isParked() const { return parked; }

void Runner::tickTiming_() {
    const uint32_t now = millis();
    if (lastTickMs == 0) { lastTickMs = now; return; }
    const uint32_t dt = (now >= lastTickMs) ? (now - lastTickMs) : 0;
    lastTickMs = now;

    if (paused) return;

    if (movement && movement->isMoving()) {
        movingActiveMs += dt;
    }
}

bool Runner::requestRestartFromLine(size_t lineAfterHeader) {
    // Allow while paused; robot will restart only when movement is idle.
    restartLineAfterHeader = lineAfterHeader;
    restartRequested = true;
    return true;
}

bool Runner::requestParkTo(double xMm, double yMm) {
    if (!paused) return false;
    if (!movement || !pen) return false;
    if (movement->isMoving()) return false;

    // Lift pen and move to requested position (simple park, no auto-return).
    pen->slowUp();
    if (penSettleMs > 0) delay((unsigned long)penSettleMs);

    try {
        movement->beginLinearTravel(xMm, yMm, moveSpeedSteps);
        parked = true;
        return true;
    } catch (...) {
        return false;
    }
}

bool Runner::requestParkToBase(double yMm) {
    if (!movement) return false;
    const double x = movement->getWidth() / 2.0;
    return requestParkTo(x, yMm);
}
