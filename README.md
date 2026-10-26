<div align="center">

```
  ▄████  ██░ ██  ▒█████    ██████ ▄▄▄█████▓ ▄████▄   ▄▄▄       ██▀███
 ██▒ ▀█▒▓██░ ██▒▒██▒  ██▒▒██    ▒ ▓  ██▒ ▓▒▒██▀ ▀█  ▒████▄    ▓██ ▒ ██▒
▒██░▄▄▄░▒██▀▀██░▒██░  ██▒░ ▓██▄   ▒ ▓██░ ▒░▒▓█    ▄ ▒██  ▀█▄  ▓██ ░▄█ ▒
░▓█  ██▓░▓█ ░██ ▒██   ██░  ▒   ██▒░ ▓██▓ ░ ▒▓▓▄ ▄██▒░██▄▄▄▄██ ▒██▀▀█▄
░▒▓███▀▒░▓█▒░██▓░ ████▓▒░▒██████▒▒  ▒██▒ ░ ▒ ▓███▀ ░ ▓█   ▓██▒░██▓ ▒██▒
```

### 👻 A car that doesn't exist — but your OBD app won't know that.

**ESP32 ELM327 OBD-II emulator.** Ten virtual cars, three ways to connect, a touchscreen to drive them.<br>
Build and test OBD apps, dash gauges and scanners on your desk — no car, no ignition, no garage at 2 AM.

[![Build](https://img.shields.io/github/actions/workflow/status/NewwieSystem/esp32-elm327-emulator/build.yml?style=for-the-badge&label=build)](https://github.com/NewwieSystem/esp32-elm327-emulator/actions)
[![PlatformIO](https://img.shields.io/badge/PlatformIO-ready-orange?style=for-the-badge&logo=platformio&logoColor=white)](https://platformio.org/)
[![ESP32](https://img.shields.io/badge/ESP32-supported-E7352C?style=for-the-badge&logo=espressif&logoColor=white)](https://www.espressif.com/)
[![CYD](https://img.shields.io/badge/Cheap_Yellow_Display-2432S028-FFD700?style=for-the-badge)](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue?style=for-the-badge)](LICENSE)

[**⚡ Flash in your browser**](https://NewwieSystem.github.io/esp32-elm327-emulator/) ·
[**Features**](#-features) ·
[**Quick start**](#-quick-start) ·
[**Vehicle guide**](docs/VEHICLES.md) ·
[**Commands**](#-what-it-answers)

<!-- TODO: replace with a real GIF of the touchscreen + an app reading live values -->
<img src="docs/demo.gif" alt="GhostCar demo" width="720">

</div>

---

## 🏁 Why GhostCar?

Writing OBD software is painful when the only test device is parked outside.
Every code change means: walk to the car → start the engine → connect → hope it works → repeat.

GhostCar turns a **$10 ESP32** into a car with an ELM327 adapter plugged in. Point any OBD app or
your own firmware at it and you get realistic, controllable data — instantly. Then switch the car
to a 2001 Honda on K-line, a diesel pickup or an EV that answers almost nothing, and see whether
your app still copes.

> Built while developing a custom ESP32 dash gauge for a Mazda 2 SKYACTIV. Tested daily on a real project, not a weekend demo.

---

## ✨ Features

|   |   |
|---|---|
| 🔌 **Real ELM327 behaviour** | Echo, spaces, headers, `SEARCHING...`, `NO DATA`, `UNABLE TO CONNECT`, ISO-TP multi-frame lines, K-line checksums, response-count digit. Not just canned strings. |
| 🚗 **10 vehicle profiles** | Modern gasoline, Mazda 2 SKYACTIV, Prius hybrid, diesel pickup, turbo 29-bit CAN, 90s Honda (ISO 9141), Toyota (KWP2000), Ford (J1850 PWM), GM (J1850 VPW), EV. [Details →](docs/VEHICLES.md) |
| 📡 **Bluetooth Classic, BLE and Wi-Fi** | Works with Android, **iPhone** (BLE / Wi-Fi), PCs and ESP32 projects. Same names and addresses as cheap clones. |
| 🏎️ **A car that drives itself** | One physics-ish model drives every value: shifts through 6 gears, boost, fuel cut, hybrid EV mode, DPF heat. 62 s demo loop or full manual control. |
| 🎛️ **Touchscreen control** | On the Cheap Yellow Display: drag throttle and speed, pick P/R/N/D, turn the key, add fault codes, switch car or connection. |
| ⚠️ **Fault codes that behave** | Stored, pending and permanent codes, check-engine lamp, readiness monitors, freeze frame — and Mode 04 is refused while the car is moving, like a real ECU. |
| 🔑 **Key off / key on** | Test what your app does when the ECU goes silent or the engine is off. |
| 🖥️ **Headless mode** | No screen? Runs on any ESP32 dev board, controlled from the serial console. |

---

## 🚀 Quick start

### Option A — Flash from your browser (easiest)

1. Plug in your ESP32 / CYD with a USB cable
2. Open **[the web flasher](https://NewwieSystem.github.io/esp32-elm327-emulator/)** in Chrome or Edge
3. Pick your board → **Install** → choose the serial port → done

### Option B — Build with PlatformIO

```bash
git clone https://github.com/NewwieSystem/esp32-elm327-emulator.git
cd esp32-elm327-emulator

pio run -e cyd -t upload        # Cheap Yellow Display ESP32-2432S028 (1 USB port)
pio run -e cyd2usb -t upload    # CYD with 2 USB ports (USB-C + micro-USB)
pio run -e esp32dev -t upload   # any ESP32 board without a screen
```

### Connect your app

| Connection | How |
|---|---|
| **Bluetooth Classic** (default) | Pair with **`OBDII`** in Android / Windows Bluetooth settings, then pick it in the app |
| **Bluetooth LE** | Don't pair in settings — select **`OBDII`** inside the app (required for iPhone) |
| **Wi-Fi** | Join the open network **`WiFi_OBDII`**, set the app to **`192.168.0.10`** port **`35000`** |

Change the connection on the **SETUP** page (or `link ble` on the serial console). The board
restarts once and remembers the choice. Only one link runs at a time — the ESP32 has room for
one radio stack plus the screen, not all three.

---

## 🎛️ The touchscreen

| Page | What you can do |
|---|---|
| **DRIVE** | Speed, RPM bar, gear, coolant / throttle / load / voltage. `AUTO` runs the demo loop; `P R N D` and the two sliders take over manually; `KEY` cycles RUN → OFF → ON. |
| **FAULTS** | See stored / pending codes and the check-engine lamp. Add a random code, clear all, restore the demo codes, toggle the lamp, take the transmission offline. |
| **SETUP** | `<` `>` switches the car profile instantly. Pick BT Classic / BLE / Wi-Fi and tap **APPLY & RESTART**. Shows the MAC or IP address and whether an app is connected. |

The top bar shows the connection, a green dot when an app is connected, and requests per second.
The CYD's RGB LED blinks on every request.

---

## ⌨️ Serial console

Open the USB serial port at **115200** baud. Works on every board, screen or not.

```
help | status | profiles
profile <n|id>          switch car (saved)        link bt|ble|wifi   change connection (restarts)
auto                    run the 62 s demo loop    P | R | N | D      hold the selector (manual)
speed <kmh> | thr <pct> manual target speed / throttle
wot | cruise | idle | decel                       manual presets
key off | key on | key run                        ignition: no ECU / engine off / running
fault add P0300 | fault pending P0301 | fault random | fault clear | fault reset
mil on|off              check-engine lamp         tcm on|off         transmission answers or not
drop <hexpid> | add <hexpid>                      remove / add a Mode 01 PID from the supported list
```

---

## 📡 What it answers

<details>
<summary><b>AT commands</b></summary>

| Command | Behaviour |
|---|---|
| `ATZ` `ATWS` `ATI` | Reset / version → `ELM327 v1.5` |
| `ATD` | Back to defaults |
| `AT@1` | `OBDII to RS232 Interpreter` |
| `ATE0/1` `ATL0/1` `ATS0/1` `ATH0/1` | Echo, linefeeds, spaces, headers — all really change the output |
| `ATCAF0/1` | CAN auto-formatting: off shows the PCI bytes and raw ISO-TP frames |
| `ATSP n` `ATSPA n` `ATTP n` | Force a protocol — the wrong one gives `CAN ERROR` / `BUS INIT: ...ERROR` like a real adapter |
| `ATDP` `ATDPN` | `AUTO, ISO 15765-4 (CAN 11/500)` / `A6` |
| `ATSH xxx` `ATSH xxyyzz` `ATCP` | Address one ECU (11-bit or 29-bit). Engine on 7E0, transmission on 7E1 |
| `ATRV` | Battery voltage — 14 V running, 12.2 V key on, 12.4 V key off |
| `ATIGN` | `ON` / `OFF` |
| anything else (`ATST`, `ATAT`, `ATAL`, `ATCFC`...) | `OK` |

</details>

<details>
<summary><b>OBD-II services</b></summary>

| Service | |
|---|---|
| `01` | Live data — up to ~40 PIDs depending on the profile, bitmask pages chained properly. Up to 6 PIDs per request on CAN (`010C0D05`). |
| `02` | Freeze frame of the first stored code |
| `03` / `07` / `0A` | Stored / pending / permanent codes, in the right format for the protocol |
| `04` | Clear codes (refused with `7F 04 22` while moving), resets readiness monitors |
| `09` | VIN, calibration id, ECU name |
| `22` | Mazda manufacturer data on the `mazda2` profile (oil temp, ATF temp, gear, boost...) |

Full per-profile tables: **[docs/VEHICLES.md](docs/VEHICLES.md)**.

</details>

<details>
<summary><b>What a session looks like</b></summary>

```
>ATZ
ELM327 v1.5
>ATE0
OK
>010C
SEARCHING...
41 0C 34 EA
>ATH1
OK
>0902
7E8 10 14 49 02 01 47 48 53
7E8 21 54 43 41 52 30 30 30
7E8 22 30 30 30 30 30 30 31
```

</details>

---

## 🧰 Hardware

| Board | Build | Notes |
|---|---|---|
| ESP32-2432S028 "Cheap Yellow Display" | `cyd` | Full touchscreen UI, RGB LED shows traffic |
| CYD with USB-C + micro-USB | `cyd2usb` | Different panel driver; if colours look wrong, remove `-DCYD_INVERT` |
| Any ESP32 (WROOM-32 / WROVER) | `esp32dev` | Headless, control via serial console; LED on GPIO 2 |
| ESP32-S3 / C3 / C6 | — | No Bluetooth Classic. A BLE + Wi-Fi port is on the roadmap |

The firmware is about 1.6 MB, so every build uses the `huge_app` partition (3 MB app, no OTA).

---

## 🔧 Using it with the REDLINE gauge

Select the **`mazda2`** profile. It answers exactly what the gauge asks for — including the Mazda
Mode 22 values and the gear on 7E1 — and refuses wrong headers, so a mistake in the gauge's
`ATSH` switching shows up as `--` just like in the real car. Put GhostCar's Bluetooth MAC (shown
on the SETUP page and at boot) into the gauge's `config.h`.

---

## 🗺️ Roadmap

- [x] Bluetooth Classic, BLE and Wi-Fi links
- [x] Vehicle profiles with real protocol formatting
- [x] Touchscreen control
- [ ] ESP32-S3 / C3 build (BLE + Wi-Fi)
- [ ] Record a real drive with a real adapter and replay it
- [ ] Profiles loaded from a JSON file on SD card
- [ ] Several ECUs answering one broadcast request (engine + transmission)

Got an idea or a car that behaves differently? [Open an issue](https://github.com/NewwieSystem/esp32-elm327-emulator/issues) 🔧

---

## 🤝 Contributing

PRs are welcome — especially new vehicle profiles, manufacturer PIDs and app compatibility
reports. To add a car, the answers to `ATDP`, `0100`, `0120`, `0140` and `0160` from the real car
are enough to start.

The ELM327 logic also builds on a PC (see [`test/`](test)), so you can check protocol output
without flashing anything.

If GhostCar saved you a trip to the garage, **drop a ⭐** so other builders can find it.

## 📄 License

MIT © NewwieSystem

<div align="center">
<sub>Made in Thailand 🇹🇭 with too much coffee and not enough parking space.</sub>
</div>
