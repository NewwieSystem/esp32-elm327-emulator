// Saved choices (profile and connection type), kept in flash across restarts.
#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "config.h"

inline uint8_t settingsGet(const char *key, uint8_t def) {
  Preferences p;
  p.begin("ghostcar", true);
  uint8_t v = p.getUChar(key, def);
  p.end();
  return v;
}

inline void settingsPut(const char *key, uint8_t v) {
  Preferences p;
  p.begin("ghostcar", false);
  p.putUChar(key, v);
  p.end();
}
