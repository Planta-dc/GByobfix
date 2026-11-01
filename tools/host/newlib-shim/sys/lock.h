/* A stand-in for newlib's sys/lock.h, so that the libctru headers can be
 * read by a host compiler when checking the 3DS port's syntax.
 *
 * The real header is this: when the locks are not retargetable, the lock
 * routines are macros that do nothing, because newlib on a bare-metal
 * target is single-threaded. There are no lock functions to link against.
 */
#ifndef __SYS_LOCK_H__
#define __SYS_LOCK_H__

typedef int _LOCK_T;
typedef int _LOCK_RECURSIVE_T;

#define __LOCK_INIT(class,lock) static int lock = 0;
#define __LOCK_INIT_RECURSIVE(class,lock) static int lock = 0;
#define __lock_init(lock) ((void) 0)
#define __lock_init_recursive(lock) ((void) 0)
#define __lock_close(lock) ((void) 0)
#define __lock_close_recursive(lock) ((void) 0)
#define __lock_acquire(lock) ((void) 0)
#define __lock_acquire_recursive(lock) ((void) 0)
#define __lock_try_acquire(lock) ((void) 0)
#define __lock_try_acquire_recursive(lock) ((void) 0)
#define __lock_release(lock) ((void) 0)
#define __lock_release_recursive(lock) ((void) 0)

#endif
