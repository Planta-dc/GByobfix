/* A stand-in for newlib's sys/iosupport.h, for checking the 3DS port's
 * syntax on a host. Only the parts the port uses are here. */
#ifndef _SYS_IOSUPPORT_H_
#define _SYS_IOSUPPORT_H_

#include <sys/types.h>
#include <sys/reent.h>

#define STD_IN  0
#define STD_OUT 1
#define STD_ERR 2

typedef ssize_t (*ssize_write)(struct _reent *r, int fd, const char *ptr, size_t len);
typedef ssize_t (*ssize_read)(struct _reent *r, int fd, char *ptr, size_t len);
typedef off_t (*off_seek)(struct _reent *r, int fd, off_t pos, int dir);
typedef int (*int_close)(struct _reent *r, int fd);
typedef int (*int_unlink)(struct _reent *r, const char *path);
typedef int (*int_stat)(struct _reent *r, const char *path, struct stat *st);
typedef int (*int_rename)(struct _reent *r, const char *from, const char *to);
typedef void *(*void_open)(struct _reent *r, const char *path, int flags, int mode);
typedef int (*int_open)(struct _reent *r, const char *path, int flags, int mode);

typedef struct {
  const char *name;
  int flags;
  void *device_data;
  int (*open_r)(struct _reent *r, const char *path, int flags, int mode);
  ssize_write write_r;
  ssize_read read_r;
  off_seek seek_r;
  int (*close_r)(struct _reent *r, int fd);
  int (*unlink_r)(struct _reent *r, const char *path);
  int (*stat_r)(struct _reent *r, const char *path, struct stat *st);
  int (*rename_r)(struct _reent *r, const char *from, const char *to);
} devoptab_t;

extern devoptab_t *devoptab_list[];

#endif
