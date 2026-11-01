/* The platform's console. The renderer does not use it. */
#pragma once

#include <3ds.h>

/* The APU prints to the console when the noise channel's frequency
 * changes. */
void printLog(const char* format, ...);
