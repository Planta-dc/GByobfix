/* A stand-in for newlib's sys/stat.h, for checking the 3DS port's syntax
 * on a host. */
#ifndef _SYS_STAT_H_
#define _SYS_STAT_H_
#include <sys/types.h>
struct stat { unsigned short st_mode; long st_size; };
#endif
