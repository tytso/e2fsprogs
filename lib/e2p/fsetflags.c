/*
 * fsetflags.c		- Set a file flags on an ext2 file system
 *
 * Copyright (C) 1993, 1994  Remy Card <card@masi.ibp.fr>
 *                           Laboratoire MASI, Institut Blaise Pascal
 *                           Universite Pierre et Marie Curie (Paris VI)
 *
 * This file can be redistributed under the terms of the GNU Library General
 * Public License
 */

/*
 * History:
 * 93/10/30	- Creation
 */

#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE

#if HAVE_ERRNO_H
#include <errno.h>
#endif
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#if HAVE_CHFLAGS
#include <sys/stat.h>		/* For the flag values.  */
#else
#include <fcntl.h>
#include <sys/ioctl.h>
#endif

#include "e2p.h"

#ifdef O_LARGEFILE
#define OPEN_FLAGS (O_RDONLY|O_NONBLOCK|O_LARGEFILE)
#else
#define OPEN_FLAGS (O_RDONLY|O_NONBLOCK)
#endif

int fsetflags (const char * name, unsigned long flags)
{
#if HAVE_CHFLAGS
	unsigned long bsd_flags = 0;

#ifdef UF_IMMUTABLE
	if (flags & EXT2_IMMUTABLE_FL)
		bsd_flags |= UF_IMMUTABLE;
#endif
#ifdef UF_APPEND
	if (flags & EXT2_APPEND_FL)
		bsd_flags |= UF_APPEND;
#endif
#ifdef UF_NODUMP
	if (flags & EXT2_NODUMP_FL)
		bsd_flags |= UF_NODUMP;
#endif

	return chflags (name, bsd_flags);
#else
#if HAVE_EXT2_IOCTLS
	int fd, r, f;

	fd = open (name, OPEN_FLAGS);
	if (fd == -1)
		return -1;
	f = (int) flags;
	r = ioctl (fd, EXT2_IOC_SETFLAGS, &f);
	close (fd);
	return r;
#else /* ! HAVE_EXT2_IOCTLS */
	extern int errno;
	errno = EOPNOTSUPP;
	return -1;
#endif /* ! HAVE_EXT2_IOCTLS */
#endif
}
