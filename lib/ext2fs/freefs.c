/*
 * freefs.c --- free an ext2 filesystem
 * 
 * Copyright (C) 1993, 1994 Theodore Ts'o.  This file may be redistributed
 * under the terms of the GNU Public License.
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#include <linux/ext2_fs.h>

#include "ext2fs.h"

void ext2fs_free(ext2_filsys fs)
{
	if (!fs || (fs->magic != EXT2_ET_MAGIC_EXT2FS_FILSYS))
		return;
	if (fs->io) {
		io_channel_close(fs->io);
	}
	if (fs->device_name)
		free(fs->device_name);
	if (fs->super)
		free(fs->super);
	if (fs->group_desc)
		free(fs->group_desc);
	if (fs->block_map)
		ext2fs_free_block_bitmap(fs->block_map);
	if (fs->inode_map)
		ext2fs_free_inode_bitmap(fs->inode_map);
	free(fs);
}

void ext2fs_free_generic_bitmap(ext2fs_inode_bitmap bitmap)
{
	if (!bitmap || (bitmap->magic != EXT2_ET_MAGIC_GENERIC_BITMAP))
		return;

	bitmap->magic = 0;
	if (bitmap->description) {
		free(bitmap->description);
		bitmap->description = 0;
	}
	if (bitmap->bitmap) {
		free(bitmap->bitmap);
		bitmap->bitmap = 0;
	}
	free(bitmap);
}

void ext2fs_free_inode_bitmap(ext2fs_inode_bitmap bitmap)
{
	if (!bitmap || (bitmap->magic != EXT2_ET_MAGIC_INODE_BITMAP))
		return;

	bitmap->magic = EXT2_ET_MAGIC_GENERIC_BITMAP;
	ext2fs_free_generic_bitmap(bitmap);
}

void ext2fs_free_block_bitmap(ext2fs_block_bitmap bitmap)
{
	if (!bitmap || (bitmap->magic != EXT2_ET_MAGIC_BLOCK_BITMAP))
		return;

	bitmap->magic = EXT2_ET_MAGIC_GENERIC_BITMAP;
	ext2fs_free_generic_bitmap(bitmap);
}

