/*
 * getsize.c --- get the size of a partition.
 * 
 * Copyright (C) 1995, 1995 Theodore Ts'o.
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
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
#endif
#ifdef HAVE_SYS_DISKLABEL_H
#include <sys/ioctl.h>
#include <sys/disklabel.h>
#endif /* HAVE_SYS_DISKLABEL_H */

#if defined(__linux__) && defined(_IO) && !defined(BLKGETSIZE)
#define BLKGETSIZE _IO(0x12,96)	/* return device size */
#endif

#include "ext2_fs.h"
#include "ext2fs.h"

static int valid_offset (int fd, ext2_loff_t offset)
{
	char ch;

	if (ext2fs_llseek (fd, offset, 0) < 0)
		return 0;
	if (read (fd, &ch, 1) < 1)
		return 0;
	return 1;
}

/*
 * Returns the number of blocks in a partition
 */
errcode_t ext2fs_get_device_size(const char *file, int blocksize,
				 blk_t *retblocks)
{
	int	fd;
#ifdef BLKGETSIZE
	unsigned long	size;
#endif
	ext2_loff_t high, low;
#ifdef FDGETPRM
	struct floppy_struct this_floppy;
#endif
#ifdef HAVE_SYS_DISKLABEL_H
	int part;
	struct disklabel lab;
	struct partition *pp;
	char ch;
#endif /* HAVE_SYS_DISKLABEL_H */

#ifdef HAVE_OPEN64
	fd = open64(file, O_RDONLY);
#else
	fd = open(file, O_RDONLY);
#endif
	if (fd < 0)
		return errno;

#ifdef BLKGETSIZE
	if (ioctl(fd, BLKGETSIZE, &size) >= 0) {
		close(fd);
		*retblocks = size / (blocksize / 512);
		return 0;
	}
#endif
#ifdef FDGETPRM
	if (ioctl(fd, FDGETPRM, &this_floppy) >= 0) {
		close(fd);
		*retblocks = this_floppy.size / (blocksize / 512);
		return 0;
	}
#endif
#ifdef HAVE_SYS_DISKLABEL_H
	part = strlen(file) - 1;
	if (part >= 0) {
		ch = file[part];
		if (isdigit(ch))
			part = 0;
		else if (ch >= 'a' && ch <= 'h')
			part = ch - 'a';
		else
			part = -1;
	}
	if (part >= 0 && (ioctl(fd, DIOCGDINFO, (char *)&lab) >= 0)) {
		pp = &lab.d_partitions[part];
		if (pp->p_size) {
			close(fd);
			*retblocks = pp->p_size / (blocksize / 512);
			return 0;
		}
	}
#endif /* HAVE_SYS_DISKLABEL_H */

	/*
	 * OK, we couldn't figure it out by using a specialized ioctl,
	 * which is generally the best way.  So do binary search to
	 * find the size of the partition.
	 */
	low = 0;
	for (high = 1024; valid_offset (fd, high); high *= 2)
		low = high;
	while (low < high - 1)
	{
		const ext2_loff_t mid = (low + high) / 2;

		if (valid_offset (fd, mid))
			low = mid;
		else
			high = mid;
	}
	valid_offset (fd, 0);
	close(fd);
	*retblocks = (low + 1) / blocksize;
	return 0;
}

#ifdef DEBUG
int main(int argc, char **argv)
{
	blk_t	blocks;
	int	retval;
	
	if (argc < 2) {
		fprintf(stderr, "Usage: %s device\n", argv[0]);
		exit(1);
	}

	retval = ext2fs_get_device_size(argv[1], 1024, &blocks);
	if (retval) {
		com_err(argv[0], retval,
			"while calling ext2fs_get_device_size");
		exit(1);
	}
	printf("Device %s has %d 1k blocks.\n", argv[1], blocks);
	exit(0);
}
#endif
