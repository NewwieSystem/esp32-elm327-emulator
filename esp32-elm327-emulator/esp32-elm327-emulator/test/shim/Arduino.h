#pragma once
#include <cstdint>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <cctype>
#include <cstdio>
#include <algorithm>
#include "WString.h"
using std::min; using std::max;
extern unsigned long fakeMillis;
inline unsigned long millis() { return fakeMillis; }
inline void delay(unsigned long ms) { fakeMillis += ms; }
inline long random(long n) { return rand() % n; }
inline bool isDigit(char c) { return isdigit((unsigned char)c); }
#define constrain(v, lo, hi) ((v) < (lo) ? (lo) : (v) > (hi) ? (hi) : (v))
struct FakeSerial { template<class T> void print(T) {} template<class T> void println(T) {} void println() {} int printf(const char*, ...) { return 0; } int available() { return 0; } int read() { return -1; } };
extern FakeSerial Serial;
