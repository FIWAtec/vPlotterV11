#include <WiFiManager.h>
#include <AsyncTCP.h>

#include <FS.h>
#include <LittleFS.h>

#include <SPI.h>
#include <SD.h>

#include <Wire.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_system.h>

#if defined(ESP32)
  #include <time.h>
#endif

#include <ArduinoJson.h>
#include <ESP32Servo.h>
#include <functional>
#include <vector>

#include "movement.h"
#include "runner.h"
#include "pen.h"
#include "display.h"
#include "phases/phasemanager.h"
#include "service/weblog.h"

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

#include "sd/sd_manager.h"
#include "sd/sd_routes.h"

#define SD_SCK_PIN   14
#define SD_MOSI_PIN  13
#define SD_MISO_PIN  12
#define SD_CS_PIN    15

static SPIClass SdSpi(HSPI);

// ================= GLOBAL OBJECTS =================

AsyncWebServer server(80);

Preferences prefs;

Movement* movement = nullptr;
Runner* runner = nullptr;
Pen* pen = nullptr;
Display* display = nullptr;
PhaseManager* phaseManager = nullptr;

// UI will toggles exactly these two
constexpr int ENABLE_PIN_A = 27;
constexpr int ENABLE_PIN_B = 33;

constexpr const char* PREF_KEY_LEFT_EN   = "leftEnPin";
constexpr const char* PREF_KEY_RIGHT_EN  = "rightEnPin";
constexpr const char* PREF_KEY_PULSE_L   = "pulseLeftUs";
constexpr const char* PREF_KEY_PULSE_R   = "pulseRightUs";
constexpr const char* PREF_KEY_PEN_DOWN  = "penDown";
constexpr const char* PREF_KEY_PEN_UP    = "penUp";

// Planner / quality tuning preference keys
// Keep keys short (Preferences/NVS key length limit)
constexpr const char* PREF_KEY_JUNC_DEV   = "jdev";
constexpr const char* PREF_KEY_LOOKAHEAD  = "lookahd";
constexpr const char* PREF_KEY_MINSEGMS   = "minsegms";
constexpr const char* PREF_KEY_CORNERSLOW = "cornslow";
constexpr const char* PREF_KEY_MINCORNER  = "mincorn";
constexpr const char* PREF_KEY_MINSEGLEN  = "minsegln";
constexpr const char* PREF_KEY_COLLINEAR  = "colinr";
constexpr const char* PREF_KEY_BACKLASHX  = "backlx";
constexpr const char* PREF_KEY_BACKLASHY  = "backly";
constexpr const char* PREF_KEY_SCURVE     = "scurve";

// Perf stats
struct PerfStats {
  uint32_t loop_us = 0;
  uint32_t yield_us = 0;
  uint32_t move_us = 0;
  uint32_t runner_us = 0;
  uint32_t phase_us = 0;

  uint32_t loop_us_avg = 0;
  uint32_t yield_us_avg = 0;
  uint32_t move_us_avg = 0;
  uint32_t runner_us_avg = 0;
  uint32_t phase_us_avg = 0;

  uint32_t max_loop_us = 0;
  uint32_t last_update_ms = 0;

  void update(uint32_t loopu, uint32_t yieldu, uint32_t moveu, uint32_t runneru, uint32_t phaseu) {
    loop_us = loopu;
    yield_us = yieldu;
    move_us = moveu;
    runner_us = runneru;
    phase_us = phaseu;

    if (loop_us > max_loop_us) max_loop_us = loop_us;

    auto ema = [](uint32_t &avg, uint32_t v) {
      if (avg == 0) avg = v;
      else avg = (avg * 9 + v) / 10;
    };

    ema(loop_us_avg, loopu);
    ema(yield_us_avg, yieldu);
    ema(move_us_avg, moveu);
    ema(runner_us_avg, runneru);
    ema(phase_us_avg, phaseu);

    last_update_ms = millis();
  }
};

static PerfStats gPerf;

std::vector<const char *> menu = {"wifi", "sep"};

bool current = false;
bool ps5WasConnected = false;
const char* PS5_MAC = "A0:AB:51:CF:9C:D9";
bool ps5ScanRunning = false;
unsigned long ps5ScanStart = 0;
const unsigned long PS5_SCAN_DURATION = 10000;

Servo sprayServo;
int sprayAngle = 90;
int sprayStep  = 5;
const int SPRAY_MIN = 0;
const int SPRAY_MAX = 180;
bool lastR1   = false;
bool lastL1   = false;
bool lastUp   = false;
bool lastDown = false;

// --- SD: stabiler Mount + Re-Mount ---
static bool gSdMounted = false;
static uint32_t gSdLastAttemptMs = 0;
static SemaphoreHandle_t gSdMutex = nullptr;

static bool sdQuickSanity()
{
  File root = SD.open("/");
  const bool ok = (root && root.isDirectory());
  if (root) root.close();
  return ok;
}

static bool ensureSdMounted(bool forceRemount = false)
{
  const uint32_t now = millis();

  if (!forceRemount && gSdMounted) {
    if (sdQuickSanity()) return true;
    gSdMounted = false;
  }

  if (!forceRemount) {
    if ((now - gSdLastAttemptMs) < 1500) return false;
  }
  gSdLastAttemptMs = now;

  if (!gSdMutex) {
    gSdMutex = xSemaphoreCreateMutex();
  }
  if (gSdMutex) {
    xSemaphoreTake(gSdMutex, pdMS_TO_TICKS(2000));
  }

  SD.end();
  SdSpi.end();
  delay(5);
  SdSpi.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  const uint32_t freq = 8000000; // 8 MHz
  gSdMounted = SD.begin(SD_CS_PIN, SdSpi, freq);

  if (gSdMounted) WebLog::info(String("SD mounted (CS=") + SD_CS_PIN + ", " + String(freq/1000000) + "MHz)");
  else WebLog::warn(String("SD not mounted (CS=") + SD_CS_PIN + ", " + String(freq/1000000) + "MHz)");

  if (gSdMutex) {
    xSemaphoreGive(gSdMutex);
  }
  return gSdMounted;
}

// RAII Lock fuer SD-Operationen
struct SdGuard {
  bool locked = false;
  explicit SdGuard(bool enable) {
    if (!enable) return;
    if (!gSdMutex) gSdMutex = xSemaphoreCreateMutex();
    if (gSdMutex) {
      locked = (xSemaphoreTake(gSdMutex, pdMS_TO_TICKS(3000)) == pdTRUE);
    }
  }
  ~SdGuard() {
    if (locked && gSdMutex) xSemaphoreGive(gSdMutex);
  }
};

static bool isSafePath(const String& p)
{
  if (p.isEmpty()) return false;
  if (!p.startsWith("/")) return false;
  if (p.indexOf("..") >= 0) return false;
  return true;
}

static String normPath(String p)
{
  p.trim();
  if (p.isEmpty()) p = "/";
  if (!p.startsWith("/")) p = "/" + p;
  while (p.indexOf("//") >= 0) p.replace("//", "/");
  return p;
}

static fs::FS* pickFs(const String& vol)
{
  if (vol == "sd") {
    if (ensureSdMounted(false)) return &SD;
    return nullptr;
  }
  return &LittleFS;
}

static String findLargestFileNameLittleFS()
{
  File root = LittleFS.open("/");
  if (!root || !root.isDirectory()) return "—";

  File f = root.openNextFile();
  size_t maxSize = 0;
  String maxName = "—";

  while (f) {
    if (!f.isDirectory()) {
      size_t s = (size_t)f.size();
      if (s > maxSize) {
        maxSize = s;
        maxName = String(f.name()) + " (" + String((float)s / 1024.0f, 1) + " KB)";
      }
    }
    f = root.openNextFile();
  }
  return maxName;
}

static bool isAllowedEnablePin(int pin)
{
  return (pin == ENABLE_PIN_A) || (pin == ENABLE_PIN_B);
}

static void loadEnablePinsFromPrefs(int &leftPin, int &rightPin)
{
  leftPin  = prefs.getInt(PREF_KEY_LEFT_EN, ENABLE_PIN_A);
  rightPin = prefs.getInt(PREF_KEY_RIGHT_EN, ENABLE_PIN_B);

  if (!isAllowedEnablePin(leftPin))  leftPin = ENABLE_PIN_A;
  if (!isAllowedEnablePin(rightPin)) rightPin = ENABLE_PIN_B;
}

static void applyEnablePinsAndSave(int leftPin, int rightPin)
{
  if (!isAllowedEnablePin(leftPin) || !isAllowedEnablePin(rightPin)) {
    WebLog::warn("EnablePins rejected (invalid GPIO)");
    return;
  }

  prefs.putInt(PREF_KEY_LEFT_EN, leftPin);
  prefs.putInt(PREF_KEY_RIGHT_EN, rightPin);

  if (movement) movement->setEnablePins(leftPin, rightPin);

  WebLog::info(String("EnablePins applied & saved: L=") + leftPin + " R=" + rightPin);
}

static void ensureWifiOrAp()
{
  WiFi.mode(WIFI_STA);

  WiFiManager wifiManager;
  wifiManager.setConnectTimeout(20);
  wifiManager.setMenu(menu);
  wifiManager.setConfigPortalTimeout(180);

  const bool ok = wifiManager.autoConnect("maniac");

  if (ok && WiFi.status() == WL_CONNECTED) {
    WebLog::info(String("WiFi connected, IP=") + WiFi.localIP().toString());
    return;
  }

  WebLog::warn("WiFi not connected -> starting AP 'maniac'");
  WiFi.mode(WIFI_AP);
  WiFi.softAP("maniac");
  delay(100);
  WebLog::info(String("AP started, AP_IP=") + WiFi.softAPIP().toString());
}

static void notFound(AsyncWebServerRequest *request)
{
  request->send(404, "text/plain", "Not found");
}

static void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final)
{
  if (!phaseManager || !phaseManager->getCurrentPhase()) {
    request->send(503, "text/plain", "Phase not ready");
    return;
  }
  phaseManager->getCurrentPhase()->handleUpload(request, filename, index, data, len, final);
}

static void handleGetState(AsyncWebServerRequest *request)
{
  if (!phaseManager) {
    request->send(503, "text/plain", "PhaseManager not ready");
    return;
  }
  phaseManager->respondWithState(request);
}

static void registerPulseWidthEndpoints(AsyncWebServer* server)
{
  server->on("/pulseWidths", HTTP_GET, [](AsyncWebServerRequest* req) {
    StaticJsonDocument<128> doc;
    doc["leftUs"]  = movement ? movement->getLeftPulseWidthUs()  : 0;
    doc["rightUs"] = movement ? movement->getRightPulseWidthUs() : 0;
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json; charset=utf-8", out);
  });

  server->on("/setPulseWidths", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!req->hasParam("leftUs", true) || !req->hasParam("rightUs", true)) {
      req->send(400, "application/json", "{\"ok\":false,\"error\":\"Missing leftUs/rightUs\"}");
      return;
    }

    int l = req->getParam("leftUs", true)->value().toInt();
    int r = req->getParam("rightUs", true)->value().toInt();

    if (l < 1) l = 1;
    if (r < 1) r = 1;
    if (l > 2000) l = 2000;
    if (r > 2000) r = 2000;

    prefs.putInt(PREF_KEY_PULSE_L, l);
    prefs.putInt(PREF_KEY_PULSE_R, r);

    WebLog::info(String("Pulse widths saved: left=") + l + "us right=" + r + "us");

    StaticJsonDocument<128> doc;
    doc["ok"] = true;
    doc["leftUs"] = l;
    doc["rightUs"] = r;
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json; charset=utf-8", out);
  });
}

static void registerDiagnosticsEndpoints(AsyncWebServer* server)
{
  server->on("/diag", HTTP_GET, [](AsyncWebServerRequest* request) {
    StaticJsonDocument<2048> doc;

    doc["printSpeedSteps"] = printSpeedSteps;
    doc["moveSpeedSteps"]  = moveSpeedSteps;

    doc["pulseLeftUs"]  = movement ? movement->getLeftPulseWidthUs()  : 0;
    doc["pulseRightUs"] = movement ? movement->getRightPulseWidthUs() : 0;

    auto t = movement ? movement->getMotionTuning() : Movement::MotionTuning();
    doc["INFINITE_STEPS"] = (long)t.infiniteSteps;
    doc["acceleration"]   = (long)t.acceleration;

    doc["stepsPerRotation"] = (int)stepsPerRotation;

    doc["USE_GT2_PULLEY"]     = USE_GT2_PULLEY;
    doc["GT2_PITCH_MM"]       = (double)GT2_PITCH_MM;
    doc["GT2_TEETH"]          = (int)GT2_TEETH;
    doc["LEGACY_DIAMETER_MM"] = (double)LEGACY_DIAMETER_MM;

    const double travelPerRot = travelPerRotationMM();
    doc["travelPerRotationMM"] = (double)travelPerRot;

    doc["diameter"]      = (double)LEGACY_DIAMETER_MM;
    doc["circumference"] = (double)travelPerRot;

    doc["midPulleyToWall"]   = (double)midPulleyToWall;
    doc["homedStepOffsetMM"] = (double)homedStepOffsetMM;
    doc["homedStepsOffset"]  = (int)homedStepsOffsetSteps();

    doc["mass_bot"]   = (double)mass_bot;
    doc["g_constant"] = (double)g_constant;

    doc["d_t"] = (double)d_t;
    doc["d_p"] = (double)d_p;
    doc["d_m"] = (double)d_m;

    doc["belt_elongation_coefficient"] = (double)belt_elongation_coefficient;

    doc["HOME_Y_OFFSET_MM"] = (int)HOME_Y_OFFSET_MM;
    doc["safeYFraction"]    = (double)safeYFraction;
    doc["safeXFraction"]    = (double)safeXFraction;

    doc["LEFT_STEP_PIN"]   = (int)LEFT_STEP_PIN;
    doc["LEFT_DIR_PIN"]    = (int)LEFT_DIR_PIN;
    doc["RIGHT_STEP_PIN"]  = (int)RIGHT_STEP_PIN;
    doc["RIGHT_DIR_PIN"]   = (int)RIGHT_DIR_PIN;

    doc["perf_loop_ms"]     = (double)gPerf.loop_us_avg / 1000.0;
    doc["perf_yield_ms"]    = (double)gPerf.yield_us_avg / 1000.0;
    doc["perf_move_ms"]     = (double)gPerf.move_us_avg / 1000.0;
    doc["perf_runner_ms"]   = (double)gPerf.runner_us_avg / 1000.0;
    doc["perf_phase_ms"]    = (double)gPerf.phase_us_avg / 1000.0;
    doc["perf_max_loop_ms"] = (double)gPerf.max_loop_us / 1000.0;

    String out;
    serializeJson(doc, out);
    request->send(200, "application/json; charset=utf-8", out);
  });
}

static bool copyFileOnFs(fs::FS* fs, const String& from, const String& to)
{
  if (!fs) return false;
  File src = fs->open(from, "r");
  if (!src || src.isDirectory()) return false;

  File dst = fs->open(to, "w");
  if (!dst) { src.close(); return false; }

  uint8_t buf[1024];
  while (src.available()) {
    size_t n = src.read(buf, sizeof(buf));
    if (n == 0) break;
    if (dst.write(buf, n) != n) { src.close(); dst.close(); return false; }
  }

  src.close();
  dst.close();
  return true;
}



// ---------- Helpers for FileManager ----------

static bool removeRecursive(fs::FS* fs, const String& path)
{
  if (!fs) return false;
  if (!fs->exists(path)) return false;

  File node = fs->open(path);
  if (!node) return false;

  if (!node.isDirectory()) {
    node.close();
    return fs->remove(path);
  }

  File child = node.openNextFile();
  while (child) {
    String childPath = String(child.name());
    const bool isDir = child.isDirectory();
    child.close();

    bool ok = false;
    if (isDir) ok = removeRecursive(fs, childPath);
    else       ok = fs->remove(childPath);

    if (!ok) {
      node.close();
      return false;
    }

    child = node.openNextFile();
  }

  node.close();
  return fs->rmdir(path);
}

static bool copyFileOnFs(fs::FS* fs, const String& from, const String& to)
{
  if (!fs) return false;
  File src = fs->open(from, "r");
  if (!src || src.isDirectory()) return false;

  File dst = fs->open(to, "w");
  if (!dst) { src.close(); return false; }

  uint8_t buf[1024];
  while (src.available()) {
    size_t n = src.read(buf, sizeof(buf));
    if (n == 0) break;
    if (dst.write(buf, n) != n) {
      src.close();
      dst.close();
      return false;
    }
  }

  src.close();
  dst.close();
  return true;
}

// ---------- Complete FileManager registration ----------
static bool removeRecursive(fs::FS* fs, const String& path)
{
  if (!fs || path.isEmpty() || path == "/") return false;
  if (!fs->exists(path)) return false;

  File node = fs->open(path);
  if (!node) return false;

  if (!node.isDirectory()) {
    node.close();
    return fs->remove(path);
  }

  // Directory: delete children first
  File child = node.openNextFile();
  while (child) {
    String childPath = String(child.path());   // full path on ESP32 FS
    const bool isDir = child.isDirectory();
    child.close();

    bool ok = false;
    if (isDir) ok = removeRecursive(fs, childPath);
    else       ok = fs->remove(childPath);

    if (!ok) {
      node.close();
      return false;
    }
    child = node.openNextFile();
  }

  node.close();
  return fs->rmdir(path);
}

static bool ensureParentDirs(fs::FS* fs, const String& fullPath)
{
  if (!fs || fullPath.isEmpty() || fullPath[0] != '/') return false;
  int lastSlash = fullPath.lastIndexOf('/');
  if (lastSlash <= 0) return true; // root-level file

  String dir = fullPath.substring(0, lastSlash);
  if (dir.isEmpty()) return true;

  // Build path step-by-step: /a, /a/b, /a/b/c
  int pos = 1;
  while (pos <= dir.length()) {
    int slash = dir.indexOf('/', pos);
    String part = (slash < 0) ? dir.substring(0) : dir.substring(0, slash);
    if (part.length() > 0 && !fs->exists(part)) {
      if (!fs->mkdir(part)) return false;
    }
    if (slash < 0) break;
    pos = slash + 1;
  }
  return true;
}


static bool deleteRecursive(fs::FS* fs, const String& path)
{
  if (!fs) return false;
  if (!isSafePath(path) || path == "/") return false;
  if (!fs->exists(path)) return false;

  File node = fs->open(path);
  if (!node) return false;

  // File -> direct delete
  if (!node.isDirectory()) {
    node.close();
    return fs->remove(path);
  }

  // Directory -> delete children first
  File child = node.openNextFile();
  while (child) {
    String childPath = String(child.path());
    const bool childIsDir = child.isDirectory();
    child.close();

    bool ok = false;
    if (childIsDir) ok = deleteRecursive(fs, childPath);
    else ok = fs->remove(childPath);

    if (!ok) {
      node.close();
      return false;
    }
    child = node.openNextFile();
  }

  node.close();
  return fs->rmdir(path);
}



static void registerFileManagerEndpoints(AsyncWebServer* server)
{
  // Info
  server->on("/fs/info", HTTP_GET, [](AsyncWebServerRequest* req) {
    StaticJsonDocument<768> doc;

    const uint32_t lfs_total = (uint32_t)LittleFS.totalBytes();
    const uint32_t lfs_used  = (uint32_t)LittleFS.usedBytes();
    const uint32_t lfs_free  = (lfs_total >= lfs_used) ? (lfs_total - lfs_used) : 0;

    doc["littlefs"]["mounted"] = true;
    doc["littlefs"]["total"]   = lfs_total;
    doc["littlefs"]["used"]    = lfs_used;
    doc["littlefs"]["free"]    = lfs_free;

    const bool sd_ok = ensureSdMounted(false);
    SdGuard sdg(sd_ok);
    const uint32_t sd_total = (sd_ok && sdg.locked) ? (uint32_t)SD.totalBytes() : 0;
    const uint32_t sd_used  = (sd_ok && sdg.locked) ? (uint32_t)SD.usedBytes()  : 0;
    const uint32_t sd_free  = (sd_total >= sd_used) ? (sd_total - sd_used) : 0;

    doc["sd"]["mounted"] = (sd_ok && sdg.locked);
    doc["sd"]["cs"]      = (int)SD_CS_PIN;
    doc["sd"]["total"]   = sd_total;
    doc["sd"]["used"]    = sd_used;
    doc["sd"]["free"]    = sd_free;

    String out;
    serializeJson(doc, out);
    req->send(200, "application/json; charset=utf-8", out);
  });

  // SD remount
  server->on("/sd/remount", HTTP_POST, [](AsyncWebServerRequest* req) {
    const bool ok = ensureSdMounted(true);
    StaticJsonDocument<128> doc;
    doc["ok"] = ok;
    doc["mounted"] = ok;
    doc["cs"] = (int)SD_CS_PIN;
    String out;
    serializeJson(doc, out);
    req->send(ok ? 200 : 503, "application/json; charset=utf-8", out);
  });

  // List directory
  server->on("/fs/list", HTTP_GET, [](AsyncWebServerRequest* req) {
    const String vol  = req->hasParam("vol")  ? req->getParam("vol")->value()  : String("lfs");
    String path       = req->hasParam("path") ? req->getParam("path")->value() : String("/");
    path = normPath(path);

    const bool wantSd = (vol == "sd");
    if (wantSd && !ensureSdMounted(false)) {
      req->send(503, "application/json", "{\"error\":\"SD not mounted\"}");
      return;
    }

    SdGuard sdg(wantSd);
    if (wantSd && !sdg.locked) {
      req->send(503, "application/json", "{\"error\":\"SD busy\"}");
      return;
    }

    if (!isSafePath(path)) {
      req->send(400, "application/json", "{\"error\":\"Bad path\"}");
      return;
    }

    fs::FS* fs = pickFs(vol);
    if (!fs) {
      req->send(400, "application/json", "{\"error\":\"Volume not available\"}");
      return;
    }

    File dir = fs->open(path);
    if (!dir || !dir.isDirectory()) {
      req->send(404, "application/json", "{\"error\":\"Not a directory\"}");
      return;
    }

    StaticJsonDocument<8192> doc;
    doc["vol"]  = vol;
    doc["path"] = path;
    JsonArray arr = doc.createNestedArray("entries");

    File f = dir.openNextFile();
    while (f) {
      JsonObject o = arr.createNestedObject();
      o["name"] = String(f.name());
      o["dir"]  = (bool)f.isDirectory();
      o["size"] = (uint32_t)(f.isDirectory() ? 0 : f.size());
      f.close();
      f = dir.openNextFile();
    }

    String out;
    serializeJson(doc, out);
    req->send(200, "application/json; charset=utf-8", out);
  });

  // Read text file
  server->on("/fs/read", HTTP_GET, [](AsyncWebServerRequest* req) {
    const String vol  = req->hasParam("vol")  ? req->getParam("vol")->value()  : String("lfs");
    String path       = req->hasParam("path") ? req->getParam("path")->value() : String("");
    path = normPath(path);

    const bool wantSd = (vol == "sd");
    if (wantSd && !ensureSdMounted(false)) {
      req->send(503, "application/json", "{\"error\":\"SD not mounted\"}");
      return;
    }

    SdGuard sdg(wantSd);
    if (wantSd && !sdg.locked) {
      req->send(503, "application/json", "{\"error\":\"SD busy\"}");
      return;
    }

    if (!isSafePath(path) || path == "/") {
      req->send(400, "application/json", "{\"error\":\"Bad path\"}");
      return;
    }

    fs::FS* fs = pickFs(vol);
    if (!fs) {
      req->send(400, "application/json", "{\"error\":\"Volume not available\"}");
      return;
    }

    if (!fs->exists(path)) {
      req->send(404, "application/json", "{\"error\":\"Not found\"}");
      return;
    }

    req->send(*fs, path, "text/plain");
  });

  // Download file
  server->on("/fs/download", HTTP_GET, [](AsyncWebServerRequest* req) {
    const String vol  = req->hasParam("vol")  ? req->getParam("vol")->value()  : String("lfs");
    String path       = req->hasParam("path") ? req->getParam("path")->value() : String("");
    path = normPath(path);

    const bool wantSd = (vol == "sd");
    if (wantSd && !ensureSdMounted(false)) {
      req->send(503, "application/json", "{\"error\":\"SD not mounted\"}");
      return;
    }

    SdGuard sdg(wantSd);
    if (wantSd && !sdg.locked) {
      req->send(503, "application/json", "{\"error\":\"SD busy\"}");
      return;
    }

    if (!isSafePath(path) || path == "/") {
      req->send(400, "application/json", "{\"error\":\"Bad path\"}");
      return;
    }

    fs::FS* fs = pickFs(vol);
    if (!fs) {
      req->send(400, "application/json", "{\"error\":\"Volume not available\"}");
      return;
    }

    if (!fs->exists(path)) {
      req->send(404, "application/json", "{\"error\":\"Not found\"}");
      return;
    }

    req->send(*fs, path, "application/octet-stream", true);
  });

  // Delete (file OR directory recursive)
  server->on("/fs/delete", HTTP_POST, [](AsyncWebServerRequest* req) {
    const String vol  = req->hasParam("vol", true)  ? req->getParam("vol", true)->value()  : String("lfs");
    String path       = req->hasParam("path", true) ? req->getParam("path", true)->value() : String("/");
    path = normPath(path);

    const bool wantSd = (vol == "sd");
    if (wantSd && !ensureSdMounted(false)) {
      req->send(503, "application/json", "{\"ok\":false,\"error\":\"SD not mounted\"}");
      return;
    }

    SdGuard sdg(wantSd);
    if (wantSd && !sdg.locked) {
      req->send(503, "application/json", "{\"ok\":false,\"error\":\"SD busy\"}");
      return;
    }

    if (!isSafePath(path) || path == "/") {
      req->send(400, "application/json", "{\"ok\":false,\"error\":\"Bad path\"}");
      return;
    }

    fs::FS* fs = pickFs(vol);
    if (!fs) {
      req->send(400, "application/json", "{\"ok\":false,\"error\":\"Volume not available\"}");
      return;
    }

    if (!fs->exists(path)) {
      req->send(404, "application/json", "{\"ok\":false,\"error\":\"Not found\"}");
      return;
    }

    File f = fs->open(path);
    if (!f) {
      req->send(500, "application/json", "{\"ok\":false,\"error\":\"Open failed\"}");
      return;
    }

    const bool isDir = f.isDirectory();
    f.close();

    bool ok = false;
    if (isDir) {
      ok = deleteRecursive(fs, path);   // now works for non-empty folders
    } else {
      ok = fs->remove(path);
    }

    if (!ok) {
      WebLog::warn(String("Delete failed: vol=") + vol + " path=" + path);
      req->send(500, "application/json", "{\"ok\":false,\"error\":\"Delete failed\"}");
      return;
    }

    req->send(200, "application/json", "{\"ok\":true}");
  });


  // Mkdir
  server->on("/fs/mkdir", HTTP_POST, [](AsyncWebServerRequest* req) {
    const String vol  = req->hasParam("vol", true)  ? req->getParam("vol", true)->value()  : String("lfs");
    String path       = req->hasParam("path", true) ? req->getParam("path", true)->value() : String("/");
    path = normPath(path);

    const bool wantSd = (vol == "sd");
    if (wantSd && !ensureSdMounted(false)) {
      req->send(503, "application/json", "{\"error\":\"SD not mounted\"}");
      return;
    }

    SdGuard sdg(wantSd);
    if (wantSd && !sdg.locked) {
      req->send(503, "application/json", "{\"error\":\"SD busy\"}");
      return;
    }

    if (!isSafePath(path) || path == "/") {
      req->send(400, "application/json", "{\"error\":\"Bad path\"}");
      return;
    }

    fs::FS* fs = pickFs(vol);
    if (!fs) {
      req->send(400, "application/json", "{\"error\":\"Volume not available\"}");
      return;
    }

    const bool ok = fs->mkdir(path);
    req->send(ok ? 200 : 500, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
  });

  // Rename
  server->on("/fs/rename", HTTP_POST, [](AsyncWebServerRequest* req) {
    const String vol = req->hasParam("vol", true) ? req->getParam("vol", true)->value() : String("lfs");
    String from      = req->hasParam("from", true) ? req->getParam("from", true)->value() : String("");
    String to        = req->hasParam("to", true) ? req->getParam("to", true)->value() : String("");

    from = normPath(from);
    to   = normPath(to);

    if (!isSafePath(from) || !isSafePath(to) || from == "/" || to == "/") {
      req->send(400, "application/json", "{\"error\":\"Bad path\"}");
      return;
    }

    const bool wantSd = (vol == "sd");
    if (wantSd && !ensureSdMounted(false)) {
      req->send(503, "application/json", "{\"error\":\"SD not mounted\"}");
      return;
    }

    SdGuard sdg(wantSd);
    if (wantSd && !sdg.locked) {
      req->send(503, "application/json", "{\"error\":\"SD busy\"}");
      return;
    }

    fs::FS* fs = pickFs(vol);
    if (!fs) {
      req->send(400, "application/json", "{\"error\":\"Volume not available\"}");
      return;
    }

    const bool ok = fs->rename(from, to);
    req->send(ok ? 200 : 500, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
  });

  // Copy file
  server->on("/fs/copy", HTTP_POST, [](AsyncWebServerRequest* req) {
    const String vol = req->hasParam("vol", true) ? req->getParam("vol", true)->value() : String("lfs");
    String from      = req->hasParam("from", true) ? req->getParam("from", true)->value() : String("");
    String to        = req->hasParam("to", true) ? req->getParam("to", true)->value() : String("");

    from = normPath(from);
    to   = normPath(to);

    if (!isSafePath(from) || !isSafePath(to) || from == "/" || to == "/") {
      req->send(400, "application/json", "{\"error\":\"Bad path\"}");
      return;
    }

    const bool wantSd = (vol == "sd");
    if (wantSd && !ensureSdMounted(false)) {
      req->send(503, "application/json", "{\"error\":\"SD not mounted\"}");
      return;
    }

    SdGuard sdg(wantSd);
    if (wantSd && !sdg.locked) {
      req->send(503, "application/json", "{\"error\":\"SD busy\"}");
      return;
    }

    fs::FS* fs = pickFs(vol);
    if (!fs) {
      req->send(400, "application/json", "{\"error\":\"Volume not available\"}");
      return;
    }

    const bool ok = copyFileOnFs(fs, from, to);
    req->send(ok ? 200 : 500, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
  });

  // Move (rename)
  server->on("/fs/move", HTTP_POST, [](AsyncWebServerRequest* req) {
    const String vol = req->hasParam("vol", true) ? req->getParam("vol", true)->value() : String("lfs");
    String from      = req->hasParam("from", true) ? req->getParam("from", true)->value() : String("");
    String to        = req->hasParam("to", true) ? req->getParam("to", true)->value() : String("");

    from = normPath(from);
    to   = normPath(to);

    if (!isSafePath(from) || !isSafePath(to) || from == "/" || to == "/") {
      req->send(400, "application/json", "{\"error\":\"Bad path\"}");
      return;
    }

    const bool wantSd = (vol == "sd");
    if (wantSd && !ensureSdMounted(false)) {
      req->send(503, "application/json", "{\"error\":\"SD not mounted\"}");
      return;
    }

    SdGuard sdg(wantSd);
    if (wantSd && !sdg.locked) {
      req->send(503, "application/json", "{\"error\":\"SD busy\"}");
      return;
    }

    fs::FS* fs = pickFs(vol);
    if (!fs) {
      req->send(400, "application/json", "{\"error\":\"Volume not available\"}");
      return;
    }

    const bool ok = fs->rename(from, to);
    req->send(ok ? 200 : 500, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
  });

  // Upload file (chunked)
  server->on(
    "/fs/upload", HTTP_POST,
    [](AsyncWebServerRequest* req) {
      req->send(200, "application/json", "{\"ok\":true}");
    },
    [](AsyncWebServerRequest* req, String filename, size_t index, uint8_t* data, size_t len, bool final) {
      const String vol  = req->hasParam("vol", true)  ? req->getParam("vol", true)->value()  : String("lfs");
      String path       = req->hasParam("path", true) ? req->getParam("path", true)->value() : String("");
      path = normPath(path);

      const bool wantSd = (vol == "sd");
      if (wantSd && !ensureSdMounted(false)) return;

      SdGuard sdg(wantSd);
      if (wantSd && !sdg.locked) return;

      fs::FS* fs = pickFs(vol);
      if (!fs || !isSafePath(path) || path == "/") return;

      if (index == 0) {
        req->_tempFile = fs->open(path, "w");
      }

      if (req->_tempFile) {
        req->_tempFile.write(data, len);
      }

      if (final) {
        if (req->_tempFile) req->_tempFile.close();
      }
    }
  );
}

constexpr bool ENABLE_ACTIVE_LOW = true;
static bool gDrvLeftOn = false;
static bool gDrvRightOn = false;

static void registerDriverEnableEndpoints(AsyncWebServer* server)
{
  server->on("/gpio/driverEnable", HTTP_GET, [](AsyncWebServerRequest* req) {
    StaticJsonDocument<128> doc;
    doc["left"]  = gDrvLeftOn;
    doc["right"] = gDrvRightOn;
    String out; serializeJson(doc, out);
    req->send(200, "application/json; charset=utf-8", out);
  });

  server->on("/gpio/driverEnable", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!req->hasParam("left", true) || !req->hasParam("right", true)) {
      req->send(400, "application/json", "{\"error\":\"Missing left/right\"}");
      return;
    }

    const int l = req->getParam("left", true)->value().toInt();
    const int r = req->getParam("right", true)->value().toInt();

    gDrvLeftOn  = (l != 0);
    gDrvRightOn = (r != 0);

    StaticJsonDocument<128> doc;
    doc["left"]  = gDrvLeftOn;
    doc["right"] = gDrvRightOn;
    String out; serializeJson(doc, out);
    req->send(200, "application/json; charset=utf-8", out);
  });
}

void setup()
{
  esp_log_level_set("vfs_api", ESP_LOG_NONE);
  esp_log_level_set("vfs", ESP_LOG_NONE);

  delay(10);
  Serial.begin(115200);

  WebLog::begin();
  WebLog::log(LOG_INFO, "Firmware gestartet");

  delay(200);

  if (!LittleFS.begin(true)) {
    WebLog::error("LittleFS mount failed");
    return;
  }

  display = new Display();
  movement = new Movement(display);

  prefs.begin("maniac", false);

  int leftEnPin = ENABLE_PIN_A;
  int rightEnPin = ENABLE_PIN_B;
  loadEnablePinsFromPrefs(leftEnPin, rightEnPin);
  if (movement) movement->setEnablePins(leftEnPin, rightEnPin);

  int storedPulseL = prefs.getInt(PREF_KEY_PULSE_L, 2);
  int storedPulseR = prefs.getInt(PREF_KEY_PULSE_R, 2);
  int storedPenDown = prefs.getInt(PREF_KEY_PEN_DOWN, 80);
  int storedPenUp   = prefs.getInt(PREF_KEY_PEN_UP, PEN_START_POS);
  if (movement) movement->setPulseWidths(storedPulseL, storedPulseR);
  WebLog::info(String("Loaded pulse widths: left=") + storedPulseL + "us right=" + storedPulseR + "us");

  int storedPrint = prefs.getInt("printSpeed", 1200);
  int storedMove  = prefs.getInt("moveSpeed", 2000);
  movement->setSpeeds(storedPrint, storedMove);

  long storedInfSteps = prefs.getLong("infSteps", 999999999L);
  long storedAccel    = prefs.getLong("accel",    999999999L);
  movement->setMotionTuning(storedInfSteps, storedAccel);

  WebLog::log(LOG_INFO, "Loaded tuning: infSteps=" + String(storedInfSteps) + " accel=" + String(storedAccel));
  WebLog::log(LOG_INFO, "Loaded speeds: print=" + String(storedPrint) + " move=" + String(storedMove));

  Movement::PlannerConfig cfg = movement->getPlannerConfig();
  cfg.junctionDeviationMM = prefs.getDouble(PREF_KEY_JUNC_DEV, cfg.junctionDeviationMM);
  cfg.lookaheadSegments   = prefs.getInt(PREF_KEY_LOOKAHEAD, cfg.lookaheadSegments);
  cfg.minSegmentTimeMs    = prefs.getInt(PREF_KEY_MINSEGMS, cfg.minSegmentTimeMs);
  cfg.cornerSlowdown      = prefs.getDouble(PREF_KEY_CORNERSLOW, cfg.cornerSlowdown);
  cfg.minCornerFactor     = prefs.getDouble(PREF_KEY_MINCORNER, cfg.minCornerFactor);
  cfg.minSegmentLenMM     = prefs.getDouble(PREF_KEY_MINSEGLEN, cfg.minSegmentLenMM);
  cfg.collinearDeg        = prefs.getDouble(PREF_KEY_COLLINEAR, cfg.collinearDeg);
  cfg.backlashXmm         = prefs.getDouble(PREF_KEY_BACKLASHX, cfg.backlashXmm);
  cfg.backlashYmm         = prefs.getDouble(PREF_KEY_BACKLASHY, cfg.backlashYmm);
  cfg.sCurveFactor        = prefs.getDouble(PREF_KEY_SCURVE, cfg.sCurveFactor);
  movement->setPlannerConfig(cfg);

  WebLog::log(LOG_INFO, "Loaded planner: jd=" + String(cfg.junctionDeviationMM, 4) +
    " lookahead=" + String(cfg.lookaheadSegments) +
    " minSegMs=" + String(cfg.minSegmentTimeMs));

  ensureWifiOrAp();

  if (!MDNS.begin("maniac")) WebLog::warn("mDNS start failed");
  else WebLog::info("mDNS started: maniac.local");

#if defined(ESP32)
  setenv("TZ", "CET-1CEST,M3.5.0/2,M10.5.0/3", 1);
  tzset();
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  WebLog::info("NTP gestartet");
#endif

  pen = new Pen();
  if (pen) {
    pen->setDownAngle(storedPenDown);
    pen->setUpAngle(storedPenUp);
  }

  runner = new Runner(movement, pen, display);
  phaseManager = new PhaseManager(movement, pen, runner, &server);

  server.on("/command", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!phaseManager || !phaseManager->getCurrentPhase()) { request->send(503, "text/plain", "Phase not ready"); return; }
    phaseManager->getCurrentPhase()->handleCommand(request);
  });

  server.on("/setTopDistance", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!phaseManager || !phaseManager->getCurrentPhase()) { request->send(503, "text/plain", "Phase not ready"); return; }
    phaseManager->getCurrentPhase()->setTopDistance(request);
  });

  server.on("/getState", HTTP_GET, [](AsyncWebServerRequest *request) { handleGetState(request); });

  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    StaticJsonDocument<1536> doc;

    auto p = movement ? movement->getCoordinatesLive() : Movement::Point();
    doc["x"] = (double)p.x;
    doc["y"] = (double)p.y;

    const int prog = runner ? runner->getProgress() : 0;
    const bool running = runner ? !runner->isStopped() : false;
    const bool paused  = runner ? runner->isPaused()   : false;

    doc["progress"] = prog;
    doc["running"]  = running;
    doc["paused"]   = paused;

    doc["phaseName"]   = (phaseManager && phaseManager->getCurrentPhase()) ? phaseManager->getCurrentPhase()->getName() : "—";
    doc["printSteps"]  = (int)printSpeedSteps;
    doc["fwMaxLoopMs"] = (double)gPerf.max_loop_us / 1000.0;

    JsonObject penObj = doc.createNestedObject("pen");
    penObj["pos"]   = (pen && pen->isDown()) ? "DOWN" : "UP";
    penObj["angle"] = pen ? pen->currentAngle() : 0;

    penObj["downAngle"]      = pen ? pen->getDownAngle() : 0;
    penObj["upAngle"]        = pen ? pen->getUpAngle() : 0;
    penObj["pendingDown"]    = pen ? pen->getPendingDownAngle() : 0;
    penObj["pendingUp"]      = pen ? pen->getPendingUpAngle() : 0;
    penObj["hasPendingDown"] = pen ? pen->pendingDown() : false;
    penObj["hasPendingUp"]   = pen ? pen->pendingUp() : false;
    penObj["state"]          = penObj["pos"];

    JsonObject perf = doc.createNestedObject("perf");
    perf["loop_ms"]     = (double)gPerf.loop_us_avg / 1000.0;
    perf["yield_ms"]    = (double)gPerf.yield_us_avg / 1000.0;
    perf["move_ms"]     = (double)gPerf.move_us_avg / 1000.0;
    perf["runner_ms"]   = (double)gPerf.runner_us_avg / 1000.0;
    perf["phase_ms"]    = (double)gPerf.phase_us_avg / 1000.0;
    perf["max_loop_ms"] = (double)gPerf.max_loop_us / 1000.0;

    JsonObject plannerObj = doc["planner"].to<JsonObject>();
    auto pcfg = movement ? movement->getPlannerConfig() : Movement::PlannerConfig();
    plannerObj["junctionDeviation"] = pcfg.junctionDeviationMM;
    plannerObj["lookaheadSegments"] = pcfg.lookaheadSegments;
    plannerObj["minSegmentTimeMs"]  = pcfg.minSegmentTimeMs;
    plannerObj["cornerSlowdown"]    = pcfg.cornerSlowdown;
    plannerObj["minCornerFactor"]   = pcfg.minCornerFactor;
    plannerObj["minSegmentLenMM"]   = pcfg.minSegmentLenMM;
    plannerObj["collinearDeg"]      = pcfg.collinearDeg;
    plannerObj["backlashXmm"]       = pcfg.backlashXmm;
    plannerObj["backlashYmm"]       = pcfg.backlashYmm;
    plannerObj["sCurveFactor"]      = pcfg.sCurveFactor;

    String out;
    serializeJson(doc, out);
    request->send(200, "application/json; charset=utf-8", out);
  });

  server.on("/pauseJob", HTTP_POST, [](AsyncWebServerRequest *request){
    if (runner) runner->pauseJob();
    request->send(200, "text/plain", "OK");
  });

  server.on("/resumeJob", HTTP_POST, [](AsyncWebServerRequest *request){
    if (runner) runner->resumeJob();
    request->send(200, "text/plain", "OK");
  });

  server.on("/stopJob", HTTP_POST, [](AsyncWebServerRequest *request){
    if (runner) runner->abortAndGoHome();
    request->send(200, "text/plain", "OK");
  });

  server.on("/logs", HTTP_GET, [](AsyncWebServerRequest *req){
    if (req->hasParam("after")) {
      uint32_t after = req->getParam("after")->value().toInt();
      req->send(200, "application/json; charset=utf-8", WebLog::toJsonAfter(after));
    } else {
      req->send(200, "application/json; charset=utf-8", WebLog::toJson());
    }
  });

  server.on("/logs/clear", HTTP_POST, [](AsyncWebServerRequest *req){
    WebLog::clear();
    req->send(200, "application/json", "{\"ok\":true}");
  });

  registerDiagnosticsEndpoints(&server);
  registerFileManagerEndpoints(&server);
  registerDriverEnableEndpoints(&server);
  registerPulseWidthEndpoints(&server);

  server.on("/setSpeeds", HTTP_POST, [](AsyncWebServerRequest *request){
    if (!request->hasParam("printSpeed", true) || !request->hasParam("moveSpeed", true)) {
      request->send(400, "text/plain", "Missing parameters");
      return;
    }

    int printSpeed = request->getParam("printSpeed", true)->value().toInt();
    int moveSpeed  = request->getParam("moveSpeed", true)->value().toInt();

    if (printSpeed <= 0 || moveSpeed <= 0) {
      request->send(400, "text/plain", "Invalid values");
      return;
    }

    movement->setSpeeds(printSpeed, moveSpeed);

    prefs.putInt("printSpeed", printSpeed);
    prefs.putInt("moveSpeed", moveSpeed);

    WebLog::info(String("Speeds updated & saved: print=") + printSpeed + " move=" + moveSpeed);
    request->send(200, "text/plain", "OK");
  });

  server.on("/motionTuning", HTTP_GET, [](AsyncWebServerRequest* request) {
    auto t = movement ? movement->getMotionTuning() : Movement::MotionTuning();
    String json = "{";
    json += "\"infiniteSteps\":" + String(t.infiniteSteps) + ",";
    json += "\"acceleration\":"  + String(t.acceleration);
    json += "}";
    request->send(200, "application/json; charset=utf-8", json);
  });

  server.on("/setMotionTuning", HTTP_POST, [](AsyncWebServerRequest* request){
    if (!request->hasParam("infiniteSteps", true) || !request->hasParam("acceleration", true)) {
      request->send(400, "text/plain", "Missing parameters");
      return;
    }

    long inf = request->getParam("infiniteSteps", true)->value().toInt();
    long acc = request->getParam("acceleration", true)->value().toInt();

    if (inf <= 0 || acc <= 0) {
      request->send(400, "text/plain", "Invalid values");
      return;
    }

    movement->setMotionTuning(inf, acc);

    auto t = movement->getMotionTuning();
    prefs.putLong("infSteps", t.infiniteSteps);
    prefs.putLong("accel", t.acceleration);

    WebLog::info(String("Tuning updated & saved: infSteps=") + t.infiniteSteps + " accel=" + t.acceleration);
    request->send(200, "text/plain", "OK");
  });

  server.on("/setPlannerConfig", HTTP_POST, [](AsyncWebServerRequest* request){
    auto cfg = movement->getPlannerConfig();

    if (request->hasParam("junctionDeviation", true)) cfg.junctionDeviationMM = request->getParam("junctionDeviation", true)->value().toDouble();
    if (request->hasParam("lookaheadSegments", true)) cfg.lookaheadSegments = request->getParam("lookaheadSegments", true)->value().toInt();
    if (request->hasParam("minSegmentTimeMs", true)) cfg.minSegmentTimeMs = request->getParam("minSegmentTimeMs", true)->value().toInt();
    if (request->hasParam("cornerSlowdown", true)) cfg.cornerSlowdown = request->getParam("cornerSlowdown", true)->value().toDouble();
    if (request->hasParam("minCornerFactor", true)) cfg.minCornerFactor = request->getParam("minCornerFactor", true)->value().toDouble();
    if (request->hasParam("minSegmentLenMM", true)) cfg.minSegmentLenMM = request->getParam("minSegmentLenMM", true)->value().toDouble();
    if (request->hasParam("collinearDeg", true)) cfg.collinearDeg = request->getParam("collinearDeg", true)->value().toDouble();
    if (request->hasParam("backlashXmm", true)) cfg.backlashXmm = request->getParam("backlashXmm", true)->value().toDouble();
    if (request->hasParam("backlashYmm", true)) cfg.backlashYmm = request->getParam("backlashYmm", true)->value().toDouble();
    if (request->hasParam("sCurveFactor", true)) cfg.sCurveFactor = request->getParam("sCurveFactor", true)->value().toDouble();

    movement->setPlannerConfig(cfg);

    prefs.putDouble(PREF_KEY_JUNC_DEV, cfg.junctionDeviationMM);
    prefs.putInt(PREF_KEY_LOOKAHEAD, cfg.lookaheadSegments);
    prefs.putInt(PREF_KEY_MINSEGMS, cfg.minSegmentTimeMs);
    prefs.putDouble(PREF_KEY_CORNERSLOW, cfg.cornerSlowdown);
    prefs.putDouble(PREF_KEY_MINCORNER, cfg.minCornerFactor);
    prefs.putDouble(PREF_KEY_MINSEGLEN, cfg.minSegmentLenMM);
    prefs.putDouble(PREF_KEY_COLLINEAR, cfg.collinearDeg);
    prefs.putDouble(PREF_KEY_BACKLASHX, cfg.backlashXmm);
    prefs.putDouble(PREF_KEY_BACKLASHY, cfg.backlashYmm);
    prefs.putDouble(PREF_KEY_SCURVE, cfg.sCurveFactor);

    request->send(200, "text/plain", "OK");
  });

  server.on("/sysinfo", HTTP_GET, [](AsyncWebServerRequest *req) {
    const size_t total = LittleFS.totalBytes();
    const size_t used  = LittleFS.usedBytes();
    const size_t freeB = (total >= used) ? (total - used) : 0;

    const bool sd_ok = ensureSdMounted(false);
    SdGuard sdg(sd_ok);
    const size_t sd_total = (sd_ok && sdg.locked) ? (size_t)SD.totalBytes() : 0;
    const size_t sd_used  = (sd_ok && sdg.locked) ? (size_t)SD.usedBytes()  : 0;
    const size_t sd_free  = (sd_total >= sd_used) ? (sd_total - sd_used) : 0;

    const int rssi = WiFi.isConnected() ? WiFi.RSSI() : -127;
    const String ip = WiFi.isConnected() ? WiFi.localIP().toString() : String("0.0.0.0");
    const char* host = WiFi.getHostname();
    const String hostname = host ? String(host) : String("maniac");

    const int cpuMhz = getCpuFrequencyMhz();
    const String board = String("ESP32");

    const int resetReason = (int)esp_reset_reason();
    const uint32_t uptimeS = (uint32_t)(millis() / 1000);

    const String largest = findLargestFileNameLittleFS();
    const String firmware = String("v1.3");

    String json = "{";
    json += "\"firmware\":\"" + firmware + "\",";
    json += "\"build\":\"" __DATE__ " " __TIME__ "\",";
    json += "\"reset_reason\":" + String(resetReason) + ",";
    json += "\"uptime_s\":" + String(uptimeS) + ",";
    json += "\"fs_total\":" + String(total) + ",";
    json += "\"fs_used\":" + String(used) + ",";
    json += "\"fs_free\":" + String(freeB) + ",";
    json += "\"fs_largest\":\"" + largest + "\",";
    json += "\"sd_mounted\":" + String((sd_ok && sdg.locked) ? "true" : "false") + ",";
    json += "\"sd_cs\":" + String((int)SD_CS_PIN) + ",";
    json += "\"sd_total\":" + String(sd_total) + ",";
    json += "\"sd_used\":"  + String(sd_used)  + ",";
    json += "\"sd_free\":"  + String(sd_free)  + ",";
    json += "\"heap\":" + String(ESP.getFreeHeap()) + ",";
    json += "\"min_heap\":" + String(ESP.getMinFreeHeap()) + ",";
    json += "\"rssi\":" + String(rssi) + ",";
    json += "\"ip\":\"" + ip + "\",";
    json += "\"host\":\"" + hostname + "\",";
    json += "\"cpu_mhz\":" + String(cpuMhz) + ",";
    json += "\"board\":\"" + board + "\",";
    json += "\"pulse_left_us\":" + String(movement ? movement->getLeftPulseWidthUs() : 0) + ",";
    json += "\"pulse_right_us\":" + String(movement ? movement->getRightPulseWidthUs() : 0) + ",";
    json += "\"mode\":\"unknown\",";
    json += "\"job\":0";
    json += "}";

    req->send(200, "application/json; charset=utf-8", json);
  });

  server.on("/reboot", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send(200, "text/plain", "reboot");
    delay(200);
    ESP.restart();
  });

  // Phase routes (guarded)
  server.on("/extendToHome", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!phaseManager || !phaseManager->getCurrentPhase()) { request->send(503, "text/plain", "Phase not ready"); return; }
    phaseManager->getCurrentPhase()->extendToHome(request);
  });

  server.on("/setServo", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!phaseManager || !phaseManager->getCurrentPhase()) { request->send(503, "text/plain", "Phase not ready"); return; }
    phaseManager->getCurrentPhase()->setServo(request);
  });

  server.on("/setPenDistance", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!phaseManager || !phaseManager->getCurrentPhase()) { request->send(503, "text/plain", "Phase not ready"); return; }
    phaseManager->getCurrentPhase()->setPenDistance(request);
  });

  // Staged pen tuning (0..80). Values are applied on next transition only.
  server.on("/pen/down/set", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!pen) { request->send(500, "text/plain", "pen not ready"); return; }
    if (!request->hasParam("value", true)) { request->send(400, "text/plain", "missing value"); return; }
    int v = request->getParam("value", true)->value().toInt();
    v = constrain(v, 0, 80);
    pen->setPendingDownAngle(v);
    prefs.putInt(PREF_KEY_PEN_DOWN, v);
    request->send(200, "application/json", String("{\"ok\":true,\"pendingDown\":") + v + "}");
  });

  server.on("/pen/up/set", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!pen) { request->send(500, "text/plain", "pen not ready"); return; }
    if (!request->hasParam("value", true)) { request->send(400, "text/plain", "missing value"); return; }
    int v = request->getParam("value", true)->value().toInt();
    v = constrain(v, 0, 80);
    pen->setPendingUpAngle(v);
    prefs.putInt(PREF_KEY_PEN_UP, v);
    request->send(200, "application/json", String("{\"ok\":true,\"pendingUp\":") + v + "}");
  });

  server.on("/estepsCalibration", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!phaseManager || !phaseManager->getCurrentPhase()) { request->send(503, "text/plain", "Phase not ready"); return; }
    phaseManager->getCurrentPhase()->estepsCalibration(request);
  });

  server.on("/doneWithPhase", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!phaseManager || !phaseManager->getCurrentPhase()) { request->send(503, "text/plain", "Phase not ready"); return; }
    phaseManager->getCurrentPhase()->doneWithPhase(request);
  });

  server.on("/run", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!phaseManager || !phaseManager->getCurrentPhase()) { request->send(503, "text/plain", "Phase not ready"); return; }
    phaseManager->getCurrentPhase()->run(request);
  });

  server.on("/resume", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!phaseManager || !phaseManager->getCurrentPhase()) { request->send(503, "text/plain", "Phase not ready"); return; }
    phaseManager->getCurrentPhase()->resumeTopDistance(request);
  });

  server.on("/uploadCommands", HTTP_POST,
    [](AsyncWebServerRequest *request) { handleGetState(request); },
    handleUpload
  );

  server.on("/downloadCommands", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/commands", "text/plain");
  });

  AsyncStaticWebHandler &h = server.serveStatic("/", LittleFS, "/www/");
  h.setDefaultFile("index.html");

  server.onNotFound(notFound);

  WebLog::info("HTTP server starting...");
  server.begin();
  WebLog::info("HTTP server started.");

  const String staIp = WiFi.localIP().toString();
  const String apIp  = WiFi.softAPIP().toString();
  const bool staOk = (WiFi.status() == WL_CONNECTED) && (staIp != "0.0.0.0");

  display->displayHomeScreen(
    String("http://") + (staOk ? staIp : apIp),
    "or",
    "http://maniac.local"
  );

  WebLog::info(String("Webserver started: ") + (staOk ? staIp : apIp));
}

void loop()
{
  if (!movement || !runner || !phaseManager) return;

  const uint32_t t0 = micros();

  delay(0);
  const uint32_t t1 = micros();

  movement->runSteppers();
  const uint32_t t2 = micros();

  runner->run();
  const uint32_t t3 = micros();

  if (phaseManager->getCurrentPhase()) {
    phaseManager->getCurrentPhase()->loopPhase();
  }
  const uint32_t t4 = micros();

  gPerf.update(
    (uint32_t)(t4 - t0),
    (uint32_t)(t1 - t0),
    (uint32_t)(t2 - t1),
    (uint32_t)(t3 - t2),
    (uint32_t)(t4 - t3)
  );
}
