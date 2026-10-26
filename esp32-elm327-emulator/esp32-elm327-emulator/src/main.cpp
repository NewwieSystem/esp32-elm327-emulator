// GhostCar - an ESP32 that pretends to be a car with an ELM327 adapter plugged in.
// https://github.com/NewwieSystem/esp32-elm327-emulator
#include <Arduino.h>
#include "config.h"
#include "settings.h"
#include "app.h"
#include "car.h"
#include "elm327.h"
#include "link.h"
#include "console.h"
#include "ui.h"

// ---------------------------------------------------------------- traffic LED
#ifdef GHOSTCAR_CYD
// CYD RGB LED, active low
static const int LED_R = 4, LED_G = 16, LED_B = 17;
static void ledSet(bool on) {
  digitalWrite(LED_R, HIGH);
  digitalWrite(LED_G, on ? LOW : HIGH);
  digitalWrite(LED_B, on ? LOW : HIGH);
}
static void ledInit() { pinMode(LED_R, OUTPUT); pinMode(LED_G, OUTPUT); pinMode(LED_B, OUTPUT); ledSet(false); }
#else
#ifndef LED_PIN
#define LED_PIN 2
#endif
static void ledSet(bool on) { digitalWrite(LED_PIN, on ? HIGH : LOW); }
static void ledInit() { pinMode(LED_PIN, OUTPUT); ledSet(false); }
#endif

static unsigned long ledOffAt = 0;
void onLinkTraffic() { ledSet(true); ledOffAt = millis() + 25; }

// ---------------------------------------------------------------- shared actions
void appSetProfile(int index) {
  elmSetProfile(index);
  settingsPut("profile", (uint8_t)activeProfileIndex());
}

void appSetLink(LinkType t) {
  settingsPut("link", (uint8_t)t);
  delay(300);
  ESP.restart();
}

// ----------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(300);
  ledInit();

  uint8_t profile = settingsGet("profile", DEFAULT_PROFILE);
  uint8_t link = settingsGet("link", DEFAULT_LINK);
  if (profile >= PROFILE_COUNT) profile = 0;
  if (link >= LINK_COUNT) link = LINK_BT;

  elmInit(profile);
  linkBegin((LinkType)link);

  Serial.println();
  Serial.println("=========================================");
  Serial.println(" GHOSTCAR  ELM327 OBD-II emulator v" GHOSTCAR_VERSION);
  Serial.println("=========================================");
  Serial.printf(" car:  %s (%s)\n", activeProfile().name, protocolName(activeProfile().protocol));
  Serial.printf(" link: %s  %s\n", linkName(linkType()),
                linkType() == LINK_WIFI ? (String(WIFI_AP_SSID) + "  " + linkAddress()).c_str()
                                        : (String("\"" BT_DEVICE_NAME "\"  ") + linkAddress()).c_str());
  Serial.println();
  consolePrintHelp();

  uiBegin();
}

void loop() {
  carStep();
  linkPoll();
  consolePoll();
  uiLoop();
  if (ledOffAt && millis() > ledOffAt) { ledSet(false); ledOffAt = 0; }
  delay(1);
}
