#include "begindrawingphase.h"
#include <SD.h>
#include "service/weblog.h"
#include "sd/sd_commands_bridge.h"

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
        if (!sdCommandsEnsureMounted()) {
            WebLog::error("SD | not mounted for upload (BeginDrawing)");
            request->send(503, "text/plain", "SD not available");
            return;
        }

        if (SD.exists("/commands")) {
            SD.remove("/commands");
        }

        WebLog::info("BeginDrawing | Upload | size=" + String(request->contentLength()));
        const size_t freeBytes = (size_t)SD.totalBytes() - (size_t)SD.usedBytes();
        if (freeBytes < request->contentLength()) {
            WebLog::error("SD | Not enough space for upload (BeginDrawing)");
            request->send(400, "text/plain", "Not enough space for upload");
            return;
        }

        request->_tempFile = SD.open("/commands", FILE_WRITE);
        if (!request->_tempFile) {
            WebLog::error("SD | cannot open /commands for write (BeginDrawing)");
            request->send(500, "text/plain", "SD open failed");
            return;
        }
        WebLog::log(LOG_INFO, "Upload started (BeginDrawing)");
    }

    if (len) {
        if (request->_tempFile) request->_tempFile.write(data, len);
    }

    if (final) {
        if (request->_tempFile) request->_tempFile.close();
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
