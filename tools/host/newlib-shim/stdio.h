/* A stand-in for newlib's stdio.h, for checking the 3DS port's syntax on
 * a host. The only thing the port needs that the host does not have is
 * siscanf, which is sscanf without the "s" in front. */
#ifndef _STDIO_SHIM_H_
#define _STDIO_SHIM_H_
#include_next <stdio.h>
#define siscanf sscanf
#define iprintf printf
#endif
