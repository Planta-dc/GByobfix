/* A stand-in for newlib's unistd.h, for checking the 3DS port's syntax
 * on a host. Only the parts the port uses are here.
 *
 * The real header wraps its declarations in extern "C" when it is read by
 * a C++ compiler. That matters: without the guard, a C++ file that calls
 * access() asks for a mangled _Z6accessPKci, which no C library defines,
 * and the link fails. The guard is what the real SDK has, so the stand-in
 * has it too.
 */
#ifndef _UNISTD_H_
#define _UNISTD_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>

#define F_OK 0
#define R_OK 4
#define W_OK 2
#define X_OK 1

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

int access(const char *path, int mode);

#ifdef __cplusplus
}
#endif

#endif
