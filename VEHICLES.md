# 🚗 Vehicle support guide

What GhostCar can pretend to be, and — just as useful when you build an OBD app — what real cars
actually answer. Real cars differ by model year, engine and market, so treat this as a map of
*typical* behaviour, not a promise about one specific car.

- [1. Which protocol does my car use?](#1-which-protocol-does-my-car-use)
- [2. What data each kind of car gives](#2-what-data-each-kind-of-car-gives)
- [3. GhostCar profiles](#3-ghostcar-profiles)
- [4. Mode 01 PIDs per profile](#4-mode-01-pids-per-profile)
- [5. Other services per profile](#5-other-services-per-profile)
- [6. Phone / app / connection compatibility](#6-phone--app--connection-compatibility)
- [7. Notes for Thai-market cars](#7-notes-for-thai-market-cars)
- [Sources](#sources)

---

## 1. Which protocol does my car use?

OBD-II has five physical protocols. An ELM327 in automatic mode (`ATSP0`) finds the right one
by itself — that is the `SEARCHING...` line you see on the first request.

| Protocol | ELM `ATSP` | Typically found on |
|---|:-:|---|
| SAE J1850 PWM | 1 | US Ford (and Ford-built Mazda / Lincoln), roughly 1996–2004 |
| SAE J1850 VPW | 2 | US GM, roughly 1996–2005; some Chrysler |
| ISO 9141-2 (K-line) | 3 | Asian and European cars, roughly 1996–2004 (Honda kept it until about 2007) |
| ISO 14230-4 KWP2000 | 4 / 5 | Asian and European cars, roughly 2003–2008 |
| ISO 15765-4 CAN 11-bit 500k | 6 | **Almost every car since 2008** (required on all US cars from model year 2008) |
| ISO 15765-4 CAN 29-bit 500k | 7 | Some makes use 29-bit identifiers |
| CAN 250k (11 / 29-bit) | 8 / 9 | Rare on passenger cars |

When the switch to CAN happened, by make (approximate — models overlap):

| Make | Before CAN | CAN from about |
|---|---|---|
| Toyota / Lexus | KWP2000 (some ISO 9141, early US trucks J1850 VPW) | 2006 (some models earlier) |
| Honda | ISO 9141-2 | 2008 |
| Mitsubishi | ISO 9141-2 | 2006 |
| Suzuki | KWP2000 | 2007 |
| Hyundai / Kia | KWP2000 / ISO 9141-2 | 2005 |
| Ford USA | J1850 PWM | 2004–2008 |
| Ford Europe | ISO 9141-2 / KWP2000 | 2004–2008 |
| GM USA | J1850 VPW | 2004–2008 |
| Volvo / Saab | KWP2000 | 2003–2005 |
| Peugeot / Citroën / Renault / Fiat | ISO 9141-2 / KWP2000 | 2004–2005 |

**Rule of thumb:** made in 2008 or later → CAN 11-bit 500k (`ATSP6`). That is GhostCar's default.

---

## 2. What data each kind of car gives

Generic OBD-II (Mode 01) exists for emissions checks, so what a car reports depends on its
engine more than its brand. Everything else (gear, oil temperature, tyre pressure, battery cells,
...) is **manufacturer-specific** (Mode 21 / 22) and differs per make.

| Kind of car | Usually reports | Usually missing |
|---|---|---|
| Modern gasoline (2008+) | RPM, speed, load, coolant, intake temp, MAF and/or MAP, throttle, fuel trims, O2 sensors, timing, voltage, catalyst temp | Gear, oil temp, fuel level (often missing on Toyota), odometer (only newer cars) |
| Turbo gasoline | All of the above, and MAP shows boost above ~100 kPa | Boost target, wastegate |
| Common-rail diesel | RPM, speed, load, coolant, MAP (boost), MAF, rail pressure, EGR, torque, sometimes DPF temperature | Narrow-band O2, fuel trims, ignition timing (diesels have no spark) |
| Hybrid | The engine side of a gasoline car — RPM drops to 0 when the engine stops in EV mode | Battery cell data, motor data (manufacturer-specific) |
| Electric | Very little: speed, voltage, maybe ambient temp, fuel type "electric", odometer | RPM, battery state of charge in most cases — EV apps read manufacturer PIDs |
| Old (1996–2004) | The original short list: RPM, speed, load, coolant, intake temp, MAF or MAP, throttle, trims, O2, timing | Everything added later: run time, voltage, distance counters, pedal position |

Two things that surprise app developers:

- **The supported-PID bitmask is the truth.** Ask `0100`, `0120`, `0140` ... first and only poll
  what the car lists. Asking for something else returns `NO DATA`.
- **Diesels are different, not worse.** `0101` byte B bit 3 is set for compression ignition, and
  the readiness monitors in bytes C/D mean other things (NOx, PM filter, boost, EGR).

---

## 3. GhostCar profiles

Pick one on the **SETUP** page or with `profile <id>` on the serial console. The choice is saved.

| # | id | Pretends to be | Protocol | Engine | Fault codes after "demo" |
|:-:|---|---|---|---|---|
| 0 | `generic` | Most 2008+ gasoline cars | CAN 11/500 | 1.5 L gasoline, 6AT | P0171, P0420 (+ pending P0301) |
| 1 | `mazda2` | Mazda 2 / 3 / CX-3 SKYACTIV-G 6AT — the REDLINE gauge test car | CAN 11/500 | 1.3 L gasoline, 6AT + TCM on 7E1 | P0171, P0420 (+ pending P0301) |
| 2 | `prius` | Toyota Prius gen 2 and similar Toyota hybrids | CAN 11/500 | 1.5 L hybrid, eCVT | P0A80 (+ pending P0171) |
| 3 | `diesel` | Common-rail diesel pickup / SUV, 2012+ | CAN 11/500 | 2.8 L turbo diesel, 6AT | P0087, P2463 (+ pending P0401) |
| 4 | `turbo29` | Turbo gasoline car on 29-bit CAN (test profile) | CAN 29/500 | 1.4 L turbo gasoline, 6AT | P0299 (+ pending P0300) |
| 5 | `honda-iso` | Honda / Mitsubishi / many Asian cars 1996–2007 | ISO 9141-2 | 1.7 L gasoline, 4AT | P0420 |
| 6 | `toyota-kwp` | Toyota / Suzuki / Hyundai / Kia 1996–2006 | KWP2000 fast | 1.8 L gasoline, 4AT | P0171 |
| 7 | `ford-pwm` | US Ford / Lincoln / Mazda 1996–2004 | J1850 PWM | 4.0 L gasoline, 4AT | P0401 |
| 8 | `gm-vpw` | US GM / Chrysler 1996–2005 | J1850 VPW | 5.3 L gasoline, 4AT | P0300 |
| 9 | `ev` | Electric car | CAN 11/500 | motor, single speed | P0AA6 |

How the car model behaves per engine type:

- **Gasoline** — closed-loop lambda around 1.00 with moving trims, rich (0.87) at full throttle,
  fuel cut on overrun, MAP vacuum at idle.
- **Turbo gasoline** — like gasoline, but MAP climbs to ~200 kPa under load.
- **Diesel** — no throttle vacuum (MAP ≈ barometric at idle), boost to ~240 kPa, rail pressure
  30 → 180 MPa, very lean lambda, EGR open at light load, DPF temperature follows load.
- **Hybrid** — the engine stops (RPM 0) below ~40 km/h at light throttle and restarts when you
  accelerate or the battery runs low.
- **Electric** — no engine values at all; `010C` answers `NO DATA`.

---

## 4. Mode 01 PIDs per profile

● = listed in the supported-PID bitmask and answered with live data.
PIDs `01` (monitor status / check-engine lamp), `21`, `30` and `31` are answered by every
profile, and the bitmask pages `00`, `20`, `40` ... are chained correctly.

| PID | Value | `generic` | `mazda2` | `prius` | `diesel` | `turbo29` | `honda-iso` | `toyota-kwp` | `ford-pwm` | `gm-vpw` | `ev` |
|---|---|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|
| `03` | Fuel system status | ● |  | ● |  | ● | ● | ● | ● | ● |  |
| `04` | Engine load | ● | ● | ● | ● | ● | ● | ● | ● | ● |  |
| `05` | Coolant temp | ● | ● | ● | ● | ● | ● | ● | ● | ● |  |
| `06` | Short-term fuel trim | ● | ● | ● |  | ● | ● | ● | ● | ● |  |
| `07` | Long-term fuel trim | ● | ● | ● |  | ● | ● | ● | ● | ● |  |
| `0B` | Manifold pressure / boost | ● | ● |  | ● | ● |  |  |  | ● |  |
| `0C` | Engine RPM | ● | ● | ● | ● | ● | ● | ● | ● | ● |  |
| `0D` | Vehicle speed | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● |
| `0E` | Timing advance | ● | ● | ● |  | ● | ● | ● | ● | ● |  |
| `0F` | Intake air temp | ● | ● | ● | ● | ● | ● | ● | ● | ● |  |
| `10` | MAF air flow | ● | ● | ● | ● | ● | ● | ● | ● |  |  |
| `11` | Throttle position | ● | ● | ● | ● | ● | ● | ● | ● | ● |  |
| `13` | O2 sensors present | ● |  | ● |  | ● | ● | ● | ● | ● |  |
| `14` | O2 B1S1 (narrow-band) |  |  |  |  |  | ● | ● | ● | ● |  |
| `15` | O2 B1S2 | ● | ● | ● |  | ● | ● | ● | ● | ● |  |
| `1C` | OBD standard | ● |  | ● | ● | ● | ● | ● | ● | ● | ● |
| `1F` | Run time | ● | ● | ● | ● | ● |  | ● |  |  |  |
| `21` | Distance with MIL on | ● |  | ● | ● | ● |  | ● |  |  |  |
| `23` | Fuel rail pressure |  | ● |  | ● | ● |  |  |  |  |  |
| `24` | Wide-band O2 (lambda) | ● | ● | ● |  | ● |  |  |  |  |  |
| `2C` | Commanded EGR |  |  |  | ● |  |  |  |  |  |  |
| `2E` | Commanded EVAP purge | ● |  | ● |  |  |  |  |  |  |  |
| `2F` | Fuel level | ● | ● |  |  | ● |  |  |  |  |  |
| `30` | Warm-ups since clear | ● |  | ● | ● | ● |  |  |  |  |  |
| `31` | Distance since clear | ● |  | ● | ● | ● |  |  |  |  |  |
| `32` | EVAP vapour pressure |  |  | ● |  |  |  |  |  |  |  |
| `33` | Barometric pressure | ● | ● | ● | ● | ● |  |  |  |  |  |
| `3C` | Catalyst temp B1S1 | ● | ● | ● |  | ● |  |  |  |  |  |
| `3E` | Catalyst temp B1S2 |  |  | ● |  |  |  |  |  |  |  |
| `42` | Module voltage | ● | ● | ● | ● | ● |  |  |  |  | ● |
| `43` | Absolute load | ● |  | ● |  | ● |  |  |  |  |  |
| `44` | Commanded lambda | ● | ● | ● |  | ● |  |  |  |  |  |
| `45` | Relative throttle | ● |  | ● |  | ● |  |  |  |  |  |
| `46` | Ambient air temp | ● |  | ● | ● | ● |  |  |  |  | ● |
| `47` | Absolute throttle B | ● |  | ● |  |  |  |  |  |  |  |
| `49` | Accelerator pedal D | ● | ● | ● | ● | ● |  |  |  |  |  |
| `4A` | Accelerator pedal E | ● |  | ● | ● | ● |  |  |  |  |  |
| `4C` | Commanded throttle | ● |  | ● |  |  |  |  |  |  |  |
| `4D` | Time with MIL on |  |  | ● |  |  |  |  |  |  |  |
| `4E` | Time since clear |  |  | ● |  |  |  |  |  |  |  |
| `51` | Fuel type | ● |  |  |  | ● |  |  |  |  | ● |
| `53` | Absolute EVAP pressure |  |  | ● |  |  |  |  |  |  |  |
| `5B` | Hybrid / EV battery remaining |  |  |  |  |  |  |  |  |  | ● |
| `5C` | Engine oil temp | ● |  |  |  | ● |  |  |  |  |  |
| `5E` | Fuel rate | ● |  |  | ● |  |  |  |  |  |  |
| `62` | Actual torque % |  |  |  | ● | ● |  |  |  |  |  |
| `63` | Reference torque |  |  |  | ● | ● |  |  |  |  |  |
| `7C` | DPF temperature |  |  |  | ● |  |  |  |  |  |  |
| `A6` | Odometer | ● |  |  |  |  |  |  |  |  | ● |

Want a car that lacks something? `drop 2F` on the console removes fuel level from the list until
the next profile change; `add 5C` adds oil temperature.

---

## 5. Other services per profile

| Service | CAN profiles | K-line / J1850 profiles |
|---|---|---|
| `02` freeze frame (frame 00) | ✅ | ✅ |
| `03` stored codes | ✅ count byte + 2 bytes per code, multi-frame when more than 2 | ✅ 3 codes per message, padded with 0000 |
| `07` pending codes | ✅ | ✅ |
| `0A` permanent codes | ✅ | — (not on old cars) |
| `04` clear codes | ✅ refused with `7F 04 22` while moving | ✅ |
| `09 00` supported info | ✅ | ✅ |
| `09 02` VIN | ✅ 3-frame ISO-TP | ✅ 5 messages of 4 bytes |
| `09 04` calibration id / `09 0A` ECU name | ✅ | — |
| Several PIDs in one request (`010C0D05`) | ✅ up to 6 | — (`NO DATA`) |
| `22` manufacturer data | `mazda2` only (oil temp `221310`, ATF `221E1C`, gear `221E12` on 7E1, boost, ...) | — |

All fake VINs start with `GHSTCAR` (or `1GHSTCAR` for US profiles) so they can never be confused
with a real car. The `mazda2` profile keeps `DESK0000000000000` from the original test sketch.

---

## 6. Phone / app / connection compatibility

| | Bluetooth Classic | Bluetooth LE | Wi-Fi |
|---|:-:|:-:|:-:|
| Android apps (Car Scanner, Torque, OBD Auto Doctor ...) | ✅ | ✅ | ✅ |
| iPhone / iPad apps | ❌ iOS blocks classic SPP adapters | ✅ | ✅ |
| Windows / macOS / Linux (python-OBD, serial terminal) | ✅ (virtual COM port) | depends on the app | ✅ (TCP) |
| ESP32 projects (ELMduino, REDLINE gauge) | ✅ | ✅ (BLE client code) | ✅ |
| Connection details | name `OBDII` | name `OBDII`, services FFF0 (FFF1/FFF2) and FFE0 (FFE1) | open network `WiFi_OBDII`, `192.168.0.10:35000` |

The BLE side offers both common layouts used by clones (FFF0 with FFF1/FFF2, and the older Vgate
FFE0/FFE1). Every characteristic accepts writes and sends notifications, so apps that pick
either layout find it.

---

## 7. Notes for Thai-market cars

- Cars sold new from about 2008 — Yaris / Vios / Altis, City / Civic / Jazz, Mazda 2 / 3, Almera,
  Mirage, current pickups — are CAN 11/500: use `generic`, `mazda2` or `diesel`.
- Pickups (Hilux, D-Max, Ranger, Triton) report the generic diesel set; values like transmission
  temperature need manufacturer PIDs and differ per gearbox. Owners of some older pre-CAN diesel
  pickups report that ELM327 adapters cannot talk to the ECU at all.
- Toyota hybrids (Prius, C-HR, Corolla Cross, Yaris Cross HEV) answer generic engine data;
  battery data is Toyota-specific.
- EVs (BYD, MG, ORA, Tesla, ...) give almost nothing over generic OBD. Use the `ev` profile to
  test that your app copes with a car that answers `NO DATA` to nearly everything.

---

## Sources

- OBD-II PID formulas and bit layouts — [Wikipedia: OBD-II PIDs](https://en.wikipedia.org/wiki/OBD-II_PIDs)
- Protocols by year — [YOUCANIC: OBD2 protocols](https://www.youcanic.com/glossary/protocols/), [ASE L1 study guide](https://open-exam-prep.com/study-guides/ase-l1/computerized-controls/obd-ii-standards)
- CAN switch-over by make — [i-k-bus-board vehicle compatibility notes](https://git.supported.systems/warehack.ing/i-k-bus-board/commit/fc1e832c3d66ed45b98c4672e5a51aa820e533d7), [pinouts.ru Toyota/Lexus list](https://pinouts.ru/CarElectronics/Toyota_lexus_obd2_diagnostic.shtml)
- Prius gen 2 generic PID list — [PriusChat: generic SAE PIDs, 2007 Prius](https://attachments.priuschat.com/attachment-files/2024/07/252951_Generic_SAE_PIDs_2007_Prius.pdf)
- Hilux owners on ELM327 — [newhilux.net forum](https://www.newhilux.net/phpbb/viewtopic.php?p=425351)
- iOS and classic Bluetooth — [OBD Auto Doctor: supported adapters](https://www.obdautodoctor.com/help/articles/supported-obd-adapters/), [Car Scanner: Bluetooth troubleshooting](https://www.carscanner.info/2023/02/)
- Wi-Fi adapter defaults — [Car Scanner: Wi-Fi setup](https://www.carscanner.info/?p=65)
- BLE service layouts — [esp_obd_ii component](https://components.espressif.com/components/lahirunirmalx/esp_obd_ii/versions/0.1.0/readme), [ObdInsight BLE transport](https://www.nuget.org/packages/ObdInsight.Transports.Ble/), [EVD release notes](https://github.com/waleedmandour/EVD/releases)
- Generic vs manufacturer PIDs — [OBDLink support](https://support.obdlink.com/support/solutions/articles/43000713278)

Found a car that behaves differently? Open an issue with the `0100` / `0120` / `0140` answers
and the protocol (`ATDP`) — that is enough to add a profile.
