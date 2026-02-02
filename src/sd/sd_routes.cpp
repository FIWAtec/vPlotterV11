#include "sd_routes.h"

static String joinPath(const String& a, const String& b) {
  if (a.endsWith("/")) return a + b;
  if (a == "/") return "/" + b;
  return a + "/" + b;
}

static void sendJson(AsyncWebServerRequest* req, int code, const String& json) {
  AsyncWebServerResponse* res = req->beginResponse(code, "application/json; charset=utf-8", json);
  res->addHeader("Cache-Control", "no-store");
  req->send(res);
}

void SdRoutes::registerRoutes(AsyncWebServer& server, SdManager& sd) {
  // LIST: GET /sd/list?path=/
  server.on("/sd/list", HTTP_GET, [&sd](AsyncWebServerRequest* req){
    String path = req->hasParam("path") ? req->getParam("path")->value() : "/";
    sendJson(req, 200, sd.listJson(path));
  });

  // INFO: GET /sd/info
  server.on("/sd/info", HTTP_GET, [&sd](AsyncWebServerRequest* req){
    sendJson(req, 200, sd.infoJson());
  });

  // MKDIR: POST /sd/mkdir  (form: path=/foo/newdir)
  server.on("/sd/mkdir", HTTP_POST, [&sd](AsyncWebServerRequest* req){
    if (!req->hasParam("path", true)) return sendJson(req, 400, "{\"ok\":false,\"error\":\"missing_path\"}");
    String path = req->getParam("path", true)->value();
    bool ok = sd.mkdirs(path);
    sendJson(req, ok ? 200 : 500, ok ? "{\"ok\":true}" : "{\"ok\":false}");
  });

  // DELETE FILE: POST /sd/delete (form: path=/foo/a.txt)
  server.on("/sd/delete", HTTP_POST, [&sd](AsyncWebServerRequest* req){
    if (!req->hasParam("path", true)) return sendJson(req, 400, "{\"ok\":false,\"error\":\"missing_path\"}");
    String path = req->getParam("path", true)->value();
    bool ok = sd.removeFile(path);
    sendJson(req, ok ? 200 : 500, ok ? "{\"ok\":true}" : "{\"ok\":false}");
  });

  // DELETE DIR RECURSIVE: POST /sd/rmdir (form: path=/foo/bar)
  server.on("/sd/rmdir", HTTP_POST, [&sd](AsyncWebServerRequest* req){
    if (!req->hasParam("path", true)) return sendJson(req, 400, "{\"ok\":false,\"error\":\"missing_path\"}");
    String path = req->getParam("path", true)->value();
    bool ok = sd.removeDirRecursive(path);
    sendJson(req, ok ? 200 : 500, ok ? "{\"ok\":true}" : "{\"ok\":false}");
  });

  // RENAME/MOVE: POST /sd/rename (form: from=/a.txt to=/b.txt)
  server.on("/sd/rename", HTTP_POST, [&sd](AsyncWebServerRequest* req){
    if (!req->hasParam("from", true) || !req->hasParam("to", true))
      return sendJson(req, 400, "{\"ok\":false,\"error\":\"missing_from_to\"}");
    String from = req->getParam("from", true)->value();
    String to   = req->getParam("to", true)->value();
    bool ok = sd.renamePath(from, to);
    sendJson(req, ok ? 200 : 500, ok ? "{\"ok\":true}" : "{\"ok\":false}");
  });

  // DOWNLOAD: GET /sd/download?path=/foo/a.txt
  server.on("/sd/download", HTTP_GET, [&sd](AsyncWebServerRequest* req){
    if (!req->hasParam("path")) return sendJson(req, 400, "{\"ok\":false,\"error\":\"missing_path\"}");
    String path = req->getParam("path")->value();
    if (!sd.exists(path)) return sendJson(req, 404, "{\"ok\":false,\"error\":\"not_found\"}");

    // stream file
    AsyncWebServerResponse* res = req->beginResponse(sd.fs(), path, "application/octet-stream", true);
    res->addHeader("Content-Disposition", "attachment");
    res->addHeader("Cache-Control", "no-store");
    req->send(res);
  });

  // UPLOAD: POST /sd/upload?dir=/foo   multipart file field name "file"
  // Example: formData.append("file", fileObj)
  server.on("/sd/upload", HTTP_POST,
    [](AsyncWebServerRequest* req){ 
      // request handler runs after upload finished; answer is sent here.
      sendJson(req, 200, "{\"ok\":true}");
    },
    [&sd](AsyncWebServerRequest* req, String filename, size_t index, uint8_t *data, size_t len, bool final){
      static File uploadFile;
      static String uploadPath;

      if (index == 0) {
        String dir = req->hasParam("dir") ? req->getParam("dir")->value() : "/";
        if (dir.length() == 0) dir = "/";
        sd.mkdirs(dir);

        uploadPath = joinPath(dir, filename);
        uploadFile = sd.fs().open(uploadPath, FILE_WRITE);
      }

      if (uploadFile) uploadFile.write(data, len);

      if (final) {
        if (uploadFile) uploadFile.close();
      }
    }
  );
}
