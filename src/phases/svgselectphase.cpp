#include "svgselectphase.h"
#include <SD.h>
#include "service/weblog.h"
#include "sd/sd_commands_bridge.h"

SvgSelectPhase::SvgSelectPhase(PhaseManager* manager) {
    this->manager = manager;
}

void SvgSelectPhase::handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final)
{
    if (!index)
    {
        if (!sdCommandsEnsureMounted()) {
            WebLog::error("SD | not mounted for upload");
            request->send(503, "text/plain", "SD not available");
            return;
        }

        if (SD.exists("/commands")) {
            SD.remove("/commands");
        }
        WebLog::info("SD | total=" + String(SD.totalBytes()) +
             " free=" + String(SD.totalBytes() - SD.usedBytes()));
        WebLog::info("Upload | size=" + String(request->contentLength()));


        if ((size_t)SD.totalBytes() - (size_t)SD.usedBytes() < request->contentLength()) {
            WebLog::error("SD | Not enough space");

            request->send(400, "text/plain", "Not enough space for upload");
            return;
        }
            
        request->_tempFile = SD.open("/commands", FILE_WRITE);
        if (!request->_tempFile) {
            WebLog::error("SD | cannot open /commands for write");
            request->send(500, "text/plain", "SD open failed");
            return;
        }
         WebLog::log(LOG_INFO, "Upload started");

    }

    if (len)
    {
        if (request->_tempFile) request->_tempFile.write(data, len);
    }

    if (final)
    {
        if (request->_tempFile) request->_tempFile.close();
       WebLog::info("Upload | finished");

        manager->setPhase(PhaseManager::RetractBelts);
    }
}

const char* SvgSelectPhase::getName() {
    return "SvgSelect";
}