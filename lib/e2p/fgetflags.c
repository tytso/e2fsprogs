/*
 * fgetflags.c		- Get a file flags on an ext2 file system
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

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include <linux/ext2_fs.h>

#include "e2p.h"

int fgetflags (const char * name, unsigned long * flags)
{
	int fd;
	int r;

	fd = open (name, O_RDONLY);
	if (fd == -1)
		return -1;
	r = ioctl (fd, EXT2_IOC_GETFLAGS, flags);
	close (fd);
	return r;
}
