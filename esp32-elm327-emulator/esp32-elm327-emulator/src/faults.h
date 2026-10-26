// Fault codes (DTCs) the simulated engine ECU reports.
#pragma once
#include <Arduino.h>
#include "profiles.h"

#define DTC_MAX 12

struct Faults {
  uint16_t stored[DTC_MAX], pending[DTC_MAX], permanent[DTC_MAX];
  int nStored = 0, nPending = 0, nPerm = 0;
  bool mil = false;
  bool cleared = false;   // a Mode 04 happened: monitors "not ready", counters reset
};

extern Faults faults;

void faultsReset(const Profile &p);        // back to the profile's demo codes
void faultsClearAll();                      // console / screen: remove everything
bool faultsAdd(uint16_t raw, bool pending = false);
bool faultsAddRandom();
bool parseDtc(String text, uint16_t &raw);  // "P0300" -> 0x0300, "U0100" -> 0xC100
String dtcToString(uint16_t raw);          // 0x0300 -> "P0300"
