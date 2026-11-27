/*
 * do_sbrk.h --- helper function for systems that don't support sbrk()
 *
 * Theodore Ts'o <tytso@mit.edu>
 */

#include "config.h"
#include <errno.h>
#include <stddef.h>
#include <unistd.h>

static inline void *compat_sbrk(int incr)
{
#if defined(HAVE_SBRK) && !defined(__APPLE__)
	return sbrk(incr);
#else
	errno = 0;
	return NULL;
#endif
}

extern int compat_daemon(int nochdir, int noclose);
