/*
 * swapfs.c --- swap ext2 filesystem data structures
 * 
 * Copyright (C) 1995 Theodore Ts'o.  This file may be redistributed
 * under the terms of the GNU Public License.
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>

#include <linux/ext2_fs.h>

#include "ext2fs.h"

void ext2fs_swap_super(struct ext2_super_block * super)
{
	super->s_inodes_count = ext2fs_swab32(super->s_inodes_count);
	super->s_blocks_count = ext2fs_swab32(super->s_blocks_count);
	super->s_r_blocks_count = ext2fs_swab32(super->s_r_blocks_count);
	super->s_free_blocks_count = ext2fs_swab32(super->s_free_blocks_count);
	super->s_free_inodes_count = ext2fs_swab32(super->s_free_inodes_count);
	super->s_first_data_block = ext2fs_swab32(super->s_first_data_block);
	super->s_log_block_size = ext2fs_swab32(super->s_log_block_size);
	super->s_log_frag_size = ext2fs_swab32(super->s_log_frag_size);
	super->s_blocks_per_group = ext2fs_swab32(super->s_blocks_per_group);
	super->s_frags_per_group = ext2fs_swab32(super->s_frags_per_group);
	super->s_inodes_per_group = ext2fs_swab32(super->s_inodes_per_group);
	super->s_mtime = ext2fs_swab32(super->s_mtime);
	super->s_wtime = ext2fs_swab32(super->s_wtime);
	super->s_mnt_count = ext2fs_swab16(super->s_mnt_count);
	super->s_max_mnt_count = ext2fs_swab16(super->s_max_mnt_count);
	super->s_magic = ext2fs_swab16(super->s_magic);
	super->s_state = ext2fs_swab16(super->s_state);
	super->s_errors = ext2fs_swab16(super->s_errors);
	super->s_lastcheck = ext2fs_swab32(super->s_lastcheck);
	super->s_checkinterval = ext2fs_swab32(super->s_checkinterval);
	super->s_creator_os = ext2fs_swab32(super->s_creator_os);
	super->s_rev_level = ext2fs_swab32(super->s_rev_level);
#ifdef	EXT2_DEF_RESUID
	super->s_def_resuid = ext2fs_swab16(super->s_def_resuid);
	super->s_def_resgid = ext2fs_swab16(super->s_def_resgid);
#endif
}

void ext2fs_swap_group_desc(struct ext2_group_desc *gdp)
{
	gdp->bg_block_bitmap = ext2fs_swab32(gdp->bg_block_bitmap);
	gdp->bg_inode_bitmap = ext2fs_swab32(gdp->bg_inode_bitmap);
	gdp->bg_inode_table = ext2fs_swab32(gdp->bg_inode_table);
	gdp->bg_free_blocks_count = ext2fs_swab16(gdp->bg_free_blocks_count);
	gdp->bg_free_inodes_count = ext2fs_swab16(gdp->bg_free_inodes_count);
	gdp->bg_used_dirs_count = ext2fs_swab16(gdp->bg_used_dirs_count);
}

	

