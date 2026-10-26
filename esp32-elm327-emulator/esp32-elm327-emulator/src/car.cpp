#include "car.h"
#include "config.h"

Car car;
bool  autoMode = true;
float manualSpeed = 0, manualThrottle = 0;

static const Profile *P = &PROFILES[0];
static unsigned long autoT0 = 0, lastStep = 0, shiftUntil = 0;
static float simT = 0;

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static float toward(float cur, float tgt, float dt, float tau) { return cur + (tgt - cur) * (dt / (tau + dt)); }

void carInit(const Profile &p) {
  P = &p;
  Ignition keep = car.ign;
  car = Car();
  car.ign = keep;
  car.rpm = P->idleRpm;
  car.engineOn = P->engine != ENG_EV && car.ign == IGN_RUN;
  autoT0 = lastStep = millis();
}

void carAuto() { autoMode = true; autoT0 = millis(); }
void carSetSelector(Sel s) { autoMode = false; car.sel = s; }
void carSetTarget(float kmh, float thr) {
  autoMode = false;
  manualSpeed = clampf(kmh, 0, 250);
  manualThrottle = clampf(thr, 0, 1);
}
void carSetIgnition(Ignition i) { car.ign = i; }

const char *ignName(Ignition i) { return i == IGN_OFF ? "OFF" : i == IGN_ON ? "ON" : "RUN"; }
const char *carSelName() {
  static char buf[4];
  if (car.sel == SEL_P) return "P";
  if (car.sel == SEL_R) return "R";
  if (car.sel == SEL_N) return "N";
  if (P->trans != TR_AUTO) return "D";
  snprintf(buf, sizeof buf, "D%d", car.gear);
  return buf;
}

int carGearCode() {
  switch (car.sel) {
    case SEL_P: return GEAR_RAW_P;
    case SEL_R: return GEAR_RAW_R;
    case SEL_N: return GEAR_RAW_N;
    default:    return car.gear;
  }
}

// The 62 s demo: P -> N -> R -> N -> D, accelerate, full throttle, cruise, coast (fuel cut), brake, stop.
static void autoScript(float t, Sel &sel, float &tgtSpeed, float &thr) {
  float top = min(165.0f, (float)P->vmax);
  if (t < 5)        { sel = SEL_P; tgtSpeed = 0;   thr = 0; }
  else if (t < 7)   { sel = SEL_N; tgtSpeed = 0;   thr = 0; }
  else if (t < 10)  { sel = SEL_R; tgtSpeed = 6;   thr = 0.12f; }
  else if (t < 12)  { sel = SEL_N; tgtSpeed = 0;   thr = 0; }
  else if (t < 15)  { sel = SEL_D; tgtSpeed = 0;   thr = 0; }
  else if (t < 30)  { sel = SEL_D; tgtSpeed = 90;  thr = 0.35f; }
  else if (t < 36)  { sel = SEL_D; tgtSpeed = top; thr = 1.0f; }
  else if (t < 45)  { sel = SEL_D; tgtSpeed = 90;  thr = 0.18f; }
  else if (t < 53)  { sel = SEL_D; tgtSpeed = 25;  thr = 0; }
  else if (t < 60)  { sel = SEL_D; tgtSpeed = 0;   thr = 0; }
  else              { sel = SEL_N; tgtSpeed = 0;   thr = 0; }
}

// Shift points (km/h) for gentle and full throttle, per number of gears.
static void shiftTable(int n, const float *&up, const float *&upW) {
  static const float u6[5] = {18, 34, 52, 70, 88}, w6[5] = {32, 58, 84, 106, 130};
  static const float u4[3] = {22, 45, 72}, w4[3] = {42, 80, 118};
  if (n >= 6) { up = u6; upW = w6; } else { up = u4; upW = w4; }
}

static void stepGears(unsigned long now) {
  if (P->trans != TR_AUTO) { car.gear = 1; return; }
  if (car.sel != SEL_D) { if (car.speed < 1) car.gear = 1; return; }
  const int n = P->nGears;
  const float *up, *upW;
  shiftTable(n, up, upW);
  int g = car.gear;
  if (car.throttle > 0.85f && car.speed > 20) {   // floor it: drop to a gear that pulls in the top third
    g = n;
    while (g > 1 && car.speed * P->rpmPerKmhTop * P->ratios[g] < P->redlineRpm * 0.67f) g--;
  } else {
    float th = clampf(car.throttle * 1.6f, 0, 1);
    while (g < n && car.speed > up[g - 1] + (upW[g - 1] - up[g - 1]) * th) g++;
    while (g > 1 && car.speed < (up[g - 2] + (upW[g - 2] - up[g - 2]) * th) * 0.62f) g--;
  }
  if (g != car.gear) { car.gear = g; shiftUntil = now + 350; }
}

void carStep() {
  unsigned long now = millis();
  if (now - lastStep < 50) return;
  float dt = (now - lastStep) / 1000.0f;
  if (dt > 0.5f) dt = 0.5f;
  lastStep = now;
  simT += dt;

  // ---- driver
  float tgtSpeed, thr;
  if (autoMode) {
    autoScript(fmodf((now - autoT0) / 1000.0f, 62.0f), car.sel, tgtSpeed, thr);
  } else {
    tgtSpeed = car.sel == SEL_D ? manualSpeed : car.sel == SEL_R ? min(manualSpeed, 8.0f) : 0;
    thr = manualThrottle;
  }
  const bool running = car.ign == IGN_RUN;
  if (!running) { tgtSpeed = 0; thr = car.ign == IGN_ON ? thr : 0; }
  car.throttle = toward(car.throttle, thr, dt, 0.25f);

  // ---- speed
  bool driving = running && (car.sel == SEL_D || car.sel == SEL_R);
  if (!driving) tgtSpeed = 0;
  float rate = tgtSpeed > car.speed ? 0.10f + car.throttle * 0.40f : 0.30f;
  if (!driving) rate = 0.7f;
  car.speed = max(0.0f, car.speed + (tgtSpeed - car.speed) * rate * dt * 3.0f);
  if (car.speed < 0.3f && tgtSpeed < 0.5f) car.speed = 0;
  car.odoKm += car.speed * dt / 3600.0f;

  stepGears(now);

  // ---- is the engine turning?
  if (!running || P->engine == ENG_EV) car.engineOn = false;
  else if (P->engine == ENG_HYBRID) {                 // EV mode at low speed and light throttle
    if (car.speed > 45 || car.throttle > 0.45f || car.battSoc < 45) car.engineOn = true;
    else if (car.speed < 38 && car.throttle < 0.30f && car.battSoc > 50) car.engineOn = false;
  } else car.engineOn = true;

  // ---- rpm
  const float idle = P->idleRpm, red = P->redlineRpm;
  float rpmT = 0;
  if (car.engineOn) {
    if (P->trans == TR_CVT) {
      rpmT = idle + car.throttle * (red - idle) * 0.85f + car.speed * 4.0f;
    } else {
      float ratio = car.sel == SEL_R ? 3.99f : P->ratios[car.gear];
      float geared = car.speed * P->rpmPerKmhTop * ratio;
      float idleRev = idle + car.throttle * (driving && car.speed > 3 ? 1500 : 4200 * red / 6400.0f);
      rpmT = (driving && car.speed > 6) ? max(geared, idle + 20 + car.throttle * 600) : idleRev;
      if (driving && car.speed > 0 && car.speed <= 6) rpmT = max(rpmT, idle + 20 + car.throttle * 1800);
      if (now < shiftUntil) rpmT -= 350;
    }
    rpmT = clampf(rpmT, idle - 80, red);
  }
  car.rpm = car.engineOn ? toward(car.rpm, rpmT, dt, 0.12f) : toward(car.rpm, 0, dt, 0.08f);
  if (car.rpm < 50) car.rpm = car.engineOn ? car.rpm : 0;

  // ---- engine values
  const bool turning = car.engineOn && car.rpm > 300;
  const bool wot = car.throttle > 0.85f;
  car.fuelCut = turning && car.sel == SEL_D && car.rpm > idle + 600 && car.throttle < 0.03f && car.speed > 15;

  float loadT = 0;
  if (turning) loadT = car.fuelCut ? 12.0f : clampf(22.0f + car.throttle * (60.0f + 20.0f * car.rpm / 6500.0f), 0, 100);
  car.load = toward(car.load, loadT, dt, 0.12f);

  float mapT;
  if (!turning) mapT = car.baro;
  else if (P->engine == ENG_DIESEL) mapT = car.baro + powf(car.throttle, 1.2f) * 140.0f * min(1.0f, car.rpm / 2000.0f);
  else if (P->engine == ENG_GAS_TURBO && car.throttle > 0.3f)
    mapT = car.baro + (car.throttle - 0.3f) / 0.7f * 100.0f * min(1.0f, car.rpm / 2500.0f);
  else mapT = 20.0f + car.load * 0.8f;
  car.map = toward(car.map, clampf(mapT, 15, 255), dt, 0.12f);

  const float disp = P->displacementL;
  if (!turning) car.maf = 0;
  else if (P->engine == ENG_DIESEL) car.maf = car.rpm / 120.0f * disp * 1.19f * (car.map / 101.0f) * 0.55f;
  else if (P->engine == ENG_GAS_TURBO) car.maf = car.rpm / 120.0f * disp * 1.19f * (car.map / 101.0f) * 0.88f;
  else car.maf = car.rpm / 120.0f * disp * 1.19f * (car.load / 100.0f) * 0.85f;

  float lamT;
  // diesel runs lean: lambda ~4 at idle, ~1.3 at full load (PID 24/34 encoding caps it below 2)
  if (P->engine == ENG_DIESEL) lamT = turning ? (car.fuelCut ? 8.0f : clampf(1.25f + (1.0f - car.load / 100.0f) * 3.5f, 1.2f, 8.0f)) : 8.0f;
  else lamT = !turning ? 1.0f : car.fuelCut ? 1.35f : wot ? 0.87f : 1.0f + 0.02f * sinf(simT * 2.3f);
  car.lambda = toward(car.lambda, lamT, dt, 0.15f);
  float stftT = (!turning || car.fuelCut || wot) ? 0 : 3.5f * sinf(simT * 1.7f);
  car.stft = toward(car.stft, stftT, dt, 0.4f);
  car.ltft = 2.3f + 0.4f * sinf(simT * 0.05f);
  car.timing = turning ? toward(car.timing, 10.0f + car.rpm / 6400.0f * 28.0f - car.load * 0.12f, dt, 0.2f) : 0;

  // fuel: rail pressure, consumption
  float railT;
  if (P->engine == ENG_DIESEL) railT = turning ? 30000.0f + clampf((car.load - 22.0f) / 78.0f, 0, 1) * 150000.0f : 0;
  else railT = turning ? (4.0f + car.load * 0.14f) * 1000.0f : 300.0f;
  car.railKpa = toward(car.railKpa, railT, dt, 0.15f);
  float afr = (P->engine == ENG_DIESEL ? 14.5f : 14.7f) * car.lambda;
  float density = P->engine == ENG_DIESEL ? 832.0f : 740.0f;
  car.fuelRateLh = (turning && !car.fuelCut) ? car.maf / afr / density * 3600.0f : 0;
  car.fuelPct = clampf(car.fuelPct - car.fuelRateLh * dt / 3600.0f / 0.45f, 5, 100);

  car.egrPct = (P->engine == ENG_DIESEL && turning && car.load < 50 && car.rpm < 2800) ? 40.0f - car.load * 0.6f : 0;
  car.torquePct = turning && !car.fuelCut ? clampf(car.load * 0.95f, 0, 100) : 0;
  car.dpfTemp = toward(car.dpfTemp, turning ? 180.0f + car.load * 4.0f : 60.0f, dt, 8.0f);
  car.catTemp = toward(car.catTemp, turning ? 420.0f + car.load * 4.2f : 150.0f, dt, 6.0f);

  // hybrid / EV battery
  if (P->engine == ENG_HYBRID || P->engine == ENG_EV) {
    float use = car.speed * (0.2f + car.throttle) * 0.004f;
    float charge = (car.engineOn ? 0.25f : 0) + (car.throttle < 0.02f && car.speed > 5 ? 0.15f : 0);
    car.battSoc = clampf(car.battSoc + (charge - use) * dt, P->engine == ENG_EV ? 5 : 40, P->engine == ENG_EV ? 100 : 80);
  }

  // temperatures and electrics
  car.coolant = P->engine == ENG_EV ? 30 : 88 + 2 * sinf(simT * 0.05f);
  car.iat = 35 + 3 * sinf(simT * 0.03f);
  car.oil = 95 + 3 * sinf(simT * 0.04f);
  car.atf = 82 + 4 * sinf(simT * 0.035f) + (car.speed > 60 ? 6 : 0);
  car.ambient = 31 + 1.5f * sinf(simT * 0.01f);
  car.volt = car.ign == IGN_OFF ? 12.4f : (turning || P->engine == ENG_EV || P->engine == ENG_HYBRID) && running
             ? 13.9f + 0.1f * sinf(simT * 0.2f) : 12.2f;
}
