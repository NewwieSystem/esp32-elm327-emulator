// USB serial console (115200 baud): drive the car and change settings from a terminal.
#include "console.h"
#include "app.h"
#include "car.h"
#include "elm327.h"
#include "faults.h"
#include "mazda_ext.h"
#include "link.h"

static String buf;

void consolePrintHelp() {
  Serial.println(F(
    "GhostCar console - type a command + Enter\n"
    "  help | status | profiles\n"
    "  profile <n|id>          switch car (saved)        link bt|ble|wifi   change connection (restarts)\n"
    "  auto                    run the 62 s demo loop    P | R | N | D      hold the selector (manual)\n"
    "  speed <kmh> | thr <pct> manual target speed / throttle\n"
    "  wot | cruise | idle | decel                       manual presets\n"
    "  key off | key on | key run                        ignition: no ECU / engine off / running\n"
    "  fault add P0300 | fault pending P0301 | fault random | fault clear | fault reset\n"
    "  mil on|off              check-engine lamp         tcm on|off         transmission answers or not\n"
    "  drop <hexpid> | add <hexpid>                      remove / add a Mode 01 PID from the supported list"));
}

static void printStatus() {
  const Profile &P = activeProfile();
  Serial.printf("[%s] %s | %s | link %s %s (%s)\n", autoMode ? "auto" : "manual", P.name, protocolName(P.protocol),
                linkName(linkType()), linkClientConnected() ? "CONNECTED" : "waiting", linkAddress().c_str());
  Serial.printf("  key %s  sel %s  gear %d  %.0f km/h  %.0f rpm  thr %.0f%%  load %.0f%%  MAP %.0f kPa  MAF %.1f g/s\n",
                ignName(car.ign), carSelName(), car.gear, car.speed, car.rpm, car.throttle * 100, car.load, car.map, car.maf);
  Serial.printf("  lambda %.3f  STFT %.1f  LTFT %.1f  adv %.1f  fuel %.0f%%  rail %.1f MPa  %.1f L/h  batt %.0f%%%s\n",
                car.lambda, car.stft, car.ltft, car.timing, car.fuelPct, car.railKpa / 1000.0f, car.fuelRateLh, car.battSoc,
                car.fuelCut ? "  FUEL CUT" : "");
  Serial.printf("  header %s  TCM %s  MIL %s  codes %d stored / %d pending  requests %lu\n", elmHeaderText().c_str(),
                tcmOnline ? "on" : "OFF", faults.mil ? "ON" : "off", faults.nStored, faults.nPending,
                (unsigned long)elmRequestCount());
}

static void printProfiles() {
  for (int i = 0; i < PROFILE_COUNT; i++)
    Serial.printf("%c %d  %-11s %-22s %-26s %s\n", i == activeProfileIndex() ? '*' : ' ', i, PROFILES[i].id,
                  PROFILES[i].name, protocolName(PROFILES[i].protocol), PROFILES[i].examples);
}

static void handle(String line) {
  line.trim();
  String u = line;
  u.toUpperCase();
  if (!u.length()) return;
  if (u == "HELP" || u == "?") return consolePrintHelp();
  if (u == "STATUS") return printStatus();
  if (u == "PROFILES") return printProfiles();
  if (u.startsWith("PROFILE ")) {
    int i = findProfile(line.substring(8));
    if (i < 0) { Serial.println("unknown profile, type profiles"); return; }
    appSetProfile(i);
    Serial.printf("profile: %s\n", activeProfile().name);
    return;
  }
  if (u.startsWith("LINK ")) {
    String t = u.substring(5);
    LinkType l = t == "BT" ? LINK_BT : t == "BLE" ? LINK_BLE : t == "WIFI" ? LINK_WIFI : LINK_COUNT;
    if (l == LINK_COUNT) { Serial.println("link bt | ble | wifi"); return; }
    Serial.printf("switching to %s, restarting...\n", linkName(l));
    appSetLink(l);
    return;
  }
  if (u == "AUTO") { carAuto(); Serial.println("auto demo loop"); return; }
  if (u == "P" || u == "N" || u == "R" || u == "D") {
    carSetSelector(u == "P" ? SEL_P : u == "N" ? SEL_N : u == "R" ? SEL_R : SEL_D);
    Serial.printf("manual: selector %s\n", u.c_str());
    return;
  }
  if (u.startsWith("SPEED ")) { carSetTarget(u.substring(6).toFloat(), manualThrottle); Serial.printf("manual: speed %.0f\n", manualSpeed); return; }
  if (u.startsWith("THR "))   { carSetTarget(manualSpeed, u.substring(4).toFloat() / 100.0f); Serial.printf("manual: throttle %.0f%%\n", manualThrottle * 100); return; }
  if (u == "WOT")    { carSetSelector(SEL_D); carSetTarget(140, 1.0f);  Serial.println("manual: full throttle"); return; }
  if (u == "CRUISE") { carSetSelector(SEL_D); carSetTarget(90, 0.18f);  Serial.println("manual: cruise 90"); return; }
  if (u == "IDLE")   { carSetSelector(SEL_D); carSetTarget(0, 0);       Serial.println("manual: idle in D"); return; }
  if (u == "DECEL")  { carSetSelector(SEL_D); carSetTarget(20, 0);      Serial.println("manual: decel (fuel cut)"); return; }
  if (u.startsWith("KEY ")) {
    String k = u.substring(4);
    Ignition i = k == "OFF" ? IGN_OFF : k == "ON" ? IGN_ON : IGN_RUN;
    carSetIgnition(i);
    Serial.printf("ignition %s\n", ignName(i));
    return;
  }
  if (u == "FAULT CLEAR")  { faultsClearAll(); Serial.println("all fault codes removed"); return; }
  if (u == "FAULT RESET")  { faultsReset(activeProfile()); Serial.println("demo fault codes restored"); return; }
  if (u == "FAULT RANDOM") { faultsAddRandom(); Serial.printf("stored codes now %d\n", faults.nStored); return; }
  if (u.startsWith("FAULT ADD ") || u.startsWith("FAULT PENDING ")) {
    bool pend = u.startsWith("FAULT PENDING ");
    uint16_t raw;
    if (!parseDtc(u.substring(pend ? 14 : 10), raw) || !faultsAdd(raw, pend)) { Serial.println("give a code like P0300 (list may be full)"); return; }
    Serial.printf("%s codes now %d\n", pend ? "pending" : "stored", pend ? faults.nPending : faults.nStored);
    return;
  }
  if (u == "MIL ON" || u == "MIL OFF") { faults.mil = u == "MIL ON"; Serial.printf("check-engine lamp %s\n", faults.mil ? "on" : "off"); return; }
  if (u == "TCM OFF") { tcmOnline = false; Serial.println("transmission stops answering"); return; }
  if (u == "TCM ON")  { tcmOnline = true;  Serial.println("transmission answers again"); return; }
  if (u.startsWith("DROP ") || u.startsWith("ADD ")) {
    bool add = u.startsWith("ADD ");
    int pid = (int)strtol(u.substring(add ? 4 : 5).c_str(), nullptr, 16);
    if (pid <= 0 || pid > 0xFF || pid % 0x20 == 0) { Serial.println("give a Mode 01 PID in hex, e.g. 24"); return; }
    elmSetSupported(pid, add);
    Serial.printf("PID %02X %s\n", pid, add ? "supported" : "no longer supported");
    return;
  }
  Serial.println("unknown command, type help");
}

void consolePoll() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r' || c == '\n') {
      if (buf.length()) { handle(buf); buf = ""; }
    } else if (buf.length() < 64) {
      buf += c;
    }
  }
}
