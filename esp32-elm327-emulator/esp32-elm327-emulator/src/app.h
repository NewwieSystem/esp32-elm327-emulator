// Actions shared by the touchscreen and the serial console.
#pragma once
#include "link.h"

void appSetProfile(int index);     // switch car now and remember it
void appSetLink(LinkType t);       // remember the connection type and restart
