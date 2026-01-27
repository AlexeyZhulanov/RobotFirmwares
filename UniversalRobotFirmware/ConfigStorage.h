#pragma once
#include <Arduino.h>

class ConfigStorage {
public:
  ConfigStorage(const char* path);
  String read();
  bool write(const String& json); // returns true if success
private:
  String _path;
};
