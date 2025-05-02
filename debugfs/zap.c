/*
 * zap.c --- zap block
 *
 * Copyright (C) 2012 Theodore Ts'o.  This file may be redistributed
 * under the terms of the GNU Public License.
 */

#include "config.h"
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <time.h>
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#include <sys/types.h>
#ifdef HAVE_GETOPT_H
#include <getopt.h>
#else
extern int optind;
extern char *optarg;
#endif

#include "debugfs.h"

void do_zap_block(int argc, ss_argv_t argv, int sci_idx EXT2FS_ATTR((unused)),
		    void *infop EXT2FS_ATTR((unused)))
{
	unsigned long	pattern = 0;
	unsigned char	*buf;
	ext2_ino_t	inode;
	errcode_t	errcode;
	blk64_t		block;
	char		*file = NULL;
	int		c, err;
	int		offset = -1;
	int		length = -1;
	int		bit = -1;

	if (check_fs_open(argv[0]))
		return;
	if (check_fs_read_write(argv[0]))
		return;

	reset_getopt();
	while ((c = getopt (argc, argv, "b:f:l:o:p:")) != EOF) {
		switch (c) {
		case 'f':
			file = optarg;
			break;
		case 'b':
			bit = parse_ulong(optarg, argv[0],
					  "bit", &err);
			if (err)
				return;
			if (bit >= (int) current_fs->blocksize * 8) {
				com_err(argv[0], 0, "The bit to flip "
					"must be within a %d block\n",
					current_fs->blocksize);
				return;
			}
			break;
		case 'p':
			pattern = parse_ulong(optarg, argv[0],
					      "pattern", &err);
			if (err)
				return;
			if (pattern >= 256) {
				com_err(argv[0], 0, "The fill pattern must "
					"be an 8-bit value\n");
				return;
			}
			break;
		case 'o':
			offset = parse_ulong(optarg, argv[0],
					     "offset", &err);
			if (err)
				return;
			if (offset >= (int) current_fs->blocksize) {
				com_err(argv[0], 0, "The offset must be "
					"within a %d block\n",
					current_fs->blocksize);
				return;
			}
			break;

			break;
		case 'l':
			length = parse_ulong(optarg, argv[0],
					     "length", &err);
			if (err)
				return;
			break;
		default:
			goto print_usage;
		}
	}

	if (bit > 0 && offset > 0) {
		com_err(argv[0], 0, "The -o and -b options can not be mixed.");
		return;
	}

	if (offset < 0)
		offset = 0;
	if (length < 0)
		length = current_fs->blocksize - offset;
	if ((offset + length) > (int) current_fs->blocksize) {
		com_err(argv[0], 0, "The specified length is too bug\n");
		return;
	}

	if (argc != optind+1) {
	print_usage:
		com_err(0, 0, "Usage:\tzap_block [-f file] [-o offset] "
			"[-l length] [-p pattern] block_num");
		com_err(0, 0, "\tzap_block [-f file] [-b bit] "
			"block_num");
		return;
	}

	block = parse_ulonglong(argv[optind], argv[0], "block", &err);
	if (err)
		return;

	if (file) {
		inode = string_to_inode(file);
		if (!inode)
			return;
		errcode = ext2fs_bmap2(current_fs, inode, 0, 0, 0,
				       block, 0, &block);
		if (errcode) {
			com_err(argv[0], errcode,
				"while mapping logical block %llu\n",
				(unsigned long long) block);
			return;
		}
	}

	buf = malloc(current_fs->blocksize);
	if (!buf) {
		com_err(argv[0], 0, "Couldn't allocate block buffer");
		return;
	}

	errcode = io_channel_read_blk64(current_fs->io, block, 1, buf);
	if (errcode) {
		com_err(argv[0], errcode,
			"while reading block %llu\n",
			(unsigned long long) block);
		goto errout;
	}

	if (bit >= 0)
		buf[bit >> 3] ^= 1 << (bit & 7);
	else
		memset(buf+offset, pattern, length);

	errcode = io_channel_write_blk64(current_fs->io, block, 1, buf);
	if (errcode) {
		com_err(argv[0], errcode,
			"while write block %llu\n",
			(unsigned long long) block);
		goto errout;
	}

errout:
	free(buf);
	return;
}

void do_block_dump(int argc, ss_argv_t argv, int sci_idx EXT2FS_ATTR((unused)),
		    void *infop EXT2FS_ATTR((unused)))
{
	unsigned char	*buf;
	ext2_ino_t	inode;
	errcode_t	errcode;
	blk64_t		block;
	char		*file = NULL;
	int		xattr_dump = 0;
	int		c, err;

	if (check_fs_open(argv[0]))
		return;

	reset_getopt();
	while ((c = getopt (argc, argv, "f:x")) != EOF) {
		switch (c) {
		case 'f':
			file = optarg;
			break;
		case 'x':
			xattr_dump = 1;
			break;
		default:
			goto print_usage;
		}
	}

	if (argc != optind + 1) {
	print_usage:
		com_err(0, 0, "Usage: block_dump [-x] [-f inode] block_num");
		return;
	}

	block = parse_ulonglong(argv[optind], argv[0], "block", &err);
	if (err)
		return;

	if (file) {
		inode = string_to_inode(file);
		if (!inode)
			return;
		errcode = ext2fs_bmap2(current_fs, inode, 0, 0, 0,
				       block, 0, &block);
		if (errcode) {
			com_err(argv[0], errcode,
				"while mapping logical block %llu\n",
				(unsigned long long) block);
			return;
		}
	}

	buf = malloc(current_fs->blocksize);
	if (!buf) {
		com_err(argv[0], 0, "Couldn't allocate block buffer");
		return;
	}

	errcode = io_channel_read_blk64(current_fs->io, block, 1, buf);
	if (errcode) {
		com_err(argv[0], errcode,
			"while reading block %llu\n",
			(unsigned long long) block);
		goto errout;
	}

	if (xattr_dump)
		block_xattr_dump(stdout, buf, current_fs->blocksize);
	else
		do_byte_hexdump(stdout, buf, current_fs->blocksize);
errout:
	free(buf);
}
