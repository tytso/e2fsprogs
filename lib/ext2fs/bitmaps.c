/*
 * bitmaps.c --- routines to read, write, and manipulate the inode and
 * block bitmaps.
 *
 * Copyright (C) 1993 Theodore Ts'o.  This file may be redistributed
 * under the terms of the GNU Public License.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <linux/fs.h>
#include <linux/ext2_fs.h>

#include "ext2fs.h"

errcode_t ext2fs_write_inode_bitmap(ext2_filsys fs)
{
	int 		i;
	int		nbytes;
	errcode_t	retval;
	char * inode_bitmap = fs->inode_map;
	char * bitmap_block = NULL;

	if (!(fs->flags & EXT2_FLAG_RW))
		return EXT2_ET_RO_FILSYS;
	if (!inode_bitmap)
		return 0;
	nbytes = EXT2_INODES_PER_GROUP(fs->super) / 8;
	bitmap_block = malloc(fs->blocksize);
	if (!bitmap_block)
		return ENOMEM;
	memset(bitmap_block, 0xff, fs->blocksize);
	for (i = 0; i < fs->group_desc_count; i++) {
		memcpy(bitmap_block, inode_bitmap, nbytes);
		retval = io_channel_write_blk(fs->io,
		      fs->group_desc[i].bg_inode_bitmap, 1,
					      bitmap_block);
		if (retval)
			return EXT2_ET_INODE_BITMAP_WRITE;
		inode_bitmap += nbytes;
	}
	fs->flags |= EXT2_FLAG_CHANGED;
	fs->flags &= ~EXT2_FLAG_IB_DIRTY;
	free(bitmap_block);
	return 0;
}

errcode_t ext2fs_write_block_bitmap (ext2_filsys fs)
{
	int 		i;
	int		j;
	int		nbytes;
	int		nbits;
	errcode_t	retval;
	char * block_bitmap = fs->block_map;
	char * bitmap_block = NULL;

	if (!(fs->flags & EXT2_FLAG_RW))
		return EXT2_ET_RO_FILSYS;
	if (!block_bitmap)
		return 0;
	nbytes = EXT2_BLOCKS_PER_GROUP(fs->super) / 8;
	bitmap_block = malloc(fs->blocksize);
	if (!bitmap_block)
		return ENOMEM;
	memset(bitmap_block, 0xff, fs->blocksize);
	for (i = 0; i < fs->group_desc_count; i++) {
		memcpy(bitmap_block, block_bitmap, nbytes);
		if (i == fs->group_desc_count - 1) {
			/* Force bitmap padding for the last group */
			nbits = (fs->super->s_blocks_count
				 - fs->super->s_first_data_block)
				% EXT2_BLOCKS_PER_GROUP(fs->super);
			for (j = nbits; j < fs->blocksize * 8; j++)
				set_bit(j, bitmap_block);
		}
		retval = io_channel_write_blk(fs->io,
		      fs->group_desc[i].bg_block_bitmap, 1,
					      bitmap_block);
		if (retval)
			return EXT2_ET_BLOCK_BITMAP_WRITE;
		block_bitmap += nbytes;
	}
	fs->flags |= EXT2_FLAG_CHANGED;
	fs->flags &= ~EXT2_FLAG_BB_DIRTY;
	free(bitmap_block);
	return 0;
}

errcode_t ext2fs_read_inode_bitmap (ext2_filsys fs)
{
	int i;
	char * inode_bitmap;
	char *buf = 0;
	errcode_t	retval;
	int nbytes;

	fs->write_bitmaps = ext2fs_write_bitmaps;

	if (fs->inode_map)
		free(fs->inode_map);
	nbytes = EXT2_INODES_PER_GROUP(fs->super) / 8;
	fs->flags &= ~EXT2_FLAG_IB_DIRTY;
	fs->inode_map = malloc((nbytes * fs->group_desc_count) + 1);
	if (!fs->inode_map)
		return ENOMEM;
	inode_bitmap = fs->inode_map;

	buf = malloc(fs->blocksize);
	if (!buf)
		return ENOMEM;

	for (i = 0; i < fs->group_desc_count; i++) {
		retval = io_channel_read_blk(fs->io,
			     fs->group_desc[i].bg_inode_bitmap, 1,
					     buf);
		if (retval) {
			retval = EXT2_ET_INODE_BITMAP_READ;
			goto cleanup;
		}
		memcpy(inode_bitmap, buf, nbytes);
		inode_bitmap += nbytes;
	}
	free(buf);
	return 0;
	
cleanup:
	free(fs->inode_map);
	fs->inode_map = 0;
	if (buf)
		free(buf);
	return retval;
}

errcode_t ext2fs_read_block_bitmap(ext2_filsys fs)
{
	int i;
	char * block_bitmap;
	char *buf = 0;
	errcode_t retval;
	int nbytes;

	fs->write_bitmaps = ext2fs_write_bitmaps;

	if (fs->block_map)
		free(fs->block_map);
	nbytes = EXT2_BLOCKS_PER_GROUP(fs->super) / 8;
	fs->flags &= ~EXT2_FLAG_BB_DIRTY;
	fs->block_map = malloc((nbytes * fs->group_desc_count) + 1);
	if (!fs->block_map)
		return ENOMEM;
	block_bitmap = fs->block_map;

	buf = malloc(fs->blocksize);
	if (!buf)
		return ENOMEM;

	for (i = 0; i < fs->group_desc_count; i++) {
		retval = io_channel_read_blk(fs->io,
			     fs->group_desc[i].bg_block_bitmap, 1,
					     buf);
		if (retval) {
			retval = EXT2_ET_BLOCK_BITMAP_READ;
			goto cleanup;
		}
		memcpy(block_bitmap, buf, nbytes);
		block_bitmap += nbytes;
	}
	free(buf);
	return 0;
	
cleanup:
	free(fs->block_map);
	fs->block_map = 0;
	if (buf)
		free(buf);
	return retval;
}

errcode_t ext2fs_allocate_inode_bitmap(ext2_filsys fs, char **ret)
{
	char	*map;
	int	size;
	
	fs->write_bitmaps = ext2fs_write_bitmaps;

	size = (fs->super->s_inodes_count / 8) + 1;
	map = malloc(size);
	if (!map)
		return ENOMEM;
	memset(map, 0, size);
	*ret = map;
	return 0;
}

errcode_t ext2fs_allocate_block_bitmap(ext2_filsys fs, char **ret)
{
	char	*map;
	int	size;

	fs->write_bitmaps = ext2fs_write_bitmaps;
	
	size = (fs->super->s_blocks_count / 8) + 1;
	map = malloc(size);
	if (!map)
		return ENOMEM;
	memset(map, 0, size);
	*ret = map;
	return 0;
}

errcode_t ext2fs_read_bitmaps(ext2_filsys fs)
{
	errcode_t	retval;

	fs->write_bitmaps = ext2fs_write_bitmaps;

	if (!fs->inode_map) {
		retval = ext2fs_read_inode_bitmap(fs);
		if (retval)
			return retval;
	}
	if (!fs->block_map) {
		retval = ext2fs_read_block_bitmap(fs);
		if (retval)
			return retval;
	}
	return 0;
}

errcode_t ext2fs_write_bitmaps(ext2_filsys fs)
{
	errcode_t	retval;

	if (fs->block_map && ext2fs_test_bb_dirty(fs)) {
		retval = ext2fs_write_block_bitmap(fs);
		if (retval)
			return retval;
	}
	if (fs->inode_map && ext2fs_test_ib_dirty(fs)) {
		retval = ext2fs_write_inode_bitmap(fs);
		if (retval)
			return retval;
	}
	return 0;
}	





