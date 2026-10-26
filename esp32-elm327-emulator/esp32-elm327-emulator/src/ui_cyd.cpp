#include "ui.h"

#ifndef GHOSTCAR_CYD
void uiBegin() {}
void uiLoop() {}
void uiShowMessage(const char *) {}
#else

#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include "config.h"
#include "app.h"
#include "car.h"
#include "elm327.h"
#include "faults.h"
#include "link.h"
#include "mazda_ext.h"

// CYD touch controller pins (it sits on its own SPI bus)
#define TOUCH_CS   33
#define TOUCH_IRQ  36
#define TOUCH_MOSI 32
#define TOUCH_MISO 39
#define TOUCH_CLK  25
// raw touch range of the panel; adjust if taps land a little off
#define TOUCH_X_MIN 200
#define TOUCH_X_MAX 3700
#define TOUCH_Y_MIN 240
#define TOUCH_Y_MAX 3800

static TFT_eSPI tft;
static SPIClass touchSpi(VSPI);
static XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);

// palette (RGB565)
static const uint16_t C_BG = 0x0861, C_PANEL = 0x18E4, C_LINE = 0x31A8, C_TXT = 0xFFFF, C_DIM = 0x8C71;
static const uint16_t C_GHOST = 0x57FB, C_AMBER = 0xFD20, C_RED = 0xF8A3, C_GREEN = 0x4F2A, C_INK = 0x0841;

enum Page : uint8_t { PG_DRIVE, PG_FAULTS, PG_SETUP };
static Page page = PG_DRIVE;
static bool needStatic = true;
static unsigned long lastDyn = 0, msgUntil = 0;
static LinkType pendingLink;
static bool wasTouched = false;

struct Btn { int16_t x, y, w, h; };
static bool hit(const Btn &b, int x, int y) { return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h; }

static void drawBtn(const Btn &b, const String &label, bool active, uint16_t accent = C_GHOST, bool enabled = true) {
  uint16_t fill = active ? accent : C_PANEL, txt = active ? C_INK : enabled ? C_TXT : C_DIM;
  tft.fillRoundRect(b.x, b.y, b.w, b.h, 5, fill);
  tft.drawRoundRect(b.x, b.y, b.w, b.h, 5, active ? accent : C_LINE);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(txt, fill);
  tft.setTextPadding(0);
  tft.drawString(label, b.x + b.w / 2, b.y + b.h / 2 + 1, 2);
}

static void text(const String &s, int x, int y, int font, uint16_t col, uint8_t datum = TL_DATUM, int pad = 0) {
  tft.setTextDatum(datum);
  tft.setTextColor(col, C_BG);
  tft.setTextPadding(pad);
  tft.drawString(s, x, y, font);
}

// ---------------------------------------------------------------------------------- layout
static const Btn TABS[3] = {{0, 214, 106, 26}, {107, 214, 106, 26}, {214, 214, 106, 26}};
static const char *TAB_NAMES[3] = {"DRIVE", "FAULTS", "SETUP"};

// DRIVE
static const Btn B_MODE[6] = {{4, 128, 50, 26}, {56, 128, 50, 26}, {108, 128, 50, 26},
                              {160, 128, 50, 26}, {212, 128, 50, 26}, {264, 128, 52, 26}};
static const Btn S_THR = {48, 160, 268, 22}, S_SPD = {48, 186, 268, 22};
// FAULTS
static const Btn B_RAND = {4, 150, 102, 26}, B_CLEAR = {109, 150, 102, 26}, B_DEMO = {214, 150, 102, 26};
static const Btn B_MIL = {4, 182, 102, 26}, B_TCM = {109, 182, 102, 26}, B_KEY = {214, 182, 102, 26};
// SETUP
static const Btn B_PREV = {4, 38, 40, 38}, B_NEXT = {276, 38, 40, 38};
static const Btn B_LINK[3] = {{4, 110, 102, 26}, {109, 110, 102, 26}, {214, 110, 102, 26}};
static const Btn B_APPLY = {60, 182, 200, 26};

// ---------------------------------------------------------------------------------- top bar
static void drawTopBar() {
  tft.fillRect(0, 0, 320, 21, C_PANEL);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(C_GHOST, C_PANEL);
  tft.setTextPadding(0);
  tft.drawString("GHOSTCAR", 6, 11, 2);
}

static int lastConn = -1;
static String lastRate, lastProf;

static void drawTopDynamic() {
  bool conn = linkClientConnected();
  String prof = activeProfile().name;
  if (prof != lastProf) {
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(C_DIM, C_PANEL);
    tft.setTextPadding(130);
    tft.drawString(prof, 86, 11, 1);
    lastProf = prof;
  }
  if ((int)conn != lastConn) {
    tft.fillCircle(232, 10, 4, conn ? C_GREEN : C_DIM);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(C_TXT, C_PANEL);
    tft.setTextPadding(36);
    tft.drawString(linkShortName(linkType()), 240, 11, 2);
    lastConn = conn;
  }
  String rate = String((int)lroundf(elmRequestsPerSecond())) + "/s";
  if (rate != lastRate) {
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(C_GHOST, C_PANEL);
    tft.setTextPadding(40);
    tft.drawString(rate, 316, 11, 2);
    lastRate = rate;
  }
}

static void drawTabs() {
  for (int i = 0; i < 3; i++) drawBtn(TABS[i], TAB_NAMES[i], page == i);
}

// ---------------------------------------------------------------------------------- DRIVE
static void drawSliderTrack(const Btn &s, const char *label) {
  text(label, 4, s.y + s.h / 2, 2, C_DIM, ML_DATUM);
  tft.fillRoundRect(s.x, s.y + s.h / 2 - 3, s.w, 6, 3, C_PANEL);
}

static void drawSliderKnob(const Btn &s, float frac, int &lastPx, const String &valueText) {
  int px = s.x + 8 + (int)(frac * (s.w - 16));
  if (px == lastPx) return;
  if (lastPx >= 0) {                                       // erase the old knob, redraw the track under it
    tft.fillRect(lastPx - 9, s.y, 19, s.h, C_BG);
    tft.fillRect(max((int)s.x, lastPx - 9), s.y + s.h / 2 - 3, 19, 6, C_PANEL);
  }
  tft.fillRoundRect(s.x, s.y + s.h / 2 - 3, px - s.x, 6, 3, C_GHOST);
  tft.fillCircle(px, s.y + s.h / 2, 8, C_TXT);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_INK, C_TXT);
  tft.setTextPadding(0);
  tft.drawString(valueText, px, s.y + s.h / 2 + 1, 1);
  lastPx = px;
}

static int knobThr = -1, knobSpd = -1, lastModeKey = -1;

static void driveStatic() {
  text("km/h", 156, 64, 2, C_DIM);
  tft.drawRoundRect(236, 26, 78, 54, 6, C_LINE);
  tft.fillRoundRect(12, 86, 296, 12, 4, C_PANEL);
  static const char *lbl[4] = {"COOLANT", "THROTTLE", "LOAD", "BATTERY"};
  for (int i = 0; i < 4; i++) text(lbl[i], 8 + i * 79, 104, 1, C_DIM);
  drawSliderTrack(S_THR, "THR");
  drawSliderTrack(S_SPD, "SPD");
  knobThr = knobSpd = lastModeKey = -1;
}

static void driveDynamic() {
  const Profile &P = activeProfile();
  text(String((int)lroundf(car.speed)), 150, 28, 7, C_TXT, TR_DATUM, 140);
  text(String((int)lroundf(car.rpm)) + " rpm", 228, 44, 2, C_GHOST, TR_DATUM, 70);

  // gear box
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(car.sel == SEL_R ? C_AMBER : C_TXT, C_BG);
  tft.setTextSize(2);
  tft.setTextPadding(70);
  tft.drawString(car.ign == IGN_RUN ? carSelName() : car.ign == IGN_ON ? "ON" : "OFF", 275, 54, 4);
  tft.setTextSize(1);

  // rpm bar
  float red = P.redlineRpm ? P.redlineRpm : 1;
  int w = (int)(292 * constrain(car.rpm / red, 0.0f, 1.0f));
  uint16_t col = car.rpm > red * 0.9f ? C_RED : car.rpm > red * 0.75f ? C_AMBER : C_GHOST;
  tft.fillRect(14, 88, w, 8, col);
  tft.fillRect(14 + w, 88, 292 - w, 8, C_PANEL);

  // readouts
  String v[4] = {String((int)lroundf(car.coolant)) + "C", String((int)lroundf(car.throttle * 100)) + "%",
                 String((int)lroundf(car.load)) + "%",
                 (P.engine == ENG_EV || P.engine == ENG_HYBRID) ? String((int)lroundf(car.battSoc)) + "%"
                                                                 : String(car.volt, 1) + "V"};
  for (int i = 0; i < 4; i++) text(v[i], 8 + i * 79, 113, 2, C_TXT, TL_DATUM, 70);

  // mode buttons only when something changed
  int key = (autoMode ? 1 : 0) | (car.sel << 1) | (car.ign << 3);
  if (key != lastModeKey) {
    drawBtn(B_MODE[0], "AUTO", autoMode);
    static const char *sels[4] = {"P", "R", "N", "D"};
    static const Sel order[4] = {SEL_P, SEL_R, SEL_N, SEL_D};
    for (int i = 0; i < 4; i++) drawBtn(B_MODE[1 + i], sels[i], !autoMode && car.sel == order[i]);
    drawBtn(B_MODE[5], String("KEY ") + ignName(car.ign), car.ign != IGN_RUN, C_AMBER);
    lastModeKey = key;
  }
  float thr = autoMode ? car.throttle : manualThrottle;
  float spd = autoMode ? car.speed : manualSpeed;
  drawSliderKnob(S_THR, thr, knobThr, String((int)lroundf(thr * 100)));
  drawSliderKnob(S_SPD, constrain(spd / 200.0f, 0.0f, 1.0f), knobSpd, String((int)lroundf(spd)));
}

static float sliderValue(const Btn &s, int x) { return constrain((x - s.x - 8) / (float)(s.w - 16), 0.0f, 1.0f); }

static void driveTouch(int x, int y, bool pressed) {
  if (hit(S_THR, x, y) || hit(S_SPD, x, y)) {
    if (autoMode) { manualSpeed = car.speed; manualThrottle = car.throttle; if (car.sel != SEL_D) car.sel = SEL_D; }
    autoMode = false;
    if (hit(S_THR, x, y)) manualThrottle = sliderValue(S_THR, x);
    else manualSpeed = sliderValue(S_SPD, x) * 200.0f;
    return;
  }
  if (!pressed) return;
  if (hit(B_MODE[0], x, y)) carAuto();
  static const Sel order[4] = {SEL_P, SEL_R, SEL_N, SEL_D};
  for (int i = 0; i < 4; i++) if (hit(B_MODE[1 + i], x, y)) carSetSelector(order[i]);
  if (hit(B_MODE[5], x, y)) carSetIgnition(car.ign == IGN_RUN ? IGN_OFF : car.ign == IGN_OFF ? IGN_ON : IGN_RUN);
}

// ---------------------------------------------------------------------------------- FAULTS
static String lastFaultKey;

static void faultsStatic() { lastFaultKey = ""; }

static void faultsDynamic() {
  String key = String(faults.mil) + faults.nStored + "/" + faults.nPending + tcmOnline + car.ign;
  for (int i = 0; i < faults.nStored; i++) key += faults.stored[i];
  if (key == lastFaultKey) return;
  lastFaultKey = key;

  tft.fillRect(0, 24, 320, 122, C_BG);
  tft.fillCircle(16, 38, 8, faults.mil ? C_AMBER : C_PANEL);
  text(faults.mil ? "CHECK ENGINE ON" : "CHECK ENGINE OFF", 32, 38, 2, faults.mil ? C_AMBER : C_DIM, ML_DATUM);
  text(String(faults.nStored) + " stored  " + faults.nPending + " pending", 316, 38, 1, C_DIM, MR_DATUM);
  tft.drawFastHLine(4, 52, 312, C_LINE);

  int shown = 0;
  for (int i = 0; i < faults.nStored && shown < 8; i++, shown++)
    text(dtcToString(faults.stored[i]), 12 + (shown / 4) * 160, 60 + (shown % 4) * 21, 4, C_TXT);
  for (int i = 0; i < faults.nPending && shown < 8; i++, shown++) {
    int x = 12 + (shown / 4) * 160, y = 60 + (shown % 4) * 21;
    text(dtcToString(faults.pending[i]), x, y, 4, C_DIM);
    text("pending", x + 84, y + 8, 1, C_DIM);
  }
  if (!shown) text("No fault codes", 160, 96, 2, C_DIM, MC_DATUM);

  drawBtn(B_RAND, "+ RANDOM", false);
  drawBtn(B_CLEAR, "CLEAR ALL", false);
  drawBtn(B_DEMO, "DEMO CODES", false);
  drawBtn(B_MIL, faults.mil ? "MIL ON" : "MIL OFF", faults.mil, C_AMBER);
  bool tcm = activeProfile().hasTcm;
  drawBtn(B_TCM, tcm ? (tcmOnline ? "TCM ON" : "TCM OFF") : "NO TCM", tcm && !tcmOnline, C_AMBER, tcm);
  drawBtn(B_KEY, String("KEY ") + ignName(car.ign), car.ign != IGN_RUN, C_AMBER);
}

static void faultsTouch(int x, int y, bool pressed) {
  if (!pressed) return;
  if (hit(B_RAND, x, y)) faultsAddRandom();
  else if (hit(B_CLEAR, x, y)) faultsClearAll();
  else if (hit(B_DEMO, x, y)) faultsReset(activeProfile());
  else if (hit(B_MIL, x, y)) faults.mil = !faults.mil;
  else if (hit(B_TCM, x, y) && activeProfile().hasTcm) tcmOnline = !tcmOnline;
  else if (hit(B_KEY, x, y)) carSetIgnition(car.ign == IGN_RUN ? IGN_OFF : car.ign == IGN_OFF ? IGN_ON : IGN_RUN);
}

// ---------------------------------------------------------------------------------- SETUP
static int lastSetupKey = -1;

static void setupStatic() {
  text("CAR PROFILE", 6, 28, 1, C_DIM);
  text("CONNECTION", 6, 98, 1, C_DIM);
  drawBtn(B_PREV, "<", false);
  drawBtn(B_NEXT, ">", false);
  text("v" GHOSTCAR_VERSION, 316, 206, 1, C_DIM, BR_DATUM);
  lastSetupKey = -1;
}

static void setupDynamic() {
  const Profile &P = activeProfile();
  int key = activeProfileIndex() | (pendingLink << 8) | (linkClientConnected() << 12);
  if (key == lastSetupKey) return;
  lastSetupKey = key;

  tft.fillRect(46, 38, 228, 52, C_BG);
  text(String(activeProfileIndex() + 1) + "/" + PROFILE_COUNT + "  " + P.name, 160, 48, 2, C_TXT, MC_DATUM, 226);
  text(P.examples, 160, 68, 1, C_DIM, MC_DATUM, 226);
  text(String(protocolName(P.protocol)) + "  -  " + P.nPids + " PIDs", 160, 84, 1, C_GHOST, MC_DATUM, 300);

  for (int i = 0; i < LINK_COUNT; i++) {
    bool cur = linkType() == i, pend = pendingLink == i;
    static const char *labels[LINK_COUNT] = {"BT Classic", "BLE", "Wi-Fi"};
    drawBtn(B_LINK[i], labels[i], pend, cur ? C_GHOST : C_AMBER);
  }

  tft.fillRect(0, 140, 320, 72, C_BG);
  text("v" GHOSTCAR_VERSION, 316, 206, 1, C_DIM, BR_DATUM);
  if (linkType() == LINK_WIFI) {
    text(String("Wi-Fi  ") + WIFI_AP_SSID, 6, 146, 2, C_TXT);
    text("IP " + linkAddress(), 6, 164, 2, C_DIM);
  } else {
    text(String(linkName(linkType())) + "  \"" BT_DEVICE_NAME "\"", 6, 146, 2, C_TXT);
    text("MAC " + linkAddress(), 6, 164, 2, C_DIM);
  }
  text(linkClientConnected() ? "app connected" : "waiting for app", 316, 146, 1,
       linkClientConnected() ? C_GREEN : C_DIM, TR_DATUM);
  if (pendingLink != linkType()) drawBtn(B_APPLY, "APPLY & RESTART", true, C_AMBER);
}

static void setupTouch(int x, int y, bool pressed) {
  if (!pressed) return;
  if (hit(B_PREV, x, y)) appSetProfile((activeProfileIndex() + PROFILE_COUNT - 1) % PROFILE_COUNT);
  if (hit(B_NEXT, x, y)) appSetProfile((activeProfileIndex() + 1) % PROFILE_COUNT);
  for (int i = 0; i < LINK_COUNT; i++) if (hit(B_LINK[i], x, y)) pendingLink = (LinkType)i;
  if (pendingLink != linkType() && hit(B_APPLY, x, y)) {
    uiShowMessage("Restarting...");
    appSetLink(pendingLink);
  }
}

// ---------------------------------------------------------------------------------- main
void uiShowMessage(const char *msg) {
  tft.fillRoundRect(40, 90, 240, 50, 8, C_AMBER);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_INK, C_AMBER);
  tft.setTextPadding(0);
  tft.drawString(msg, 160, 116, 4);
  msgUntil = millis() + 1500;
}

void uiBegin() {
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(C_BG);
  touchSpi.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  ts.begin(touchSpi);
  ts.setRotation(1);
  pendingLink = linkType();

  // splash
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_GHOST, C_BG);
  tft.drawString("GHOSTCAR", 160, 100, 4);
  tft.setTextColor(C_DIM, C_BG);
  tft.drawString("ELM327 OBD-II emulator  v" GHOSTCAR_VERSION, 160, 130, 2);
  delay(900);
}

void uiLoop() {
  unsigned long now = millis();
  if (msgUntil && now < msgUntil) return;
  if (msgUntil) { msgUntil = 0; needStatic = true; }

  // touch
  bool touched = ts.touched();
  if (touched) {
    TS_Point p = ts.getPoint();
    int x = constrain(map(p.x, TOUCH_X_MIN, TOUCH_X_MAX, 0, 320), 0, 319);
    int y = constrain(map(p.y, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, 240), 0, 239);
    bool pressed = !wasTouched;
    if (pressed) {
      for (int i = 0; i < 3; i++)
        if (hit(TABS[i], x, y) && page != i) { page = (Page)i; needStatic = true; }
    }
    if (!needStatic) {
      if (page == PG_DRIVE) driveTouch(x, y, pressed);
      else if (page == PG_FAULTS) faultsTouch(x, y, pressed);
      else setupTouch(x, y, pressed);
    }
  }
  wasTouched = touched;

  if (needStatic) {
    tft.fillScreen(C_BG);
    drawTopBar();
    drawTabs();
    if (page == PG_DRIVE) driveStatic();
    else if (page == PG_FAULTS) faultsStatic();
    else setupStatic();
    needStatic = false;
    lastDyn = 0;
    lastConn = -1;
    lastRate = lastProf = "";
  }
  if (now - lastDyn < 80) return;
  lastDyn = now;
  drawTopDynamic();
  if (page == PG_DRIVE) driveDynamic();
  else if (page == PG_FAULTS) faultsDynamic();
  else setupDynamic();
}

#endif
