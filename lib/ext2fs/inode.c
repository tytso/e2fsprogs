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
	scan->inode_size = EXT2_INODE_SIZE(fs->super);
	scan->bytes_left = 0;
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
	scan->temp_buffer = malloc(scan->inode_size);
	if (!scan->temp_buffer) {
		free(scan->inode_buffer);
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
	free(scan->temp_buffer);
	scan->temp_buffer = NULL;
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
	int		extra_bytes = 0;
	
	EXT2_CHECK_MAGIC(scan, EXT2_ET_MAGIC_INODE_SCAN);

	/*
	 * Do we need to start reading a new block group?
	 */
	if (scan->inodes_left <= 0) {
		if (scan->done_group) {
			retval = (scan->done_group)
				(scan->fs, scan,
				 scan->current_group,
				 scan->done_group_data);
			if (retval)
				return retval;
		}
		if (scan->groups_left <= 0) {
			*ino = 0;
			return 0;
		}
		scan->current_group++;
		scan->groups_left--;
			
		scan->current_block = scan->fs->
			group_desc[scan->current_group].bg_inode_table;

		if (scan->current_block == 0)
			return EXT2_ET_MISSING_INODE_TABLE;
		scan->bytes_left = 0;
		scan->inodes_left = EXT2_INODES_PER_GROUP(scan->fs->super);
		scan->blocks_left = scan->fs->inode_blocks_per_group;
	}

	/*
	 * Have we run out of space in the inode buffer?  If so, we
	 * need to read in more blocks.
	 */
	if (scan->bytes_left < scan->inode_size) {
		memcpy(scan->temp_buffer, scan->ptr, scan->bytes_left);
		extra_bytes = scan->bytes_left;
		
		scan->blocks_left -= scan->inode_buffer_blocks;
		num_blocks = scan->inode_buffer_blocks;
		if (scan->blocks_left < 0)
			num_blocks += scan->blocks_left;
		
		retval = io_channel_read_blk(scan->fs->io, scan->current_block,
					     num_blocks, scan->inode_buffer);
		if (retval)
			return EXT2_ET_NEXT_INODE_READ;
		scan->ptr = scan->inode_buffer;
		scan->bytes_left = num_blocks * scan->fs->blocksize;
	
		scan->current_block += scan->inode_buffer_blocks;
	}

	if (extra_bytes) {
		memcpy(scan->temp_buffer+extra_bytes, scan->ptr,
		       scan->inode_size - extra_bytes);
		scan->ptr += scan->inode_size - extra_bytes;
		scan->bytes_left -= scan->inode_size - extra_bytes;

		if ((scan->fs->flags & EXT2_FLAG_SWAP_BYTES) ||
		    (scan->fs->flags & EXT2_FLAG_SWAP_BYTES_READ))
			ext2fs_swap_inode(scan->fs, inode,
				 (struct ext2_inode *) scan->temp_buffer, 0);
		else
			*inode = *((struct ext2_inode *) scan->temp_buffer);
	} else {
		if ((scan->fs->flags & EXT2_FLAG_SWAP_BYTES) ||
		    (scan->fs->flags & EXT2_FLAG_SWAP_BYTES_READ))
			ext2fs_swap_inode(scan->fs, inode,
				 (struct ext2_inode *) scan->ptr, 0);
		else
			*inode = *((struct ext2_inode *) scan->ptr);
		scan->ptr += scan->inode_size;
		scan->bytes_left -= scan->inode_size;
	}

	scan->inodes_left--;
	scan->current_inode++;
	*ino = scan->current_inode;
	return 0;
}

/*
 * Functions to read and write a single inode.
 */
static char *inode_buffer = 0;
static blk_t inode_buffer_block = 0;
static int inode_buffer_size = 0;
#define INODE_CACHE_SIZE 4
#ifdef INODE_CACHE_SIZE
static int cache_last = -1;
static struct {
	ino_t	inode;
	struct ext2_inode value;
} inode_cache[INODE_CACHE_SIZE];
#endif


errcode_t ext2fs_read_inode (ext2_filsys fs, unsigned long ino,
			     struct ext2_inode * inode)
{
	unsigned long 	group, block, block_nr, offset;
	char 		*ptr;
	errcode_t	retval;
	int 		clen, length, i;

	EXT2_CHECK_MAGIC(fs, EXT2_ET_MAGIC_EXT2FS_FILSYS);

	/* Check to see if user has an override function */
	if (fs->read_inode) {
		retval = (fs->read_inode)(fs, ino, inode);
		if (retval != EXT2_ET_CALLBACK_NOTHANDLED)
			return retval;
	}
	/* Check to see if it's in the inode cache */
#ifdef INODE_CACHE_SIZE
	if (cache_last == -1) {
		for (i=0; i < INODE_CACHE_SIZE; i++)
			inode_cache[i].inode = 0;
		cache_last = INODE_CACHE_SIZE-1;
	} else for (i=0; i < INODE_CACHE_SIZE; i++) {
		if (inode_cache[i].inode == ino) {
			*inode = inode_cache[i].value;
			return 0;
		}
	}
#endif
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
	offset = ((ino - 1) % EXT2_INODES_PER_GROUP(fs->super)) *
		EXT2_INODE_SIZE(fs->super);
	block = offset >> EXT2_BLOCK_SIZE_BITS(fs->super);
	block_nr = fs->group_desc[group].bg_inode_table + block;
	if (block_nr != inode_buffer_block) {
		retval = io_channel_read_blk(fs->io, block_nr, 1,
					     inode_buffer);
		if (retval)
			return retval;
		inode_buffer_block = block_nr;
	}
	offset &= (EXT2_BLOCK_SIZE(fs->super) - 1);
	ptr = ((char *) inode_buffer) + offset;

	memset(inode, 0, sizeof(struct ext2_inode));
	length = EXT2_INODE_SIZE(fs->super);
	if (length > sizeof(struct ext2_inode))
		length = sizeof(struct ext2_inode);
	
	if ((offset + length) > EXT2_BLOCK_SIZE(fs->super)) {
		clen = EXT2_BLOCK_SIZE(fs->super) - offset;
		memcpy((char *) inode, ptr, clen);
		length -= clen;
		
		retval = io_channel_read_blk(fs->io, block_nr+1, 1,
					     inode_buffer);
		if (retval)
			return retval;
		inode_buffer_block = block_nr+1;
		
		memcpy(((char *) inode) + clen,
		       inode_buffer, length);
	} else
		memcpy((char *) inode, ptr, length);
	
	if ((fs->flags & EXT2_FLAG_SWAP_BYTES) ||
	    (fs->flags & EXT2_FLAG_SWAP_BYTES_READ))
		ext2fs_swap_inode(fs, inode, inode, 0);

	/* Update the inode cache */
#ifdef INODE_CACHE_SIZE
	cache_last = (cache_last + 1) % INODE_CACHE_SIZE;
	inode_cache[cache_last].inode = ino;
	inode_cache[cache_last].value = *inode;
#endif

	return 0;
}

errcode_t ext2fs_write_inode(ext2_filsys fs, unsigned long ino,
		     struct ext2_inode * inode)
{
	unsigned long group, block, block_nr, offset;
	errcode_t	retval;
	struct ext2_inode temp_inode;
	char *ptr;
	int clen, length, i;

	EXT2_CHECK_MAGIC(fs, EXT2_ET_MAGIC_EXT2FS_FILSYS);

	/* Check to see if user provided an override function */
	if (fs->write_inode) {
		retval = (fs->write_inode)(fs, ino, inode);
		if (retval != EXT2_ET_CALLBACK_NOTHANDLED)
			return retval;
	}
	/* Check to see if the inode cache needs to be updated */
#ifdef INODE_CACHE_SIZE
	for (i=0; i < INODE_CACHE_SIZE; i++) {
		if (inode_cache[i].inode == ino) {
			inode_cache[i].value = *inode;
			break;
		}
	}
#endif
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
	if ((fs->flags & EXT2_FLAG_SWAP_BYTES) ||
	    (fs->flags & EXT2_FLAG_SWAP_BYTES_WRITE))
		ext2fs_swap_inode(fs, &temp_inode, inode, 1);
	else
		memcpy(&temp_inode, inode, sizeof(struct ext2_inode));
	
	group = (ino - 1) / EXT2_INODES_PER_GROUP(fs->super);
	offset = ((ino - 1) % EXT2_INODES_PER_GROUP(fs->super)) *
		EXT2_INODE_SIZE(fs->super);
	block = offset >> EXT2_BLOCK_SIZE_BITS(fs->super);
	block_nr = fs->group_desc[group].bg_inode_table + block;
	offset &= (EXT2_BLOCK_SIZE(fs->super) - 1);
	ptr = (char *) inode_buffer + offset;

	length = EXT2_INODE_SIZE(fs->super);
	if (length > sizeof(struct ext2_inode))
		length = sizeof(struct ext2_inode);
	
	if (inode_buffer_block != block_nr) {
		retval = io_channel_read_blk(fs->io, block_nr, 1,
					     inode_buffer);
		if (retval)
			return retval;
		inode_buffer_block = block_nr;
	}
	
	if ((offset + length) > EXT2_BLOCK_SIZE(fs->super)) {
		clen = EXT2_BLOCK_SIZE(fs->super) - offset;
		memcpy(ptr, &temp_inode, clen);
		length -= clen;
	} else {
		memcpy(ptr, &temp_inode, length);
		length = 0;
	}
	retval = io_channel_write_blk(fs->io, block_nr, 1, inode_buffer);
	if (retval)
		return retval;

	if (length) {
		retval = io_channel_read_blk(fs->io, ++block_nr, 1,
					     inode_buffer);
		if (retval) {
			inode_buffer_block = 0;
			return retval;
		}
		inode_buffer_block = block_nr;
		memcpy(inode_buffer, ((char *) &temp_inode) + clen, length);

		retval = io_channel_write_blk(fs->io, block_nr, 1,
					      inode_buffer);
		if (retval)
			return retval;
	}
	
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

