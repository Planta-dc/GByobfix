/* A stand-in for newlib's reent.h, for checking the 3DS port's syntax
 * on a host.
 *
 * The real sys/iosupport.h includes <reent.h>, so the stand-in for it needs
 * one too. The port only ever passes a struct _reent around, so the host
 * version is the small one.
 */
#ifndef _REENT_H_
#define _REENT_H_

#include <sys/reent.h>

#endif
