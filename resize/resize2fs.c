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
	unsigned long	i, j;
	struct ext2_group_desc *new;
	char		*buf;
	int		old_numblocks, numblocks, adjblocks;
	
	fs = rfs->new_fs;
	fs->super->s_blocks_count = new_size;
	ext2fs_mark_super_dirty(fs);
	ext2fs_mark_bb_dirty(fs);
	ext2fs_mark_ib_dirty(fs);

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

	/*
	 * Fix the count of the last (old) block group
	 */
	if (rfs->old_fs->group_desc_count > fs->group_desc_count)
		return 0;
	old_numblocks = (rfs->old_fs->super->s_blocks_count -
			 rfs->old_fs->super->s_first_data_block) %
				 rfs->old_fs->super->s_blocks_per_group;
	if (!old_numblocks)
		old_numblocks = rfs->old_fs->super->s_blocks_per_group;
	if (rfs->old_fs->group_desc_count == fs->group_desc_count) {
		numblocks = (rfs->new_fs->super->s_blocks_count -
			     rfs->new_fs->super->s_first_data_block) %
				     rfs->new_fs->super->s_blocks_per_group;
		if (!numblocks)
			numblocks = rfs->new_fs->super->s_blocks_per_group;
	} else
		numblocks = rfs->new_fs->super->s_blocks_per_group;
	i = rfs->old_fs->group_desc_count - 1;
	fs->group_desc[i].bg_free_blocks_count += (numblocks-old_numblocks);
		
	/*
	 * Initialize the new block group descriptors
	 */
	if (rfs->old_fs->group_desc_count >= fs->group_desc_count)
		return 0;
	buf = malloc(fs->blocksize);
	if (!buf)
		return ENOMEM;
	memset(buf, 0, fs->blocksize);
	group_block = fs->super->s_first_data_block +
		rfs->old_fs->group_desc_count * fs->super->s_blocks_per_group;
	for (i = rfs->old_fs->group_desc_count;
	     i < fs->group_desc_count; i++) {
		memset(&fs->group_desc[i], 0,
		       sizeof(struct ext2_group_desc));
		adjblocks = 0;

		if (i == fs->group_desc_count-1) {
			numblocks = (fs->super->s_blocks_count -
				     fs->super->s_first_data_block) %
					     fs->super->s_blocks_per_group;
			if (!numblocks)
				numblocks = fs->super->s_blocks_per_group;
		} else
			numblocks = fs->super->s_blocks_per_group;

		if (ext2fs_bg_has_super(fs, i)) {
			for (j=0; j < fs->desc_blocks+1; j++)
				ext2fs_mark_block_bitmap(fs->block_map,
							 group_block + j);
			adjblocks = 1 + fs->desc_blocks;
		}
		adjblocks += 2 + fs->inode_blocks_per_group;
		
		numblocks -= adjblocks;
		fs->super->s_free_blocks_count -= adjblocks;
		fs->super->s_free_inodes_count +=
			fs->super->s_inodes_per_group;
		fs->group_desc[i].bg_free_blocks_count = numblocks;
		fs->group_desc[i].bg_free_inodes_count =
			fs->super->s_inodes_per_group;
		fs->group_desc[i].bg_used_dirs_count = 0;

		retval = ext2fs_allocate_group_table(fs, i, 0);
		if (retval)
			return retval;

		for (blk=fs->group_desc[i].bg_inode_table, j=0;
		     j < fs->inode_blocks_per_group;
		     blk++, j++) {
			retval = io_channel_write_blk(fs->io, blk, 1, buf);
			if (retval)
				return retval;
		}
		group_block += fs->super->s_blocks_per_group;
	}
	return 0;
}

/*
 * This routine marks and unmarks reserved blocks in the new block
 * bitmap.  It also determines which blocks need to be moved and
 * places this information into the move_blocks bitmap.
 */
static errcode_t determine_relocations(ext2_resize_t rfs)
{
	int	i, j, max, adj;
	blk_t	blk, group_blk;
	unsigned long old_blocks, new_blocks;
	errcode_t	retval;
	ext2_filsys 	fs = rfs->new_fs;

	retval = ext2fs_allocate_block_bitmap(rfs->old_fs,
					      "blocks to be moved",
					      &rfs->reserve_blocks);
	if (retval)
		return retval;

	/*
	 * If we're shrinking the filesystem, we need to move all of
	 * the blocks that don't fit any more
	 */
	for (blk = fs->super->s_blocks_count;
	     blk < rfs->old_fs->super->s_blocks_count; blk++) {
		if (ext2fs_test_block_bitmap(rfs->old_fs->block_map, blk))
			rfs->needed_blocks++;
		ext2fs_mark_block_bitmap(rfs->reserve_blocks, blk);
	}
	
	old_blocks = rfs->old_fs->desc_blocks;
	new_blocks = fs->desc_blocks;

	if (old_blocks == new_blocks)
		return 0;

	max = fs->group_desc_count;
	if (max > rfs->old_fs->group_desc_count)
		max = rfs->old_fs->group_desc_count;
	group_blk = rfs->old_fs->super->s_first_data_block;
	/*
	 * If we're reducing the number of descriptor blocks, this
	 * makes life easy.  :-)   We just have to mark some extra
	 * blocks as free.
	 */
	if (old_blocks > new_blocks) {
		for (i = 0; i < max; i++) {
			if (!ext2fs_bg_has_super(fs, i)) {
				group_blk += fs->super->s_blocks_per_group;
				continue;
			}
			for (blk = group_blk+1+old_blocks;
			     blk < group_blk+1+new_blocks; blk++) {
				ext2fs_unmark_block_bitmap(fs->block_map,
							   blk);
				rfs->needed_blocks--;
			}
			group_blk += fs->super->s_blocks_per_group;
		}
		return 0;
	}
	/*
	 * If we're increasing the number of descriptor blocks, life
	 * gets interesting....  
	 */
	for (i = 0; i < max; i++) {
		if (!ext2fs_bg_has_super(fs, i))
			goto next_group;

		for (blk = group_blk;
		     blk < group_blk + 1 + new_blocks; blk++) {
			ext2fs_mark_block_bitmap(rfs->reserve_blocks, blk);
			ext2fs_mark_block_bitmap(fs->block_map, blk);

			/*
			 * Check to see if we overlap with the inode
			 * or block bitmap
			 */
			if (blk == fs->group_desc[i].bg_block_bitmap) {
				fs->group_desc[i].bg_block_bitmap = 0;
				rfs->needed_blocks++;
			}
			if (blk == fs->group_desc[i].bg_inode_bitmap) {
				fs->group_desc[i].bg_inode_bitmap = 0;
				rfs->needed_blocks++;
			}
			/*
			 * Check to see if we overlap with the inode
			 * table
			 */
			if (blk < fs->group_desc[i].bg_inode_table)
				continue;
			if (blk >= (fs->group_desc[i].bg_inode_table +
				    fs->inode_blocks_per_group))
				continue;
			fs->group_desc[i].bg_inode_table = 0;
			blk = fs->group_desc[i].bg_inode_table +
				fs->inode_blocks_per_group - 1;
		}
		if (fs->group_desc[i].bg_inode_table &&
		    fs->group_desc[i].bg_inode_bitmap &&
		    fs->group_desc[i].bg_block_bitmap)
			goto next_group;

		/*
		 * Allocate the missing bitmap and inode table
		 * structures, passing in rfs->reserve_blocks to
		 * prevent a conflict.  
		 */
		if (fs->group_desc[i].bg_block_bitmap)
			ext2fs_mark_block_bitmap(rfs->reserve_blocks,
				 fs->group_desc[i].bg_block_bitmap);
		if (fs->group_desc[i].bg_inode_bitmap)
			ext2fs_mark_block_bitmap(rfs->reserve_blocks,
				 fs->group_desc[i].bg_inode_bitmap);
		if (fs->group_desc[i].bg_inode_table)
			for (blk = fs->group_desc[i].bg_inode_table, j=0;
			     j < fs->inode_blocks_per_group ; j++, blk++)
				ext2fs_mark_block_bitmap(rfs->reserve_blocks,
							 blk);

		retval = ext2fs_allocate_group_table(fs, i,
						     rfs->reserve_blocks);
		if (retval)
			return retval;

		/*
		 * Now make sure these blocks are reserved in the new
		 * block bitmap
		 */
		ext2fs_mark_block_bitmap(fs->block_map,
					 fs->group_desc[i].bg_block_bitmap);
		ext2fs_mark_block_bitmap(fs->block_map,
					 fs->group_desc[i].bg_inode_bitmap);

		/*
		 * The inode table, if we need to relocate it, is
		 * handled specially.  We have to reserve the blocks
		 * for both the old and the new inode table, since we
		 * can't have the inode table be destroyed during the
		 * block relocation phase.
		 */
		adj = fs->group_desc[i].bg_inode_table -
			rfs->old_fs->group_desc[i].bg_inode_table;
		if (!adj)
			goto next_group; /* inode table not moved */

		/*
		 * Figure out how many blocks we need to have free.
		 * This takes into account that we need to reserve
		 * both inode tables, which may be overallping.
		 */
		if (adj < 0)
			adj = -adj;
		if (adj > fs->inode_blocks_per_group)
			adj = fs->inode_blocks_per_group;
		rfs->needed_blocks += fs->inode_blocks_per_group + adj;

		/*
		 * Mark the new inode table as in use in the new block
		 * allocation bitmap.
		 */
		for (blk = fs->group_desc[i].bg_inode_table, j=0;
		     j < fs->inode_blocks_per_group ; j++, blk++)
			ext2fs_mark_block_bitmap(fs->block_map, blk);
		/*
		 * Make sure the old inode table is reserved in the
		 * block reservation bitmap.
		 */
		for (blk = rfs->old_fs->group_desc[i].bg_inode_table, j=0;
		     j < fs->inode_blocks_per_group ; j++, blk++)
			ext2fs_mark_block_bitmap(rfs->reserve_blocks, blk);
		
	next_group:
		group_blk += rfs->new_fs->super->s_blocks_per_group;
	}
	return 0;
}


/*
 * A very scary routine --- this one moves the inode table around!!!
 *
 * After this you have to use the rfs->new_fs file handle to read and
 * write inodes.
 */
errcode_t move_itables(ext2_resize_t rfs)
{
	int	i, max;
	ext2_filsys	fs = rfs->new_fs;
	char		*buf;
	blk_t		old, new;
	errcode_t	retval, err;

	printf("Hide the women and children --- "
	       "commencing inode table moves!!\n");
	
	max = fs->group_desc_count;
	if (max > rfs->old_fs->group_desc_count)
		max = rfs->old_fs->group_desc_count;

	buf = malloc(fs->blocksize * fs->inode_blocks_per_group);
	if (!buf)
		return ENOMEM;
	
	for (i=0; i < max; i++) {
		old = rfs->old_fs->group_desc[i].bg_inode_table;
		new = fs->group_desc[i].bg_inode_table;
		
		printf("Group %d block %ld->%ld\n", i, old, new);
		
		if (old == new)
			continue;

		retval = io_channel_read_blk(fs->io, old,
					     fs->inode_blocks_per_group, buf);
		if (retval) 
			goto backout;
		retval = io_channel_write_blk(fs->io, new,
					      fs->inode_blocks_per_group, buf);
		if (retval) {
			io_channel_write_blk(fs->io, old,
					      fs->inode_blocks_per_group, buf);
			goto backout;
		}
	}
	ext2fs_flush(rfs->new_fs);
	printf("Inode table move finished.\n");
	return 0;
	
backout:
	printf("Error: %s; now backing out!\n", error_message(retval));
	while (--i >= 0) {
		printf("Group %d block %ld->%ld\n", i, new, old);
		old = rfs->old_fs->group_desc[i].bg_inode_table;
		new = fs->group_desc[i].bg_inode_table;
		
		err = io_channel_read_blk(fs->io, new,
					  fs->inode_blocks_per_group, buf);
		if (err)
			continue;
		err = io_channel_write_blk(fs->io, old,
					   fs->inode_blocks_per_group, buf);
	}
	return retval;
}

/*
 * Finally, recalculate the summary information
 */
static errcode_t ext2fs_calculate_summary_stats(ext2_filsys fs)
{
	blk_t	blk;
	ino_t	ino;
	int	group = 0;
	int	count = 0;
	int	total_free = 0;
	int	group_free = 0;

	/*
	 * First calculate the block statistics
	 */
	for (blk = fs->super->s_first_data_block;
	     blk < fs->super->s_blocks_count; blk++) {
		if (!ext2fs_fast_test_block_bitmap(fs->block_map, blk)) {
			group_free++;
			total_free++;
		}
		count++;
		if ((count == fs->super->s_blocks_per_group) ||
		    (blk == fs->super->s_blocks_count-1)) {
			fs->group_desc[group++].bg_free_blocks_count =
				group_free;
			count = 0;
			group_free = 0;
		}
	}
	fs->super->s_free_blocks_count = total_free;
	
	/*
	 * Next, calculate the inode statistics
	 */
	group_free = 0;
	total_free = 0;
	count = 0;
	group = 0;
	for (ino = 1; ino <= fs->super->s_inodes_count; ino++) {
		if (!ext2fs_fast_test_inode_bitmap(fs->inode_map, ino)) {
			group_free++;
			total_free++;
		}
		count++;
		if ((count == fs->super->s_inodes_per_group) ||
		    (ino == fs->super->s_inodes_count)) {
			fs->group_desc[group++].bg_free_inodes_count =
				group_free;
			count = 0;
			group_free = 0;
		}
	}
	fs->super->s_free_inodes_count = total_free;
	ext2fs_mark_super_dirty(fs);
	return 0;
}



/*
 * This is the top-level routine which does the dirty deed....
 */
errcode_t resize_fs(ext2_filsys fs, blk_t new_size)
{
	ext2_resize_t	rfs;
	errcode_t	retval;

	retval = ext2fs_read_bitmaps(fs);
	if (retval)
		return retval;
	
	/*
	 * Create the data structure
	 */
	rfs = malloc(sizeof(struct ext2_resize_struct));
	if (!rfs)
		return ENOMEM;
	memset(rfs, 0, sizeof(struct ext2_resize_struct));

	rfs->old_fs = fs;
	retval = ext2fs_dup_handle(fs, &rfs->new_fs);
	if (retval)
		goto errout;

	retval = adjust_superblock(rfs, new_size);
	if (retval)
		goto errout;

	retval = determine_relocations(rfs);
	if (retval)
		goto errout;

	printf("Number of free blocks: %d, Needed: %d\n",
	       fs->super->s_free_blocks_count, rfs->needed_blocks);
	
	if (rfs->needed_blocks > fs->super->s_free_blocks_count) {
		retval = ENOSPC;
		goto errout;
	}
	
	printf("\nOld superblock:\n");
	list_super(rfs->old_fs->super);
	printf("\n\nNew superblock:\n");
	list_super(rfs->new_fs->super);
	printf("\n");

	retval = ext2fs_move_blocks(rfs->old_fs, rfs->reserve_blocks,
				    rfs->new_fs->block_map,
				    EXT2_BMOVE_GET_DBLIST);
	if (retval)
		return retval;

	retval = ext2fs_inode_move(rfs);
	if (retval)
		return retval;

	retval = move_itables(rfs);
	if (retval)
		return retval;

	retval = ext2fs_calculate_summary_stats(rfs->new_fs);
	if (retval)
		return retval;
	
	retval = ext2fs_close(rfs->new_fs);
	if (retval)
		return retval;
	
	ext2fs_free(rfs->old_fs);
	
	return 0;

errout:
	if (rfs->new_fs)
		ext2fs_free(rfs->new_fs);
	free(rfs);
	return retval;
}
