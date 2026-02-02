#pragma once
#include <Arduino.h>

enum LogLevel : uint8_t {
  LOG_INFO  = 0,
  LOG_WARN  = 1,
  LOG_ERROR = 2
};

struct WebLogEntry {
  uint32_t seq;   
  uint32_t ms;    
  uint32_t epoch;  
  uint8_t  level; 
  String   msg;
  String   iso;    
};

class WebLog {
public:
  static void begin();

  static void log(uint8_t level, const String& msg);
  static String toJson();
  static String toJsonAfter(uint32_t afterSeq);
  static void clear();

  static inline void info (const String& msg) { log(LOG_INFO,  msg); }
  static inline void warn (const String& msg) { log(LOG_WARN,  msg); }
  static inline void error(const String& msg) { log(LOG_ERROR, msg); }

  static void infof (const char* fmt, ...);
  static void warnf (const char* fmt, ...);
  static void errorf(const char* fmt, ...);

  static uint32_t lastSeq();

private:
  static const size_t MAX_LOGS = 200;
  static const size_t MAX_SEND = 80;

  static WebLogEntry buffer[MAX_LOGS];
  static size_t head;
  static size_t count;
  static uint32_t seqCounter;

  static void fillTimeFields(WebLogEntry& e);
  static bool isTimeValid(uint32_t epoch);
};
