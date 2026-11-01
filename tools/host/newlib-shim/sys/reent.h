/* A stand-in for newlib's sys/reent.h, for checking the 3DS port's syntax
 * on a host.
 *
 * The real header wraps its declarations in extern "C" when it is read by
 * a C++ compiler. The stand-in does the same, so a C++ file that passes a
 * struct _reent* to a C function asks for the C name and not a mangled one.
 */
#ifndef _SYS_REENT_H_
#define _SYS_REENT_H_

#ifdef __cplusplus
extern "C" {
#endif

struct _reent { int _errno; };

#ifdef __cplusplus
}
#endif

#endif
