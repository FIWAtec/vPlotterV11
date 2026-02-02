#pragma once
#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>

#include "sdcard_service.h"

class FsApi {
public:
  FsApi(AsyncWebServer& server, SdCardService& sd);
  void registerRoutes();

private:
  AsyncWebServer& _server;
  SdCardService&  _sd;

  FS* resolveFs(const String& target);
  String normPath(String p);
  bool isSafePath(const String& p);

  // routes
  void handleInfo(AsyncWebServerRequest* req);
  void handleList(AsyncWebServerRequest* req);
  void handleRead(AsyncWebServerRequest* req);
  void handleDownload(AsyncWebServerRequest* req);

  void handleDelete(AsyncWebServerRequest* req);
  void handleMkdir(AsyncWebServerRequest* req);
  void handleRename(AsyncWebServerRequest* req);
  void handleCopy(AsyncWebServerRequest* req);
  void handleMove(AsyncWebServerRequest* req);

  void handleUpload(
    AsyncWebServerRequest* req,
    String filename,
    size_t index,
    uint8_t* data,
    size_t len,
    bool final
  );

  bool copyFile(FS& fs, const String& from, const String& to);
};
