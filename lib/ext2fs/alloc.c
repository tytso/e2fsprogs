/*
 * alloc.c --- allocate new inodes, blocks for ext2fs
 *
 * Copyright (C) 1993 Theodore Ts'o.  This file may be redistributed
 * under the terms of the GNU Public License.
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <linux/fs.h>
#include <linux/ext2_fs.h>

#include "ext2fs.h"

/*
 * Right now, just search forward from the parent directory's block
 * group to find the next free inode.
 *
 * Should have a special policy for directories.
 */
errcode_t ext2fs_new_inode(ext2_filsys fs, ino_t dir, int mode, char *map,
			   ino_t *ret)
{
	int	dir_group = 0;
	ino_t	i;
	ino_t	start_inode;

	if (!map)
		map = fs->inode_map;
	if (!map)
		return EXT2_ET_NO_INODE_BITMAP;
	
	if (dir > 0) 
		dir_group = (dir - 1) / EXT2_INODES_PER_GROUP(fs->super);

	start_inode = (dir_group * EXT2_INODES_PER_GROUP(fs->super)) + 1;
	i = start_inode;
	if (i < EXT2_FIRST_INO)
		i = EXT2_FIRST_INO;

	do {
		if (!ext2fs_test_inode_bitmap(fs, map, i))
			break;
		i++;
		if (i > fs->super->s_inodes_count)
			i = EXT2_FIRST_INO;
	} while (i != start_inode);
	
	if (ext2fs_test_inode_bitmap(fs, map, i))
		return ENOSPC;
	*ret = i;
	return 0;
}

/*
 * Stupid algorithm --- we now just search forward starting from the
 * goal.  Should put in a smarter one someday....
 */
errcode_t ext2fs_new_block(ext2_filsys fs, blk_t goal, char *map, blk_t *ret)
{
	blk_t	i = goal;

	if (!map)
		map = fs->block_map;
	if (!map)
		return EXT2_ET_NO_BLOCK_BITMAP;
	if (!i)
		i = fs->super->s_first_data_block;
	do {
		if (!ext2fs_test_block_bitmap(fs, map, i)) {
			*ret = i;
			return 0;
		}
		i++;
		if (i > fs->super->s_blocks_count)
			i = fs->super->s_first_data_block;
	} while (i != goal);
	return ENOSPC;
}

static int check_blocks_free(ext2_filsys fs, char *map, blk_t blk, int num)
{
	int	i;

	for (i=0; i < num; i++) {
		if ((blk+i) > fs->super->s_blocks_count)
			return 0;
		if (ext2fs_test_block_bitmap(fs, map, blk+i))
			return 0;
	}
	return 1;
}

errcode_t ext2fs_get_free_blocks(ext2_filsys fs, blk_t start, blk_t finish,
				 int num, char *map, blk_t *ret)
{
	blk_t	b = start;

	if (!map)
		map = fs->block_map;
	if (!map)
		return EXT2_ET_NO_BLOCK_BITMAP;
	if (!b)
		b = fs->super->s_first_data_block;
	if (!finish)
		finish = start;
	if (!num)
		num = 1;
	do {
		if (check_blocks_free(fs, map, b, num)) {
			*ret = b;
			return 0;
		}
		b++;
		if (b > fs->super->s_blocks_count)
			b = fs->super->s_first_data_block;
	} while (b != finish);
	return ENOSPC;
}

