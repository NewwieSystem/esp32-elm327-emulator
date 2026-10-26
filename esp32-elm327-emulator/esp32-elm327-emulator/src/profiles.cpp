#include "profiles.h"

// Gear ratios ([0] unused). Approximate values for a typical box of each kind - close enough that
// rpm, speed and gear move together like a real car.
static const float R_MAZDA6AT[]  = {0, 3.487f, 1.992f, 1.449f, 1.000f, 0.707f, 0.600f};
static const float R_DIESEL6AT[] = {0, 3.600f, 2.090f, 1.488f, 1.000f, 0.687f, 0.580f};
static const float R_TURBO6AT[]  = {0, 4.150f, 2.370f, 1.560f, 1.160f, 0.860f, 0.690f};
static const float R_OLD4AT[]    = {0, 2.800f, 1.550f, 1.000f, 0.700f};
static const float R_NONE[]      = {0, 1.0f};

// ---- Mode 01 PID sets ----------------------------------------------------------------------
// Modern gasoline car on CAN: a broad, typical set.
static const uint8_t P_GENERIC[] = {0x03, 0x04, 0x05, 0x06, 0x07, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x13,
                                    0x15, 0x1C, 0x1F, 0x21, 0x24, 0x2E, 0x2F, 0x30, 0x31, 0x33, 0x3C, 0x42, 0x43,
                                    0x44, 0x45, 0x46, 0x47, 0x49, 0x4A, 0x4C, 0x51, 0x5C, 0x5E, 0xA6};
// Mazda 2 SKYACTIV-G 1.3: the list from the REDLINE test sketch (no 0x46 ambient, like the real car).
static const uint8_t P_MAZDA2[]  = {0x04, 0x05, 0x06, 0x07, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11,
                                    0x15, 0x1F, 0x23, 0x24, 0x2F, 0x33, 0x3C, 0x42, 0x44, 0x49};
// 2nd-generation Prius: the generic list a 2007 car reports (no fuel level, no 5B).
static const uint8_t P_PRIUS[]   = {0x03, 0x04, 0x05, 0x06, 0x07, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x13, 0x15,
                                    0x1C, 0x1F, 0x21, 0x24, 0x2E, 0x30, 0x31, 0x32, 0x33, 0x3C, 0x3E, 0x42, 0x43,
                                    0x44, 0x45, 0x46, 0x47, 0x49, 0x4A, 0x4C, 0x4D, 0x4E, 0x53};
// Common-rail diesel: boost, rail pressure, EGR, torque, DPF temperature; no O2 / fuel trims.
static const uint8_t P_DIESEL[]  = {0x04, 0x05, 0x0B, 0x0C, 0x0D, 0x0F, 0x10, 0x11, 0x1C, 0x1F, 0x21, 0x23, 0x2C,
                                    0x30, 0x31, 0x33, 0x42, 0x46, 0x49, 0x4A, 0x5E, 0x62, 0x63, 0x7C};
static const uint8_t P_TURBO[]   = {0x03, 0x04, 0x05, 0x06, 0x07, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x13,
                                    0x15, 0x1C, 0x1F, 0x21, 0x23, 0x24, 0x2F, 0x30, 0x31, 0x33, 0x3C, 0x42, 0x43,
                                    0x44, 0x45, 0x46, 0x49, 0x4A, 0x51, 0x5C, 0x62, 0x63};
// Late-90s / early-2000s cars: the short original OBD-II list.
static const uint8_t P_OLD_MAF[] = {0x03, 0x04, 0x05, 0x06, 0x07, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x13,
                                    0x14, 0x15, 0x1C};
static const uint8_t P_OLD_KWP[] = {0x03, 0x04, 0x05, 0x06, 0x07, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x13,
                                    0x14, 0x15, 0x1C, 0x1F, 0x21};
static const uint8_t P_OLD_MAP[] = {0x03, 0x04, 0x05, 0x06, 0x07, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x11, 0x13,
                                    0x14, 0x15, 0x1C};
// Electric car: very little generic data.
static const uint8_t P_EV[]      = {0x0D, 0x1C, 0x42, 0x46, 0x51, 0x5B, 0xA6};

#define N(a) (uint8_t)(sizeof(a) / sizeof(a[0]))

const Profile PROFILES[] = {
  {"generic", "Generic gasoline", "Most 2008+ gasoline cars (CAN)",
   PROTO_CAN11_500, ENG_GAS, TR_AUTO, 1.5f, 750, 6500, 190, 6, R_MAZDA6AT, 38.8f, P_GENERIC, N(P_GENERIC),
   6, 1, 0, "GHSTCAR0000000001", "ECM-EngineControl", {0x0171, 0x0420, 0}, 0x0301, false, false},

  {"mazda2", "Mazda 2 SKYACTIV-G", "Mazda 2 / 3 / CX-3 SKYACTIV-G, 6AT (REDLINE test car)",
   PROTO_CAN11_500, ENG_GAS, TR_AUTO, 1.298f, 780, 6400, 175, 6, R_MAZDA6AT, 38.8f, P_MAZDA2, N(P_MAZDA2),
   6, 1, 0, "DESK0000000000000", "ECM-EngineControl", {0x0171, 0x0420, 0}, 0x0301, true, true},

  {"prius", "Toyota Prius hybrid", "Toyota Prius gen 2 and similar Toyota hybrids (CAN)",
   PROTO_CAN11_500, ENG_HYBRID, TR_CVT, 1.5f, 1000, 5000, 170, 1, R_NONE, 0, P_PRIUS, N(P_PRIUS),
   1, 17, 0, "GHSTCAR0000000003", "ECM-EngineControl", {0x0A80, 0, 0}, 0x0171, false, false},

  {"diesel", "Diesel pickup", "Common-rail diesel pickups and SUVs, 2012+ (CAN)",
   PROTO_CAN11_500, ENG_DIESEL, TR_AUTO, 2.8f, 750, 4200, 175, 6, R_DIESEL6AT, 29.3f, P_DIESEL, N(P_DIESEL),
   6, 4, 500, "GHSTCAR0000000004", "ECM-EngineControl", {0x0087, 0x2463, 0}, 0x0401, false, false},

  {"turbo29", "Turbo gas 29-bit", "Turbo gasoline car on 29-bit CAN (test profile)",
   PROTO_CAN29_500, ENG_GAS_TURBO, TR_AUTO, 1.4f, 700, 6500, 220, 6, R_TURBO6AT, 31.9f, P_TURBO, N(P_TURBO),
   6, 1, 250, "GHSTCAR0000000005", "ECM-EngineControl", {0x0299, 0, 0}, 0x0300, false, false},

  {"honda-iso", "Honda ISO 9141-2", "Honda / Mitsubishi / many Asian cars 1996-2007 (K-line)",
   PROTO_ISO9141, ENG_GAS, TR_AUTO, 1.7f, 700, 6800, 180, 4, R_OLD4AT, 40.0f, P_OLD_MAF, N(P_OLD_MAF),
   1, 1, 0, "GHSTCAR0000000006", "", {0x0420, 0, 0}, 0, false, false},

  {"toyota-kwp", "Toyota KWP2000", "Toyota / Suzuki / Hyundai / Kia 1996-2006 (KWP2000 fast init)",
   PROTO_KWP_FAST, ENG_GAS, TR_AUTO, 1.8f, 700, 6400, 180, 4, R_OLD4AT, 38.0f, P_OLD_KWP, N(P_OLD_KWP),
   7, 1, 0, "GHSTCAR0000000007", "", {0x0171, 0, 0}, 0, false, false},

  {"ford-pwm", "Ford J1850 PWM", "US Ford / Lincoln / Mazda 1996-2004",
   PROTO_J1850_PWM, ENG_GAS, TR_AUTO, 4.0f, 650, 5500, 170, 4, R_OLD4AT, 28.0f, P_OLD_MAF, N(P_OLD_MAF),
   1, 1, 0, "1GHSTCAR000000008", "", {0x0401, 0, 0}, 0, false, false},

  {"gm-vpw", "GM J1850 VPW", "US GM / Chrysler 1996-2005",
   PROTO_J1850_VPW, ENG_GAS, TR_AUTO, 5.3f, 600, 5600, 170, 4, R_OLD4AT, 24.0f, P_OLD_MAP, N(P_OLD_MAP),
   1, 1, 0, "1GHSTCAR000000009", "", {0x0300, 0, 0}, 0, false, false},

  {"ev", "Electric car", "EVs: almost no generic OBD data (CAN)",
   PROTO_CAN11_500, ENG_EV, TR_EV, 0, 0, 0, 160, 1, R_NONE, 0, P_EV, N(P_EV),
   1, 8, 0, "GHSTCAR0000000010", "VCU-VehicleControl", {0x0AA6, 0, 0}, 0, false, false},
};
const uint8_t PROFILE_COUNT = N(PROFILES);

const char *protocolName(uint8_t p) {
  switch (p) {
    case PROTO_J1850_PWM: return "SAE J1850 PWM";
    case PROTO_J1850_VPW: return "SAE J1850 VPW";
    case PROTO_ISO9141:   return "ISO 9141-2";
    case PROTO_KWP_SLOW:  return "ISO 14230-4 (KWP 5BAUD)";
    case PROTO_KWP_FAST:  return "ISO 14230-4 (KWP FAST)";
    case PROTO_CAN11_500: return "ISO 15765-4 (CAN 11/500)";
    case PROTO_CAN29_500: return "ISO 15765-4 (CAN 29/500)";
    case PROTO_CAN11_250: return "ISO 15765-4 (CAN 11/250)";
    case PROTO_CAN29_250: return "ISO 15765-4 (CAN 29/250)";
    default:              return "AUTO";
  }
}

bool protocolIsCan(uint8_t p) { return p >= PROTO_CAN11_500 && p <= PROTO_CAN29_250; }
bool protocolIs29bit(uint8_t p) { return p == PROTO_CAN29_500 || p == PROTO_CAN29_250; }

int findProfile(const String &key) {
  String k = key;
  k.trim();
  k.toLowerCase();
  if (k.length() == 0) return -1;
  if (isDigit(k[0])) {
    int i = k.toInt();
    return (i >= 0 && i < PROFILE_COUNT) ? i : -1;
  }
  for (int i = 0; i < PROFILE_COUNT; i++)
    if (k == PROFILES[i].id) return i;
  return -1;
}
