/*
 * closefs.c --- close an ext2 filesystem
 * 
 * Copyright (C) 1993, 1994 Theodore Ts'o.  This file may be redistributed
 * under the terms of the GNU Public License.
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>

#include <linux/fs.h>
#include <linux/ext2_fs.h>

#include "ext2fs.h"

errcode_t ext2fs_flush(ext2_filsys fs)
{
	int		i,j;
	int		group_block;
	errcode_t	retval;
	char		*group_ptr;
	
	/*
	 * Write out master superblock.  This has to be done
	 * separately, since it is located at a fixed location
	 * (SUPERBLOCK_OFFSET).
	 */
	fs->super->s_wtime = time(NULL);
	io_channel_set_blksize(fs->io, SUPERBLOCK_OFFSET);
	retval = io_channel_write_blk(fs->io, 1, -SUPERBLOCK_SIZE, fs->super);
	if (retval)
		return retval;
	io_channel_set_blksize(fs->io, fs->blocksize);

	/*
	 * Write out the master group descriptors, and the backup
	 * superblocks and group descriptors.
	 */
	group_block = fs->super->s_first_data_block;
	for (i = 0; i < fs->group_desc_count; i++) {
		if (i !=0 ) {
			retval = io_channel_write_blk(fs->io, group_block,
						      -SUPERBLOCK_SIZE,
						      fs->super);
			if (retval)
				return retval;
		}
		group_ptr = (char *) fs->group_desc;
		for (j=0; j < fs->desc_blocks; j++) {
			retval = io_channel_write_blk(fs->io,
						      group_block+1+j, 1,
						      group_ptr);
			if (retval)
				return retval;
			group_ptr += fs->blocksize;
		}
		group_block += EXT2_BLOCKS_PER_GROUP(fs->super);
	}

	/*
	 * If the write_bitmaps() function is present, call it to
	 * flush the bitmaps.  This is done this way so that a simple
	 * program that doesn't mess with the bitmaps doesn't need to
	 * drag in the bitmaps.c code.
	 */
	if (fs->write_bitmaps) {
		retval = fs->write_bitmaps(fs);
		if (retval)
			return retval;
	}
		
	return 0;
}

errcode_t ext2fs_close(ext2_filsys fs)
{
	errcode_t	retval;
	
	if (fs->flags & EXT2_FLAG_DIRTY) {
		retval = ext2fs_flush(fs);
		if (retval)
			return retval;
	}
	ext2fs_free(fs);
	return 0;
}
