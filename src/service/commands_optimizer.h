#pragma once
#include <Arduino.h>

struct CommandsOptimizeStats {
  uint32_t removedCycles = 0;     // removed (p0 + p1) around a short travel
  uint32_t removedPenLines = 0;   // removed pen command lines total (2 per cycle)
  uint32_t inPenLines = 0;        // original p0/p1 count
  uint32_t outPenLines = 0;       // output p0/p1 count
};

bool optimizeCommandsPenMergeMM(double mmThreshold, CommandsOptimizeStats &stats);
