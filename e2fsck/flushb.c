/*
 * flushb.c --- This routine flushes the disk buffers for a disk
 *
 * Copyright 1997, 2000, by Theodore Ts'o.
 * 
 * This program may be used under the provisions of the GNU Public
 * License, *EXCEPT* that a binary copy of the executable may not be
 * packaged as a part of binary package which is distributed as part
 * of a Linux distribution.  (Yes, this violates the Debian Free
 * Software Guidelines of restricting its field of use.  That's the
 * point.  I don't want this program being distributed in Debian,
 * because I don't care to support it, and the maintainer, Yann
 * Dirson, doesn't seem to pay attention to my wishes on this matter.
 * So I'm deliberately adding this clause so it violates the Debian
 * Free Software Guidelines to force him to take it out.  (What part
 * of THIS IS FOR MY OWN USE don't you understand?  And no, I'm going
 * to write a man page for it either.  And don't file a bug about it
 * or bug me about it.)  If this doesn't work, I'll have to remove it
 * from the upstream source distribution at the next release.  End of
 * Rant.  :-)
 * 
 * (BTW, use of flushb on some older 2.2 kernels on a heavily loaded
 * system will corrupt filesystems.)
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include "../misc/nls-enable.h"

/* For Linux, define BLKFLSBUF if necessary */
#if (!defined(BLKFLSBUF) && defined(__linux__))
#define BLKFLSBUF	_IO(0x12,97)	/* flush buffer cache */
#endif

const char *progname;

static void usage(void)
{
	fprintf(stderr, _("Usage: %s disk\n"), progname);
	exit(1);
}	
	
int main(int argc, char **argv)
{
	int	fd;
	
	progname = argv[0];
	if (argc != 2)
		usage();

	fd = open(argv[1], O_RDONLY, 0);
	if (fd < 0) {
		perror("open");
		exit(1);
	}
	/*
	 * Note: to reread the partition table, use the ioctl
	 * BLKRRPART instead of BLKFSLBUF.
	 */
#ifdef BLKFLSBUF
	if (ioctl(fd, BLKFLSBUF, 0) < 0) {
		perror("ioctl BLKFLSBUF");
		exit(1);
	}
	return 0;
#else
	fprintf(stderr,
		_("BLKFLSBUF ioctl not supported!  Can't flush buffers.\n"));
	return 1;
#endif
}
