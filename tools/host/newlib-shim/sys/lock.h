/* A stand-in for newlib's sys/lock.h, so that the libctru headers can be
 * read by a host compiler when checking the 3DS port's syntax. */
#ifndef _SYS_LOCK_H_
#define _SYS_LOCK_H_
#include <sys/cdefs.h>

typedef struct {
  int __lock;
} _LOCK_T;

typedef struct {
  int __lock;
  int __counter;
} _LOCK_RECURSIVE_T;

extern void __lock_acquire (_LOCK_T *);
extern void __lock_release (_LOCK_T *);
extern void __lock_acquire_recursive (_LOCK_RECURSIVE_T *);
extern void __lock_release_recursive (_LOCK_RECURSIVE_T *);

#endif
