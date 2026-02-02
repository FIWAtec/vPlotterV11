#include <SD.h>
#include <FS.h>

#if __has_include(<sys/statvfs.h>)
  #include <sys/statvfs.h>
  #define HAS_STATVFS 1
#else
  #define HAS_STATVFS 0
#endif

struct SdStats {
  uint64_t total = 0;
  uint64_t used  = 0;
  uint64_t free  = 0;
  bool ok = false;
};

static SdStats getSdStats() {
  SdStats s;

  // Variante A: Arduino-ESP32 SD API (cardSize)
  // cardSize() liefert Bytes, wenn gemountet
  uint64_t total = 0;
  #if defined(ARDUINO_ARCH_ESP32)
    total = SD.cardSize();
  #endif

  // usedBytes() gibt es in manchen Cores auch für SD, aber nicht immer.
  uint64_t used = 0;
  bool usedOk = false;

  // Versuch 1: SD.usedBytes() falls verfügbar (manche Core-Versionen)
  // Wir können das nicht sauber per preprocessor "detecten", daher fallback über statvfs.
  // -> usedOk bleibt erstmal false.

  // Variante B: POSIX statvfs (funktioniert bei ESP32 oft für gemountete FAT)
  #if HAS_STATVFS
    struct statvfs vfs;
    // SD wird in ESP32 Arduino i.d.R. unter "/sd" gemountet, manchmal "/sdcard"
    // Wenn du einen anderen Mountpoint nutzt: hier anpassen!
    const char* mountCandidates[] = { "/sd", "/sdcard", "/SD", "/SDCARD" };

    for (auto mp : mountCandidates) {
      if (statvfs(mp, &vfs) == 0) {
        const uint64_t blockSize = (uint64_t)vfs.f_frsize;
        const uint64_t blocksTotal = (uint64_t)vfs.f_blocks;
        const uint64_t blocksFree  = (uint64_t)vfs.f_bfree;

        s.total = blockSize * blocksTotal;
        s.free  = blockSize * blocksFree;
        s.used  = (s.total >= s.free) ? (s.total - s.free) : 0;
        s.ok = true;
        return s;
      }
    }
  #endif

  // Fallback: wenn wir total haben, aber used nicht, liefern wir wenigstens total und ok=false für used/free
  if (total > 0) {
    s.total = total;
    s.used = 0;
    s.free = 0;
    s.ok = true; // total ist gültig
    return s;
  }

  s.ok = false;
  return s;
}
