// Host test for the ELM327 interpreter: builds the protocol code on a PC and checks real replies.
// Run: make -C test
#include "Arduino.h"
#include "elm327.h"
#include "car.h"
#include "faults.h"

unsigned long fakeMillis = 1000;
FakeSerial Serial;
static int fails = 0, passes = 0;

static String ask(const char *line) {
  String r = elmHandleLine(line);
  r.replace("\r", "|");
  return r;
}
static void expect(const char *line, const char *want) {
  String got = ask(line);
  if (got == want) { passes++; return; }
  fails++;
  printf("FAIL  %-12s\n  want %s\n  got  %s\n", line, want, got.c_str());
}
static void expectStart(const char *line, const char *want) {
  String got = ask(line);
  if (got.startsWith(want)) { passes++; return; }
  fails++;
  printf("FAIL  %-12s\n  want %s...\n  got  %s\n", line, want, got.c_str());
}

int main() {
  // --- generic CAN car
  elmInit(0);
  expect("ATZ", "ATZ|ELM327 v1.5||>");                 // echo is on after reset
  expect("ATE0", "ATE0|OK||>");
  expectStart("0100", "SEARCHING...|41 00 ");           // first request in auto mode
  expect("ATDPN", "A6||>");
  expect("ATS0", "OK||>");
  expect("0902", "014|0:490201474853|1:54434152303030|2:30303030303031||>");
  expect("ATS1", "OK||>");
  expect("ATH1", "OK||>");
  expect("0902", "7E8 10 14 49 02 01 47 48 53|7E8 21 54 43 41 52 30 30 30|7E8 22 30 30 30 30 30 30 31||>");
  expect("03", "7E8 06 43 02 01 71 04 20 00||>");
  expect("ATH0", "OK||>");
  expect("XYZ", "?||>");
  expect("2201", "NO DATA||>");                        // no manufacturer data on generic
  expectStart("010C0D", "41 0C ");                     // two PIDs in one request

  // --- Mazda: headers decide who answers
  elmSetProfile(1);
  expect("ATSH7E1", "OK||>");
  expectStart("221E12", "SEARCHING...|62 1E 12 ");
  expect("010C", "NO DATA||>");                        // the transmission does not answer engine PIDs
  expect("ATSH7E0", "OK||>");
  expect("221E12", "NO DATA||>");                      // gear only on 7E1
  expect("0100", "41 00 1E 3F 88 03||>");              // same list as the original test sketch

  // --- K-line and J1850 formatting
  elmSetProfile(5);
  expect("ATH1", "OK||>");
  expect("010D", "SEARCHING...|48 6B 10 41 0D 00 11||>");
  expect("010C0D", "NO DATA||>");                      // no multi-PID requests on K-line
  elmSetProfile(6);
  expect("010D", "SEARCHING...|83 F1 10 41 0D 00 D2||>");
  elmSetProfile(7);
  expect("010D", "SEARCHING...|41 6B 10 41 0D 00 86||>");

  // --- 29-bit CAN
  elmSetProfile(4);
  expectStart("010D", "SEARCHING...|18 DA F1 10 03 41 0D ");
  expect("ATSH7E0", "OK||>");
  expect("010D", "NO DATA||>");                        // 11-bit header on a 29-bit bus
  expect("ATH0", "OK||>");

  // --- ignition and wrong protocol
  elmSetProfile(0);
  expect("ATZ", "ELM327 v1.5||>");
  expect("ATE0", "ATE0|OK||>");
  carSetIgnition(IGN_OFF);
  expect("010C", "SEARCHING...|UNABLE TO CONNECT||>");
  carSetIgnition(IGN_RUN);
  expect("ATSP3", "OK||>");
  expect("010C", "BUS INIT: ...ERROR||>");
  expect("ATSP6", "OK||>");
  expect("ATDP", "ISO 15765-4 (CAN 11/500)||>");

  // --- EV: almost nothing, but the bitmask chain still reaches the 40 page
  elmSetProfile(9);
  expect("010C", "NO DATA||>");
  expect("0100", "41 00 00 08 00 11||>");
  expect("0120", "41 20 00 00 00 01||>");
  expect("0151", "41 51 08||>");

  // --- clear codes only when stopped
  elmSetProfile(0);
  car.speed = 50;
  expect("04", "7F 04 22||>");
  car.speed = 0;
  autoMode = false;
  expect("04", "44||>");
  expect("03", "43 00||>");

  printf("%d passed, %d failed\n", passes, fails);
  return fails ? 1 : 0;
}
