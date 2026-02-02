#pragma once
#include <Arduino.h>
#include <FS.h>

class SdManager {
public:
  bool begin();
  fs::FS& fs();

  String listJson(const String& path);
  bool mkdirs(const String& path);
  bool removeFile(const String& path);
  bool removeDirRecursive(const String& path);
  bool renamePath(const String& from, const String& to);
  bool exists(const String& path);
  String infoJson();

private:
  String normalizePath(const String& p);
  String jsonEscape(const String& s);
  bool removeDirRecursiveImpl(const String& path);
};
