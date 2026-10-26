#include "stdlib_noniso.h"
char *itoa(int v, char *s, int r) { return ltoa(v, s, r); }
char *utoa(unsigned v, char *s, int r) { return ultoa(v, s, r); }
