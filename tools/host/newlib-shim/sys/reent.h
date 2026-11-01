/* A stand-in for newlib's sys/reent.h, for checking the 3DS port's syntax
 * on a host. */
#ifndef _SYS_REENT_H_
#define _SYS_REENT_H_
struct _reent { int _errno; };
#endif
