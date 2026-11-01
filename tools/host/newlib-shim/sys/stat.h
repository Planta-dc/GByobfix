/* A stand-in for newlib's sys/stat.h, for checking the 3DS port's syntax
 * on a host.
 *
 * The real header wraps its declarations in extern "C" when it is read by
 * a C++ compiler, so the stand-in does the same.
 */
#ifndef _SYS_STAT_H_
#define _SYS_STAT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>

struct stat { unsigned short st_mode; long st_size; };

#ifdef __cplusplus
}
#endif

#endif
