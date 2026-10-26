// Vehicle profiles: what kind of car GhostCar pretends to be.
// A profile picks the OBD protocol, the engine / transmission model and the list of Mode 01 PIDs
// the "ECU" says it supports. See docs/VEHICLES.md for what each one is based on.
#pragma once
#include <Arduino.h>

enum EngineType : uint8_t { ENG_GAS, ENG_GAS_TURBO, ENG_DIESEL, ENG_HYBRID, ENG_EV };
enum TransType  : uint8_t { TR_AUTO, TR_CVT, TR_EV };

// ELM327 protocol numbers (ATSP / ATDPN)
enum : uint8_t {
  PROTO_J1850_PWM = 1, PROTO_J1850_VPW = 2, PROTO_ISO9141 = 3, PROTO_KWP_SLOW = 4, PROTO_KWP_FAST = 5,
  PROTO_CAN11_500 = 6, PROTO_CAN29_500 = 7, PROTO_CAN11_250 = 8, PROTO_CAN29_250 = 9
};

struct Profile {
  const char *id;          // short key for the console ("profile mazda2")
  const char *name;        // shown on the screen
  const char *examples;    // cars that behave like this
  uint8_t protocol;
  EngineType engine;
  TransType trans;
  float displacementL;
  uint16_t idleRpm, redlineRpm;
  uint16_t vmax;           // km/h, caps the demo loop
  uint8_t nGears;          // TR_AUTO only
  const float *ratios;     // [0] unused, [1..nGears]
  float rpmPerKmhTop;      // rpm per km/h with ratio 1.0
  const uint8_t *pids;     // supported Mode 01 PIDs (not the 00/20/40.. mask PIDs)
  uint8_t nPids;
  uint8_t obdStandard;     // PID 1C
  uint8_t fuelType;        // PID 51
  uint16_t refTorqueNm;    // PID 63
  const char *vin;         // 17 characters
  const char *ecuName;     // Mode 09 0A (CAN profiles)
  uint16_t demoStored[3];  // fault codes after "fault reset" (0 = none)
  uint16_t demoPending;
  bool mazdaExt;           // answer the Mazda Mode 22 set used by the REDLINE gauge
  bool hasTcm;             // a transmission module answers on 7E1
};

extern const Profile PROFILES[];
extern const uint8_t PROFILE_COUNT;

const char *protocolName(uint8_t p);
bool protocolIsCan(uint8_t p);
bool protocolIs29bit(uint8_t p);
int findProfile(const String &key);   // by index or id, -1 if unknown
