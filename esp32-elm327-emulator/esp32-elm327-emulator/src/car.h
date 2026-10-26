// The simulated car. One small physical model drives every value the OBD side reports,
// so rpm, speed, gear, load, boost and fuel all move together like a real car.
#pragma once
#include <Arduino.h>
#include "profiles.h"

enum Sel : uint8_t { SEL_P, SEL_R, SEL_N, SEL_D };
enum Ignition : uint8_t { IGN_OFF, IGN_ON, IGN_RUN };   // key off / key on, engine off / running

struct Car {
  Ignition ign = IGN_RUN;
  Sel   sel = SEL_P;
  float speed = 0;          // km/h
  float throttle = 0;       // 0..1 (accelerator)
  int   gear = 1;           // 1..n in D
  float rpm = 780;
  bool  engineOn = true;    // false: key on engine off, hybrid in EV mode, electric car
  bool  fuelCut = false;
  float load = 25, map = 40, maf = 2.5f;
  float lambda = 1.0f, stft = 0, ltft = 2.3f, timing = 12;
  float fuelPct = 62;
  float coolant = 88, iat = 35, oil = 95, atf = 82, volt = 13.9f;
  float railKpa = 5000, fuelRateLh = 0.8f, egrPct = 0, torquePct = 15, dpfTemp = 220, catTemp = 520;
  float battSoc = 62, odoKm = 48231.6f, ambient = 31, baro = 100;
};

extern Car car;
extern bool  autoMode;
extern float manualSpeed, manualThrottle;

void carInit(const Profile &p);           // reset to a warm idle for this profile
void carStep();                           // call often; steps every 50 ms
void carAuto();                           // run the 62 s demo loop
void carSetSelector(Sel s);               // manual
void carSetTarget(float kmh, float thr);  // manual
void carSetIgnition(Ignition i);
int  carGearCode();                       // Mazda TCM code: 1..6 in D, N=50, R=60, P=70
const char *carSelName();
const char *ignName(Ignition i);
