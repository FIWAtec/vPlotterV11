#include "phasemanager.h"
#include "retractbeltsphase.h"
#include "settopdistancephase.h"
#include "extendtohomephase.h"
#include "pencalibrationphase.h"
#include "svgselectphase.h"
#include "begindrawingphase.h"
#include "AsyncJson.h"
#include <ArduinoJson.h>
#include <stdexcept>
#include "service/weblog.h"

PhaseManager::PhaseManager(Movement* movement, Pen* pen, Runner* runner, AsyncWebServer* server) {
    retractBeltsPhase = new RetractBeltsPhase(this, movement);
    setTopDistancePhase = new SetTopDistancePhase(this, movement, pen);
    extendToHomePhase = new ExtendToHomePhase(this, movement);
    penCalibrationPhase = new PenCalibrationPhase(this, pen);
    svgSelectPhase = new SvgSelectPhase(this);
    beginDrawingPhase = new BeginDrawingPhase(this, runner, server);

    this->movement = movement;
    reset();
}

Phase* PhaseManager::getCurrentPhase() {
    return currentPhase;
}

void PhaseManager::setPhase(PhaseNames name) {

    switch (name) {
        case PhaseNames::RetractBelts:
            WebLog::info("Phase | RetractBelts");
            currentPhase = retractBeltsPhase;
            break;
        case PhaseNames::SetTopDistance:
            WebLog::info("Phase | SetTopDistance");
            currentPhase = setTopDistancePhase;
            break;
        case PhaseNames::ExtendToHome:
           WebLog::info("Phase | ExtendToHome");
            currentPhase = extendToHomePhase;
            break;
        case PhaseNames::PenCalibration:
            WebLog::info("Phase | PenCalibration");
            currentPhase = penCalibrationPhase;
            break;
        case PhaseNames::SvgSelect:
           WebLog::info("Phase | SvgSelect");
            currentPhase = svgSelectPhase;
            break;
        case PhaseNames::BeginDrawing:
            WebLog::info("Phase | BeginDrawing");
            currentPhase = beginDrawingPhase;
            break;
        default:
            throw std::invalid_argument("Invalid Phase");
    }
}

void PhaseManager::respondWithState(AsyncWebServerRequest *request) {
    auto currentPhase = getCurrentPhase()->getName();
    auto moving = movement->isMoving();

    auto topDistance = movement->getTopDistance();
    auto safeWidth = topDistance != -1 ? movement->getWidth() : -1;

    AsyncResponseStream *response = request->beginResponseStream("application/json");

    StaticJsonDocument<256> doc;

    doc["phase"] = currentPhase;
    doc["moving"] = moving;
    doc["topDistance"] = topDistance;
    doc["safeWidth"] = safeWidth;

    if (topDistance != -1) {
        auto homePosition = movement->getHomeCoordinates();
        doc["homeX"] = homePosition.x;
        doc["homeY"] = homePosition.y;
    } else {
        doc["homeX"] = 0;
        doc["homeY"] = 0;
    }

    serializeJson(doc, *response);
    request->send(response);
}


void PhaseManager::reset() {
    setPhase(PhaseManager::SetTopDistance);
}
