#include "elm327.h"
#include "config.h"
#include "car.h"
#include "faults.h"
#include "mazda_ext.h"

// ---------------------------------------------------------------------------------------------
// state
// ---------------------------------------------------------------------------------------------
struct ElmState {
  bool echo = true, linefeeds = false, spaces = true, headers = false, caf = true;
  uint8_t proto = 0;        // 0 = automatic, else the protocol the app forced with ATSP n
  bool searched = false;    // auto mode already found the car's protocol
  bool headerDefault = true;
  uint32_t header = 0x7DF;  // ATSH value (11-bit id or 3 bytes)
  uint8_t headerDigits = 3;
  uint8_t canPriority = 0x18;
};

static ElmState st;
static int profileIdx = 0;
static bool supp[256];
static uint32_t reqCount = 0;
static unsigned long lastReqMs = 0, clearedAtMs = 0;
static float reqRate = 0;
static unsigned long rateT0 = 0;
static uint32_t rateN = 0;

const Profile &activeProfile() { return PROFILES[profileIdx]; }
int activeProfileIndex() { return profileIdx; }
bool elmSupported(uint8_t pid) { return supp[pid]; }
void elmSetSupported(uint8_t pid, bool on) { supp[pid] = on; }
uint32_t elmRequestCount() { return reqCount; }
unsigned long elmLastRequestMs() { return lastReqMs; }
float elmRequestsPerSecond() { return millis() - lastReqMs > 2000 ? 0 : reqRate; }

static void resetAdapter() {
  st = ElmState();
}

void elmSetProfile(int idx) {
  if (idx < 0 || idx >= PROFILE_COUNT) idx = 0;
  profileIdx = idx;
  const Profile &p = activeProfile();
  for (int i = 0; i < 256; i++) supp[i] = false;
  for (int i = 0; i < p.nPids; i++) supp[p.pids[i]] = true;
  carInit(p);
  faultsReset(p);
  tcmOnline = true;
  st.searched = false;
  st.headerDefault = true;
}

void elmInit(int idx) {
  resetAdapter();
  elmSetProfile(idx);
}

// ---------------------------------------------------------------------------------------------
// addressing
// ---------------------------------------------------------------------------------------------
static uint8_t carProto() { return activeProfile().protocol; }

uint16_t elmTarget() {
  const uint8_t p = carProto();
  if (!protocolIsCan(p) || st.headerDefault) return ECU_FUNCTIONAL;
  if (protocolIs29bit(p)) {
    if (st.headerDigits != 6) return 0;                      // 11-bit frames on a 29-bit bus: nobody answers
    uint8_t fmt = (st.header >> 16) & 0xFF, tgt = (st.header >> 8) & 0xFF;
    if (fmt == 0xDB) return ECU_FUNCTIONAL;
    if (fmt != 0xDA) return 0;
    return tgt == 0x10 ? ECU_ENGINE : tgt == 0x18 ? ECU_TCM : 0x700 | tgt;
  }
  if (st.headerDigits != 3) return 0;
  return (uint16_t)st.header;
}

bool elmEngineAddressed() { uint16_t t = elmTarget(); return t == ECU_FUNCTIONAL || t == ECU_ENGINE; }
bool elmHeaderAllowed(uint16_t m) { uint16_t t = elmTarget(); return t == ECU_FUNCTIONAL || t == m; }

String elmHeaderText() {
  char b[12];
  if (st.headerDefault) return protocolIs29bit(carProto()) ? "18DB33F1" : protocolIsCan(carProto()) ? "7DF" : "686AF1";
  snprintf(b, sizeof b, st.headerDigits == 3 ? "%03lX" : "%06lX", (unsigned long)st.header);
  return b;
}

// ---------------------------------------------------------------------------------------------
// formatting
// ---------------------------------------------------------------------------------------------
static void hexAppend(String &s, uint8_t v) {
  static const char *d = "0123456789ABCDEF";
  s += d[v >> 4];
  s += d[v & 15];
}

static String bytesText(const uint8_t *b, int n) {
  String s;
  s.reserve(n * 3);
  for (int i = 0; i < n; i++) {
    if (i && st.spaces) s += ' ';
    hexAppend(s, b[i]);
  }
  return s;
}

static String canIdText(uint16_t module) {
  if (protocolIs29bit(carProto())) {
    uint8_t src = module == ECU_TCM ? 0x18 : (module == ECU_ENGINE || module == ECU_FUNCTIONAL) ? 0x10 : module & 0xFF;
    uint8_t id[4] = {st.canPriority, 0xDA, 0xF1, src};
    return bytesText(id, 4);
  }
  uint16_t id = (module == ECU_FUNCTIONAL ? ECU_ENGINE : module) + 8;
  char b[5];
  snprintf(b, sizeof b, "%03X", id);
  return b;
}

static String formatCan(const Reply &r) {
  String out;
  const String id = canIdText(r.module);
  auto line = [&](const String &l) { if (out.length()) out += '\r'; out += l; };
  auto frame = [&](const uint8_t *f) { line((st.headers ? id + (st.spaces ? " " : "") : String()) + bytesText(f, 8)); };

  for (int m = 0; m < r.count; m++) {
    const ObdMsg &msg = r.msg[m];
    if (msg.n <= 7) {                                          // single frame
      if (st.caf && !st.headers) { line(bytesText(msg.b, msg.n)); continue; }
      uint8_t f[8] = {0};
      f[0] = msg.n;
      memcpy(f + 1, msg.b, msg.n);
      frame(f);
      continue;
    }
    if (st.caf && !st.headers) {                               // multi-frame, ELM327 style numbered lines
      char head[6];
      snprintf(head, sizeof head, "%03X", msg.n);
      line(head);
      int pos = 0, idx = 0;
      while (pos < msg.n) {
        int chunk = idx == 0 ? 6 : 7;
        uint8_t part[7] = {0};
        for (int i = 0; i < chunk && pos + i < msg.n; i++) part[i] = msg.b[pos + i];
        char tag[4];
        snprintf(tag, sizeof tag, "%X:", idx & 15);
        line(String(tag) + (st.spaces ? " " : "") + bytesText(part, chunk));
        pos += chunk;
        idx++;
      }
      continue;
    }
    uint8_t f[8] = {0};                                        // raw ISO-TP frames
    f[0] = 0x10 | ((msg.n >> 8) & 0x0F);
    f[1] = msg.n & 0xFF;
    memcpy(f + 2, msg.b, 6);
    frame(f);
    int pos = 6, seq = 1;
    while (pos < msg.n) {
      memset(f, 0, 8);
      f[0] = 0x20 | (seq & 15);
      for (int i = 0; i < 7 && pos + i < msg.n; i++) f[1 + i] = msg.b[pos + i];
      frame(f);
      pos += 7;
      seq++;
    }
  }
  return out;
}

static uint8_t crcJ1850(const uint8_t *b, int n) {
  uint8_t crc = 0xFF;
  for (int i = 0; i < n; i++) {
    crc ^= b[i];
    for (int k = 0; k < 8; k++) crc = crc & 0x80 ? (uint8_t)((crc << 1) ^ 0x1D) : (uint8_t)(crc << 1);
  }
  return ~crc;
}

static String formatKline(const Reply &r) {
  String out;
  const uint8_t p = carProto();
  for (int m = 0; m < r.count; m++) {
    const ObdMsg &msg = r.msg[m];
    uint8_t f[48];
    int n = 0;
    if (st.headers) {
      if (p == PROTO_ISO9141)                               { f[n++] = 0x48; f[n++] = 0x6B; f[n++] = 0x10; }
      else if (p == PROTO_KWP_FAST || p == PROTO_KWP_SLOW)  { f[n++] = 0x80 | msg.n; f[n++] = 0xF1; f[n++] = 0x10; }
      else if (p == PROTO_J1850_PWM)                        { f[n++] = 0x41; f[n++] = 0x6B; f[n++] = 0x10; }
      else                                                  { f[n++] = 0x48; f[n++] = 0x6B; f[n++] = 0x10; }
    }
    memcpy(f + n, msg.b, msg.n);
    n += msg.n;
    if (st.headers) {
      uint8_t cs = 0;
      if (p == PROTO_J1850_PWM || p == PROTO_J1850_VPW) cs = crcJ1850(f, n);
      else for (int i = 0; i < n; i++) cs += f[i];
      f[n++] = cs;
    }
    if (out.length()) out += '\r';
    out += bytesText(f, n);
  }
  return out;
}

static String formatReply(const Reply &r) {
  if (r.kind == Reply::NODATA || r.count == 0) return "NO DATA";
  return protocolIsCan(carProto()) ? formatCan(r) : formatKline(r);
}

// ---------------------------------------------------------------------------------------------
// Mode 01
// ---------------------------------------------------------------------------------------------
static inline uint8_t pct255(float pct) { return (uint8_t)constrain(lroundf(pct * 255.0f / 100.0f), 0, 255); }

static void put16(uint8_t *o, int &n, uint32_t v) { o[n++] = (v >> 8) & 0xFF; o[n++] = v & 0xFF; }

// Data bytes of one Mode 01 PID (without the 41 / PID bytes). Returns -1 when not answered.
static int pid01(uint8_t pid, uint8_t *o) {
  const Profile &P = activeProfile();
  int n = 0;

  if (pid % 0x20 == 0) {                                     // supported-PID bitmask pages
    // A page answers if any PID at or after it is supported; its last bit says "the next page
    // exists", so apps can walk 00 -> 20 -> 40 ... even across empty pages.
    auto anyFrom = [&](int from) { for (int q = from; q < 256; q++) if (supp[q]) return true; return false; };
    if (pid != 0 && !anyFrom(pid + 1)) return -1;
    uint32_t m = 0;
    for (int i = 1; i <= 32; i++) {
      int q = pid + i;
      bool on = i == 32 ? anyFrom(pid + 33) : (q < 256 && supp[q]);
      if (on) m |= 1UL << (32 - i);
    }
    o[0] = m >> 24; o[1] = m >> 16; o[2] = m >> 8; o[3] = m;
    return 4;
  }

  const bool diesel = P.engine == ENG_DIESEL;
  // always answered (the old test sketch answered these too, even when not in the list)
  if (pid == 0x01) {
    o[0] = (faults.mil ? 0x80 : 0) | (faults.nStored & 0x7F);
    if (P.engine == ENG_EV)  { o[1] = 0x04; o[2] = 0x00; o[3] = 0x00; }
    else if (diesel)         { o[1] = 0x0F; o[2] = 0xE9; o[3] = faults.cleared ? 0x40 : 0x00; }   // PM filter not ready
    else                     { o[1] = 0x07; o[2] = 0xE5; o[3] = faults.cleared ? 0x04 : 0x00; }   // EVAP not ready
    return 4;
  }
  if (pid == 0x21) { put16(o, n, faults.mil ? 46 : 0); return n; }
  if (pid == 0x30) { o[0] = faults.cleared ? 0 : 41; return 1; }
  if (pid == 0x31) { put16(o, n, faults.cleared ? 0 : 312); return n; }
  if (!supp[pid]) return -1;

  const bool turning = car.engineOn && car.rpm > 300;
  switch (pid) {
    case 0x03: o[0] = !turning ? 0 : (car.fuelCut || car.throttle > 0.85f) ? 4 : car.coolant < 60 ? 1 : 2; o[1] = 0; return 2;
    case 0x04: o[0] = pct255(car.load); return 1;
    case 0x05: o[0] = (uint8_t)(lroundf(car.coolant) + 40); return 1;
    case 0x06: o[0] = (uint8_t)lroundf(car.stft * 128.0f / 100.0f + 128.0f); return 1;
    case 0x07: o[0] = (uint8_t)lroundf(car.ltft * 128.0f / 100.0f + 128.0f); return 1;
    case 0x0B: o[0] = (uint8_t)constrain(lroundf(car.map), 0, 255); return 1;
    case 0x0C: put16(o, n, (uint32_t)(car.rpm * 4)); return n;
    case 0x0D: o[0] = (uint8_t)constrain(lroundf(car.speed), 0, 255); return 1;
    case 0x0E: o[0] = (uint8_t)constrain(lroundf((car.timing + 64.0f) * 2.0f), 0, 255); return 1;
    case 0x0F: o[0] = (uint8_t)(lroundf(car.iat) + 40); return 1;
    case 0x10: put16(o, n, (uint32_t)lroundf(car.maf * 100.0f)); return n;
    case 0x11: o[0] = (uint8_t)lroundf(car.throttle * 255.0f); return 1;
    case 0x13: o[0] = 0x03; return 1;                                          // bank 1 sensors 1 and 2
    case 0x14: {                                                               // narrow-band front O2 + trim
      float v = !turning ? 0.45f : car.fuelCut ? 0.05f : car.lambda < 0.95f ? 0.85f : 0.45f + 0.38f * sinf(millis() / 160.0f);
      o[0] = (uint8_t)lroundf(v * 200.0f);
      o[1] = (uint8_t)lroundf(car.stft * 128.0f / 100.0f + 128.0f);
      return 2;
    }
    case 0x15: o[0] = (uint8_t)lroundf((car.lambda > 1.15f ? 0.08f : car.rpm < 400 ? 0.45f : 0.70f) * 200.0f); o[1] = 0xFF; return 2;
    case 0x1C: o[0] = P.obdStandard; return 1;
    case 0x1F: put16(o, n, millis() / 1000); return n;
    case 0x23: put16(o, n, (uint32_t)lroundf(car.railKpa / 10.0f)); return n;
    case 0x24: put16(o, n, lroundf(min(car.lambda, 1.999f) * 32768.0f)); put16(o, n, lroundf((0.45f + (car.lambda - 1.0f) * 2.0f) * 8192.0f + 8192.0f)); return n;
    case 0x2C: o[0] = pct255(car.egrPct); return 1;
    case 0x2E: o[0] = pct255(turning ? 18 : 0); return 1;
    case 0x2F: o[0] = pct255(car.fuelPct); return 1;
    case 0x32: put16(o, n, (uint16_t)(int16_t)(-120 * 4)); return n;           // EVAP vapour pressure, Pa/4
    case 0x33: o[0] = (uint8_t)car.baro; return 1;
    case 0x34: put16(o, n, lroundf(min(car.lambda, 1.999f) * 32768.0f)); put16(o, n, lroundf(((car.lambda - 1.0f) * 6.0f + 128.0f) * 256.0f)); return n;
    case 0x3C: put16(o, n, lroundf((car.catTemp + 40.0f) * 10.0f)); return n;
    case 0x3E: put16(o, n, lroundf((car.catTemp - 60.0f + 40.0f) * 10.0f)); return n;
    case 0x42: put16(o, n, lroundf(car.volt * 1000.0f)); return n;
    case 0x43: put16(o, n, lroundf(car.load * 255.0f / 100.0f)); return n;
    case 0x44: put16(o, n, lroundf(32768.0f * (car.throttle > 0.85f ? 0.88f : 1.0f))); return n;
    case 0x45: o[0] = pct255(car.throttle * 90.0f); return 1;
    case 0x46: o[0] = (uint8_t)(lroundf(car.ambient) + 40); return 1;
    case 0x47: o[0] = pct255(15.0f + car.throttle * 80.0f); return 1;
    case 0x49: o[0] = pct255(15.0f + car.throttle * 65.0f); return 1;
    case 0x4A: o[0] = pct255(7.5f + car.throttle * 32.5f); return 1;
    case 0x4C: o[0] = pct255(car.throttle * 85.0f); return 1;
    case 0x4D: put16(o, n, faults.mil ? 128 : 0); return n;
    case 0x4E: put16(o, n, faults.cleared ? (millis() - clearedAtMs) / 60000 : 5432); return n;
    case 0x51: o[0] = P.fuelType; return 1;
    case 0x53: put16(o, n, lroundf(car.baro * 200.0f)); return n;
    case 0x5B: o[0] = pct255(car.battSoc); return 1;
    case 0x5C: o[0] = (uint8_t)(lroundf(car.oil) + 40); return 1;
    case 0x5E: put16(o, n, lroundf(car.fuelRateLh * 20.0f)); return n;
    case 0x62: o[0] = (uint8_t)(lroundf(car.torquePct) + 125); return 1;
    case 0x63: put16(o, n, P.refTorqueNm); return n;
    case 0x7C:                                                                 // DPF bank 1 inlet / outlet
      o[n++] = 0x03;
      put16(o, n, lroundf((car.dpfTemp + 40.0f) * 10.0f));
      put16(o, n, lroundf((car.dpfTemp - 25.0f + 40.0f) * 10.0f));
      for (int i = 0; i < 4; i++) o[n++] = 0;
      return n;
    case 0xA6: {
      uint32_t v = (uint32_t)lroundf(car.odoKm * 10.0f);
      o[0] = v >> 24; o[1] = v >> 16; o[2] = v >> 8; o[3] = v;
      return 4;
    }
    default: return -1;
  }
}

// "010C" or, on CAN, up to six PIDs at once ("010C0D05"): one answer message with every PID.
static void mode01(const String &cmd, Reply &r) {
  if (!elmEngineAddressed()) return;                       // the transmission does not answer engine PIDs
  const int count = (cmd.length() - 2) / 2;
  if (count < 1 || count > 6 || (count > 1 && !protocolIsCan(carProto()))) return;
  ObdMsg tmp;
  tmp.b[tmp.n++] = 0x41;
  for (int i = 0; i < count; i++) {
    uint8_t pid = (uint8_t)strtol(cmd.substring(2 + i * 2, 4 + i * 2).c_str(), nullptr, 16);
    uint8_t data[12];
    int len = pid01(pid, data);
    if (len < 0) continue;
    if (tmp.n + 1 + len > (int)sizeof(tmp.b)) break;
    tmp.b[tmp.n++] = pid;
    memcpy(tmp.b + tmp.n, data, len);
    tmp.n += len;
  }
  if (tmp.n > 1) r.add() = tmp;
}

// ---------------------------------------------------------------------------------------------
// other services
// ---------------------------------------------------------------------------------------------
static void freezeFrame(uint8_t pid, Reply &r) {          // Mode 02, frame 00: a warm cruise
  ObdMsg &m = r.add();
  m.b[m.n++] = 0x42; m.b[m.n++] = pid; m.b[m.n++] = 0x00;
  auto b = [&](uint8_t v) { m.b[m.n++] = v; };
  switch (pid) {
    case 0x02: b(faults.nStored ? faults.stored[0] >> 8 : 0); b(faults.nStored ? faults.stored[0] & 0xFF : 0); return;
    case 0x0C: b((2150 * 4) >> 8); b((2150 * 4) & 0xFF); break;
    case 0x0D: b(64); break;
    case 0x05: b(91 + 40); break;
    case 0x04: b(97); break;
    case 0x06: b(152); break;
    case 0x07: b(140); break;
    case 0x0B: b(41); break;
    case 0x0F: b(37 + 40); break;
    case 0x11: b(41); break;
    default:   r = Reply(); return;
  }
  if (!faults.nStored) r = Reply();
}

static void dtcList(uint8_t svc, const uint16_t *list, int n, Reply &r) {
  if (protocolIsCan(carProto())) {                         // count byte, then two bytes per code
    ObdMsg &m = r.add();
    m.b[m.n++] = svc;
    m.b[m.n++] = (uint8_t)n;
    for (int i = 0; i < n; i++) { m.b[m.n++] = list[i] >> 8; m.b[m.n++] = list[i] & 0xFF; }
    return;
  }
  int i = 0;                                               // K-line / J1850: three codes per message
  do {
    ObdMsg &m = r.add();
    m.b[m.n++] = svc;
    for (int k = 0; k < 3; k++, i++) {
      m.b[m.n++] = i < n ? list[i] >> 8 : 0;
      m.b[m.n++] = i < n ? list[i] & 0xFF : 0;
    }
  } while (i < n && r.count < 6);
}

static void textInfo(uint8_t pid, const char *text, int len, Reply &r) {   // Mode 09 on CAN
  ObdMsg &m = r.add();
  m.b[m.n++] = 0x49; m.b[m.n++] = pid; m.b[m.n++] = 0x01;
  for (int i = 0; i < len; i++) m.b[m.n++] = (uint8_t)(i < (int)strlen(text) ? text[i] : 0);
}

static void mode09(uint8_t pid, Reply &r) {
  const Profile &P = activeProfile();
  const bool can = protocolIsCan(carProto());
  if (pid == 0x00) {
    uint32_t mask = can ? (0x40000000UL | 0x10000000UL | (P.ecuName[0] ? 0x00400000UL : 0)) : 0x40000000UL;
    ObdMsg &m = r.add();
    m.b[m.n++] = 0x49; m.b[m.n++] = 0x00;
    m.b[m.n++] = mask >> 24; m.b[m.n++] = mask >> 16; m.b[m.n++] = mask >> 8; m.b[m.n++] = mask;
    return;
  }
  if (pid == 0x02) {
    if (can) { textInfo(0x02, P.vin, 17, r); return; }
    uint8_t padded[20] = {0};                              // K-line: five messages of four bytes
    memcpy(padded + 3, P.vin, 17);
    for (int i = 0; i < 5; i++) {
      ObdMsg &m = r.add();
      m.b[m.n++] = 0x49; m.b[m.n++] = 0x02; m.b[m.n++] = i + 1;
      for (int k = 0; k < 4; k++) m.b[m.n++] = padded[i * 4 + k];
    }
    return;
  }
  if (!can) return;
  if (pid == 0x04) { textInfo(0x04, "GHOSTCAL01", 16, r); return; }
  if (pid == 0x0A && P.ecuName[0]) { textInfo(0x0A, P.ecuName, 20, r); return; }
}

// ---------------------------------------------------------------------------------------------
// request dispatch
// ---------------------------------------------------------------------------------------------
static bool isHex(const String &s) {
  for (unsigned i = 0; i < s.length(); i++) if (!isxdigit((unsigned char)s[i])) return false;
  return s.length() >= 2;
}

static void hexToMsg(const String &hex, ObdMsg &m) {
  m.n = 0;
  for (unsigned i = 0; i + 1 < hex.length() && m.n < sizeof(m.b); i += 2)
    m.b[m.n++] = (uint8_t)strtol(hex.substring(i, i + 2).c_str(), nullptr, 16);
}

static String obdRequest(String cmd) {
  const Profile &P = activeProfile();
  const uint8_t proto = P.protocol;

  // response-count digit: an odd number of hex digits means the last one is "stop after N answers"
  bool counted = false;
  if (cmd.length() >= 3 && (cmd.length() % 2) == 1) { cmd.remove(cmd.length() - 1); counted = true; }
#if EMULATE_ELM_LATENCY
  delay(counted ? 10 : 55);
#endif
  (void)counted;

  // the adapter cannot reach the car
  if (st.proto && st.proto != proto) {
    if (protocolIsCan(st.proto)) return "CAN ERROR";
    if (st.proto >= PROTO_ISO9141 && st.proto <= PROTO_KWP_FAST) return "BUS INIT: ...ERROR";
    return "NO DATA";
  }
  String prefix;
#if EMULATE_SEARCHING
  if (!st.proto && !st.searched) prefix = "SEARCHING...\r";
#endif
  if (car.ign == IGN_OFF) {
    if (!st.proto && !st.searched) return prefix + "UNABLE TO CONNECT";
    if (proto >= PROTO_ISO9141 && proto <= PROTO_KWP_FAST) return "BUS INIT: ...ERROR";
    return "NO DATA";
  }
  st.searched = true;

  Reply r;
  r.module = elmTarget() == ECU_FUNCTIONAL ? ECU_ENGINE : elmTarget();
  const String svc = cmd.substring(0, 2);

  if (svc == "01") mode01(cmd, r);
  else if (svc == "02" && cmd.length() == 6 && cmd.endsWith("00")) {
    if (elmEngineAddressed()) freezeFrame((uint8_t)strtol(cmd.substring(2, 4).c_str(), nullptr, 16), r);
  }
  else if (cmd == "03" || cmd == "07" || cmd == "0A") {
    if (elmEngineAddressed()) {
      if (cmd == "03") dtcList(0x43, faults.stored, faults.nStored, r);
      else if (cmd == "07") dtcList(0x47, faults.pending, faults.nPending, r);
      else if (protocolIsCan(proto)) dtcList(0x4A, faults.permanent, faults.nPerm, r);
    }
  }
  else if (cmd == "04") {
    if (elmEngineAddressed()) {
      ObdMsg &m = r.add();
      if (car.speed > 3) { m.b[0] = 0x7F; m.b[1] = 0x04; m.b[2] = 0x22; m.n = 3; }   // refused while driving
      else {
        faults.nStored = faults.nPending = 0;            // permanent codes survive a clear
        faults.mil = false;
        faults.cleared = true;
        clearedAtMs = millis();
        m.b[0] = 0x44; m.n = 1;
      }
    }
  }
  else if (svc == "09" && cmd.length() == 4) {
    if (elmEngineAddressed()) mode09((uint8_t)strtol(cmd.substring(2).c_str(), nullptr, 16), r);
  }
  else if (P.mazdaExt) {
    String hex;
    uint16_t module;
    if (mazdaMode22(cmd, hex, module) && hex.length()) {
      r.module = module;
      hexToMsg(hex, r.add());
    }
  }
  return prefix + formatReply(r);
}

static String atCommand(const String &c) {   // c = upper case, no spaces, without the "AT"
  const Profile &P = activeProfile();
  auto flag = [&](const char *name, bool &v) -> bool {
    size_t l = strlen(name);
    if (c.length() == l + 1 && c.startsWith(name) && (c[l] == '0' || c[l] == '1')) { v = c[l] == '1'; return true; }
    return false;
  };

  if (c == "Z" || c == "WS") { resetAdapter(); return ELM_ID_STRING; }
  if (c == "D") { resetAdapter(); return "OK"; }
  if (c == "I") return ELM_ID_STRING;
  if (c == "@1") return "OBDII to RS232 Interpreter";
  if (c == "@2") return "?";
  if (c == "RV") return String(car.volt, 1) + "V";
  if (c == "IGN") return car.ign == IGN_OFF ? "OFF" : "ON";
  if (c == "PC") { st.searched = false; return "OK"; }
  if (c == "MA" || c.startsWith("MR") || c.startsWith("MT")) return "?";   // bus monitoring is not simulated
  if (flag("E", st.echo) || flag("L", st.linefeeds) || flag("S", st.spaces) || flag("H", st.headers) || flag("CAF", st.caf))
    return "OK";

  if (c == "DP") return st.proto ? protocolName(st.proto) : String("AUTO, ") + protocolName(P.protocol);
  if (c == "DPN") return st.proto ? String(st.proto) : "A" + String(P.protocol);

  if (c.startsWith("SP") || c.startsWith("TP")) {          // SP n, SP An, TP n, TP An
    String a = c.substring(2);
    bool autoFallback = c.startsWith("TP") || a.startsWith("A");
    if (a.startsWith("A")) a.remove(0, 1);
    if (a.length() != 1 || !isDigit(a[0])) return "?";
    uint8_t n = a[0] - '0';
    st.proto = (n == 0 || autoFallback) ? 0 : n;
    st.searched = false;
    return "OK";
  }
  if (c.startsWith("SH")) {
    String h = c.substring(2);
    if ((h.length() != 3 && h.length() != 6) || !isHex(h + "0")) return "?";
    st.header = strtoul(h.c_str(), nullptr, 16);
    st.headerDigits = h.length();
    st.headerDefault = false;
    return "OK";
  }
  if (c.startsWith("CP") && c.length() == 4) { st.canPriority = strtoul(c.substring(2).c_str(), nullptr, 16); return "OK"; }
  // ATAT ATAL ATST ATCFC ATCRA ATFCSH ATM ATLP ATBI ATKW ... accepted, nothing to model
  return "OK";
}

static String process(String cmd) {
  cmd.replace(" ", "");
  cmd.replace("\t", "");
  cmd.toUpperCase();
  if (cmd.length() == 0) return "";
  if (cmd.startsWith("AT")) return atCommand(cmd.substring(2));
  if (!isHex(cmd)) return "?";
  return obdRequest(cmd);
}

String elmHandleLine(const String &raw) {
  unsigned long now = millis();
  reqCount++;
  lastReqMs = now;
  rateN++;
  if (now - rateT0 >= 1000) {
    reqRate = rateN * 1000.0f / (now - rateT0);
    rateN = 0;
    rateT0 = now;
  }
#if LOG_TRAFFIC
  Serial.print("RX: "); Serial.println(raw);
#endif
  carStep();
  String out;
  if (st.echo) out = raw + "\r";
  String body = process(raw);
  if (body.length()) out += body + "\r";
  out += "\r>";
  if (st.linefeeds) out.replace("\r", "\r\n");
#if LOG_TRAFFIC
  Serial.print("TX: "); Serial.println(body);
#endif
  return out;
}
