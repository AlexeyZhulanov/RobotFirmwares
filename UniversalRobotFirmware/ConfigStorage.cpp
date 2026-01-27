#include "ConfigStorage.h"
#include <FS.h>

ConfigStorage::ConfigStorage(const char* path) {
  _path = String(path);
}

String ConfigStorage::read() {
  if (!SPIFFS.exists(_path)) {
    return String();
  }
  File f = SPIFFS.open(_path, "r");
  if (!f) return String();
  String content = f.readString();
  f.close();
  return content;
}

bool ConfigStorage::write(const String& json) {
  File f = SPIFFS.open(_path, "w");
  if (!f) return false;
  f.print(json);
  f.close();
  return true;
}
