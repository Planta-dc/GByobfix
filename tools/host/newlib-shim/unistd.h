/* A stand-in for newlib's unistd.h, for checking the 3DS port's syntax on
 * a host. Only the parts the port uses are here. */
#ifndef _UNISTD_H_
#define _UNISTD_H_

#include <sys/types.h>

#define F_OK 0
#define R_OK 4
#define W_OK 2
#define X_OK 1

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

int access(const char *path, int mode);

#endif
