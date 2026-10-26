// Mazda manufacturer-specific Mode 22 data (oil temp, ATF temp, gear, boost, ...), answered only
// by the "mazda2" profile. These are the requests the REDLINE gauge sends.
#pragma once
#include <Arduino.h>

extern bool tcmOnline;   // false: the transmission stops answering (console "tcm off" / screen)

// cmd = request without spaces, e.g. "221E12". Returns false if this is not a Mazda request.
// On true, hexOut holds the answer ("621E1203") or is empty for NO DATA.
bool mazdaMode22(const String &cmd, String &hexOut, uint16_t &module);
