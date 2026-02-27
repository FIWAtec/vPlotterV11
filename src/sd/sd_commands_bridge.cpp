#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include "sd_commands_bridge.h"

// This bridge exists because several modules need a simple "SD is mounted" check
// without depending on main.cpp internals.

#ifndef SD_SCK_PIN
  #define SD_SCK_PIN   14
#endif
#ifndef SD_MOSI_PIN
  #define SD_MOSI_PIN  13
#endif
#ifndef SD_MISO_PIN
  #define SD_MISO_PIN  12
#endif
#ifndef SD_CS_PIN
  #define SD_CS_PIN    15
#endif

static SPIClass gSdSpi(HSPI);
static bool gMounted = false;
static uint32_t gLastAttemptMs = 0;

static bool sanity()
{
  File root = SD.open("/");
  const bool ok = (root && root.isDirectory());
  if (root) root.close();
  return ok;
}

bool sdCommandsIsMounted()
{
  if (!gMounted) return false;
  if (sanity()) return true;
  gMounted = false;
  return false;
}

bool sdCommandsEnsureMounted()
{
  const uint32_t now = millis();

  if (sdCommandsIsMounted()) return true;

  // throttle retries (keeps UI responsive)
  if ((now - gLastAttemptMs) < 1500) return false;
  gLastAttemptMs = now;

  SD.end();
  gSdSpi.end();
  delay(5);

  gSdSpi.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  const uint32_t freq = 8000000; // 8 MHz

  gMounted = SD.begin(SD_CS_PIN, gSdSpi, freq);
  if (!gMounted) return false;

  if (!sanity()) {
    gMounted = false;
    return false;
  }

  return true;
}
