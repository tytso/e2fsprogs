/*
 * inode.c --- utility routines to read and write inodes
 * 
 * Copyright (C) 1993 Theodore Ts'o.  This file may be redistributed
 * under the terms of the GNU Public License.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <linux/fs.h>
#include <linux/ext2_fs.h>

#include "ext2fs.h"

errcode_t ext2fs_open_inode_scan(ext2_filsys fs, int buffer_blocks,
				 ext2_inode_scan *ret_scan)
{
	ext2_inode_scan	scan;

	scan = (ext2_inode_scan) malloc(sizeof(struct ext2_struct_inode_scan));
	if (!scan)
		return ENOMEM;
	memset(scan, 0, sizeof(struct ext2_struct_inode_scan));

	scan->fs = fs;
	scan->current_group = -1;
	scan->inode_buffer_blocks = buffer_blocks ? buffer_blocks : 8;
	scan->groups_left = fs->group_desc_count;
	scan->inode_buffer = malloc(scan->inode_buffer_blocks * fs->blocksize);
	if (!scan->inode_buffer) {
		free(scan);
		return ENOMEM;
	}
	*ret_scan = scan;
	return 0;
}

void ext2fs_close_inode_scan(ext2_inode_scan scan)
{
	free(scan->inode_buffer);
	scan->inode_buffer = NULL;
	free(scan);
	return;
}

errcode_t ext2fs_get_next_inode(ext2_inode_scan scan, ino_t *ino,
				struct ext2_inode *inode)
{
	errcode_t	retval;
	int		num_blocks;
	
	if (!scan->inode_buffer)
		return EINVAL;
	
	if (scan->inodes_left <= 0) {
		if (scan->blocks_left <= 0) {
			if (scan->groups_left <= 0) {
				*ino = 0;
				return 0;
			}
			scan->current_group++;
			scan->groups_left--;
			
			scan->current_block = scan->fs->group_desc[scan->current_group].bg_inode_table;
			scan->blocks_left = (EXT2_INODES_PER_GROUP(scan->fs->super) /
					     EXT2_INODES_PER_BLOCK(scan->fs->super));
		} else {
			scan->current_block += scan->inode_buffer_blocks;
		}
		scan->blocks_left -= scan->inode_buffer_blocks;
		num_blocks = scan->inode_buffer_blocks;
		if (scan->blocks_left < 0)
			num_blocks += scan->blocks_left;
		
		scan->inodes_left = EXT2_INODES_PER_BLOCK(scan->fs->super) *
			num_blocks;

		retval = io_channel_read_blk(scan->fs->io, scan->current_block,
					     num_blocks, scan->inode_buffer);
		if (retval)
			return EXT2_ET_NEXT_INODE_READ;
		scan->inode_scan_ptr = (struct ext2_inode *) scan->inode_buffer;
	}
	*inode = *scan->inode_scan_ptr++;
	scan->inodes_left--;
	scan->current_inode++;
	*ino = scan->current_inode;
	return 0;
}

/*
 * Functions to read and write a single inode.
 */
static char *inode_buffer = 0;
static blk_t inode_buffer_block;
static int inode_buffer_size = 0;

errcode_t ext2fs_read_inode (ext2_filsys fs, unsigned long ino,
			     struct ext2_inode * inode)
{
	unsigned long group;
	unsigned long block;
	unsigned long block_nr;
	errcode_t	retval;
	int i;

	if (ino > fs->super->s_inodes_count)
		return EXT2_ET_BAD_INODE_NUM;
	if (inode_buffer_size != fs->blocksize) {
		if (inode_buffer)
			free(inode_buffer);
		inode_buffer_size = 0;
		inode_buffer = malloc(fs->blocksize);
		if (!inode_buffer)
			return ENOMEM;
		inode_buffer_size = fs->blocksize;
		inode_buffer_block = 0;
	}
		
	group = (ino - 1) / EXT2_INODES_PER_GROUP(fs->super);
	block = ((ino - 1) % EXT2_INODES_PER_GROUP(fs->super)) /
		EXT2_INODES_PER_BLOCK(fs->super);
	i = ((ino - 1) % EXT2_INODES_PER_GROUP(fs->super)) %
		EXT2_INODES_PER_BLOCK(fs->super);
	block_nr = fs->group_desc[group].bg_inode_table + block;
	if (block_nr != inode_buffer_block) {
		retval = io_channel_read_blk(fs->io, block_nr, 1,
					     inode_buffer);
		if (retval)
			return retval;
		inode_buffer_block = block_nr;
	}
	memcpy (inode, (struct ext2_inode *) inode_buffer + i,
		sizeof (struct ext2_inode));
	return 0;
}

errcode_t ext2fs_write_inode(ext2_filsys fs, unsigned long ino,
		     struct ext2_inode * inode)
{
	unsigned long group;
	unsigned long block;
	unsigned long block_nr;
	errcode_t	retval;
	int i;

	if (!(fs->flags & EXT2_FLAG_RW))
		return EXT2_ET_RO_FILSYS;

	if (ino > fs->super->s_inodes_count)
		return EXT2_ET_BAD_INODE_NUM;

	if (inode_buffer_size != fs->blocksize) {
		if (inode_buffer)
			free(inode_buffer);
		inode_buffer_size = 0;
		inode_buffer = malloc(fs->blocksize);
		if (!inode_buffer)
			return ENOMEM;
		inode_buffer_size = fs->blocksize;
		inode_buffer_block = 0;
	}
		
	group = (ino - 1) / EXT2_INODES_PER_GROUP(fs->super);
	block = ((ino - 1) % EXT2_INODES_PER_GROUP(fs->super)) /
		EXT2_INODES_PER_BLOCK(fs->super);
	i = ((ino - 1) % EXT2_INODES_PER_GROUP(fs->super)) %
		EXT2_INODES_PER_BLOCK(fs->super);
	block_nr = fs->group_desc[group].bg_inode_table + block;
	if (inode_buffer_block != block_nr) {
		retval = io_channel_read_blk(fs->io, block_nr, 1,
					     inode_buffer);
		if (retval)
			return retval;
		inode_buffer_block = block_nr;
	}
	memcpy ((struct ext2_inode *) inode_buffer + i, inode,
		sizeof (struct ext2_inode));
	retval = io_channel_write_blk(fs->io, block_nr, 1, inode_buffer);
	if (retval)
		return retval;
	fs->flags |= EXT2_FLAG_CHANGED;
	return 0;
}

errcode_t ext2fs_get_blocks(ext2_filsys fs, ino_t ino, blk_t *blocks)
{
	struct ext2_inode	inode;
	int			i;
	errcode_t		retval;
	
	if (ino > fs->super->s_inodes_count)
		return EXT2_ET_BAD_INODE_NUM;

	if (fs->get_blocks) {
		if (!(*fs->get_blocks)(fs, ino, blocks))
			return 0;
	}
	retval = ext2fs_read_inode(fs, ino, &inode);
	if (retval)
		return retval;
	for (i=0; i < EXT2_N_BLOCKS; i++)
		blocks[i] = inode.i_block[i];
	return 0;
}

errcode_t ext2fs_check_directory(ext2_filsys fs, ino_t ino)
{
	struct	ext2_inode	inode;
	errcode_t		retval;
	
	if (ino > fs->super->s_inodes_count)
		return EXT2_ET_BAD_INODE_NUM;

	if (fs->check_directory)
		return (fs->check_directory)(fs, ino);
	retval = ext2fs_read_inode(fs, ino, &inode);
	if (retval)
		return retval;
	if (!S_ISDIR(inode.i_mode))
		return ENOTDIR;
	return 0;
}

	

