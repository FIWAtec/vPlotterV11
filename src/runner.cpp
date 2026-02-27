#include "runner.h"
#include "tasks/interpolatingmovementtask.h"
#include "tasks/pentask.h"

#include <stdexcept>
#include <math.h>
#include <algorithm>
#include <deque>

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


static bool parseArcLine(const String& line, bool& cw, double& x, double& y, double& i, double& j) {
    if (line.length() < 2) return false;
    const char c0 = tolower(line.charAt(0));
    const char c1 = tolower(line.charAt(1));
    if (c0 != 'g' || (c1 != '2' && c1 != '3')) return false;

    // expected: g2 X Y I J  (CW)  or g3 X Y I J (CCW)
    cw = (c1 == '2');

    // split by spaces
    double vals[4];
    int found = 0;
    int pos = 2;
    while (pos < line.length() && found < 4) {
        while (pos < line.length() && line.charAt(pos) == ' ') pos++;
        if (pos >= line.length()) break;
        int next = line.indexOf(' ', pos);
        String token = (next < 0) ? line.substring(pos) : line.substring(pos, next);
        token.trim();
        if (token.length() > 0) {
            vals[found++] = token.toDouble();
        }
        if (next < 0) break;
        pos = next + 1;
    }
    if (found != 4) return false;
    x = vals[0]; y = vals[1]; i = vals[2]; j = vals[3];
    return true;
}

static void appendArcSamples(const Movement::Point& start, bool cw, double endX, double endY, double i, double j,
                            const Movement::PlannerConfig& cfg, std::deque<Movement::Point>& out) {
    const double cx = start.x + i;
    const double cy = start.y + j;
    const double sx = start.x - cx;
    const double sy = start.y - cy;
    const double ex = endX - cx;
    const double ey = endY - cy;

    const double r0 = sqrt(sx*sx + sy*sy);
    const double r1 = sqrt(ex*ex + ey*ey);
    const double r = (r0 + r1) * 0.5;
    if (r < 1e-6) {
        out.emplace_back(Movement::Point(endX, endY));
        return;
    }

    double a0 = atan2(sy, sx);
    double a1 = atan2(ey, ex);

    // normalize sweep to follow cw/ccw
    double sweep = a1 - a0;
    if (cw) {
        if (sweep > 0) sweep -= 2.0 * PI;
    } else {
        if (sweep < 0) sweep += 2.0 * PI;
    }
    const double arcLen = fabs(sweep) * r;

    // chord error from junction deviation (bounded)
    double chordErr = cfg.junctionDeviationMM * 2.0;
    if (chordErr < 0.05) chordErr = 0.05;
    if (chordErr > 0.50) chordErr = 0.50;

    // step length from chord error: s ~= sqrt(8*e*R)
    double step = sqrt(std::max(1e-9, 8.0 * chordErr * r));
    const double minStep = std::max(0.05, cfg.minSegmentLenMM);
    const double maxStep = std::max(minStep, 8.0); // keep sane
    if (step < minStep) step = minStep;
    if (step > maxStep) step = maxStep;

    int n = (int)ceil(arcLen / step);
    if (n < 1) n = 1;
    if (n > 4096) n = 4096;

    for (int k = 1; k <= n; k++) {
        const double t = (double)k / (double)n;
        const double ang = a0 + sweep * t;
        const double px = cx + r * cos(ang);
        const double py = cy + r * sin(ang);
        out.emplace_back(Movement::Point(px, py));
    }
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
    pendingMovePoints.clear();
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

        // Optional arc command: g2 X Y I J or g3 X Y I J
        {
            bool cw=false; double ex=0, ey=0, ii=0, jj=0;
            if (parseArcLine(l, cw, ex, ey, ii, jj)) {
                // approximate arc length for progress skipping
                const double cx = virtualPos.x + ii;
                const double cy = virtualPos.y + jj;
                const double sx = virtualPos.x - cx;
                const double sy = virtualPos.y - cy;
                const double tx = ex - cx;
                const double ty = ey - cy;
                const double r0 = sqrt(sx*sx + sy*sy);
                const double r1 = sqrt(tx*tx + ty*ty);
                const double r = (r0 + r1) * 0.5;
                if (r > 1e-6) {
                    double a0 = atan2(sy, sx);
                    double a1 = atan2(ty, tx);
                    double sweep = a1 - a0;
                    if (cw) { if (sweep > 0) sweep -= 2.0 * PI; }
                    else { if (sweep < 0) sweep += 2.0 * PI; }
                    skippedDistance += fabs(sweep) * r;
                } else {
                    skippedDistance += Movement::distanceBetweenPoints(virtualPos, Movement::Point(ex, ey));
                }
                virtualPos = Movement::Point(ex, ey);
                consumed++;
                continue;
            }
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

    // First, flush any pending expanded move points (e.g., from arcs)
    while ((int)lookaheadQ.size() < maxSegments && !pendingMovePoints.empty()) {
        lookaheadQ.emplace_back(pendingMovePoints.front());
        pendingMovePoints.pop_front();
    }

    // Determine current virtual position for parsing arcs.
    Movement::Point virtualPos = targetPosition;
    for (auto it = lookaheadQ.rbegin(); it != lookaheadQ.rend(); ++it) {
        if (it->type == QueuedCommand::Move) { virtualPos = it->p; break; }
    }

    while (!eofReached && (int)lookaheadQ.size() < maxSegments && openedFile.available()) {
        String line = openedFile.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;

        const char c0 = tolower(line.charAt(0));
        if (c0 == 'p') {
            const char c1 = (line.length() > 1) ? line.charAt(1) : '0';
            lookaheadQ.emplace_back(c1 == '1');
            continue;
        }

        // Optional arc: g2 X Y I J / g3 X Y I J
        {
            bool cw=false; double ex=0, ey=0, ii=0, jj=0;
            if (parseArcLine(line, cw, ex, ey, ii, jj)) {
                std::deque<Movement::Point> samples;
                appendArcSamples(virtualPos, cw, ex, ey, ii, jj, movement->getPlannerConfig(), samples);

                // push as many samples as we can; keep the rest for next fill call
                while ((int)lookaheadQ.size() < maxSegments && !samples.empty()) {
                    Movement::Point p = samples.front();
                    samples.pop_front();
                    lookaheadQ.emplace_back(p);
                    virtualPos = p;
                }
                while (!samples.empty()) {
                    pendingMovePoints.push_back(samples.front());
                    samples.pop_front();
                }
                continue;
            }
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
    pendingMovePoints.clear();
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
    pendingMovePoints.clear();
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