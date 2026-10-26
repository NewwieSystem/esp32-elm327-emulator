// ESP32 fake ELM327 - a pretend Mazda 2 SKYACTIV-G 1.3 (6AT) for testing the REDLINE gauge on the desk.
//
// What it does
//   * Bluetooth Classic SPP, name "OBDII" (put its MAC in the gauge's config.h).
//   * A small car model drives a loop by itself: P -> N -> R -> D, accelerates through gears 1..6,
//     full throttle, cruise, decel with fuel cut, brake, repeat (62 s). Everything the gauge asks
//     for is derived from that one model, so the numbers move together like a real car.
//   * Answers every request the gauge sends: Mode 01 (with proper "supported PID" masks), the Mazda
//     Mode 22 PIDs (oil 221310, ATF 221E1C, gear 221E12) and all the other Mode 22 PIDs of your old sketch.
//   * The ELM327 response-count digit ("010C1"): accepted, and it makes answers come ~5x sooner (see EMULATE_ELM_LATENCY).
//   * Fault codes: Mode 03 (stored), 07 (pending), 0A (permanent), 0101 (lamp), and Mode 04 to clear them,
//     in the real CAN format (single frame, or the numbered multi-frame lines for more than two codes).
//     Mode 04 is refused ("7F 04 22") while the simulated car is moving, like a real ECU.
//   * Header handling like the real car: engine PIDs only answer on 7DF/7E0, the gear only on 7E1
//     (STRICT_HEADERS), so a bug in the gauge's ATSH switching shows up as "--" / hidden gear.
//
// USB serial console (115200), type a line + Enter:
//   help                 this list
//   auto                 run the 62 s demo loop (default)
//   P | N | R | D        hold the selector (manual mode)
//   speed 80             manual: target speed km/h        thr 40   manual: throttle %
//   wot | cruise | idle | decel     manual presets
//   drop 24 | add 24     take a Mode 01 PID out of / into the supported list (hex): test "--" tiles,
//                        e.g. "drop 24" + "add 34" makes the gauge fall back to the current-type lambda PID
//   tcm off | tcm on     transmission stops / starts answering (gauge must hide the gear, then recover)
//   fault add P0300      add a stored fault code (P/C/B/U + 4 hex digits)   fault clear   fault reset (back to the demo codes)
//   mil on | mil off     check-engine lamp (the gauge polls it every 30 s and warns when it comes on)
// Also answered: readiness monitors (0101, EVAP "not ready" after a clear), 0121 / 0130 / 0131, a Mode 02 freeze
// frame for the first stored code, and Mode 09 VIN / ECU name / calibration id (the gauge's STATUS page).
//   status               print the current model values
#include <Arduino.h>
#include "BluetoothSerial.h"
#include "esp_bt_device.h"

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled
#endif

#if !defined(CONFIG_BT_SPP_ENABLED)
#error Bluetooth Classic SPP is not available on this board
#endif

BluetoothSerial SerialBT;

const char *BT_NAME = "OBDII";

#define LOG_TRAFFIC    0   // 1 = print every RX / TX line on the USB serial. Debugging only: at 115200 baud each
                           // request costs ~5 ms of printing, which slows every answer (and the gauge's numbers with it)
// A real ELM327 waits out its timeout for further modules after the first answer, unless the request ends
// with the response-count digit ("010C1"). This makes the simulator behave the same way so you can SEE what
// the gauge's one-answer requests buy: ~55 ms per plain request, ~10 ms with the digit. 0 = answer at once.
#define EMULATE_ELM_LATENCY 0
#define STRICT_HEADERS 1   // engine PIDs only on 7DF/7E0, gear only on 7E1 (like the real modules)

// Gear raw codes the transmission reports (same as GEAR_RAW_* in the gauge's config.h);
// in D it reports the gear number 1..6.
const int GEAR_RAW_N = 50, GEAR_RAW_R = 60, GEAR_RAW_P = 70;

String inputBuffer;
String consoleBuffer;
String currentHeader = "7DF";
bool dtcCleared = false;
bool tcmOnline = true;
bool supp[256];            // Mode 01 PIDs this "car" lists as supported

// ----------------------------------------------------------------------------------------------
// The car
// ----------------------------------------------------------------------------------------------
enum Sel : uint8_t { SEL_P, SEL_R, SEL_N, SEL_D };

struct Car {
  Sel   sel = SEL_P;
  float speed = 0;         // km/h
  float throttle = 0;      // 0..1
  int   gear = 1;          // 1..6 (used in D)
  float rpm = 780;
  bool  fuelCut = false;
  float load = 25, map = 40, maf = 2.5f;
  float lambda = 1.0f, stft = 0, ltft = 2.3f, timing = 12;
  float fuelPct = 62;
  float coolant = 88, iat = 35, oil = 95, atf = 82, volt = 13.9f;
} car;

bool  autoMode = true;
float manualSpeed = 0, manualThrottle = 0;
unsigned long autoT0 = 0, lastStep = 0, shiftUntil = 0;
float simT = 0;

// Old-sketch variables (the Mode 22 answers below use them)
int simRPM = 780, simSpeed = 0, simCoolant = 88, simIAT = 35, simOil = 95, simFuelTemp = 40, simATF = 82;
int simBaro = 100, simBoostKpa = 140, simBoostDesiredKpa = 150, simFuelLiters = 40;
int simGearCode = GEAR_RAW_P;
float simVoltage = 13.9f;

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static float toward(float cur, float tgt, float dt, float tau) {
  float k = dt / (tau + dt);
  return cur + (tgt - cur) * k;
}

// 6AT ratios (approximate) and rpm per km/h in top gear
const float RATIO[7] = {0, 3.487f, 1.992f, 1.449f, 1.000f, 0.707f, 0.600f};
const float RPM_PER_KMH_TOP = 38.8f;     // x ratio

void autoScript(float t, Sel &sel, float &tgtSpeed, float &thr) {
  if (t < 5)        { sel = SEL_P; tgtSpeed = 0;   thr = 0; }
  else if (t < 7)   { sel = SEL_N; tgtSpeed = 0;   thr = 0; }
  else if (t < 10)  { sel = SEL_R; tgtSpeed = 6;   thr = 0.12f; }
  else if (t < 12)  { sel = SEL_N; tgtSpeed = 0;   thr = 0; }
  else if (t < 15)  { sel = SEL_D; tgtSpeed = 0;   thr = 0; }
  else if (t < 30)  { sel = SEL_D; tgtSpeed = 90;  thr = 0.35f; }
  else if (t < 36)  { sel = SEL_D; tgtSpeed = 165; thr = 1.0f; }
  else if (t < 45)  { sel = SEL_D; tgtSpeed = 90;  thr = 0.18f; }
  else if (t < 53)  { sel = SEL_D; tgtSpeed = 25;  thr = 0; }
  else if (t < 60)  { sel = SEL_D; tgtSpeed = 0;   thr = 0; }
  else              { sel = SEL_N; tgtSpeed = 0;   thr = 0; }
}

void stepSim() {
  unsigned long now = millis();
  if (now - lastStep < 50) return;
  float dt = (now - lastStep) / 1000.0f;
  if (dt > 0.5f) dt = 0.5f;
  lastStep = now;
  simT += dt;

  float tgtSpeed, thr;
  if (autoMode) {
    float t = fmodf((now - autoT0) / 1000.0f, 62.0f);
    autoScript(t, car.sel, tgtSpeed, thr);
  } else {
    tgtSpeed = (car.sel == SEL_D) ? manualSpeed : (car.sel == SEL_R ? min(manualSpeed, 8.0f) : 0);
    thr = manualThrottle;
  }
  car.throttle = toward(car.throttle, thr, dt, 0.25f);

  // speed
  bool driving = car.sel == SEL_D || car.sel == SEL_R;
  if (!driving) tgtSpeed = 0;
  float rate = tgtSpeed > car.speed ? 0.10f + car.throttle * 0.40f : 0.30f;
  if (!driving) rate = 0.7f;
  car.speed = max(0.0f, car.speed + (tgtSpeed - car.speed) * rate * dt * 3.0f);
  if (car.speed < 0.3f && tgtSpeed < 0.5f) car.speed = 0;

  // gear (D only): shift points move up with throttle, hysteresis on the way down, kick-down at full throttle
  if (car.sel == SEL_D) {
    static const float upN[5] = {18, 34, 52, 70, 88}, upW[5] = {32, 58, 84, 106, 130};
    int g = car.gear;
    if (car.throttle > 0.85f && car.speed > 20) {                 // floor it: drop to a gear that pulls ~4300-6200 rpm
      g = 6;
      while (g > 1 && car.speed * RPM_PER_KMH_TOP * RATIO[g] < 4300) g--;
    } else {
      float th = clampf(car.throttle * 1.6f, 0, 1);
      while (g < 6 && car.speed > upN[g - 1] + (upW[g - 1] - upN[g - 1]) * th) g++;
      while (g > 1 && car.speed < (upN[g - 2] + (upW[g - 2] - upN[g - 2]) * th) * 0.62f) g--;
    }
    if (g != car.gear) { car.gear = g; shiftUntil = now + 350; }
  } else if (car.speed < 1) car.gear = 1;

  // rpm
  float ratio = car.sel == SEL_R ? 3.99f : RATIO[car.gear];
  float geared = car.speed * RPM_PER_KMH_TOP * ratio;
  float idleRev = 780 + car.throttle * (driving && car.speed > 3 ? 1500 : 4200);
  float rpmT = (driving && car.speed > 6) ? max(geared, 800 + car.throttle * 600) : idleRev;
  if (driving && car.speed > 0 && car.speed <= 6) rpmT = max(rpmT, 800 + car.throttle * 1800);
  if (millis() < shiftUntil) rpmT -= 350;
  car.rpm = toward(car.rpm, clampf(rpmT, 700, 6400), dt, 0.12f);

  // engine
  car.fuelCut = car.sel == SEL_D && car.rpm > 1400 && car.throttle < 0.03f && car.speed > 15;
  float loadT = car.fuelCut ? 12.0f : clampf(22.0f + car.throttle * (60.0f + 20.0f * car.rpm / 6500.0f), 0, 100);
  car.load = toward(car.load, loadT, dt, 0.12f);
  car.map = toward(car.map, 20.0f + car.load * 0.8f, dt, 0.12f);
  car.maf = car.rpm / 120.0f * 1.298f * 1.19f * (car.load / 100.0f) * 0.85f;
  bool wot = car.throttle > 0.85f;
  float lamT = car.fuelCut ? 1.35f : wot ? 0.87f : 1.0f + 0.02f * sinf(simT * 2.3f);
  car.lambda = toward(car.lambda, lamT, dt, 0.15f);
  float stftT = (car.fuelCut || wot) ? 0 : 3.5f * sinf(simT * 1.7f);
  car.stft = toward(car.stft, stftT, dt, 0.4f);
  car.ltft = 2.3f + 0.4f * sinf(simT * 0.05f);
  car.timing = toward(car.timing, 10.0f + car.rpm / 6400.0f * 28.0f - car.load * 0.12f, dt, 0.2f);
  car.fuelPct = clampf(car.fuelPct - dt * 0.0015f, 5, 100);
  car.coolant = 88 + 2 * sinf(simT * 0.05f);
  car.iat = 35 + 3 * sinf(simT * 0.03f);
  car.oil = 95 + 3 * sinf(simT * 0.04f);
  car.atf = 82 + 4 * sinf(simT * 0.035f) + (car.speed > 60 ? 6 : 0);
  car.volt = 13.9f + 0.1f * sinf(simT * 0.2f);

  // mirror into the old-sketch variables
  simRPM = (int)car.rpm; simSpeed = (int)car.speed; simCoolant = (int)car.coolant; simIAT = (int)car.iat;
  simOil = (int)car.oil; simATF = (int)car.atf; simVoltage = car.volt;
  simFuelLiters = (int)(car.fuelPct * 0.44f);
  simBoostKpa = (int)car.map; simBoostDesiredKpa = simBoostKpa + 10;
  switch (car.sel) {
    case SEL_P: simGearCode = GEAR_RAW_P; break;
    case SEL_R: simGearCode = GEAR_RAW_R; break;
    case SEL_N: simGearCode = GEAR_RAW_N; break;
    default:    simGearCode = car.gear; break;
  }
}

// ----------------------------------------------------------------------------------------------
// fault codes
// ----------------------------------------------------------------------------------------------
String hex2(uint32_t value);

const int DTC_MAX = 12;
uint16_t dtcStored[DTC_MAX], dtcPending[DTC_MAX], dtcPerm[DTC_MAX];
int nStored = 0, nPending = 0, nPerm = 0;
bool milOn = false;

void resetFaults() {
  nStored = 2; dtcStored[0] = 0x0171; dtcStored[1] = 0x0420;      // P0171 system too lean, P0420 catalyst efficiency
  nPending = 1; dtcPending[0] = 0x0301;                           // P0301 cylinder 1 misfire (pending)
  nPerm = 0;
  milOn = true;
}

// "P0300" -> 0x0300, "U0100" -> 0xC100. returns false for anything that is not a code
bool parseCode(String t, uint16_t &raw) {
  t.toUpperCase();
  if (t.length() != 5) return false;
  const char sys = t.c_str()[0];
  int hi = sys == 'P' ? 0 : sys == 'C' ? 1 : sys == 'B' ? 2 : sys == 'U' ? 3 : -1;
  if (hi < 0) return false;
  char *end = nullptr;
  long v = strtol(t.c_str() + 1, &end, 16);
  if (*end || v < 0 || v > 0x3FFF) return false;
  raw = (uint16_t)((hi << 14) | v);
  return true;
}

// The reply to Mode 03 / 07 / 0A on CAN: service+0x40, a count byte, then 2 bytes per code. Up to two codes fit
// one frame; more are sent the way the ELM327 prints ISO-TP: a length line, then "0:", "1:" ... lines.
String dtcReply(uint8_t svc, const uint16_t *list, int n) {
  uint8_t pl[2 + 2 * DTC_MAX];
  int len = 0;
  pl[len++] = svc; pl[len++] = (uint8_t)n;
  for (int i = 0; i < n; i++) { pl[len++] = list[i] >> 8; pl[len++] = list[i] & 0xFF; }
  if (len <= 7) {
    String r;
    for (int i = 0; i < 7; i++) r += hex2(i < len ? pl[i] : 0);
    return r;
  }
  char head[12];
  snprintf(head, sizeof head, "%03X", len);
  String r = String(head);
  int pos = 0, line = 0;
  while (pos < len) {
    int chunk = line == 0 ? 6 : 7;
    char tag[6];
    snprintf(tag, sizeof tag, "%X:", line & 15);
    r += "\r" + String(tag);
    for (int i = 0; i < chunk; i++) r += hex2(pos + i < len ? pl[pos + i] : 0);
    pos += chunk; line++;
  }
  return r;
}

// ----------------------------------------------------------------------------------------------
// helpers
// ----------------------------------------------------------------------------------------------
String hex2(uint32_t value) {
  char buf[3];
  snprintf(buf, sizeof(buf), "%02X", (unsigned int)(value & 0xFF));
  return String(buf);
}

String hex4(uint32_t value) {
  char buf[5];
  snprintf(buf, sizeof(buf), "%04X", (unsigned int)(value & 0xFFFF));
  return String(buf);
}

String hex6(uint32_t value) {
  char buf[7];
  snprintf(buf, sizeof(buf), "%06X", (unsigned int)(value & 0xFFFFFF));
  return String(buf);
}

String response22(const String &pid, const String &data) {
  return "62" + pid + data;
}

void printBTMac() {
  const uint8_t *mac = esp_bt_dev_get_address();
  if (!mac) {
    Serial.println("Bluetooth MAC: unavailable");
    return;
  }

  Serial.print("Bluetooth MAC: ");
  for (int i = 0; i < 6; i++) {
    if (mac[i] < 0x10) Serial.print('0');
    Serial.print(mac[i], HEX);
    if (i < 5) Serial.print(':');
  }
  Serial.println();
}

bool headerAllowed(const String &required) {
  if (required.length() == 0) return true;
  if (currentHeader == "7DF") return true;
  return currentHeader == required;
}

// engine ECU: functional broadcast or addressed to it
bool engineHeaderOk() { return currentHeader == "7DF" || currentHeader == "7E0"; }

void initSupported() {
  for (int i = 0; i < 256; i++) supp[i] = false;
  const uint8_t list[] = {0x04, 0x05, 0x06, 0x07, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11,
                          0x15, 0x1F, 0x23, 0x24, 0x2F, 0x33, 0x3C, 0x42, 0x44, 0x49};
  // (no 0x46 ambient: the real Mazda 2 doesn't answer it either)
  for (uint8_t p : list) supp[p] = true;
}

// 0100 / 0120 / 0140 / 0160 ...: bitmask of the 32 PIDs that follow, last bit = "the next page exists"
String maskResponse(int base) {
  uint32_t m = 0;
  for (int i = 1; i <= 32; i++) {
    int pid = base + i;
    bool on = pid < 256 && supp[pid];
    if (i == 32) {                                   // chain bit: is there anything on the next page?
      on = false;
      for (int p = base + 33; p <= base + 64 && p < 256; p++) if (supp[p]) on = true;
    }
    if (on) m |= 1UL << (32 - i);
  }
  char buf[9];
  snprintf(buf, sizeof buf, "%08lX", (unsigned long)m);
  return "41" + hex2(base) + String(buf);
}

// ----------------------------------------------------------------------------------------------
// Mode 01
// ----------------------------------------------------------------------------------------------
String handleMode01(const String &cmd) {
  if (!engineHeaderOk()) return "NO DATA";           // the transmission does not answer engine PIDs
  int pid = (int)strtol(cmd.substring(2).c_str(), nullptr, 16);

  if (pid == 0x00 || pid == 0x20 || pid == 0x40 || pid == 0x60) {
    if (pid != 0) {                                  // a page exists only if it lists at least one PID
      bool any = false;
      for (int p = pid + 1; p <= pid + 32 && p < 256; p++) if (supp[p]) any = true;
      if (!any) return "NO DATA";
    }
    return maskResponse(pid);
  }
  // lamp + number of codes; monitors: misfire / fuel / components, catalyst, EVAP, O2, O2 heater, EGR supported;
  // after a clear (Mode 04) the EVAP monitor reads "not ready" (byte D bit 2), as on a real car
  if (pid == 0x01) return "4101" + hex2((milOn ? 0x80 : 0) | (nStored & 0x7F)) + "07E5" + hex2(dtcCleared ? 0x04 : 0x00);
  if (pid == 0x21) return "4121" + hex4(milOn ? 46 : 0);                  // km driven with the lamp on
  if (pid == 0x30) return "4130" + hex2(dtcCleared ? 0 : 41);              // warm-ups since codes cleared
  if (pid == 0x31) return "4131" + hex4(dtcCleared ? 0 : 312);             // km since codes cleared
  if (!supp[pid]) return "NO DATA";

  String tag = "41" + hex2(pid);
  switch (pid) {
    case 0x04: return tag + hex2((int)lroundf(car.load * 255.0f / 100.0f));
    case 0x05: return tag + hex2((int)lroundf(car.coolant) + 40);
    case 0x06: return tag + hex2((int)lroundf(car.stft * 128.0f / 100.0f + 128.0f));
    case 0x07: return tag + hex2((int)lroundf(car.ltft * 128.0f / 100.0f + 128.0f));
    case 0x0B: return tag + hex2((int)lroundf(car.map));
    case 0x0C: return tag + hex4((uint32_t)(car.rpm * 4));
    case 0x0D: return tag + hex2((int)lroundf(car.speed));
    case 0x0E: return tag + hex2((int)lroundf((car.timing + 64.0f) * 2.0f));
    case 0x0F: return tag + hex2((int)lroundf(car.iat) + 40);
    case 0x10: return tag + hex4((uint32_t)lroundf(car.maf * 100.0f));
    case 0x11: return tag + hex2((int)lroundf(car.throttle * 255.0f));
    case 0x24: return tag + hex4((uint32_t)lroundf(car.lambda * 32768.0f)) + hex4((uint32_t)lroundf((0.45f + (car.lambda - 1.0f) * 2.0f) * 8192.0f + 8192.0f));
    case 0x34: return tag + hex4((uint32_t)lroundf(car.lambda * 32768.0f)) + hex4((uint32_t)lroundf(((car.lambda - 1.0f) * 6.0f + 128.0f) * 256.0f));
    case 0x2F: return tag + hex2((int)lroundf(car.fuelPct * 255.0f / 100.0f));
    case 0x33: return tag + hex2(simBaro);
    case 0x42: return tag + hex4((uint32_t)lroundf(car.volt * 1000.0f));
    case 0x44: return tag + hex4((uint32_t)lroundf(32768.0f * (car.throttle > 0.85f ? 0.88f : 1.0f)));   // commanded lambda
    case 0x46: return tag + hex2(30 + 40);
    case 0x15: return tag + hex2((int)lroundf((car.lambda > 1.15f ? 0.08f : car.rpm < 400 ? 0.45f : 0.70f) * 200.0f)) + "FF";   // rear O2
    case 0x3C: return tag + hex4((uint32_t)lroundf((420.0f + car.load * 4.2f + 40.0f) * 10.0f));   // catalyst temperature
    case 0x49: return tag + hex2((int)lroundf((15.0f + car.throttle * 65.0f) * 255.0f / 100.0f));   // pedal D
    case 0x1F: return tag + hex4((uint32_t)(millis() / 1000));                                 // run time, s
    case 0x23: return tag + hex4((uint32_t)lroundf((4.0f + car.load * 0.14f) * 100.0f));         // fuel rail, 10 kPa units
    default:   return "NO DATA";
  }
}

void sendResponse(const String &response) {
#if LOG_TRAFFIC
  Serial.print("TX: ");
  Serial.println(response);
#endif
  SerialBT.print(response + "\r>");               // ONE write: two small Bluetooth packets per answer add latency and jitter
}

String handleCommandInner(String cmd);

// OBD requests may carry the ELM327 response-count digit: an odd number of hex digits means the last one is
// "stop after this many answers" ("010C1", "221E121"). It never changes the answer, only how long the adapter waits.
String handleCommand(String cmd) {
  cmd.trim();
  cmd.replace(" ", "");
  cmd.toUpperCase();
  bool counted = false;
  if (!cmd.startsWith("AT") && cmd.length() >= 5 && (cmd.length() % 2) == 1) {
    cmd = cmd.substring(0, cmd.length() - 1);
    counted = true;
  }
#if EMULATE_ELM_LATENCY
  if (!cmd.startsWith("AT")) delay(counted ? 10 : 55);
#endif
  (void)counted;
  return handleCommandInner(cmd);
}

String handleCommandInner(String cmd) {

#if LOG_TRAFFIC
  Serial.print("RX: ");
  Serial.println(cmd);
#endif
  stepSim();

  // ELM327 AT commands
  if (cmd == "ATZ" || cmd == "ATWS") {
    dtcCleared = false;
    currentHeader = "7DF";
    return "ELM327 v1.5";
  }
  if (cmd == "ATI") return "ELM327 v1.5";
  if (cmd == "ATDP") return "ISO 15765-4 (CAN 11/500)";
  if (cmd == "ATDPN") return "A6";
  if (cmd == "ATRV") return String(simVoltage, 1) + "V";
  if (cmd.startsWith("ATSH")) {
    String newHeader = cmd.substring(4);
    if (newHeader.length() != 3) return "?";
    currentHeader = newHeader;
    return "OK";
  }
  if (cmd.startsWith("AT")) {
    // ATE ATL ATS ATH ATAT ATAL ATSP ATST ATCAF ATCFC ATM ATD ... : accepted, nothing to model
    return "OK";
  }

  // Standard OBD-II Mode 01
  if (cmd.length() == 4 && cmd.startsWith("01")) return handleMode01(cmd);

  // Mode 02 freeze frame (frame 00): the first stored code, at a warm cruise
  if (cmd.length() == 6 && cmd.startsWith("02") && cmd.substring(4) == "00") {
    if (!engineHeaderOk()) return "NO DATA";
    int pid = (int)strtol(cmd.substring(2, 4).c_str(), nullptr, 16);
    if (pid == 0x02) return "420200" + hex4(nStored ? dtcStored[0] : 0);
    if (!nStored) return "NO DATA";
    String tag = "42" + hex2(pid) + "00";
    switch (pid) {
      case 0x0C: return tag + hex4(2150 * 4);
      case 0x0D: return tag + hex2(64);
      case 0x05: return tag + hex2(91 + 40);
      case 0x04: return tag + hex2(97);                                        // 38 %
      case 0x06: return tag + hex2(152);                                       // +18.8 %
      case 0x07: return tag + hex2(140);                                       // +9.4 %
      case 0x0B: return tag + hex2(41);
      case 0x0F: return tag + hex2(37 + 40);
      case 0x11: return tag + hex2(41);                                        // 16 %
      default:   return "NO DATA";
    }
  }

  // Mode 09: VIN (3 CAN frames, numbered lines like a real ELM327), ECU name, calibration id
  if (cmd == "0902") {
    if (!engineHeaderOk()) return "NO DATA";
    const char *vin = "DESK0000000000000";                                   // obviously not a real car
    String h;
    for (int i = 0; i < 17; i++) h += hex2(vin[i]);
    return "014\r0:490201" + h.substring(0, 6) + "\r1:" + h.substring(6, 20) + "\r2:" + h.substring(20);
  }
  if (cmd == "090A") {
    if (!engineHeaderOk()) return "NO DATA";
    const char *n = "ECM-EngineControl";
    String h;
    for (int i = 0; n[i]; i++) h += hex2(n[i]);
    return "490A01" + h;
  }
  if (cmd == "0904") {
    if (!engineHeaderOk()) return "NO DATA";
    const char *n = "DESKCAL01";
    String h;
    for (int i = 0; n[i]; i++) h += hex2(n[i]);
    return "490401" + h;
  }

  // Enhanced Mode 22 temperatures used by REDLINE
  // Engine oil temp = ((A*256+B)/100)-40
  if (cmd == "221310") {
    if (!engineHeaderOk()) return "NO DATA";
    return response22("1310", hex4((simOil + 40) * 100));
  }

  // Transmission fluid temp = (SIGNED(A)*256+B)/16
  if (cmd == "221E1C") {
    if (!tcmOnline || !headerAllowed("7E1")) return "NO DATA";
    return response22("1E1C", hex4(simATF * 16));
  }

  // Gear from the transmission: D = 1..6, N = 50, R = 60, P = 70 (module 7E1 only)
  if (cmd == "221E12") {
#if STRICT_HEADERS
    if (currentHeader != "7E1") return "NO DATA";
#else
    if (!headerAllowed("7E1")) return "NO DATA";
#endif
    if (!tcmOnline) return "NO DATA";
    return response22("1E12", hex2(simGearCode));
  }

  // Additional Mazda/Ford Mode 22 simulation (from the old sketch)
  if (cmd == "22F470") {
    if (!headerAllowed("7E0")) return "NO DATA";
    return response22("F470", hex2(0) + hex4(simBoostDesiredKpa * 32) + hex4(simBoostKpa * 32));
  }
  if (cmd == "22F433") return response22("F433", hex2(simBaro));
  if (cmd == "221305") return response22("1305", hex4((simBoostDesiredKpa - simBaro) * 64));
  if (cmd == "22F477") {
    if (!headerAllowed("7E0")) return "NO DATA";
    return response22("F477", hex2(0) + hex2(0) + hex2(simIAT + 40));
  }
  if (cmd == "22F446") return response22("F446", hex2(simIAT + 50));
  if (cmd == "220522") return response22("0522", hex2(simFuelTemp + 40));
  if (cmd == "22F46F") {
    if (!headerAllowed("7E0")) return "NO DATA";
    return response22("F46F", hex2(0) + hex2(12) + hex2(13));
  }
  if (cmd == "22280A") {
    if (!headerAllowed("7E0")) return "NO DATA";
    return response22("280A", hex4(640));
  }
  if (cmd == "22DA05") {
    if (!headerAllowed("726")) return "NO DATA";
    return response22("DA05", hex2(0x80));
  }
  if (cmd == "221E7A") {
    if (!headerAllowed("761")) return "NO DATA";
    return response22("1E7A", hex2(90));
  }
  if (cmd == "221E24") {
    if (!tcmOnline || !headerAllowed("7E1")) return "NO DATA";
    return response22("1E24", hex2(1));
  }
  if (cmd == "221EE4") {
    if (!tcmOnline || !headerAllowed("7E1")) return "NO DATA";
    return response22("1EE4", hex4(180 * 50));
  }
  if (cmd == "221EC7") {
    if (!tcmOnline || !headerAllowed("7E1")) return "NO DATA";
    return response22("1EC7", hex4(200 * 50));
  }
  if (cmd == "22D901") {
    if (!headerAllowed("793")) return "NO DATA";
    return response22("D901", hex2(0) + hex2(24));
  }
  if (cmd == "22DC74") {
    if (!headerAllowed("764")) return "NO DATA";
    return response22("DC74", hex4(1250));
  }
  if (cmd == "22DD01") {
    if (!headerAllowed("7E0")) return "NO DATA";
    return response22("DD01", hex6(65432));
  }
  if (cmd == "22DAAE") {
    if (!headerAllowed("7E0")) return "NO DATA";
    String data;
    for (int i = 0; i < 8; i++) data += hex2(0);
    data += hex4(2000);
    data += hex4(5000);
    data += hex2(3);
    return response22("DAAE", data);
  }
  if (cmd == "22F411") {
    if (!headerAllowed("7E0")) return "NO DATA";
    return response22("F411", hex2(160));
  }
  if (cmd == "22093C") return response22("093C", hex4(35 * 512));
  if (cmd == "2203FB") return response22("03FB", hex2(77));
  if (cmd == "220306") return response22("0306", hex4(1200));
  if (cmd == "2203DB") return response22("03DB", hex4(500));
  if (cmd == "220301") return response22("0301", hex4(1600));
  if (cmd == "2203DF") return response22("03DF", hex4(29491));
  if (cmd == "22050F") return response22("050F", hex2(103));
  if (cmd == "2261B1") {
    if (!headerAllowed("720")) return "NO DATA";
    return response22("61B1", hex2(simFuelLiters));
  }
  if (cmd == "220307") return response22("0307", hex4(32768));
  if (cmd == "220435") return response22("0435", hex2(2));
  if (cmd == "2203E1") return response22("03E1", hex4(2));
  if (cmd == "220519") {
    if (!headerAllowed("7E0")) return "NO DATA";
    return response22("0519", hex4(1250));
  }
  if (cmd == "220542") return response22("0542", hex4(20000));
  if (cmd == "220451") return response22("0451", hex2(200));
  if (cmd == "22DAA1") {
    if (!headerAllowed("7E0")) return "NO DATA";
    return response22("DAA1", hex4(32768 + 2500) + hex4(32768 + 2700));
  }

  // Fault codes (engine ECU)
  if (cmd == "03" || cmd == "07" || cmd == "0A") {
    if (!engineHeaderOk()) return "NO DATA";
    if (cmd == "03") return dtcReply(0x43, dtcStored, nStored);
    if (cmd == "07") return dtcReply(0x47, dtcPending, nPending);
    return dtcReply(0x4A, dtcPerm, nPerm);
  }
  if (cmd == "04") {
    if (!engineHeaderOk()) return "NO DATA";
    if (car.speed > 3) return "7F0422";                // conditions not correct: a real ECU refuses while driving
    nStored = 0; nPending = 0;                         // permanent codes survive a clear, as in a real car
    milOn = false;
    dtcCleared = true;
    return "44";
  }

  return "NO DATA";
}

// ----------------------------------------------------------------------------------------------
// USB serial console
// ----------------------------------------------------------------------------------------------
void printHelp() {
  Serial.println("commands: help | auto | P N R D | speed <kmh> | thr <pct> | wot | cruise | idle | decel");
  Serial.println("          drop <hexpid> | add <hexpid> | tcm on|off | fault add P0300 | fault clear | fault reset | mil on|off | status");
}

void printStatus() {
  const char *sel = car.sel == SEL_P ? "P" : car.sel == SEL_R ? "R" : car.sel == SEL_N ? "N" : "D";
  Serial.printf("[%s] sel=%s gear=%d code=%d  %.0f km/h  %.0f rpm  thr %.0f%%  load %.0f%%  MAP %.0f  MAF %.1f  lambda %.3f (AFR %.1f)  "
                "STFT %.1f LTFT %.1f  adv %.1f  fuel %.0f%%  header %s  TCM %s%s\n",
                autoMode ? "auto" : "manual", sel, car.gear, simGearCode, car.speed, car.rpm, car.throttle * 100, car.load, car.map,
                car.maf, car.lambda, car.lambda * 14.7f, car.stft, car.ltft, car.timing, car.fuelPct, currentHeader.c_str(),
                tcmOnline ? "on" : "OFF", car.fuelCut ? "  FUEL CUT" : "");
}

void handleConsole(String line) {
  line.trim();
  String u = line;
  u.toUpperCase();
  if (u.length() == 0) return;
  if (u == "HELP" || u == "?") { printHelp(); return; }
  if (u == "AUTO") { autoMode = true; autoT0 = millis(); Serial.println("auto demo loop"); return; }
  if (u == "STATUS") { printStatus(); return; }
  if (u == "P" || u == "N" || u == "R" || u == "D") {
    autoMode = false;
    car.sel = u == "P" ? SEL_P : u == "N" ? SEL_N : u == "R" ? SEL_R : SEL_D;
    if (car.sel == SEL_D && manualSpeed < 1) manualSpeed = 0;
    Serial.printf("manual: selector %s\n", u.c_str());
    return;
  }
  if (u.startsWith("SPEED ")) { autoMode = false; manualSpeed = clampf(u.substring(6).toFloat(), 0, 200); Serial.printf("manual: speed %.0f\n", manualSpeed); return; }
  if (u.startsWith("THR "))   { autoMode = false; manualThrottle = clampf(u.substring(4).toFloat() / 100.0f, 0, 1); Serial.printf("manual: throttle %.0f%%\n", manualThrottle * 100); return; }
  if (u == "WOT")    { autoMode = false; car.sel = SEL_D; manualSpeed = 140; manualThrottle = 1.0f; Serial.println("manual: full throttle"); return; }
  if (u == "CRUISE") { autoMode = false; car.sel = SEL_D; manualSpeed = 90;  manualThrottle = 0.18f; Serial.println("manual: cruise 90"); return; }
  if (u == "IDLE")   { autoMode = false; car.sel = SEL_D; manualSpeed = 0;   manualThrottle = 0; Serial.println("manual: idle in D"); return; }
  if (u == "DECEL")  { autoMode = false; car.sel = SEL_D; manualSpeed = 20;  manualThrottle = 0; Serial.println("manual: decel (fuel cut)"); return; }
  if (u == "FAULT CLEAR") { nStored = nPending = nPerm = 0; milOn = false; Serial.println("all fault codes removed"); return; }
  if (u == "FAULT RESET") { resetFaults(); Serial.println("demo fault codes restored"); return; }
  if (u == "MIL ON" || u == "MIL OFF") { milOn = u == "MIL ON"; Serial.printf("check-engine lamp %s\n", milOn ? "on" : "off"); return; }
  if (u.startsWith("FAULT ADD ")) {
    uint16_t raw;
    if (!parseCode(u.substring(10), raw) || nStored >= DTC_MAX) { Serial.println("give a code like P0300 (list may be full)"); return; }
    dtcStored[nStored++] = raw; milOn = true;
    Serial.printf("stored codes now %d\n", nStored);
    return;
  }
  if (u == "TCM OFF") { tcmOnline = false; Serial.println("transmission stops answering"); return; }
  if (u == "TCM ON")  { tcmOnline = true;  Serial.println("transmission answers again"); return; }
  if (u.startsWith("DROP ") || u.startsWith("ADD ")) {
    bool add = u.startsWith("ADD ");
    int pid = (int)strtol(u.substring(add ? 4 : 5).c_str(), nullptr, 16);
    if (pid <= 0 || pid > 0xFF || pid % 0x20 == 0) { Serial.println("give a Mode 01 PID in hex, e.g. 24"); return; }
    supp[pid] = add;
    Serial.printf("PID %02X %s\n", pid, add ? "supported" : "no longer supported");
    return;
  }
  Serial.println("unknown command, type help");
}

// ----------------------------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("====================================");
  Serial.println("ESP32 Fake ELM327 - Mazda 2 / REDLINE");
  Serial.println("Bluetooth Classic SPP");
  Serial.println("====================================");

  initSupported();
  resetFaults();
  autoT0 = millis();
  lastStep = millis();

  if (!SerialBT.begin(BT_NAME)) {
    Serial.println("Bluetooth start failed");
    return;
  }

  delay(300);
  printBTMac();
  Serial.print("BT Name: ");
  Serial.println(BT_NAME);
  Serial.println("Ready. Configure the Gauge with this Bluetooth MAC.");
  printHelp();
}

void loop() {
  stepSim();

  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r' || c == '\n') {
      if (consoleBuffer.length() > 0) { handleConsole(consoleBuffer); consoleBuffer = ""; }
    } else if (consoleBuffer.length() < 48) {
      consoleBuffer += c;
    }
  }

  while (SerialBT.available()) {
    char c = (char)SerialBT.read();

    if (c == '\r' || c == '\n') {
      if (inputBuffer.length() > 0) {
        String response = handleCommand(inputBuffer);
        sendResponse(response);
        inputBuffer = "";
      }
    } else if (inputBuffer.length() < 64) {
      inputBuffer += c;
    } else {
      inputBuffer = "";
      sendResponse("?");
    }
  }
}
