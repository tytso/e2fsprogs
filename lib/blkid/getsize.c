/*
 * getsize.c --- get the size of a partition.
 *
 * Copyright (C) 1995, 1995 Theodore Ts'o.
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 * %End-Header%
 */

#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE

#include <stdio.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#if HAVE_ERRNO_H
#include <errno.h>
#endif
#include <fcntl.h>
#ifdef HAVE_LINUX_FD_H
#include <sys/ioctl.h>
#include <linux/fd.h>
#endif /* HAVE_LINUX_FD_H */
#ifdef HAVE_SYS_DISKLABEL_H
#include <sys/ioctl.h>
#include <sys/disklabel.h>
#include <sys/stat.h>
#endif /* HAVE_SYS_DISKLABEL_H */

#include "blkidP.h"

#if defined(__linux__) && defined(_IO) && !defined(BLKGETSIZE)
#define BLKGETSIZE _IO(0x12,96)	/* return device size */
#endif

#ifdef APPLE_DARWIN
#include <sys/ioctl.h>
#include <sys/disk.h>

#define BLKGETSIZE DKIOCGETBLOCKCOUNT32
#endif /* APPLE_DARWIN */

static int valid_offset(int fd, blkid_loff_t offset)
{
	char ch;

	if (blkid_llseek(fd, offset, 0) < 0)
		return 0;
	if (read(fd, &ch, 1) < 1)
		return 0;
	return 1;
}

/*
 * Returns the number of blocks in a partition
 */
blkid_loff_t blkid_get_dev_size(int fd)
{
#ifdef BLKGETSIZE
	unsigned long size;
#endif
	blkid_loff_t high, low;
#ifdef FDGETPRM
	struct floppy_struct this_floppy;
#endif
#ifdef HAVE_SYS_DISKLABEL_H
	int part = -1;
	struct disklabel lab;
	struct partition *pp;
	char ch;
	struct stat st;
#endif /* HAVE_SYS_DISKLABEL_H */

#ifdef BLKGETSIZE
	if (ioctl(fd, BLKGETSIZE, &size) >= 0)
		return (blkid_loff_t)size << 9;
#endif
#ifdef FDGETPRM
	if (ioctl(fd, FDGETPRM, &this_floppy) >= 0)
		return (blkid_loff_t)this_floppy.size << 9;
#endif
#ifdef HAVE_SYS_DISKLABEL_H
#if 0
	/*
	 * This should work in theory but I haven't tested it.  Anyone
	 * on a BSD system want to test this for me?  In the meantime,
	 * binary search mechanism should work just fine.
	 */
	if ((fstat(fd, &st) >= 0) && S_ISBLK(st.st_mode))
		part = st.st_rdev & 7;
	if (part >= 0 && (ioctl(fd, DIOCGDINFO, (char *)&lab) >= 0)) {
		pp = &lab.d_partitions[part];
		if (pp->p_size)
			return pp->p_size << 9;
	}
#endif
#endif /* HAVE_SYS_DISKLABEL_H */

	/*
	 * OK, we couldn't figure it out by using a specialized ioctl,
	 * which is generally the best way.  So do binary search to
	 * find the size of the partition.
	 */
	low = 0;
	for (high = 1024; valid_offset(fd, high); high *= 2)
		low = high;
	while (low < high - 1)
	{
		const blkid_loff_t mid = (low + high) / 2;

		if (valid_offset(fd, mid))
			low = mid;
		else
			high = mid;
	}
	return low + 1;
}

#ifdef TEST_PROGRAM
int main(int argc, char **argv)
{
	blkid_loff_t bytes;
	int	fd;

	if (argc < 2) {
		fprintf(stderr, "Usage: %s device\n"
			"Determine the size of a device\n", argv[0]);
		return 1;
	}

	if ((fd = open(argv[1], O_RDONLY)) < 0)
		perror(argv[0]);

	bytes = blkid_get_dev_size(fd);
	printf("Device %s has %Ld 1k blocks.\n", argv[1], bytes >> 10);

	return 0;
}
#endif
