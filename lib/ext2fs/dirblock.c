/*
 * dirblock.c --- directory block routines.
 * 
 * Copyright (C) 1995, 1996 Theodore Ts'o.
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 */

#include <stdio.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <string.h>
#include <time.h>

#include "ext2_fs.h"
#include "ext2fs.h"

errcode_t ext2fs_read_dir_block(ext2_filsys fs, blk_t block,
				void *buf)
{
	errcode_t	retval;
	char		*p, *end;
	struct ext2_dir_entry *dirent;
	struct ext2_dir_entry_2 *dirent2;
	unsigned int	rec_len, do_swap;

 	retval = io_channel_read_blk(fs->io, block, 1, buf);
	if (retval)
		return retval;
	do_swap = (fs->flags & (EXT2_FLAG_SWAP_BYTES|
				EXT2_FLAG_SWAP_BYTES_READ)) != 0;
	p = (char *) buf;
	end = (char *) buf + fs->blocksize;
	while (p < end-8) {
		dirent = (struct ext2_dir_entry *) p;
		if (do_swap) {
			dirent->inode = ext2fs_swab32(dirent->inode);
			dirent->rec_len = ext2fs_swab16(dirent->rec_len);
			dirent->name_len = ext2fs_swab16(dirent->name_len);
		}
		rec_len = dirent->rec_len;
		if ((rec_len < 8) || (rec_len % 4)) {
			rec_len = 8;
			retval = EXT2_ET_DIR_CORRUPTED;
		}
		dirent2 = (struct ext2_dir_entry_2 *) dirent;
		if ((dirent2->name_len +8) > dirent2->rec_len)
			retval = EXT2_ET_DIR_CORRUPTED;
		p += rec_len;
	}
	return retval;
}

errcode_t ext2fs_write_dir_block(ext2_filsys fs, blk_t block,
				 void *inbuf)
{
	errcode_t	retval;
	char		*p, *end, *write_buf;
	char		*buf = 0;
	struct ext2_dir_entry *dirent;

	if ((fs->flags & EXT2_FLAG_SWAP_BYTES) ||
	    (fs->flags & EXT2_FLAG_SWAP_BYTES_WRITE)) {
		retval = ext2fs_get_mem(fs->blocksize, (void **) &buf);
		if (retval)
			return retval;
		write_buf = buf;
		memcpy(buf, inbuf, fs->blocksize);
		p = buf;
		end = buf + fs->blocksize;
		while (p < end) {
			dirent = (struct ext2_dir_entry *) p;
			if ((dirent->rec_len < 8) ||
			    (dirent->rec_len % 4)) {
				retval = EXT2_ET_DIR_CORRUPTED;
				goto errout;
			}
			p += dirent->rec_len;
			dirent->inode = ext2fs_swab32(dirent->inode);
			dirent->rec_len = ext2fs_swab16(dirent->rec_len);
			dirent->name_len = ext2fs_swab16(dirent->name_len);
		}
	} else
		write_buf = (char *) inbuf;
 	retval = io_channel_write_blk(fs->io, block, 1, write_buf);
errout:
	if (buf)
		ext2fs_free_mem((void **) &buf);
	return retval;
}


