#include "mazda_ext.h"
#include "config.h"
#include "car.h"
#include "elm327.h"

bool tcmOnline = true;

static String h2(uint32_t v) { char b[3]; snprintf(b, sizeof b, "%02X", (unsigned)(v & 0xFF)); return b; }
static String h4(uint32_t v) { char b[5]; snprintf(b, sizeof b, "%04X", (unsigned)(v & 0xFFFF)); return b; }
static String h6(uint32_t v) { char b[7]; snprintf(b, sizeof b, "%06X", (unsigned)(v & 0xFFFFFF)); return b; }

bool mazdaMode22(const String &cmd, String &out, uint16_t &module) {
  if (!cmd.startsWith("22") || cmd.length() != 6) return false;
  const String did = cmd.substring(2);
  out = "";
  module = ECU_ENGINE;

  // values in the units the old sketch used
  const int oil = (int)car.oil, atf = (int)car.atf, iat = (int)car.iat, fuelTemp = 40, baro = 100;
  const int boost = (int)car.map, boostDesired = boost + 10, fuelLiters = (int)(car.fuelPct * 0.44f);
  auto ok = [&](uint16_t m) { module = m; return elmHeaderAllowed(m); };
  auto r = [&](const String &data) { out = "62" + did + data; return true; };

  // Engine oil temp = ((A*256+B)/100)-40
  if (did == "1310") { module = ECU_ENGINE; return elmEngineAddressed() ? r(h4((oil + 40) * 100)) : true; }
  // Transmission fluid temp = (SIGNED(A)*256+B)/16
  if (did == "1E1C") return (tcmOnline && ok(ECU_TCM)) ? r(h4(atf * 16)) : true;
  // Gear from the transmission: D = 1..6, N = 50, R = 60, P = 70 (module 7E1 only)
  if (did == "1E12") {
    module = ECU_TCM;
#if STRICT_HEADERS
    if (elmTarget() != ECU_TCM) return true;
#else
    if (!elmHeaderAllowed(ECU_TCM)) return true;
#endif
    return tcmOnline ? r(h2(carGearCode())) : true;
  }
  if (did == "1E24") return (tcmOnline && ok(ECU_TCM)) ? r(h2(1)) : true;
  if (did == "1EE4") return (tcmOnline && ok(ECU_TCM)) ? r(h4(180 * 50)) : true;
  if (did == "1EC7") return (tcmOnline && ok(ECU_TCM)) ? r(h4(200 * 50)) : true;

  // Additional Mazda / Ford Mode 22 values (from the original test sketch)
  if (did == "F470") return ok(ECU_ENGINE) ? r(h2(0) + h4(boostDesired * 32) + h4(boost * 32)) : true;
  if (did == "F433") return r(h2(baro));
  if (did == "1305") return r(h4((boostDesired - baro) * 64));
  if (did == "F477") return ok(ECU_ENGINE) ? r(h2(0) + h2(0) + h2(iat + 40)) : true;
  if (did == "F446") return r(h2(iat + 50));
  if (did == "0522") return r(h2(fuelTemp + 40));
  if (did == "F46F") return ok(ECU_ENGINE) ? r(h2(0) + h2(12) + h2(13)) : true;
  if (did == "280A") return ok(ECU_ENGINE) ? r(h4(640)) : true;
  if (did == "DA05") return ok(0x726) ? r(h2(0x80)) : true;
  if (did == "1E7A") return ok(0x761) ? r(h2(90)) : true;
  if (did == "D901") return ok(0x793) ? r(h2(0) + h2(24)) : true;
  if (did == "DC74") return ok(0x764) ? r(h4(1250)) : true;
  if (did == "DD01") return ok(ECU_ENGINE) ? r(h6(65432)) : true;
  if (did == "DAAE") {
    if (!ok(ECU_ENGINE)) return true;
    String d;
    for (int i = 0; i < 8; i++) d += h2(0);
    return r(d + h4(2000) + h4(5000) + h2(3));
  }
  if (did == "F411") return ok(ECU_ENGINE) ? r(h2(160)) : true;
  if (did == "093C") return r(h4(35 * 512));
  if (did == "03FB") return r(h2(77));
  if (did == "0306") return r(h4(1200));
  if (did == "03DB") return r(h4(500));
  if (did == "0301") return r(h4(1600));
  if (did == "03DF") return r(h4(29491));
  if (did == "050F") return r(h2(103));
  if (did == "61B1") return ok(0x720) ? r(h2(fuelLiters)) : true;
  if (did == "0307") return r(h4(32768));
  if (did == "0435") return r(h2(2));
  if (did == "03E1") return r(h4(2));
  if (did == "0519") return ok(ECU_ENGINE) ? r(h4(1250)) : true;
  if (did == "0542") return r(h4(20000));
  if (did == "0451") return r(h2(200));
  if (did == "DAA1") return ok(ECU_ENGINE) ? r(h4(32768 + 2500) + h4(32768 + 2700)) : true;
  return false;
}
