/*
 * resize2fs.c --- ext2 main routine
 *
 * Copyright (C) 1997 Theodore Ts'o
 * 
 * %Begin-Header%
 * All rights reserved.
 * %End-Header%
 */

#include "resize2fs.h"

/*
 * This routine adjusts the superblock and other data structures...
 */
static errcode_t adjust_superblock(ext2_resize_t rfs, blk_t new_size)
{
	ext2_filsys fs;
	int		overhead = 0;
	int		rem;
	errcode_t	retval;
	ino_t		real_end;
	blk_t		blk, group_block;
	unsigned long	i;
	struct ext2_group_desc *new;
	
	fs = rfs->new_fs;
	fs->super->s_blocks_count = new_size;

retry:
	fs->group_desc_count = (fs->super->s_blocks_count -
				fs->super->s_first_data_block +
				EXT2_BLOCKS_PER_GROUP(fs->super) - 1)
		/ EXT2_BLOCKS_PER_GROUP(fs->super);
	if (fs->group_desc_count == 0)
		return EXT2_ET_TOOSMALL;
	fs->desc_blocks = (fs->group_desc_count +
			   EXT2_DESC_PER_BLOCK(fs->super) - 1)
		/ EXT2_DESC_PER_BLOCK(fs->super);

	/*
	 * Overhead is the number of bookkeeping blocks per group.  It
	 * includes the superblock backup, the group descriptor
	 * backups, the inode bitmap, the block bitmap, and the inode
	 * table.
	 *
	 * XXX Not all block groups need the descriptor blocks, but
	 * being clever is tricky...
	 */
	overhead = 3 + fs->desc_blocks + fs->inode_blocks_per_group;
	
	/*
	 * See if the last group is big enough to support the
	 * necessary data structures.  If not, we need to get rid of
	 * it.
	 */
	rem = (fs->super->s_blocks_count - fs->super->s_first_data_block) %
		fs->super->s_blocks_per_group;
	if ((fs->group_desc_count == 1) && rem && (rem < overhead))
		return EXT2_ET_TOOSMALL;
	if (rem && (rem < overhead+50)) {
		fs->super->s_blocks_count -= rem;
		goto retry;
	}
	/*
	 * Adjust the number of inodes
	 */
	fs->super->s_inodes_count = fs->super->s_inodes_per_group *
		fs->group_desc_count;

	/*
	 * Adjust the number of free blocks
	 */
	blk = rfs->old_fs->super->s_blocks_count;
	if (blk > fs->super->s_blocks_count)
		fs->super->s_free_blocks_count -=
			(blk - fs->super->s_blocks_count);
	else
		fs->super->s_free_blocks_count +=
			(fs->super->s_blocks_count - blk);

	/*
	 * Adjust the bitmaps for size
	 */
	retval = ext2fs_resize_inode_bitmap(fs->super->s_inodes_count,
					    fs->super->s_inodes_count,
					    fs->inode_map);
	if (retval)
		return retval;
	
	real_end = ((EXT2_BLOCKS_PER_GROUP(fs->super)
		     * fs->group_desc_count)) - 1 +
			     fs->super->s_first_data_block;
	retval = ext2fs_resize_block_bitmap(fs->super->s_blocks_count-1,
					    real_end, fs->block_map);

	if (retval)
		return retval;

	/*
	 * Reallocate the group descriptors as necessary.
	 */
	if (rfs->old_fs->desc_blocks != fs->desc_blocks) {
		new = realloc(fs->group_desc,
			      fs->desc_blocks * fs->blocksize);
		if (!new)
			return ENOMEM;
		fs->group_desc = new;
	}
	group_block = rfs->old_fs->super->s_first_data_block;
	for (i = 0; i < fs->group_desc_count; i++) {
		if (i < rfs->old_fs->group_desc_count) {
			group_block += fs->super->s_blocks_per_group;
			continue;
		}
		/* XXXX */
	}
	
	return 0;
}

/*
 * This routine reserves a block in the new filesystem.  If the block
 * is already used, we mark it as needing relocation.  Otherwise, we
 * just mark it as used.
 */
static reserve_block(ext2_resize_t rfs, blk_t blk)
{
	if (ext2fs_test_block_bitmap(rfs->new_fs->block_map, blk))
		ext2fs_mark_block_bitmap(rfs->move_blocks, blk);
	else
		ext2fs_mark_block_bitmap(rfs->new_fs->block_map, blk);
}

/*
 * This routine is a helper function for determine_relocations().  It
 * is called for each block group which has a superblock, and for
 * which we need to expand the size of the descriptor table.  We have
 * to account for the fact that in some cases we will need to move the
 * inode table, which will mean moving or reserving blocks at the end
 * of the inode table, since the inode table will be moved down to
 * make space.
 *
 * "And the block group descriptors waddled across the street..."
 */
static void make_way_for_descriptors(ext2_resize_t rfs,
				     int block_group,
				     blk_t group_blk)
{
	blk_t		blk, start_blk, end_blk, itable, move_by;
	unsigned long	i;
	ext2_filsys 	fs;
	
	start_blk = group_blk + rfs->old_fs->desc_blocks + 1;
	end_blk = group_blk + rfs->new_fs->desc_blocks + 1;
	fs = rfs->new_fs;
	itable = fs->group_desc[block_group].bg_inode_table;
	if (end_blk > itable) {
		move_by = itable - end_blk;
		for (blk = itable, i=0; i < move_by; blk++, i++) {
			ext2fs_unmark_block_bitmap(fs->block_map, blk);
			reserve_block(rfs, blk+fs->inode_blocks_per_group);
		}
		end_blk -= move_by;
		fs->group_desc[i].bg_inode_table += move_by;
	}
	for (blk = start_blk; blk < end_blk; blk++)
		reserve_block(rfs, blk);
}


/*
 * This routine marks and unmarks reserved blocks in the new block
 * bitmap.  It also determines which blocks need to be moved and
 * places this information into the move_blocks bitmap.
 */
static errcode_t determine_relocations(ext2_resize_t rfs)
{
	int	i;
	blk_t	blk, group_blk;
	unsigned long old_blocks, new_blocks;
	errcode_t	retval;

	retval = ext2fs_allocate_block_bitmap(rfs->old_fs,
					      "blocks to be moved",
					      &rfs->move_blocks);
	if (retval)
		return retval;
	
	old_blocks = rfs->old_fs->desc_blocks;
	new_blocks = rfs->new_fs->desc_blocks;

	group_blk = rfs->old_fs->super->s_first_data_block;
	/*
	 * If we're reducing the number of descriptor blocks, this
	 * makes life easy.  :-)   We just have to mark some extra
	 * blocks as free.
	 */
	if (old_blocks > new_blocks) {
		for (i = 0; i < rfs->new_fs->group_desc_count; i++) {
			if (!ext2fs_bg_has_super(rfs->new_fs, i)) {
				group_blk += rfs->new_fs->super->s_blocks_per_group;
				continue;
			}
			for (blk = group_blk+1+old_blocks;
			     blk < group_blk+1+new_blocks; blk++)
				ext2fs_unmark_block_bitmap(rfs->new_fs->block_map,
							   blk);
			group_blk += rfs->new_fs->super->s_blocks_per_group;
		}
	}
	/*
	 * If we're increasing the number of descriptor blocks, life
	 * gets interesting.  In some cases, we will need to move the
	 * inode table.
	 */
	if (old_blocks < new_blocks) {
		for (i = 0; i < rfs->new_fs->group_desc_count; i++) {
			if (!ext2fs_bg_has_super(rfs->new_fs, i)) {
				group_blk += rfs->new_fs->super->s_blocks_per_group;
				continue;
			}
			make_way_for_descriptors(rfs, i, group_blk);
			group_blk += rfs->new_fs->super->s_blocks_per_group;
		}
	}
	/*
	 * Finally, if we're shrinking the filesystem, we need to
	 * move all of the blocks that don't fit any more
	 */
	for (blk = rfs->new_fs->super->s_blocks_count;
	     blk < rfs->old_fs->super->s_blocks_count; blk++) {
		if (ext2fs_test_block_bitmap(rfs->old_fs->block_map, blk))
			ext2fs_mark_block_bitmap(rfs->move_blocks, blk);

	}
}






/*
 * This is the top-level routine which does the dirty deed....
 */
errcode_t resize_fs(ext2_filsys fs, blk_t new_size)
{
	ext2_resize_t	rfs;
	errcode_t	retval;

	/*
	 * First, create the data structure
	 */
	rfs = malloc(sizeof(struct ext2_resize_struct));
	if (!rfs)
		return ENOMEM;
	memset(rfs, 0, sizeof(struct ext2_resize_struct));

	rfs->old_fs = fs;
	retval = ext2fs_dup_handle(fs, &rfs->new_fs);
	if (retval) {
		free(rfs);
		return retval;
	}
	retval = adjust_superblock(rfs, new_size);
	if (retval)
		goto errout;
	
	return 0;

errout:
	ext2fs_free(rfs->new_fs);
	free(rfs);
	return retval;
}

