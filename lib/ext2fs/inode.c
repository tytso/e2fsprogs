/*
 * inode.c --- utility routines to read and write inodes
 * 
 * Copyright (C) 1993, 1994, 1995, 1996, 1997 Theodore Ts'o.
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
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

#include "ext2fsP.h"

struct ext2_struct_inode_scan {
	int			magic;
	ext2_filsys		fs;
	ino_t			current_inode;
	blk_t			current_block;
	dgrp_t			current_group;
	int			inodes_left, blocks_left, groups_left;
	int			inode_buffer_blocks;
	char *			inode_buffer;
	int			inode_size;
	char *			ptr;
	int			bytes_left;
	char			*temp_buffer;
	errcode_t		(*done_group)(ext2_filsys fs,
					      ext2_inode_scan scan,
					      dgrp_t group,
					      void * private);
	void *			done_group_data;
	int			bad_block_ptr;
	int			scan_flags;
	int			reserved[6];
};

errcode_t ext2fs_open_inode_scan(ext2_filsys fs, int buffer_blocks,
				 ext2_inode_scan *ret_scan)
{
	ext2_inode_scan	scan;
	errcode_t	retval;
	errcode_t (*save_get_blocks)(ext2_filsys fs, ino_t ino, blk_t *blocks);

	EXT2_CHECK_MAGIC(fs, EXT2_ET_MAGIC_EXT2FS_FILSYS);

	/*
	 * If fs->badblocks isn't set, then set it --- since the inode
	 * scanning functions require it.
	 */
	if (fs->badblocks == 0) {
		/*
		 * Temporarly save fs->get_blocks and set it to zero,
		 * for compatibility with old e2fsck's.
		 */
		save_get_blocks = fs->get_blocks;
		fs->get_blocks = 0;
		retval = ext2fs_read_bb_inode(fs, &fs->badblocks);
		if (retval && fs->badblocks) {
			badblocks_list_free(fs->badblocks);
			fs->badblocks = 0;
		}
		fs->get_blocks = save_get_blocks;
	}

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
	scan->bad_block_ptr = 0;
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
	if (scan->fs->badblocks && scan->fs->badblocks->num)
		scan->scan_flags |= EXT2_SF_CHK_BADBLOCKS;
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

int ext2fs_inode_scan_flags(ext2_inode_scan scan, int set_flags,
			    int clear_flags)
{
	int	old_flags;

	if (!scan || (scan->magic != EXT2_ET_MAGIC_INODE_SCAN))
		return 0;

	old_flags = scan->scan_flags;
	scan->scan_flags &= ~clear_flags;
	scan->scan_flags |= set_flags;
	return old_flags;
}

/*
 * This function is called by ext2fs_get_next_inode when it needs to
 * get ready to read in a new blockgroup.
 */
static errcode_t get_next_blockgroup(ext2_inode_scan scan)
{
	scan->current_group++;
	scan->groups_left--;
			
	scan->current_block = scan->fs->
		group_desc[scan->current_group].bg_inode_table;

	scan->bytes_left = 0;
	scan->inodes_left = EXT2_INODES_PER_GROUP(scan->fs->super);
	scan->blocks_left = scan->fs->inode_blocks_per_group;
	return 0;
}

errcode_t ext2fs_inode_scan_goto_blockgroup(ext2_inode_scan scan,
					    int	group)
{
	scan->current_group = group - 1;
	scan->groups_left = scan->fs->group_desc_count - group;
	return get_next_blockgroup(scan);
}

/*
 * This function is called by get_next_blocks() to check for bad
 * blocks in the inode table.
 *
 * This function assumes that badblocks_list->list is sorted in
 * increasing order.
 */
static errcode_t check_for_inode_bad_blocks(ext2_inode_scan scan,
					    int *num_blocks)
{
	blk_t	blk = scan->current_block;
	badblocks_list	bb = scan->fs->badblocks;

	/*
	 * If the inode table is missing, then obviously there are no
	 * bad blocks.  :-)
	 */
	if (blk == 0)
		return 0;

	/*
	 * If the current block is greater than the bad block listed
	 * in the bad block list, then advance the pointer until this
	 * is no longer the case.  If we run out of bad blocks, then
	 * we don't need to do any more checking!
	 */
	while (blk > bb->list[scan->bad_block_ptr]) {
		if (++scan->bad_block_ptr >= bb->num) {
			scan->scan_flags &= ~EXT2_SF_CHK_BADBLOCKS;
			return 0;
		}
	}

	/*
	 * If the current block is equal to the bad block listed in
	 * the bad block list, then handle that one block specially.
	 * (We could try to handle runs of bad blocks, but that
	 * only increases CPU efficiency by a small amount, at the
	 * expense of a huge expense of code complexity, and for an
	 * uncommon case at that.)
	 */
	if (blk == bb->list[scan->bad_block_ptr]) {
		scan->scan_flags |= EXT2_SF_BAD_INODE_BLK;
		*num_blocks = 1;
		if (++scan->bad_block_ptr >= bb->num)
			scan->scan_flags &= ~EXT2_SF_CHK_BADBLOCKS;
		return 0;
	}

	/*
	 * If there is a bad block in the range that we're about to
	 * read in, adjust the number of blocks to read so that we we
	 * don't read in the bad block.  (Then the next block to read
	 * will be the bad block, which is handled in the above case.)
	 */
	if ((blk + *num_blocks) > bb->list[scan->bad_block_ptr])
		*num_blocks = bb->list[scan->bad_block_ptr] - blk;

	return 0;
}

/*
 * This function is called by ext2fs_get_next_inode when it needs to
 * read in more blocks from the current blockgroup's inode table.
 */
static errcode_t get_next_blocks(ext2_inode_scan scan)
{
	int		num_blocks;
	errcode_t	retval;

	/*
	 * Figure out how many blocks to read; we read at most
	 * inode_buffer_blocks, and perhaps less if there aren't that
	 * many blocks left to read.
	 */
	num_blocks = scan->inode_buffer_blocks;
	if (num_blocks > scan->blocks_left)
		num_blocks = scan->blocks_left;

	/*
	 * If the past block "read" was a bad block, then mark the
	 * left-over extra bytes as also being bad.
	 */
	if (scan->scan_flags & EXT2_SF_BAD_INODE_BLK) {
		if (scan->bytes_left)
			scan->scan_flags |= EXT2_SF_BAD_EXTRA_BYTES;
		scan->scan_flags &= ~EXT2_SF_BAD_INODE_BLK;
	}

	/*
	 * Do inode bad block processing, if necessary.
	 */
	if (scan->scan_flags & EXT2_SF_CHK_BADBLOCKS) {
		retval = check_for_inode_bad_blocks(scan, &num_blocks);
		if (retval)
			return retval;
	}
		
	if ((scan->scan_flags & EXT2_SF_BAD_INODE_BLK) ||
	    (scan->current_block == 0)) {
		memset(scan->inode_buffer, 0,
		       num_blocks * scan->fs->blocksize);
	} else {
		retval = io_channel_read_blk(scan->fs->io,
					     scan->current_block,
					     num_blocks, scan->inode_buffer);
		if (retval)
			return EXT2_ET_NEXT_INODE_READ;
	}
	scan->ptr = scan->inode_buffer;
	scan->bytes_left = num_blocks * scan->fs->blocksize;

	scan->blocks_left -= num_blocks;
	if (scan->current_block)
		scan->current_block += num_blocks;
	return 0;
}

errcode_t ext2fs_get_next_inode(ext2_inode_scan scan, ino_t *ino,
				struct ext2_inode *inode)
{
	errcode_t	retval;
	int		extra_bytes = 0;
	
	EXT2_CHECK_MAGIC(scan, EXT2_ET_MAGIC_INODE_SCAN);

	/*
	 * Do we need to start reading a new block group?
	 */
	if (scan->inodes_left <= 0) {
	retry:
		if (scan->done_group) {
			retval = (scan->done_group)
				(scan->fs, scan, scan->current_group,
				 scan->done_group_data);
			if (retval)
				return retval;
		}
		if (scan->groups_left <= 0) {
			*ino = 0;
			return 0;
		}
		retval = get_next_blockgroup(scan);
		if (retval)
			return retval;
		if (scan->current_block == 0) {
			if (scan->scan_flags & EXT2_SF_SKIP_MISSING_ITABLE) {
				goto retry;
			} else
				return EXT2_ET_MISSING_INODE_TABLE;
		}
	}

	/*
	 * Have we run out of space in the inode buffer?  If so, we
	 * need to read in more blocks.
	 */
	if (scan->bytes_left < scan->inode_size) {
		memcpy(scan->temp_buffer, scan->ptr, scan->bytes_left);
		extra_bytes = scan->bytes_left;

		retval = get_next_blocks(scan);
		if (retval)
			return retval;
	}

	retval = 0;
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
		if (scan->scan_flags & EXT2_SF_BAD_EXTRA_BYTES)
			retval = EXT2_ET_BAD_BLOCK_IN_INODE_TABLE;
		scan->scan_flags &= ~EXT2_SF_BAD_EXTRA_BYTES;
	} else {
		if ((scan->fs->flags & EXT2_FLAG_SWAP_BYTES) ||
		    (scan->fs->flags & EXT2_FLAG_SWAP_BYTES_READ))
			ext2fs_swap_inode(scan->fs, inode,
				 (struct ext2_inode *) scan->ptr, 0);
		else
			*inode = *((struct ext2_inode *) scan->ptr);
		scan->ptr += scan->inode_size;
		scan->bytes_left -= scan->inode_size;
		if (scan->scan_flags & EXT2_SF_BAD_INODE_BLK)
			retval = EXT2_ET_BAD_BLOCK_IN_INODE_TABLE;
	}

	scan->inodes_left--;
	scan->current_inode++;
	*ino = scan->current_inode;
	return retval;
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


errcode_t ext2fs_read_inode (ext2_filsys fs, ino_t ino,
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
	if (!fs->group_desc[group].bg_inode_table)
		return EXT2_ET_MISSING_INODE_TABLE;
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

errcode_t ext2fs_write_inode(ext2_filsys fs, ino_t ino,
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
	if (!fs->group_desc[group].bg_inode_table)
		return EXT2_ET_MISSING_INODE_TABLE;
	block_nr = fs->group_desc[group].bg_inode_table + block;
	offset &= (EXT2_BLOCK_SIZE(fs->super) - 1);
	ptr = (char *) inode_buffer + offset;

	length = EXT2_INODE_SIZE(fs->super);
	clen = length;
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
		length -= clen;
	} else {
		length = 0;
	}
	memcpy(ptr, &temp_inode, clen);
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

