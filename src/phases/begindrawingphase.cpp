#include "begindrawingphase.h"
#include "LittleFS.h"
#include "service/weblog.h"

BeginDrawingPhase::BeginDrawingPhase(PhaseManager* manager, Runner* runner, AsyncWebServer* server) {
    this->manager = manager;
    this->runner = runner;
    this->server = server;
}

// Batch-Mode / Multi-Upload Support:
// In "BeginDrawing" darf ein neues /commands hochgeladen werden, damit mehrere Bilder
// nacheinander gezeichnet werden koennen, ohne den kompletten Kalibrier-Flow neu zu starten.
// Sicherheit: Upload ist nur erlaubt, wenn der Runner NICHT laeuft.
void BeginDrawingPhase::handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final)
{
    if (!request) return;

    // Wenn gerade gezeichnet wird: niemals Datei austauschen.
    if (runner && !runner->isStopped()) {
        if (index == 0) {
            WebLog::warn("Upload rejected (BeginDrawing): runner is active");
            request->send(409, "text/plain", "Runner is active");
        }
        return;
    }

    if (!index) {
        if (LittleFS.exists("/commands")) {
            LittleFS.remove("/commands");
        }

        WebLog::info("BeginDrawing | Upload | size=" + String(request->contentLength()));
        const size_t freeBytes = (size_t)LittleFS.totalBytes() - (size_t)LittleFS.usedBytes();
        if (freeBytes < request->contentLength()) {
            WebLog::error("LittleFS | Not enough space for upload (BeginDrawing)");
            request->send(400, "text/plain", "Not enough space for upload");
            return;
        }

        request->_tempFile = LittleFS.open("/commands", "w");
        WebLog::log(LOG_INFO, "Upload started (BeginDrawing)");
    }

    if (len) {
        request->_tempFile.write(data, len);
    }

    if (final) {
        request->_tempFile.close();
        WebLog::info("Upload | finished (BeginDrawing)");
        // Wichtig: Phase bleibt BeginDrawing (kein Reset / keine Kalibrier-Schleife).
    }
}

void BeginDrawingPhase::run(AsyncWebServerRequest *request) {
    size_t startLine = 0;
    if (request && request->hasParam("startLine", true)) {
        startLine = (size_t)request->getParam("startLine", true)->value().toInt();
    }

    if (runner) {
        runner->setStartLine(startLine);
        runner->start();
    }

    request->send(200, "text/plain", "OK");
}

void BeginDrawingPhase::doneWithPhase(AsyncWebServerRequest *request) {
    manager->reset();
    manager->respondWithState(request);
}

const char* BeginDrawingPhase::getName() {
    return "BeginDrawing";
}
