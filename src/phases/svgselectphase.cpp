#include "svgselectphase.h"
#include "LittleFS.h"
#include "service/weblog.h"

SvgSelectPhase::SvgSelectPhase(PhaseManager* manager) {
    this->manager = manager;
}

void SvgSelectPhase::handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final)
{
    if (!index)
    {
        if (LittleFS.exists("/commands")) {
            LittleFS.remove("/commands");
        }
        WebLog::info("LittleFS | total=" + String(LittleFS.totalBytes()) +
             " free=" + String(LittleFS.totalBytes() - LittleFS.usedBytes()));
        WebLog::info("Upload | size=" + String(request->contentLength()));


        if (LittleFS.totalBytes() -  LittleFS.usedBytes() < request->contentLength()) {
            WebLog::error("LittleFS | Not enough space");

            request->send(400, "text/plain", "Not enough space for upload");
            return;
        }
            
        request->_tempFile = LittleFS.open("/commands", "w");
         WebLog::log(LOG_INFO, "Upload started");

    }

    if (len)
    {
        request->_tempFile.write(data, len);
    }

    if (final)
    {
        request->_tempFile.close();
       WebLog::info("Upload | finished");

        manager->setPhase(PhaseManager::RetractBelts);
    }
}

const char* SvgSelectPhase::getName() {
    return "SvgSelect";
}