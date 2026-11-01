/* A stand-in for newlib's dirent.h, for checking the 3DS port's syntax on
 * a host. Only the parts the port uses are here. */
#ifndef _DIRENT_H_
#define _DIRENT_H_

#include <sys/types.h>

#define DT_UNKNOWN 0
#define DT_DIR     4
#define DT_REG     8

struct dirent {
    ino_t d_ino;
    unsigned char d_type;
    char d_name[256];
};

#endif
