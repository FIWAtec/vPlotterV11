#include "sdcard_service.h"

#include <SPI.h>
#include <SD.h>

#ifndef SD_CS_PIN
  #define SD_CS_PIN 5
#endif

bool SdCardService::begin() {
  _info = SdInfo{};

  if (!SD.begin(SD_CS_PIN)) {
    _info.mounted = false;
    _info.error = "SD.begin failed (CS pin / wiring / card format prüfen)";
    return false;
  }

  _info.mounted = true;
  _info.error = "";
  refreshStats();
  return true;
}

SdInfo SdCardService::info() {
  refreshStats();
  return _info;
}

FS* SdCardService::fs() {
  if (!_info.mounted) return nullptr;
  return (FS*)&SD; 
}

void SdCardService::refreshStats() {
  if (!_info.mounted) return;

  uint64_t total = SD.totalBytes();
  uint64_t used  = SD.usedBytes();

  _info.totalBytes = total;
  _info.usedBytes  = used;
  _info.freeBytes  = (total > used) ? (total - used) : 0;
}
