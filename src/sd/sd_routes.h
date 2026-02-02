#pragma once
#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include "sd_manager.h"

class SdRoutes {
public:
  static void registerRoutes(AsyncWebServer& server, SdManager& sd);
};
