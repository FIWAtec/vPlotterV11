#ifndef BeginDrawingPhase_h
#define BeginDrawingPhase_h
#include "notsupportedphase.h"
#include "phasemanager.h"
class BeginDrawingPhase : public NotSupportedPhase {
    private:
    PhaseManager* manager;
    Runner* runner;
    AsyncWebServer* server;
    public:
    BeginDrawingPhase(PhaseManager* manager, Runner* runner, AsyncWebServer* server);
    const char* getName();
    void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
    void run(AsyncWebServerRequest *request);
    void doneWithPhase(AsyncWebServerRequest *request);
};
#endif