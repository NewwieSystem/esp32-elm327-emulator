#include "faults.h"

Faults faults;

void faultsReset(const Profile &p) {
  faults = Faults();
  for (uint16_t c : p.demoStored)
    if (c) faults.stored[faults.nStored++] = c;
  if (p.demoPending) faults.pending[faults.nPending++] = p.demoPending;
  faults.mil = faults.nStored > 0;
}

void faultsClearAll() {
  faults.nStored = faults.nPending = faults.nPerm = 0;
  faults.mil = false;
}

bool faultsAdd(uint16_t raw, bool pending) {
  if (pending) {
    if (faults.nPending >= DTC_MAX) return false;
    faults.pending[faults.nPending++] = raw;
    return true;
  }
  if (faults.nStored >= DTC_MAX) return false;
  for (int i = 0; i < faults.nStored; i++)
    if (faults.stored[i] == raw) return true;
  faults.stored[faults.nStored++] = raw;
  faults.mil = true;
  return true;
}

// A pool of everyday codes, handy for testing an app's code list and descriptions.
bool faultsAddRandom() {
  static const uint16_t pool[] = {0x0300, 0x0301, 0x0302, 0x0171, 0x0172, 0x0420, 0x0128, 0x0442, 0x0455,
                                  0x0113, 0x0335, 0x0101, 0x0130, 0x0500, 0x0700, 0xC100, 0xC121, 0x0087,
                                  0x2002, 0x0A80, 0x1000 /* P1000: manufacturer code */};
  for (int tries = 0; tries < 20; tries++) {
    uint16_t c = pool[random(sizeof(pool) / sizeof(pool[0]))];
    bool have = false;
    for (int i = 0; i < faults.nStored; i++) have |= faults.stored[i] == c;
    if (!have) return faultsAdd(c);
  }
  return false;
}

bool parseDtc(String t, uint16_t &raw) {
  t.trim();
  t.toUpperCase();
  if (t.length() != 5) return false;
  const char sys = t[0];
  int hi = sys == 'P' ? 0 : sys == 'C' ? 1 : sys == 'B' ? 2 : sys == 'U' ? 3 : -1;
  if (hi < 0) return false;
  char *end = nullptr;
  long v = strtol(t.c_str() + 1, &end, 16);
  if (*end || v < 0 || v > 0x3FFF) return false;
  raw = (uint16_t)((hi << 14) | v);
  return true;
}

String dtcToString(uint16_t raw) {
  static const char sys[4] = {'P', 'C', 'B', 'U'};
  char buf[6];
  snprintf(buf, sizeof buf, "%c%04X", sys[raw >> 14], raw & 0x3FFF);
  return String(buf);
}
