/*
 * ext2_block_move.c --- ext2resizer block mover
 *
 * Copyright (C) 1997 Theodore Ts'o
 * 
 * %Begin-Header%
 * All rights reserved.
 * %End-Header%
 */

#include "resize2fs.h"

struct process_block_struct {
	ino_t			ino;
	struct ext2_inode *	inode;
	ext2_extent		bmap;
	errcode_t		error;
	int			is_dir;
	int			flags;
};

static int process_block(ext2_filsys fs, blk_t	*block_nr,
			 int blockcnt, blk_t ref_block,
			 int ref_offset, void *private)
{
	struct process_block_struct *pb = private;
	errcode_t	retval;
	blk_t		block, new;
	int		ret = 0;

	block = *block_nr;

	new = ext2fs_extent_translate(pb->bmap, block);
	if (new) {
		*block_nr = new;
		ret |= BLOCK_CHANGED;
		if (pb->flags & RESIZE_DEBUG_BMOVE)
			printf("ino=%ld, blockcnt=%d, %u->%u\n", pb->ino,
			       blockcnt, block, new);
	}

	if (pb->is_dir) {
		retval = ext2fs_add_dir_block(fs->dblist, pb->ino,
					      *block_nr, blockcnt);
		if (retval) {
			pb->error = retval;
			ret |= BLOCK_ABORT;
		}
	}

	return ret;
}

errcode_t ext2fs_block_move(ext2_resize_t rfs)
{
	ext2_extent		bmap;
	blk_t			blk, old, new;
	ext2_filsys		fs = rfs->new_fs;
	ext2_filsys		old_fs = rfs->old_fs;
	ino_t			ino;
	struct ext2_inode 	inode;
	errcode_t		retval;
	struct process_block_struct pb;
	ext2_inode_scan		scan = 0;
	char			*block_buf;
	int			size, c;
	int			to_move, moved;
	ext2_sim_progmeter progress = 0;

	new = fs->super->s_first_data_block;
	if (!rfs->itable_buf) {
		rfs->itable_buf = malloc(fs->blocksize *
					 fs->inode_blocks_per_group);
		if (!rfs->itable_buf)
			return ENOMEM;
	}
	retval = ext2fs_create_extent_table(&bmap, 0);
	if (retval)
		return retval;

	/*
	 * The first step is to figure out where all of the blocks
	 * will go.
	 */
	to_move = moved = 0;
	for (blk = old_fs->super->s_first_data_block;
	     blk < old_fs->super->s_blocks_count; blk++) {
		if (!ext2fs_test_block_bitmap(old_fs->block_map, blk))
			continue;
		if (!ext2fs_test_block_bitmap(rfs->move_blocks, blk))
			continue;

		while (1) {
			if (new >= fs->super->s_blocks_count) {
				retval = ENOSPC;
				goto errout;
			}
			if (!ext2fs_test_block_bitmap(fs->block_map, new) &&
			    !ext2fs_test_block_bitmap(rfs->reserve_blocks,
						      new))
				break;
			new++;
		}
		ext2fs_mark_block_bitmap(fs->block_map, new);
		ext2fs_add_extent_entry(bmap, blk, new);
		to_move++;
	}
	if (to_move == 0)
		return 0;
	/*
	 * Step two is to actually move the blocks
	 */
	retval =  ext2fs_iterate_extent(bmap, 0, 0, 0);
	if (retval) goto errout;

	if (rfs->flags & RESIZE_PERCENT_COMPLETE) {
		retval = ext2fs_progress_init(&progress,
		      "Relocating blocks", 30, 40, to_move, 0);
		if (retval)
			return retval;
	}
	
	while (1) {
		retval = ext2fs_iterate_extent(bmap, &old, &new, &size);
		if (retval) goto errout;
		if (!size)
			break;
		if (rfs->flags & RESIZE_DEBUG_BMOVE)
			printf("Moving %d blocks %u->%u\n", size,
			       old, new);
		do {
			c = size;
			if (c > fs->inode_blocks_per_group)
				c = fs->inode_blocks_per_group;
			retval = io_channel_read_blk(fs->io, old, c,
						     rfs->itable_buf);
			if (retval) goto errout;
			retval = io_channel_write_blk(fs->io, new, c,
						      rfs->itable_buf);
			if (retval) goto errout;
			size -= c;
			new += c;
			old += c;
			moved += c;
			io_channel_flush(fs->io);
			if (progress)
				ext2fs_progress_update(progress, moved);
		} while (size > 0);
		io_channel_flush(fs->io);
	}
	if (progress) {
		ext2fs_progress_close(progress);
		progress = 0;
	}
	
	/*
	 * Step 3 is where we update the block pointers
	 */
	retval = ext2fs_open_inode_scan(old_fs, 0, &scan);
	if (retval) goto errout;

	pb.error = 0;
	pb.bmap = bmap;	
	pb.flags = rfs->flags;

	block_buf = malloc(old_fs->blocksize * 3);
	if (!block_buf) {
		retval = ENOMEM;
		goto errout;
	}

	/*
	 * We're going to initialize the dblist while we're at it.
	 */
	if (old_fs->dblist) {
		ext2fs_free_dblist(old_fs->dblist);
		old_fs->dblist = NULL;
	}
	retval = ext2fs_init_dblist(old_fs, 0);
	if (retval)
		return retval;

	retval = ext2fs_get_next_inode(scan, &ino, &inode);
	if (retval) goto errout;
	
	if (rfs->flags & RESIZE_PERCENT_COMPLETE) {
		retval = ext2fs_progress_init(&progress,
		      "Updating block references", 30, 40,
		      old_fs->super->s_inodes_count, 0);
		if (retval)
			return retval;
	}
	
	while (ino) {
		if ((inode.i_links_count == 0) ||
		    !ext2fs_inode_has_valid_blocks(&inode))
			goto next;
		
		pb.ino = ino;
		pb.inode = &inode;

		pb.is_dir = LINUX_S_ISDIR(inode.i_mode);
		
		retval = ext2fs_block_iterate2(old_fs, ino, 0, block_buf,
					      process_block, &pb);
		if (retval)
			goto errout;
		if (pb.error) {
			retval = pb.error;
			goto errout;
		}

	next:
		if (progress)
			ext2fs_progress_update(progress, ino);
		retval = ext2fs_get_next_inode(scan, &ino, &inode);
		if (retval == EXT2_ET_BAD_BLOCK_IN_INODE_TABLE)
			goto next;
	}
	retval = 0;
errout:
	if (progress)
		ext2fs_progress_close(progress);
	
	ext2fs_free_extent_table(bmap);
	if (scan)
		ext2fs_close_inode_scan(scan);
	return retval;
}

