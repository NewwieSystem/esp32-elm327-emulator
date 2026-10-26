// Touchscreen control panel for the ESP32-2432S028 "Cheap Yellow Display".
// On boards without a screen (GHOSTCAR_CYD not defined) these do nothing.
#pragma once

void uiBegin();
void uiLoop();
void uiShowMessage(const char *text);   // short banner, e.g. "Restarting..."
