// How the app talks to GhostCar: Bluetooth Classic (SPP), Bluetooth LE or Wi-Fi (TCP).
// One link runs at a time (the ESP32 has too little RAM for all three); the choice is saved and
// changing it restarts the board.
#pragma once
#include <Arduino.h>

enum LinkType : uint8_t { LINK_BT = 0, LINK_BLE = 1, LINK_WIFI = 2, LINK_COUNT = 3 };

void linkBegin(LinkType t);
void linkPoll();                 // read requests, answer them
LinkType linkType();
bool linkClientConnected();
const char *linkName(LinkType t);
const char *linkShortName(LinkType t);
String linkAddress();            // Bluetooth MAC or "192.168.0.10:35000"

// called on every request (LED blink); main.cpp provides it
void onLinkTraffic();
