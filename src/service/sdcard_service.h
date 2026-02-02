#pragma once
#include <Arduino.h>
#include <FS.h>

struct SdInfo {
  bool mounted = false;
  uint64_t totalBytes = 0;
  uint64_t usedBytes  = 0;
  uint64_t freeBytes  = 0;
  String   error;
};

class SdCardService {
public:
  bool begin();
  SdInfo info();

  FS* fs();

private:
  SdInfo _info;
  void refreshStats();
};
