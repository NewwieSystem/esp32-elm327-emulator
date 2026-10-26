// GhostCar - compile-time settings. Most things can also be changed at run time
// (touchscreen SETUP page or the USB serial console) and are then saved in flash.
#pragma once

#define GHOSTCAR_VERSION "2.0.0"

// ---------------------------------------------------------------------------------------------
// Names and addresses the "adapter" uses. These match what cheap ELM327 clones ship with, so
// apps that have presets for real adapters find GhostCar without extra setup.
// ---------------------------------------------------------------------------------------------
#define BT_DEVICE_NAME   "OBDII"          // Bluetooth Classic (SPP) and BLE advertised name
#define WIFI_AP_SSID     "WiFi_OBDII"     // open network, like most Wi-Fi ELM327 clones
#define WIFI_AP_PASSWORD ""               // "" = open. 8+ characters to protect it
#define WIFI_AP_IP       192, 168, 0, 10  // the address almost every app expects
#define WIFI_TCP_PORT    35000

// BLE layout: most clones use service FFF0 with FFF1/FFF2, older Vgate units FFE0/FFE1.
// GhostCar exposes both, and every characteristic accepts writes and sends notifications,
// so apps that probe either layout find it.
#define BLE_SVC_A   "0000fff0-0000-1000-8000-00805f9b34fb"
#define BLE_CHR_A1  "0000fff1-0000-1000-8000-00805f9b34fb"
#define BLE_CHR_A2  "0000fff2-0000-1000-8000-00805f9b34fb"
#define BLE_SVC_B   "0000ffe0-0000-1000-8000-00805f9b34fb"
#define BLE_CHR_B1  "0000ffe1-0000-1000-8000-00805f9b34fb"

// Defaults on first boot (afterwards the saved choice wins)
#define DEFAULT_PROFILE   0               // index in profiles.cpp (0 = generic CAN gasoline)
#define DEFAULT_LINK      0               // 0 = Bluetooth Classic, 1 = BLE, 2 = Wi-Fi

// ---------------------------------------------------------------------------------------------
// ELM327 behaviour
// ---------------------------------------------------------------------------------------------
#define ELM_ID_STRING     "ELM327 v1.5"
#define LOG_TRAFFIC       0   // 1 = print every RX / TX line on USB serial (slows answers ~5 ms each)
// A real ELM327 waits out its timeout for more ECUs after the first answer unless the request ends
// with the response-count digit ("010C1"). 1 = imitate that (~55 ms plain, ~10 ms with the digit).
#define EMULATE_ELM_LATENCY 0
// Print "SEARCHING..." before the first answer in automatic protocol mode, like a real adapter.
#define EMULATE_SEARCHING 1
// Engine PIDs only answer on 7DF/7E0, the transmission only on 7E1 (profiles with a TCM).
#define STRICT_HEADERS    1

// Gear codes the simulated transmission reports in Mode 22 1E12 (Mazda profile);
// in D it reports the gear number 1..6. Same values as GEAR_RAW_* in the REDLINE gauge config.
#define GEAR_RAW_N 50
#define GEAR_RAW_R 60
#define GEAR_RAW_P 70
