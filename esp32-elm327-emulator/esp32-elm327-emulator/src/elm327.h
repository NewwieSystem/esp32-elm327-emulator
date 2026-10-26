// The ELM327 interpreter: parses what the app sends, asks the simulated ECUs for an answer and
// prints it exactly the way a real ELM327 would for the car's protocol (echo, spaces, headers,
// CAN multi-frame lines, K-line checksums, SEARCHING..., NO DATA, ...).
#pragma once
#include <Arduino.h>
#include "profiles.h"

// Logical ECU addresses (11-bit CAN request ids). Requests on other protocols map onto these.
#define ECU_FUNCTIONAL 0x7DF
#define ECU_ENGINE     0x7E0
#define ECU_TCM        0x7E1

// What an ECU sends back: one or more OBD messages (service byte first).
struct ObdMsg { uint8_t n = 0; uint8_t b[40]; };
struct Reply {
  enum Kind : uint8_t { DATA, NODATA } kind = NODATA;
  uint16_t module = ECU_ENGINE;   // who answers (sets the header shown with ATH1)
  uint8_t count = 0;
  ObdMsg msg[6];
  ObdMsg &add() { kind = DATA; return msg[count < 6 ? count++ : 5]; }
};

void elmInit(int profileIndex);
void elmSetProfile(int profileIndex);         // switch car: resets ECU state, faults and the PID list
const Profile &activeProfile();
int activeProfileIndex();

String elmHandleLine(const String &raw);      // one request line in, full reply out (ends with '>')

// state other modules read or change
bool elmSupported(uint8_t pid);
void elmSetSupported(uint8_t pid, bool on);
uint16_t elmTarget();                         // ECU the current header addresses
bool elmEngineAddressed();                    // functional or engine
bool elmHeaderAllowed(uint16_t module);       // functional or exactly this module
String elmHeaderText();
uint32_t elmRequestCount();
float elmRequestsPerSecond();
unsigned long elmLastRequestMs();
