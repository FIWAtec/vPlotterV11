#include "weblog.h"
#include <ArduinoJson.h>
#include <stdarg.h>

#if defined(ESP32)
  #include <time.h>
#endif

WebLogEntry WebLog::buffer[WebLog::MAX_LOGS];
size_t WebLog::head  = 0;
size_t WebLog::count = 0;
uint32_t WebLog::seqCounter = 0;

void WebLog::begin() {
  head = 0;
  count = 0;
  seqCounter = 0;
}

bool WebLog::isTimeValid(uint32_t epoch) {
  return epoch >= 1609459200UL;
}

void WebLog::fillTimeFields(WebLogEntry& e) {
  e.ms = millis();
  e.epoch = 0;
  e.iso = "";

#if defined(ESP32)
  time_t now = time(nullptr);
  if (now > 0 && isTimeValid((uint32_t)now)) {
    e.epoch = (uint32_t)now;

    struct tm tmInfo;
    if (localtime_r(&now, &tmInfo)) {
      char buf[24];
      snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d",
               tmInfo.tm_year + 1900, tmInfo.tm_mon + 1, tmInfo.tm_mday,
               tmInfo.tm_hour, tmInfo.tm_min, tmInfo.tm_sec);
      e.iso = String(buf);
    }
  }
#endif
}

void WebLog::log(uint8_t level, const String& msg) {
  WebLogEntry& e = buffer[head];
  e.seq = ++seqCounter;
  e.level = level;
  e.msg = msg;
  fillTimeFields(e);

  head = (head + 1) % MAX_LOGS;
  if (count < MAX_LOGS) count++;
}

uint32_t WebLog::lastSeq() {
  return seqCounter;
}

String WebLog::toJson() {
  const size_t n = (count < MAX_SEND) ? count : MAX_SEND;

  StaticJsonDocument<8192> doc;
  doc["total"] = (uint32_t)count;
  doc["sent"] = (uint32_t)n;
  doc["lastSeq"] = (uint32_t)seqCounter;

  JsonArray arr = doc.createNestedArray("logs");

  for (size_t k = 0; k < n; k++) {
    size_t idx = (head + MAX_LOGS - 1 - k) % MAX_LOGS;
    JsonObject o = arr.createNestedObject();
    o["seq"]   = buffer[idx].seq;
    o["ms"]    = buffer[idx].ms;
    o["epoch"] = buffer[idx].epoch;
    o["iso"]   = buffer[idx].iso;
    o["level"] = buffer[idx].level;
    o["msg"]   = buffer[idx].msg;
  }

  String out;
  serializeJson(doc, out);
  return out;
}

String WebLog::toJsonAfter(uint32_t afterSeq) {
  StaticJsonDocument<8192> doc;
  doc["total"] = (uint32_t)count;
  doc["lastSeq"] = (uint32_t)seqCounter;

  JsonArray arr = doc.createNestedArray("logs");

  size_t sent = 0;

  for (size_t k = 0; k < count && sent < MAX_SEND; k++) {
    size_t idx = (head + MAX_LOGS - 1 - k) % MAX_LOGS;
    if (buffer[idx].seq <= afterSeq) break;

    JsonObject o = arr.createNestedObject();
    o["seq"]   = buffer[idx].seq;
    o["ms"]    = buffer[idx].ms;
    o["epoch"] = buffer[idx].epoch;
    o["iso"]   = buffer[idx].iso;
    o["level"] = buffer[idx].level;
    o["msg"]   = buffer[idx].msg;
    sent++;
  }

  doc["sent"] = (uint32_t)sent;

  String out;
  serializeJson(doc, out);
  return out;
}

void WebLog::clear() {
  head = 0;
  count = 0;
}

static void vlogf(uint8_t lvl, const char* fmt, va_list args) {
  char buf[160];
  vsnprintf(buf, sizeof(buf), fmt, args);
  WebLog::log(lvl, String(buf));
}

void WebLog::infof(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vlogf(LOG_INFO, fmt, args);
  va_end(args);
}

void WebLog::warnf(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vlogf(LOG_WARN, fmt, args);
  va_end(args);
}

void WebLog::errorf(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vlogf(LOG_ERROR, fmt, args);
  va_end(args);
}
