/*
 * resize2fs.c --- ext2 main routine
 *
 * Copyright (C) 1997 Theodore Ts'o
 * 
 * %Begin-Header%
 * All rights reserved.
 * %End-Header%
 */

/*
 * Resizing a filesystem consists of the following phases:
 *
 *	1.  Adjust superblock and (*) write out new parts of the inode
 * 		table
 * 	2.  Determine blocks which need to be relocated.
 * 	3.  (*) Relocate blocks which must be moved, adjusting entries
 * 		in the filesystem in the process.
 * 	4.  (*) Move inodes which must be moved (only when shrinking a
 * 		filesystem)
 * 	5.  (*) Move the inode tables, if necessary.
 */
#include "resize2fs.h"

/*
 * This routine adjusts the superblock and other data structures...
 */
static errcode_t adjust_superblock(ext2_resize_t rfs, blk_t new_size)
{
	ext2_filsys fs;
	int		overhead = 0;
	int		rem, adj = 0;
	errcode_t	retval;
	ino_t		real_end;
	blk_t		blk, group_block;
	unsigned long	i, j;
	struct ext2_group_desc *new;
	int		old_numblocks, numblocks, adjblocks;
	ext2_sim_progmeter progress = 0;
	
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
	 * Adjust the number of reserved blocks
	 */
	blk = rfs->old_fs->super->s_r_blocks_count * 100 /
		rfs->old_fs->super->s_blocks_count;
	fs->super->s_r_blocks_count = ((fs->super->s_blocks_count * blk)
				       / 100);

	/*
	 * Adjust the bitmaps for size
	 */
	retval = ext2fs_resize_inode_bitmap(fs->super->s_inodes_count,
					    fs->super->s_inodes_count,
					    fs->inode_map);
	if (retval) goto errout;
	
	real_end = ((EXT2_BLOCKS_PER_GROUP(fs->super)
		     * fs->group_desc_count)) - 1 +
			     fs->super->s_first_data_block;
	retval = ext2fs_resize_block_bitmap(fs->super->s_blocks_count-1,
					    real_end, fs->block_map);

	if (retval) goto errout;

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
	if (rfs->old_fs->group_desc_count > fs->group_desc_count) {
		retval = 0;
		goto errout;
	}
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
	if (rfs->old_fs->group_desc_count >= fs->group_desc_count) {
		retval = 0;
		goto errout;
	}
	rfs->itable_buf = malloc(fs->blocksize * fs->inode_blocks_per_group);
	if (!rfs->itable_buf) {
		retval = ENOMEM;
		goto errout;
	}
	memset(rfs->itable_buf, 0, fs->blocksize * fs->inode_blocks_per_group);
	group_block = fs->super->s_first_data_block +
		rfs->old_fs->group_desc_count * fs->super->s_blocks_per_group;

	if (rfs->flags & RESIZE_PERCENT_COMPLETE) {
		adj = rfs->old_fs->group_desc_count;
		retval = ext2fs_progress_init(&progress,
		      "Initializing inode table", 30, 40,
		      fs->group_desc_count - adj, 0);
		if (retval) goto errout;
	}
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
		if (retval) goto errout;

		/*
		 * Write out the new inode table
		 */
		retval = io_channel_write_blk(fs->io,
					      fs->group_desc[i].bg_inode_table,
					      fs->inode_blocks_per_group,
					      rfs->itable_buf);
		if (retval) goto errout;

		/* io_channel_flush(fs->io); */
		if (progress)
			ext2fs_progress_update(progress, i - adj + 1);
		
		group_block += fs->super->s_blocks_per_group;
	}
	io_channel_flush(fs->io);
	retval = 0;

errout:
	if (progress)
		ext2fs_progress_close(progress);
	return retval;
}

/*
 * This helper function creates a block bitmap with all of the
 * filesystem meta-data blocks.
 */
static errcode_t mark_table_blocks(ext2_filsys fs,
				   ext2fs_block_bitmap *ret_bmap)
{
	blk_t			block, b;
	int			i,j;
	ext2fs_block_bitmap	bmap;
	errcode_t		retval;

	retval = ext2fs_allocate_block_bitmap(fs, "meta-data blocks", &bmap);
	if (retval)
		return retval;
	
	block = fs->super->s_first_data_block;
	for (i = 0; i < fs->group_desc_count; i++) {
		if (ext2fs_bg_has_super(fs, i)) {
			/*
			 * Mark this group's copy of the superblock
			 */
			ext2fs_mark_block_bitmap(bmap, block);
		
			/*
			 * Mark this group's copy of the descriptors
			 */
			for (j = 0; j < fs->desc_blocks; j++)
				ext2fs_mark_block_bitmap(bmap, block + j + 1);
		}
		
		/*
		 * Mark the blocks used for the inode table
		 */
		for (j = 0, b = fs->group_desc[i].bg_inode_table;
		     j < fs->inode_blocks_per_group;
		     j++, b++)
			ext2fs_mark_block_bitmap(bmap, b);
			    
		/*
		 * Mark block used for the block bitmap 
		 */
		ext2fs_mark_block_bitmap(bmap,
					 fs->group_desc[i].bg_block_bitmap);
		/*
		 * Mark block used for the inode bitmap 
		 */
		ext2fs_mark_block_bitmap(bmap,
					 fs->group_desc[i].bg_inode_bitmap);
		block += fs->super->s_blocks_per_group;
	}
	*ret_bmap = bmap;
	return 0;
}



/*
 * Some helper CPP macros
 */
#define FS_BLOCK_BM(fs, i) ((fs)->group_desc[(i)].bg_block_bitmap)
#define FS_INODE_BM(fs, i) ((fs)->group_desc[(i)].bg_inode_bitmap)
#define FS_INODE_TB(fs, i) ((fs)->group_desc[(i)].bg_inode_table)

#define IS_BLOCK_BM(fs, i, blk) ((blk) == FS_BLOCK_BM((fs),(i)))
#define IS_INODE_BM(fs, i, blk) ((blk) == FS_INODE_BM((fs),(i)))

#define IS_INODE_TB(fs, i, blk) (((blk) >= FS_INODE_TB((fs), (i))) && \
				 ((blk) < (FS_INODE_TB((fs), (i)) + \
					   (fs)->inode_blocks_per_group)))

/*
 * This routine marks and unmarks reserved blocks in the new block
 * bitmap.  It also determines which blocks need to be moved and
 * places this information into the move_blocks bitmap.
 */
static errcode_t blocks_to_move(ext2_resize_t rfs)
{
	int	i, j, max;
	blk_t	blk, group_blk;
	unsigned long old_blocks, new_blocks;
	errcode_t	retval;
	ext2_filsys 	fs, old_fs;
	ext2fs_block_bitmap	meta_bmap;

	fs = rfs->new_fs;
	old_fs = rfs->old_fs;
	if (old_fs->super->s_blocks_count > fs->super->s_blocks_count)
		fs = rfs->old_fs;
	
	retval = ext2fs_allocate_block_bitmap(fs, "reserved blocks",
					      &rfs->reserve_blocks);
	if (retval)
		return retval;

	retval = ext2fs_allocate_block_bitmap(fs, "blocks to be moved",
					      &rfs->move_blocks);
	if (retval)
		return retval;

	retval = mark_table_blocks(fs, &meta_bmap);
	if (retval)
		return retval;

	fs = rfs->new_fs;
	
	/*
	 * If we're shrinking the filesystem, we need to move all of
	 * the blocks that don't fit any more
	 */
	for (blk = fs->super->s_blocks_count;
	     blk < old_fs->super->s_blocks_count; blk++) {
		if (ext2fs_test_block_bitmap(old_fs->block_map, blk) &&
		    !ext2fs_test_block_bitmap(meta_bmap, blk)) {
			ext2fs_mark_block_bitmap(rfs->move_blocks, blk);
			rfs->needed_blocks++;
		}
		ext2fs_mark_block_bitmap(rfs->reserve_blocks, blk);
	}
	
	old_blocks = old_fs->desc_blocks;
	new_blocks = fs->desc_blocks;

	if (old_blocks == new_blocks) {
		retval = 0;
		goto errout;
	}

	max = fs->group_desc_count;
	if (max > old_fs->group_desc_count)
		max = old_fs->group_desc_count;
	group_blk = old_fs->super->s_first_data_block;
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
			for (blk = group_blk+1+new_blocks;
			     blk < group_blk+1+old_blocks; blk++) {
				ext2fs_unmark_block_bitmap(fs->block_map,
							   blk);
				rfs->needed_blocks--;
			}
			group_blk += fs->super->s_blocks_per_group;
		}
		retval = 0;
		goto errout;
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
			 * or block bitmap, or the inode tables.  If
			 * not, and the block is in use, then mark it
			 * as a block to be moved.
			 */
			if (IS_BLOCK_BM(fs, i, blk)) {
				FS_BLOCK_BM(fs, i) = 0;
				rfs->needed_blocks++;
			} else if (IS_INODE_BM(fs, i, blk)) {
				FS_INODE_BM(fs, i) = 0;
				rfs->needed_blocks++;
			} else if (IS_INODE_TB(fs, i, blk)) {
				FS_INODE_TB(fs, i) = 0;
				rfs->needed_blocks++;
			} else if (ext2fs_test_block_bitmap(old_fs->block_map,
							    blk) &&
				   !ext2fs_test_block_bitmap(meta_bmap, blk)) {
				ext2fs_mark_block_bitmap(rfs->move_blocks,
							 blk);
				rfs->needed_blocks++;
			}
		}
		if (fs->group_desc[i].bg_inode_table &&
		    fs->group_desc[i].bg_inode_bitmap &&
		    fs->group_desc[i].bg_block_bitmap)
			goto next_group;

		/*
		 * Reserve the existing meta blocks that we know
		 * aren't to be moved.
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

		/*
		 * Allocate the missing data structures
		 */
		retval = ext2fs_allocate_group_table(fs, i,
						     rfs->reserve_blocks);
		if (retval)
			goto errout;

		/*
		 * For those structures that have changed, we need to
		 * do bookkeepping.
		 */
		if (FS_BLOCK_BM(old_fs, i) !=
		    (blk = FS_BLOCK_BM(fs, i))) {
			ext2fs_mark_block_bitmap(fs->block_map, blk);
			if (ext2fs_test_block_bitmap(old_fs->block_map, blk) &&
			    !ext2fs_test_block_bitmap(meta_bmap, blk))
				ext2fs_mark_block_bitmap(rfs->move_blocks,
							 blk);
		}
		if (FS_INODE_BM(old_fs, i) !=
		    (blk = FS_INODE_BM(fs, i))) {
			ext2fs_mark_block_bitmap(fs->block_map, blk);
			if (ext2fs_test_block_bitmap(old_fs->block_map, blk) &&
			    !ext2fs_test_block_bitmap(meta_bmap, blk))
				ext2fs_mark_block_bitmap(rfs->move_blocks,
							 blk);
		}

		/*
		 * The inode table, if we need to relocate it, is
		 * handled specially.  We have to reserve the blocks
		 * for both the old and the new inode table, since we
		 * can't have the inode table be destroyed during the
		 * block relocation phase.
		 */
		if (FS_INODE_TB(fs, i) == FS_INODE_TB(old_fs, i))
			goto next_group; /* inode table not moved */

		rfs->needed_blocks += fs->inode_blocks_per_group;

		/*
		 * Mark the new inode table as in use in the new block
		 * allocation bitmap, and move any blocks that might 
		 * be necessary.
		 */
		for (blk = fs->group_desc[i].bg_inode_table, j=0;
		     j < fs->inode_blocks_per_group ; j++, blk++) {
			ext2fs_mark_block_bitmap(fs->block_map, blk);
			if (ext2fs_test_block_bitmap(old_fs->block_map, blk) &&
			    !ext2fs_test_block_bitmap(meta_bmap, blk))
				ext2fs_mark_block_bitmap(rfs->move_blocks,
							 blk);
		}
		
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
	retval = 0;

errout:
	if (meta_bmap)
		ext2fs_free_block_bitmap(meta_bmap);
	
	return retval;
}


/*
 * A very scary routine --- this one moves the inode table around!!!
 *
 * After this you have to use the rfs->new_fs file handle to read and
 * write inodes.
 */
static errcode_t move_itables(ext2_resize_t rfs)
{
	int		i, n, num, max, size, diff;
	ext2_filsys	fs = rfs->new_fs;
	char		*cp;
	blk_t		old, new;
	errcode_t	retval, err;
	ext2_sim_progmeter progress = 0;
	int		to_move, moved;

	max = fs->group_desc_count;
	if (max > rfs->old_fs->group_desc_count)
		max = rfs->old_fs->group_desc_count;

	size = fs->blocksize * fs->inode_blocks_per_group;
	if (!rfs->itable_buf) {
		rfs->itable_buf = malloc(size);
		if (!rfs->itable_buf)
			return ENOMEM;
	}

	/*
	 * Figure out how many inode tables we need to move
	 */
	to_move = moved = 0;
	for (i=0; i < max; i++)
		if (rfs->old_fs->group_desc[i].bg_inode_table !=
		    fs->group_desc[i].bg_inode_table)
			to_move++;

	if (to_move == 0)
		return 0;

	if (rfs->flags & RESIZE_PERCENT_COMPLETE) {
		retval = ext2fs_progress_init(&progress,
		      "Moving inode table", 30, 40, to_move, 0);
		if (retval)
			return retval;
	}
	
	for (i=0; i < max; i++) {
		old = rfs->old_fs->group_desc[i].bg_inode_table;
		new = fs->group_desc[i].bg_inode_table;
		diff = new - old;
		
		if (rfs->flags & RESIZE_DEBUG_ITABLEMOVE) 
			printf("Itable move group %d block "
			       "%u->%u (diff %d)\n", 
			       i, old, new, diff);
		
		if (!diff)
			continue;

		retval = io_channel_read_blk(fs->io, old,
					     fs->inode_blocks_per_group,
					     rfs->itable_buf);
		if (retval) 
			goto backout;
		/*
		 * The end of the inode table segment often contains
		 * all zeros.  Find out if we have several blocks of
		 * zeros so we can optimize the write.
		 */
		for (cp = rfs->itable_buf+size, n=0; n < size; n++, cp--)
			if (*cp)
				break;
		n = n >> EXT2_BLOCK_SIZE_BITS(fs->super);
		if (rfs->flags & RESIZE_DEBUG_ITABLEMOVE) 
			printf("%d blocks of zeros...\n", n);
		num = fs->inode_blocks_per_group;
		if (n > diff)
			num -= n;

		retval = io_channel_write_blk(fs->io, new,
					      num, rfs->itable_buf);
		if (retval) {
			io_channel_write_blk(fs->io, old,
					     num, rfs->itable_buf);
			goto backout;
		}
		if (n > diff) {
			retval = io_channel_write_blk(fs->io,
			      old + fs->inode_blocks_per_group,
			      diff, rfs->itable_buf - fs->blocksize * diff);
			if (retval)
				goto backout;
		}
		io_channel_flush(fs->io);
		if (progress)
			ext2fs_progress_update(progress, ++moved);
	}
	ext2fs_flush(rfs->new_fs);
	io_channel_flush(fs->io);
	if (rfs->flags & RESIZE_DEBUG_ITABLEMOVE) 
		printf("Inode table move finished.\n");
	if (progress)
		ext2fs_progress_close(progress);
	return 0;
	
backout:
	if (progress)
		ext2fs_progress_close(progress);
	if (rfs->flags & RESIZE_DEBUG_ITABLEMOVE) 
		printf("Error: %s; now backing out!\n", error_message(retval));
	while (--i >= 0) {
		if (rfs->flags & RESIZE_DEBUG_ITABLEMOVE) 
			printf("Group %d block %u->%u\n", i, new, old);
		old = rfs->old_fs->group_desc[i].bg_inode_table;
		new = fs->group_desc[i].bg_inode_table;
		
		err = io_channel_read_blk(fs->io, new,
					  fs->inode_blocks_per_group,
					  rfs->itable_buf);
		if (err)
			continue;
		err = io_channel_write_blk(fs->io, old,
					   fs->inode_blocks_per_group,
					   rfs->itable_buf);
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
errcode_t resize_fs(ext2_filsys fs, blk_t new_size, int flags)
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
	rfs->flags = flags;
	rfs->itable_buf	 = 0;
	retval = ext2fs_dup_handle(fs, &rfs->new_fs);
	if (retval)
		goto errout;

	retval = adjust_superblock(rfs, new_size);
	if (retval)
		goto errout;

	retval = blocks_to_move(rfs);
	if (retval)
		goto errout;

	if (rfs->flags & RESIZE_DEBUG_BMOVE)
		printf("Number of free blocks: %d/%d, Needed: %d\n",
		       rfs->old_fs->super->s_free_blocks_count,
		       rfs->new_fs->super->s_free_blocks_count,
		       rfs->needed_blocks);
	
	retval = ext2fs_block_move(rfs);
	if (retval)
		goto errout;

	retval = ext2fs_inode_move(rfs);
	if (retval)
		goto errout;

	retval = move_itables(rfs);
	if (retval)
		goto errout;

	retval = ext2fs_calculate_summary_stats(rfs->new_fs);
	if (retval)
		goto errout;
	
	retval = ext2fs_close(rfs->new_fs);
	if (retval)
		goto errout;

	rfs->flags = flags;
	
	ext2fs_free(rfs->old_fs);
	if (rfs->itable_buf)
		free(rfs->itable_buf);
	free(rfs);
	
	return 0;

errout:
	if (rfs->new_fs)
		ext2fs_free(rfs->new_fs);
	if (rfs->itable_buf)
		free(rfs->itable_buf);
	free(rfs);
	return retval;
}
