#include "fs_api.h"
#include <ArduinoJson.h>

// ------------------------------------------------------------
// Helpers: Rekursives Löschen (Datei + Ordner)
// ------------------------------------------------------------
static bool fs_isDir(FS& fs, const String& path) {
  File f = fs.open(path);
  if (!f) return false;
  bool isDir = f.isDirectory();
  f.close();
  return isDir;
}

static bool fs_rmdir_safe(FS& fs, const String& path) {
  bool ok = false;
  #if defined(ARDUINO_ARCH_ESP32)
    ok = fs.rmdir(path);
  #endif
  if (!ok) ok = fs.remove(path);
  return ok;
}

static bool fs_delete_recursive(FS& fs, const String& path) {
  File dir = fs.open(path);
  if (!dir) return false;

  if (!dir.isDirectory()) {
    dir.close();
    return fs.remove(path);
  }

  File child = dir.openNextFile();
  while (child) {
    const bool isDir = child.isDirectory();
    const String childPath = String(child.name());
    child.close();

    bool ok = isDir ? fs_delete_recursive(fs, childPath) : fs.remove(childPath);
    if (!ok) { dir.close(); return false; }

    child = dir.openNextFile();
  }
  dir.close();

  return fs_rmdir_safe(fs, path);
}

FsApi::FsApi(AsyncWebServer& server, SdCardService& sd)
: _server(server), _sd(sd) {}

void FsApi::registerRoutes() {
  _server.on("/fs/info", HTTP_GET, [this](AsyncWebServerRequest* req){ handleInfo(req); });
  _server.on("/fs/list", HTTP_GET, [this](AsyncWebServerRequest* req){ handleList(req); });
  _server.on("/fs/read", HTTP_GET, [this](AsyncWebServerRequest* req){ handleRead(req); });
  _server.on("/fs/download", HTTP_GET, [this](AsyncWebServerRequest* req){ handleDownload(req); });

  _server.on("/fs/delete", HTTP_POST, [this](AsyncWebServerRequest* req){ handleDelete(req); });
  _server.on("/fs/mkdir", HTTP_POST, [this](AsyncWebServerRequest* req){ handleMkdir(req); });
  _server.on("/fs/rename", HTTP_POST, [this](AsyncWebServerRequest* req){ handleRename(req); });
  _server.on("/fs/copy", HTTP_POST, [this](AsyncWebServerRequest* req){ handleCopy(req); });
  _server.on("/fs/move", HTTP_POST, [this](AsyncWebServerRequest* req){ handleMove(req); });

  _server.on(
    "/fs/upload",
    HTTP_POST,
    [](AsyncWebServerRequest* req){ req->send(200, "application/json", "{\"ok\":true}"); },
    [this](AsyncWebServerRequest* req, String filename, size_t index, uint8_t* data, size_t len, bool final){
      handleUpload(req, filename, index, data, len, final);
    }
  );
}

FS* FsApi::resolveFs(const String& target) {
  if (target == "sd") return _sd.fs();
  return &LittleFS;
}

String FsApi::normPath(String p) {
  p.trim();
  if (p.isEmpty()) p = "/";
  if (!p.startsWith("/")) p = "/" + p;

  while (p.indexOf("..") >= 0) p.replace("..", "");
  while (p.indexOf("//") >= 0) p.replace("//", "/");
  return p;
}

bool FsApi::isSafePath(const String& p) {
  return p.startsWith("/") && p.indexOf("..") < 0;
}

void FsApi::handleInfo(AsyncWebServerRequest* req) {
  StaticJsonDocument<512> doc;

  auto sd = _sd.info();
  doc["sd"]["mounted"] = sd.mounted;
  doc["sd"]["total"] = (uint64_t)sd.totalBytes;
  doc["sd"]["used"]  = (uint64_t)sd.usedBytes;
  doc["sd"]["free"]  = (uint64_t)sd.freeBytes;
  doc["sd"]["error"] = sd.error;

  doc["littlefs"]["total"] = (uint64_t)LittleFS.totalBytes();
  doc["littlefs"]["used"]  = (uint64_t)LittleFS.usedBytes();

  String out;
  serializeJson(doc, out);
  req->send(200, "application/json", out);
}

void FsApi::handleList(AsyncWebServerRequest* req) {
  String target = req->hasParam("target") ? req->getParam("target")->value() : "littlefs";
  String path   = req->hasParam("path") ? req->getParam("path")->value() : "/";

  FS* fs = resolveFs(target);
  if (!fs) { req->send(400, "application/json", "{\"ok\":false,\"error\":\"FS not available\"}"); return; }

  path = normPath(path);
  if (!isSafePath(path)) { req->send(400, "application/json", "{\"ok\":false,\"error\":\"Bad path\"}"); return; }

  File dir = fs->open(path);
  if (!dir || !dir.isDirectory()) {
    req->send(404, "application/json", "{\"ok\":false,\"error\":\"Not a directory\"}");
    return;
  }

  StaticJsonDocument<4096> doc;
  doc["ok"] = true;
  doc["target"] = target;
  doc["path"] = path;
  JsonArray items = doc.createNestedArray("items");

  File f = dir.openNextFile();
  while (f) {
    JsonObject it = items.createNestedObject();
    it["name"]  = String(f.name());
    it["isDir"] = f.isDirectory();
    it["size"]  = (uint64_t)f.size();
    f = dir.openNextFile();
  }

  String out;
  serializeJson(doc, out);
  req->send(200, "application/json", out);
}

void FsApi::handleRead(AsyncWebServerRequest* req) {
  String target = req->hasParam("target") ? req->getParam("target")->value() : "littlefs";
  String path   = req->hasParam("path") ? req->getParam("path")->value() : "";

  FS* fs = resolveFs(target);
  if (!fs) { req->send(400, "application/json", "{\"ok\":false,\"error\":\"FS not available\"}"); return; }

  path = normPath(path);
  if (!isSafePath(path) || path == "/") { req->send(400, "application/json", "{\"ok\":false,\"error\":\"Bad path\"}"); return; }

  File f = fs->open(path, "r");
  if (!f || f.isDirectory()) { req->send(404, "application/json", "{\"ok\":false,\"error\":\"Not found\"}"); return; }

  const size_t maxLen = 64 * 1024;
  String content;
  content.reserve(min((size_t)f.size(), maxLen));

  while (f.available() && content.length() < maxLen) content += (char)f.read();

  StaticJsonDocument<1024> doc;
  doc["ok"] = true;
  doc["path"] = path;
  doc["truncated"] = (f.available() > 0);

  StaticJsonDocument<65536> doc2;
  doc2["ok"] = true;
  doc2["path"] = path;
  doc2["truncated"] = (f.available() > 0);
  doc2["content"] = content;

  String out;
  serializeJson(doc2, out);
  req->send(200, "application/json", out);
}

void FsApi::handleDownload(AsyncWebServerRequest* req) {
  String target = req->hasParam("target") ? req->getParam("target")->value() : "littlefs";
  String path   = req->hasParam("path") ? req->getParam("path")->value() : "";

  FS* fs = resolveFs(target);
  if (!fs) { req->send(400, "application/json", "{\"ok\":false,\"error\":\"FS not available\"}"); return; }

  path = normPath(path);
  if (!isSafePath(path) || path == "/") { req->send(400, "application/json", "{\"ok\":false,\"error\":\"Bad path\"}"); return; }
  if (!fs->exists(path)) { req->send(404, "application/json", "{\"ok\":false,\"error\":\"Not found\"}"); return; }

  req->send(*fs, path, "application/octet-stream", true);
}

void FsApi::handleDelete(AsyncWebServerRequest* req) {
  String target = req->hasParam("target", true) ? req->getParam("target", true)->value()
                : (req->hasParam("vol", true) ? req->getParam("vol", true)->value() : "littlefs");
  String path   = req->hasParam("path", true) ? req->getParam("path", true)->value() : "";
  String recStr = req->hasParam("recursive", true) ? req->getParam("recursive", true)->value() : "0";
  bool recursive = (recStr == "1" || recStr == "true" || recStr == "yes" || recStr == "on");

  // Kompatibilität: UI nutzt "lfs" / "sd", Firmware "littlefs" / "sd"
  if (target == "lfs") target = "littlefs";

  FS* fs = resolveFs(target);
  if (!fs) { req->send(400, "application/json", "{\"ok\":false,\"error\":\"FS not available\"}"); return; }

  path = normPath(path);
  if (!isSafePath(path) || path == "/") { req->send(400, "application/json", "{\"ok\":false,\"error\":\"Bad path\"}"); return; }

  if (!fs->exists(path)) { req->send(404, "application/json", "{\"ok\":false,\"error\":\"Not found\"}"); return; }

  const bool isDir = fs_isDir(*fs, path);
  if (isDir && !recursive) {
    req->send(400, "application/json", "{\"ok\":false,\"error\":\"Directory. Use recursive=1\"}");
    return;
  }

  bool ok = isDir ? fs_delete_recursive(*fs, path) : fs->remove(path);
  req->send(ok ? 200 : 500, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"delete failed\"}");
}

void FsApi::handleMkdir(AsyncWebServerRequest* req) {
  String target = req->hasParam("target", true) ? req->getParam("target", true)->value() : "littlefs";
  String path   = req->hasParam("path", true) ? req->getParam("path", true)->value() : "";

  FS* fs = resolveFs(target);
  if (!fs) { req->send(400, "application/json", "{\"ok\":false,\"error\":\"FS not available\"}"); return; }

  path = normPath(path);
  if (!isSafePath(path) || path == "/") { req->send(400, "application/json", "{\"ok\":false,\"error\":\"Bad path\"}"); return; }

  bool ok = fs->mkdir(path);
  req->send(ok ? 200 : 500, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"mkdir failed\"}");
}

void FsApi::handleRename(AsyncWebServerRequest* req) {
  String target = req->hasParam("target", true) ? req->getParam("target", true)->value() : "littlefs";
  String from   = req->hasParam("from", true) ? req->getParam("from", true)->value() : "";
  String to     = req->hasParam("to", true) ? req->getParam("to", true)->value() : "";

  FS* fs = resolveFs(target);
  if (!fs) { req->send(400, "application/json", "{\"ok\":false,\"error\":\"FS not available\"}"); return; }

  from = normPath(from);
  to   = normPath(to);
  if (!isSafePath(from) || !isSafePath(to) || from == "/" || to == "/") {
    req->send(400, "application/json", "{\"ok\":false,\"error\":\"Bad path\"}");
    return;
  }

  bool ok = fs->rename(from, to);
  req->send(ok ? 200 : 500, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"rename failed\"}");
}

bool FsApi::copyFile(FS& fs, const String& from, const String& to) {
  File src = fs.open(from, "r");
  if (!src || src.isDirectory()) return false;

  File dst = fs.open(to, "w");
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

void FsApi::handleCopy(AsyncWebServerRequest* req) {
  String target = req->hasParam("target", true) ? req->getParam("target", true)->value() : "littlefs";
  String from   = req->hasParam("from", true) ? req->getParam("from", true)->value() : "";
  String to     = req->hasParam("to", true) ? req->getParam("to", true)->value() : "";

  FS* fs = resolveFs(target);
  if (!fs) { req->send(400, "application/json", "{\"ok\":false,\"error\":\"FS not available\"}"); return; }

  from = normPath(from);
  to   = normPath(to);

  if (!isSafePath(from) || !isSafePath(to) || from == "/" || to == "/") {
    req->send(400, "application/json", "{\"ok\":false,\"error\":\"Bad path\"}");
    return;
  }

  if (!fs->exists(from)) { req->send(404, "application/json", "{\"ok\":false,\"error\":\"Source not found\"}"); return; }

  bool ok = copyFile(*fs, from, to);
  req->send(ok ? 200 : 500, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"copy failed\"}");
}

void FsApi::handleMove(AsyncWebServerRequest* req) {
  handleRename(req);
}

void FsApi::handleUpload(AsyncWebServerRequest* req, String filename, size_t index, uint8_t* data, size_t len, bool final) {
  String target = req->hasParam("target", true) ? req->getParam("target", true)->value() : "littlefs";
  String dir    = req->hasParam("dir", true) ? req->getParam("dir", true)->value() : "/";

  FS* fs = resolveFs(target);
  if (!fs) return;

  dir = normPath(dir);
  if (!isSafePath(dir)) return;

  String full = dir;
  if (!full.endsWith("/")) full += "/";
  full += filename;
  full = normPath(full);
  if (!isSafePath(full)) return;

  static File uploadFile;

  if (index == 0) {
    uploadFile = fs->open(full, "w");
  }
  if (uploadFile) {
    uploadFile.write(data, len);
  }
  if (final) {
    if (uploadFile) uploadFile.close();
  }
}
