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
#if HAVE_ERRNO_H
#include <errno.h>
#endif

#include <linux/ext2_fs.h>

#include "ext2fs.h"

static void inocpy_with_swap(struct ext2_inode *t, struct ext2_inode *f);

errcode_t ext2fs_open_inode_scan(ext2_filsys fs, int buffer_blocks,
				 ext2_inode_scan *ret_scan)
{
	ext2_inode_scan	scan;

	EXT2_CHECK_MAGIC(fs, EXT2_ET_MAGIC_EXT2FS_FILSYS);

	scan = (ext2_inode_scan) malloc(sizeof(struct ext2_struct_inode_scan));
	if (!scan)
		return ENOMEM;
	memset(scan, 0, sizeof(struct ext2_struct_inode_scan));

	scan->magic = EXT2_ET_MAGIC_INODE_SCAN;
	scan->fs = fs;
	scan->current_group = -1;
	scan->inode_buffer_blocks = buffer_blocks ? buffer_blocks : 8;
	scan->groups_left = fs->group_desc_count;
	scan->inode_buffer = malloc(scan->inode_buffer_blocks * fs->blocksize);
	scan->done_group = 0;
	scan->done_group_data = 0;
	if (!scan->inode_buffer) {
		free(scan);
		return ENOMEM;
	}
	*ret_scan = scan;
	return 0;
}

void ext2fs_close_inode_scan(ext2_inode_scan scan)
{
	if (!scan || (scan->magic != EXT2_ET_MAGIC_INODE_SCAN))
		return;
	
	free(scan->inode_buffer);
	scan->inode_buffer = NULL;
	free(scan);
	return;
}

void ext2fs_set_inode_callback(ext2_inode_scan scan,
			       errcode_t (*done_group)(ext2_filsys fs,
						       ext2_inode_scan scan,
						       dgrp_t group,
						       void * private),
			       void *done_group_data)
{
	if (!scan || (scan->magic != EXT2_ET_MAGIC_INODE_SCAN))
		return;
	
	scan->done_group = done_group;
	scan->done_group_data = done_group_data;
}

errcode_t ext2fs_get_next_inode(ext2_inode_scan scan, ino_t *ino,
				struct ext2_inode *inode)
{
	errcode_t	retval;
	int		num_blocks;
	
	EXT2_CHECK_MAGIC(scan, EXT2_ET_MAGIC_INODE_SCAN);

	if (scan->inodes_left <= 0) {
		if (scan->blocks_left <= 0) {
			if (scan->done_group) {
				retval = (scan->done_group)
					(scan->fs, scan,
					 scan->current_group,
					 scan->done_group_data);
				if (retval)
					return retval;
			}
			do {
				if (scan->groups_left <= 0) {
					*ino = 0;
					return 0;
				}
				scan->current_group++;
				scan->groups_left--;
			
				scan->current_block =
		scan->fs->group_desc[scan->current_group].bg_inode_table;
				scan->blocks_left = (EXT2_INODES_PER_GROUP(scan->fs->super) /
						     EXT2_INODES_PER_BLOCK(scan->fs->super));
			} while (scan->current_block == 0);
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
	if (scan->fs->flags & EXT2_SWAP_BYTES)
		inocpy_with_swap(inode, scan->inode_scan_ptr++);
	else
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

	EXT2_CHECK_MAGIC(fs, EXT2_ET_MAGIC_EXT2FS_FILSYS);

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
	if (fs->flags & EXT2_SWAP_BYTES)
		inocpy_with_swap(inode,
				 (struct ext2_inode *) inode_buffer + i);
	else
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

	EXT2_CHECK_MAGIC(fs, EXT2_ET_MAGIC_EXT2FS_FILSYS);

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
	if (fs->flags & EXT2_SWAP_BYTES)
		inocpy_with_swap((struct ext2_inode *) inode_buffer + i,
				 inode);
	else
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
	
	EXT2_CHECK_MAGIC(fs, EXT2_ET_MAGIC_EXT2FS_FILSYS);

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
	
	EXT2_CHECK_MAGIC(fs, EXT2_ET_MAGIC_EXT2FS_FILSYS);

	if (ino > fs->super->s_inodes_count)
		return EXT2_ET_BAD_INODE_NUM;

	if (fs->check_directory)
		return (fs->check_directory)(fs, ino);
	retval = ext2fs_read_inode(fs, ino, &inode);
	if (retval)
		return retval;
	if (!LINUX_S_ISDIR(inode.i_mode))
		return ENOTDIR;
	return 0;
}

static void inocpy_with_swap(struct ext2_inode *t, struct ext2_inode *f)
{
	unsigned i;
	
	t->i_mode = ext2fs_swab16(f->i_mode);
	t->i_uid = ext2fs_swab16(f->i_uid);
	t->i_size = ext2fs_swab32(f->i_size);
	t->i_atime = ext2fs_swab32(f->i_atime);
	t->i_ctime = ext2fs_swab32(f->i_ctime);
	t->i_mtime = ext2fs_swab32(f->i_mtime);
	t->i_dtime = ext2fs_swab32(f->i_dtime);
	t->i_gid = ext2fs_swab16(f->i_gid);
	t->i_links_count = ext2fs_swab16(f->i_links_count);
	t->i_blocks = ext2fs_swab32(f->i_blocks);
	t->i_flags = ext2fs_swab32(f->i_flags);
	for (i = 0; i < EXT2_N_BLOCKS; i++)
		t->i_block[i] = ext2fs_swab32(f->i_block[i]);
	t->i_version = ext2fs_swab32(f->i_version);
	t->i_file_acl = ext2fs_swab32(f->i_file_acl);
	t->i_dir_acl = ext2fs_swab32(f->i_dir_acl);
	t->i_faddr = ext2fs_swab32(f->i_faddr);
	t->osd2.linux2.l_i_frag = f->osd2.linux2.l_i_frag;
	t->osd2.linux2.l_i_fsize = f->osd2.linux2.l_i_fsize;
	t->osd2.linux2.i_pad1 = ext2fs_swab16(f->osd2.linux2.i_pad1);
}
