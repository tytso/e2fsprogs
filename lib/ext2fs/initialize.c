/*
 * initialize.c --- initialize a filesystem handle given superblock
 * 	parameters.  Used by mke2fs when initializing a filesystem.
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

errcode_t ext2fs_initialize(const char *name, int flags,
			    struct ext2_super_block *param,
			    io_manager manager, ext2_filsys *ret_fs)
{
	ext2_filsys	fs;
	errcode_t	retval;
	struct ext2_super_block *super;
	int		frags_per_block;
	int		rem;
	int		overhead = 0;
	blk_t		group_block;
	int		i, j;

	if (!param || !param->s_blocks_count)
		return EINVAL;
	
	fs = (ext2_filsys) malloc(sizeof(struct struct_ext2_filsys));
	if (!fs)
		return ENOMEM;
	
	memset(fs, 0, sizeof(struct struct_ext2_filsys));
	fs->flags = flags | EXT2_FLAG_RW;
	retval = manager->open(name, IO_FLAG_RW, &fs->io);
	if (retval)
		goto cleanup;
	fs->device_name = malloc(strlen(name)+1);
	if (!fs->device_name) {
		retval = ENOMEM;
		goto cleanup;
	}
	strcpy(fs->device_name, name);
	fs->super = super = malloc(SUPERBLOCK_SIZE);
	if (!super) {
		retval = ENOMEM;
		goto cleanup;
	}
	memset(super, 0, SUPERBLOCK_SIZE);

#define set_field(field, default) (super->field = param->field ? \
				   param->field : (default))

	super->s_magic = EXT2_SUPER_MAGIC;
	super->s_state = EXT2_VALID_FS;

	set_field(s_log_block_size, 0);	/* default blocksize: 1024 bytes */
	set_field(s_log_frag_size, 0); /* default fragsize: 1024 bytes */
	set_field(s_first_data_block, super->s_log_block_size ? 0 : 1);
	set_field(s_max_mnt_count, EXT2_DFL_MAX_MNT_COUNT);
	set_field(s_errors, EXT2_ERRORS_DEFAULT);

	set_field(s_checkinterval, EXT2_DFL_CHECKINTERVAL);
	super->s_lastcheck = time(NULL);

	fs->blocksize = EXT2_BLOCK_SIZE(super);
	fs->fragsize = EXT2_FRAG_SIZE(super);
	frags_per_block = fs->blocksize / fs->fragsize;
	
	set_field(s_blocks_per_group, 8192); /* default: 8192 blocks/group */
	super->s_frags_per_group = super->s_blocks_per_group * frags_per_block;
	
	super->s_blocks_count = param->s_blocks_count;

retry:
	set_field(s_r_blocks_count, super->s_blocks_count/20); /* 5% default */
		  
	fs->group_desc_count = (super->s_blocks_count -
				super->s_first_data_block +
				EXT2_BLOCKS_PER_GROUP(super) - 1)
		/ EXT2_BLOCKS_PER_GROUP(super);
	fs->desc_blocks = (fs->group_desc_count +
			   EXT2_DESC_PER_BLOCK(super) - 1)
		/ EXT2_DESC_PER_BLOCK(super);

	set_field(s_inodes_count, (super->s_blocks_count*fs->blocksize)/4096);

	/*
	 * There should be at least as many inodes as the user
	 * requested.  Figure out how many inodes per group that
	 * should be.
	 */
	super->s_inodes_per_group = (super->s_inodes_count +
				     fs->group_desc_count - 1) /
					     fs->group_desc_count;
	
	/*
	 * Make sure the number of inodes per group completely fills
	 * the inode table blocks in the descriptor.  If not, add some
	 * additional inodes/group.  Waste not, want not...
	 */
	fs->inode_blocks_per_group = (super->s_inodes_per_group +
				      EXT2_INODES_PER_BLOCK(super) - 1) /
					      EXT2_INODES_PER_BLOCK(super);
	super->s_inodes_per_group = fs->inode_blocks_per_group *
		EXT2_INODES_PER_BLOCK(super);
		
	/*
	 * adjust inode count to reflect the adjusted inodes_per_group
	 */
	super->s_inodes_count = super->s_inodes_per_group *
		fs->group_desc_count;
	super->s_free_inodes_count = super->s_inodes_count;

	/*
	 * Overhead is the number of bookkeeping blocks per group.  It
	 * includes the superblock backup, the group descriptor
	 * backups, the inode bitmap, the block bitmap, and the inode
	 * table.
	 */
	overhead = 3 + fs->desc_blocks + fs->inode_blocks_per_group;
	super->s_free_blocks_count = super->s_blocks_count -
		super->s_first_data_block - (overhead*fs->group_desc_count);
	
	/*
	 * See if the last group is big enough to support the
	 * necessary data structures.  If not, we need to get rid of
	 * it.
	 */
	rem = (super->s_blocks_count - super->s_first_data_block) %
		super->s_blocks_per_group;
	if ((fs->group_desc_count == 1) && rem && (rem < overhead))
		return EXT2_ET_TOOSMALL;
	if (rem && (rem < overhead+50)) {
		super->s_blocks_count -= rem;
		goto retry;
	}

	/*
	 * At this point we know how big the filesystem will be.  So
	 * we can do any and all allocations that depend on the block
	 * count.
	 */

	retval = ext2fs_allocate_block_bitmap(fs, &fs->block_map);
	if (retval)
		goto cleanup;
	
	retval = ext2fs_allocate_inode_bitmap(fs, &fs->inode_map);
	if (retval)
		goto cleanup;

	fs->group_desc = malloc(fs->desc_blocks * fs->blocksize);
	if (!fs->group_desc) {
		retval = ENOMEM;
		goto cleanup;
	}
	memset(fs->group_desc, 0, fs->desc_blocks * fs->blocksize);

	group_block = super->s_first_data_block;
	for (i = 0; i < fs->group_desc_count; i++) {
		for (j=0; j < fs->desc_blocks+1; j++)
			ext2fs_mark_block_bitmap(fs, fs->block_map,
						 group_block + j);
		group_block += super->s_blocks_per_group;
	}
	
	ext2fs_mark_super_dirty(fs);
	ext2fs_mark_bb_dirty(fs);
	ext2fs_mark_ib_dirty(fs);
	
	io_channel_set_blksize(fs->io, fs->blocksize);

	*ret_fs = fs;
	return 0;
cleanup:
	ext2fs_free(fs);
	return retval;
}
	


