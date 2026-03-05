#include "commands_optimizer.h"
#include <SD.h>
#include <math.h>

static bool parsePointLine_(const String &line, double &x, double &y) {
  // expected: "<x> <y>"
  int sp = line.indexOf(' ');
  if (sp <= 0) return false;
  String sx = line.substring(0, sp);
  String sy = line.substring(sp + 1);
  sx.trim(); sy.trim();
  if (sx.length() == 0 || sy.length() == 0) return false;
  x = sx.toDouble();
  y = sy.toDouble();
  return true;
}

static void writeLine_(File &out, const String &line) {
  out.print(line);
  out.print('\n');
}

bool optimizeCommandsPenMergeMM(double mmThreshold, CommandsOptimizeStats &stats) {
  if (mmThreshold < 0) mmThreshold = 0;
  if (mmThreshold > 20) mmThreshold = 20;

  if (!SD.exists("/commands")) return false;

  // temp output
  if (SD.exists("/commands.tmp")) SD.remove("/commands.tmp");
  File in = SD.open("/commands", FILE_READ);
  if (!in) return false;

  File out = SD.open("/commands.tmp", FILE_WRITE);
  if (!out) { in.close(); return false; }

  // Copy header lines (d... / h...)
  String dLine = in.readStringUntil('\n'); dLine.trim();
  String hLine = in.readStringUntil('\n'); hLine.trim();
  if (!dLine.startsWith("d") || !hLine.startsWith("h")) {
    in.close(); out.close();
    SD.remove("/commands.tmp");
    return false;
  }
  writeLine_(out, dLine);
  writeLine_(out, hLine);

  bool havePrev = false;
  double prevX = 0, prevY = 0;

  while (in.available()) {
    String line = in.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;

    if (line == "p0") {
      stats.inPenLines++;

      // Lookahead: point + pen
      if (!in.available()) { writeLine_(out, line); stats.outPenLines++; break; }
      String ptLine = in.readStringUntil('\n'); ptLine.trim();
      if (!in.available()) {
        writeLine_(out, line); stats.outPenLines++;
        if (ptLine.length()) writeLine_(out, ptLine);
        break;
      }
      String penLine = in.readStringUntil('\n'); penLine.trim();
      if (penLine == "p0" || penLine == "p1") stats.inPenLines++;

      double x=0,y=0;
      const bool isPt = parsePointLine_(ptLine, x, y);
      const bool isP1 = (penLine == "p1");

      if (isPt && isP1 && havePrev) {
        const double dx = x - prevX;
        const double dy = y - prevY;
        const double dist = sqrt(dx*dx + dy*dy);

        if (dist <= mmThreshold) {
          // remove p0 and p1, keep point as draw-through
          writeLine_(out, ptLine);
          stats.removedCycles++;
          stats.removedPenLines += 2;
          prevX = x; prevY = y; havePrev = true;
          continue;
        }
      }

      // keep original triple
      writeLine_(out, "p0"); stats.outPenLines++;
      if (ptLine.length()) writeLine_(out, ptLine);
      if (penLine.length()) {
        writeLine_(out, penLine);
        if (penLine == "p0" || penLine == "p1") stats.outPenLines++;
      }

      if (isPt) { prevX = x; prevY = y; havePrev = true; }
      continue;
    }

    if (line == "p1") {
      stats.inPenLines++;
      writeLine_(out, line);
      stats.outPenLines++;
      continue;
    }

    // point or other lines
    double x=0,y=0;
    if (parsePointLine_(line, x, y)) { prevX = x; prevY = y; havePrev = true; }
    writeLine_(out, line);
  }

  in.close();
  out.close();

  // Replace /commands
  SD.remove("/commands.bak");
  if (SD.exists("/commands")) SD.rename("/commands", "/commands.bak");
  if (!SD.rename("/commands.tmp", "/commands")) {
    // rollback best-effort
    SD.remove("/commands.tmp");
    if (SD.exists("/commands.bak")) SD.rename("/commands.bak", "/commands");
    return false;
  }
  SD.remove("/commands.bak");
  return true;
}
