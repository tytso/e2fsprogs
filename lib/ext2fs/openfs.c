/*
 * openfs.c --- open an ext2 filesystem
 * 
 * Copyright (C) 1993, 1994 Theodore Ts'o.  This file may be redistributed
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

/*
 *  Note: if superblock is non-zero, block-size must also be non-zero.
 * 	Superblock and block_size can be zero to use the default size.
 */
errcode_t ext2fs_open(const char *name, int flags, int superblock,
		      int block_size, io_manager manager, ext2_filsys *ret_fs)
{
	ext2_filsys	fs;
	errcode_t	retval;
	int		i, group_block;
	char		*dest;
	
	fs = (ext2_filsys) malloc(sizeof(struct struct_ext2_filsys));
	if (!fs)
		return ENOMEM;
	
	memset(fs, 0, sizeof(struct struct_ext2_filsys));
	fs->flags = flags;
	retval = manager->open(name, (flags & EXT2_FLAG_RW) ? IO_FLAG_RW : 0,
			       &fs->io);
	if (retval)
		goto cleanup;
	fs->device_name = malloc(strlen(name)+1);
	if (!fs->device_name) {
		retval = ENOMEM;
		goto cleanup;
	}
	strcpy(fs->device_name, name);
	fs->super = malloc(SUPERBLOCK_SIZE);
	if (!fs->super) {
		retval = ENOMEM;
		goto cleanup;
	}

	/*
	 * If the user specifies a specific block # for the
	 * superblock, then he/she must also specify the block size!
	 * Otherwise, read the master superblock located at offset
	 * SUPERBLOCK_OFFSET from the start of the partition.
	 */
	if (superblock) {
		if (!block_size) {
			retval = EINVAL;
			goto cleanup;
		}
		io_channel_set_blksize(fs->io, block_size);
	} else {
		io_channel_set_blksize(fs->io, SUPERBLOCK_OFFSET);
		superblock = 1;
	}
	retval = io_channel_read_blk(fs->io, superblock, -SUPERBLOCK_SIZE,
				     fs->super);
	if (retval)
		goto cleanup;
	
	if (fs->super->s_magic != EXT2_SUPER_MAGIC) {
		retval = EXT2_ET_BAD_MAGIC;
		goto cleanup;
	}
	fs->blocksize = EXT2_BLOCK_SIZE(fs->super);
	fs->fragsize = EXT2_FRAG_SIZE(fs->super);
	fs->inode_blocks_per_group = (fs->super->s_inodes_per_group /
				      EXT2_INODES_PER_BLOCK(fs->super));
	if (block_size) {
		if (block_size != fs->blocksize) {
			retval = EXT2_ET_UNEXPECTED_BLOCK_SIZE;
			goto cleanup;
		}
	}
	/*
	 * Set the blocksize to the filesystem's blocksize.
	 */
	io_channel_set_blksize(fs->io, fs->blocksize);
	
	/*
	 * Read group descriptors
	 */
	fs->group_desc_count = (fs->super->s_blocks_count -
				fs->super->s_first_data_block +
				EXT2_BLOCKS_PER_GROUP(fs->super) - 1)
		/ EXT2_BLOCKS_PER_GROUP(fs->super);
	fs->desc_blocks = (fs->group_desc_count +
			   EXT2_DESC_PER_BLOCK(fs->super) - 1)
		/ EXT2_DESC_PER_BLOCK(fs->super);
	fs->group_desc = malloc(fs->desc_blocks * fs->blocksize);
	if (!fs->group_desc) {
		retval = ENOMEM;
		goto cleanup;
	}
	group_block = fs->super->s_first_data_block + 1;
	dest = (char *) fs->group_desc;
	for (i=0 ; i < fs->desc_blocks; i++) {
		retval = io_channel_read_blk(fs->io, group_block, 1, dest);
		if (retval)
			goto cleanup;
		group_block++;
		dest += fs->blocksize;
	}

	*ret_fs = fs;
	return 0;
cleanup:
	ext2fs_free(fs);
	return retval;
}

/*
 * This routine sanity checks the group descriptors
 */
errcode_t ext2fs_check_desc(ext2_filsys fs)
{
	int i;
	int block = fs->super->s_first_data_block;
	int next, inode_blocks_per_group;

	inode_blocks_per_group = fs->super->s_inodes_per_group /
		EXT2_INODES_PER_BLOCK (fs->super);

	for (i = 0; i < fs->group_desc_count; i++) {
		next = block + fs->super->s_blocks_per_group;
		/*
		 * Check to make sure block bitmap for group is
		 * located within the group.
		 */
		if (fs->group_desc[i].bg_block_bitmap < block ||
		    fs->group_desc[i].bg_block_bitmap >= next)
			return EXT2_ET_GDESC_BAD_BLOCK_MAP;
		/*
		 * Check to make sure inode bitmap for group is
		 * located within the group
		 */
		if (fs->group_desc[i].bg_inode_bitmap < block ||
		    fs->group_desc[i].bg_inode_bitmap >= next)
			return EXT2_ET_GDESC_BAD_INODE_MAP;
		/*
		 * Check to make sure inode table for group is located
		 * within the group
		 */
		if (fs->group_desc[i].bg_inode_table < block ||
		    fs->group_desc[i].bg_inode_table+inode_blocks_per_group >=
		    next)
			return EXT2_ET_GDESC_BAD_INODE_TABLE;
		
		block = next;
	}
	return 0;
}

