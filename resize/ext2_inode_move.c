/*
 * ext2_inode_move.c --- ext2resizer inode mover
 *
 * Copyright (C) 1997 Theodore Ts'o
 * 
 * %Begin-Header%
 * All rights reserved.
 * %End-Header%
 */

#include "resize2fs.h"

/*
 * Progress callback
 */
struct callback_info {
	ext2_sim_progmeter progress;
	int	offset;
};
		
static errcode_t progress_callback(ext2_filsys fs, ext2_inode_scan scan,
				   dgrp_t group, void * priv_data)
{
	struct callback_info *cb = (struct callback_info *) priv_data;

	if (!cb->progress)
		return 0;

	ext2fs_progress_update(cb->progress, group - cb->offset + 1);
	return 0;
}


struct istruct {
	ext2_sim_progmeter progress;
	ext2_extent	imap;
	int		flags;
	int		num;
};

static int check_and_change_inodes(ino_t dir, int entry,
				   struct ext2_dir_entry *dirent, int offset,
				   int	blocksize, char *buf, void *priv_data)
{
	struct istruct *is = (struct istruct *) priv_data;
	ino_t	new_inode;

	if (is->progress && offset == 0) {
		ext2fs_progress_update(is->progress, ++is->num);
	}

	if (!dirent->inode)
		return 0;

	new_inode = ext2fs_extent_translate(is->imap, dirent->inode);

	if (!new_inode)
		return 0;
#ifdef RESIZE2FS_DEBUG
	if (is->flags & RESIZE_DEBUG_INODEMAP)
		printf("Inode translate (dir=%ld, name=%.*s, %u->%ld)\n",
		       dir, dirent->name_len, dirent->name,
		       dirent->inode, new_inode);
#endif

	dirent->inode = new_inode;

	return DIRENT_CHANGED;
}

/*
 * Function to obtain the dblist information (if we didn't get it
 * earlier)
 */
struct process_block_struct {
	ino_t			ino;
	struct ext2_inode *	inode;
	errcode_t		error;
};

static int process_block(ext2_filsys fs, blk_t	*block_nr,
			 int blockcnt, blk_t ref_block,
			 int ref_offset, void *priv_data)
{
	struct process_block_struct *pb;
	errcode_t	retval;
	int		ret = 0;

	pb = (struct process_block_struct *) priv_data;
	retval = ext2fs_add_dir_block(fs->dblist, pb->ino,
				      *block_nr, blockcnt);
	if (retval) {
		pb->error = retval;
		ret |= BLOCK_ABORT;
	}
	return ret;
}

static errcode_t get_dblist(ext2_filsys fs, int flags)
{
	ext2_inode_scan		scan = 0;
	errcode_t		retval;
	char			*block_buf = 0;
	struct process_block_struct	pb;
	ext2_sim_progmeter 	progress = 0; 
	ino_t			ino;
	struct ext2_inode 	inode;

	retval = ext2fs_open_inode_scan(fs, 0, &scan);
	if (retval) goto errout;

	pb.error = 0;

	retval = ext2fs_get_mem(fs->blocksize * 3, (void **) &block_buf);
	if (retval)
		goto errout;

	/*
	 * We're going to initialize the dblist while we're at it.
	 */
	if (fs->dblist) {
		ext2fs_free_dblist(fs->dblist);
		fs->dblist = NULL;
	}
	retval = ext2fs_init_dblist(fs, 0);
	if (retval)
		return retval;

	retval = ext2fs_get_next_inode(scan, &ino, &inode);
	if (retval) goto errout;
	
	if (flags & RESIZE_PERCENT_COMPLETE) {
		retval = ext2fs_progress_init(&progress,
		      "Finding directories", 30, 40,
		      fs->super->s_inodes_count, 0);
		if (retval)
			return retval;
	}
	
	while (ino) {
		if ((inode.i_links_count == 0) ||
		    !ext2fs_inode_has_valid_blocks(&inode) ||
		    !LINUX_S_ISDIR(inode.i_mode))
			goto next;
		
		pb.ino = ino;
		pb.inode = &inode;

		retval = ext2fs_block_iterate2(fs, ino, 0, block_buf,
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
	if (scan)
		ext2fs_close_inode_scan(scan);
	if (block_buf)
		ext2fs_free_mem((void **) &block_buf);
	return retval;
}


errcode_t ext2fs_inode_move(ext2_resize_t rfs)
{
	ino_t			ino, new_inode;
	struct ext2_inode 	inode;
	ext2_inode_scan 	scan = NULL;
	ext2_extent		imap;
	errcode_t		retval;
	int			group;
	struct istruct 		is;
	struct callback_info	callback_info;
	ext2_sim_progmeter 	progress = 0; 

	if (rfs->old_fs->group_desc_count <=
	    rfs->new_fs->group_desc_count)
		return 0;

	retval = ext2fs_create_extent_table(&imap, 0);
	if (retval)
		return retval;

	retval = ext2fs_open_inode_scan(rfs->old_fs, 0, &scan);
	if (retval) goto errout;

	retval = ext2fs_inode_scan_goto_blockgroup(scan,
				   rfs->new_fs->group_desc_count);
	if (retval) goto errout;

	
	if (rfs->flags & RESIZE_PERCENT_COMPLETE) {
		callback_info.offset = rfs->new_fs->group_desc_count;
	
		group = (rfs->old_fs->group_desc_count -
			 rfs->new_fs->group_desc_count);
	
		retval = ext2fs_progress_init(&progress,
		      "Moving inodes", 30, 40, group, 0);
		if (retval)
			return retval;
		ext2fs_set_inode_callback(scan, progress_callback,
					  &callback_info);
	}
	callback_info.progress = progress;

	new_inode = EXT2_FIRST_INODE(rfs->new_fs->super);
	/*
	 * First, copy all of the inodes that need to be moved
	 * elsewhere in the inode table
	 */
	while (1) {
		retval = ext2fs_get_next_inode(scan, &ino, &inode);
		if (retval) goto errout;

		if (!ino)
			break;
		
		if (!ext2fs_test_inode_bitmap(rfs->old_fs->inode_map, ino)) 
			continue;

		/*
		 * Find a new inode
		 */
		while (1) { 
			if (!ext2fs_test_inode_bitmap(rfs->new_fs->inode_map, 
						      new_inode))
				break;
			new_inode++;
			if (new_inode > rfs->new_fs->super->s_inodes_count) {
				retval = ENOSPC;
				goto errout;
			}
		}
		ext2fs_mark_inode_bitmap(rfs->new_fs->inode_map, new_inode);
		retval = ext2fs_write_inode(rfs->old_fs, new_inode, &inode);
		if (retval) goto errout;

		group = (new_inode-1) / EXT2_INODES_PER_GROUP(rfs->new_fs->super);
		if (LINUX_S_ISDIR(inode.i_mode))
			rfs->new_fs->group_desc[group].bg_used_dirs_count++;
		
#ifdef RESIZE2FS_DEBUG
		if (rfs->flags & RESIZE_DEBUG_INODEMAP)
			printf("Inode moved %ld->%ld\n", ino, new_inode);
#endif

		ext2fs_add_extent_entry(imap, ino, new_inode);
	}
	io_channel_flush(rfs->new_fs->io);
	if (progress) {
		ext2fs_progress_close(progress);
		progress = 0;
	}
	/*
	 * Get the list of directory blocks, if necessary
	 */
	if (!rfs->old_fs->dblist) {
		retval = get_dblist(rfs->old_fs, rfs->flags);
		if (retval) goto errout;
	}
	/*
	 * Now, we iterate over all of the directories to update the
	 * inode references
	 */
	if (rfs->flags & RESIZE_PERCENT_COMPLETE) {
		retval = ext2fs_progress_init(&progress,
		      "Updating inode references", 30, 40,
		      ext2fs_dblist_count(rfs->old_fs->dblist), 0);
		if (retval)
			return retval;
	}
	is.imap = imap;
	is.flags = rfs->flags;
	is.num = 0;
	is.progress = progress;

	retval = ext2fs_dblist_dir_iterate(rfs->old_fs->dblist,
					   DIRENT_FLAG_INCLUDE_EMPTY, 0,
					   check_and_change_inodes, &is);
	/* if (retval) goto errout; */

errout:
	if (progress)
		ext2fs_progress_close(progress);
	ext2fs_free_extent_table(imap);
	if (scan)
		ext2fs_close_inode_scan(scan);
	return retval;
}

