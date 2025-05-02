/*
 * pass3.c -- pass #3 of e2fsck: Check for directory connectivity
 *
 * Copyright (C) 1993, 1994, 1995, 1996, 1997, 1998, 1999 Theodore Ts'o.
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 *
 * Pass #3 assures that all directories are connected to the
 * filesystem tree, using the following algorithm:
 *
 * First, the root directory is checked to make sure it exists; if
 * not, e2fsck will offer to create a new one.  It is then marked as
 * "done".
 *
 * Then, pass3 iterates over all directory inodes; for each directory
 * it attempts to trace up the filesystem tree, using dirinfo.parent
 * until it reaches a directory which has been marked "done".  If it
 * can not do so, then the directory must be disconnected, and e2fsck
 * will offer to reconnect it to /lost+found.  While it is chasing
 * parent pointers up the filesystem tree, if pass3 sees a directory
 * twice, then it has detected a filesystem loop, and it will again
 * offer to reconnect the directory to /lost+found in order to break the
 * filesystem loop.
 *
 * Pass 3 also contains the subroutine, e2fsck_reconnect_file() to
 * reconnect inodes to /lost+found; this subroutine is also used by
 * pass 4.  e2fsck_reconnect_file() calls get_lost_and_found(), which
 * is responsible for creating /lost+found if it does not exist.
 *
 * Pass 3 frees the following data structures:
 *     	- The dirinfo directory information cache.
 */

#include "config.h"
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif

#include "e2fsck.h"
#include "problem.h"

static void check_root(e2fsck_t ctx);
static int check_directory(e2fsck_t ctx, ext2_ino_t ino,
			   struct problem_context *pctx);
static void fix_dotdot(e2fsck_t ctx, ext2_ino_t ino, ext2_ino_t parent);

static ext2fs_inode_bitmap inode_loop_detect = 0;
static ext2fs_inode_bitmap inode_done_map = 0;

void e2fsck_pass3(e2fsck_t ctx)
{
	ext2_filsys fs = ctx->fs;
	struct dir_info_iter *iter = NULL;
#ifdef RESOURCE_TRACK
	struct resource_track	rtrack;
#endif
	struct problem_context	pctx;
	struct dir_info	*dir;
	unsigned long maxdirs, count;

	init_resource_track(&rtrack, ctx->fs->io);
	clear_problem_context(&pctx);

#ifdef MTRACE
	mtrace_print("Pass 3");
#endif

	if (!(ctx->options & E2F_OPT_PREEN))
		fix_problem(ctx, PR_3_PASS_HEADER, &pctx);

	/*
	 * Allocate some bitmaps to do loop detection.
	 */
	pctx.errcode = e2fsck_allocate_inode_bitmap(fs, _("inode done bitmap"),
					EXT2FS_BMAP64_AUTODIR,
					"inode_done_map", &inode_done_map);
	if (pctx.errcode) {
		pctx.num = 2;
		fix_problem(ctx, PR_3_ALLOCATE_IBITMAP_ERROR, &pctx);
		ctx->flags |= E2F_FLAG_ABORT;
		goto abort_exit;
	}
	print_resource_track(ctx, _("Peak memory"), &ctx->global_rtrack, NULL);

	check_root(ctx);
	if (ctx->flags & E2F_FLAG_SIGNAL_MASK)
		goto abort_exit;

	ext2fs_mark_inode_bitmap2(inode_done_map, EXT2_ROOT_INO);

	maxdirs = e2fsck_get_num_dirinfo(ctx);
	count = 1;

	if (ctx->progress)
		if ((ctx->progress)(ctx, 3, 0, maxdirs))
			goto abort_exit;

	iter = e2fsck_dir_info_iter_begin(ctx);
	while ((dir = e2fsck_dir_info_iter(ctx, iter)) != 0) {
		if (ctx->flags & E2F_FLAG_SIGNAL_MASK ||
		    ctx->flags & E2F_FLAG_RESTART)
			goto abort_exit;
		if (ctx->progress && (ctx->progress)(ctx, 3, count++, maxdirs))
			goto abort_exit;
		if (ext2fs_test_inode_bitmap2(ctx->inode_dir_map, dir->ino))
			if (check_directory(ctx, dir->ino, &pctx))
				goto abort_exit;
	}

	/*
	 * Force the creation of /lost+found if not present
	 */
	if ((ctx->options & E2F_OPT_READONLY) == 0)
		e2fsck_get_lost_and_found(ctx, 1);

	/*
	 * If there are any directories that need to be indexed or
	 * optimized, do it here.
	 */
	e2fsck_rehash_directories(ctx);

abort_exit:
	if (iter)
		e2fsck_dir_info_iter_end(ctx, iter);
	e2fsck_free_dir_info(ctx);
	if (inode_loop_detect) {
		ext2fs_free_inode_bitmap(inode_loop_detect);
		inode_loop_detect = 0;
	}
	if (inode_done_map) {
		ext2fs_free_inode_bitmap(inode_done_map);
		inode_done_map = 0;
	}

	if (ctx->lnf_repair_block) {
		ext2fs_unmark_block_bitmap2(ctx->block_found_map,
					    ctx->lnf_repair_block);
		ctx->lnf_repair_block = 0;
	}
	if (ctx->root_repair_block) {
		ext2fs_unmark_block_bitmap2(ctx->block_found_map,
					    ctx->root_repair_block);
		ctx->root_repair_block = 0;
	}

	print_resource_track(ctx, _("Pass 3"), &rtrack, ctx->fs->io);
}

/*
 * This makes sure the root inode is present; if not, we ask if the
 * user wants us to create it.  Not creating it is a fatal error.
 */
static void check_root(e2fsck_t ctx)
{
	ext2_filsys fs = ctx->fs;
	blk64_t			blk;
	struct ext2_inode_large	inode;
	struct ext2_inode	*iptr = (struct ext2_inode *) &inode;
	char *			block;
	struct problem_context	pctx;

	clear_problem_context(&pctx);

	if (ext2fs_test_inode_bitmap2(ctx->inode_used_map, EXT2_ROOT_INO)) {
		/*
		 * If the root inode is not a directory, die here.  The
		 * user must have answered 'no' in pass1 when we
		 * offered to clear it.
		 */
		if (!(ext2fs_test_inode_bitmap2(ctx->inode_dir_map,
					       EXT2_ROOT_INO))) {
			fix_problem(ctx, PR_3_ROOT_NOT_DIR_ABORT, &pctx);
			ctx->flags |= E2F_FLAG_ABORT;
		}
		return;
	}

	if (!fix_problem(ctx, PR_3_NO_ROOT_INODE, &pctx)) {
		fix_problem(ctx, PR_3_NO_ROOT_INODE_ABORT, &pctx);
		ctx->flags |= E2F_FLAG_ABORT;
		return;
	}

	e2fsck_read_bitmaps(ctx);

	/*
	 * First, find a free block
	 */
	if (ctx->root_repair_block) {
		blk = ctx->root_repair_block;
		ctx->root_repair_block = 0;
		goto skip_new_block;
	}
	pctx.errcode = ext2fs_new_block2(fs, 0, ctx->block_found_map, &blk);
	if (pctx.errcode) {
		pctx.str = "ext2fs_new_block";
		fix_problem(ctx, PR_3_CREATE_ROOT_ERROR, &pctx);
		ctx->flags |= E2F_FLAG_ABORT;
		return;
	}
	ext2fs_mark_block_bitmap2(ctx->block_found_map, blk);
skip_new_block:
	ext2fs_mark_block_bitmap2(fs->block_map, blk);
	ext2fs_mark_bb_dirty(fs);

	/*
	 * Set up the inode structure
	 */
	memset(&inode, 0, sizeof(inode));
	inode.i_mode = 040755;
	inode.i_size = fs->blocksize;
	ext2fs_inode_xtime_set(&inode, i_atime, ctx->now);
	ext2fs_inode_xtime_set(&inode, i_ctime, ctx->now);
	ext2fs_inode_xtime_set(&inode, i_mtime, ctx->now);
	inode.i_links_count = 2;
	ext2fs_iblk_set(fs, iptr, 1);
	inode.i_block[0] = blk;
	inode.i_extra_isize = sizeof(struct ext2_inode_large) -
		EXT2_GOOD_OLD_INODE_SIZE;

	/*
	 * Write out the inode.
	 */
	pctx.errcode = ext2fs_write_new_inode(fs, EXT2_ROOT_INO, iptr);
	if (pctx.errcode) {
		pctx.str = "ext2fs_write_inode";
		fix_problem(ctx, PR_3_CREATE_ROOT_ERROR, &pctx);
		ctx->flags |= E2F_FLAG_ABORT;
		return;
	}

	/*
	 * Now let's create the actual data block for the inode.
	 * Due to metadata_csum, we must write the dir blocks AFTER
	 * the inode has been written to disk!
	 */
	pctx.errcode = ext2fs_new_dir_block(fs, EXT2_ROOT_INO, EXT2_ROOT_INO,
					    &block);
	if (pctx.errcode) {
		pctx.str = "ext2fs_new_dir_block";
		fix_problem(ctx, PR_3_CREATE_ROOT_ERROR, &pctx);
		ctx->flags |= E2F_FLAG_ABORT;
		return;
	}

	pctx.errcode = ext2fs_write_dir_block4(fs, blk, block, 0,
					       EXT2_ROOT_INO);
	ext2fs_free_mem(&block);
	if (pctx.errcode) {
		pctx.str = "ext2fs_write_dir_block4";
		fix_problem(ctx, PR_3_CREATE_ROOT_ERROR, &pctx);
		ctx->flags |= E2F_FLAG_ABORT;
		return;
	}

	/*
	 * Miscellaneous bookkeeping...
	 */
	e2fsck_add_dir_info(ctx, EXT2_ROOT_INO, EXT2_ROOT_INO);
	ext2fs_icount_store(ctx->inode_count, EXT2_ROOT_INO, 2);
	ext2fs_icount_store(ctx->inode_link_info, EXT2_ROOT_INO, 2);

	ext2fs_mark_inode_bitmap2(ctx->inode_used_map, EXT2_ROOT_INO);
	ext2fs_mark_inode_bitmap2(ctx->inode_dir_map, EXT2_ROOT_INO);
	ext2fs_mark_inode_bitmap2(fs->inode_map, EXT2_ROOT_INO);
	ext2fs_mark_ib_dirty(fs);
	quota_data_add(ctx->qctx, &inode, EXT2_ROOT_INO,
		       EXT2_CLUSTER_SIZE(fs->super));
	quota_data_inodes(ctx->qctx, &inode, EXT2_ROOT_INO, +1);
}

/*
 * This subroutine is responsible for making sure that a particular
 * directory is connected to the root; if it isn't we trace it up as
 * far as we can go, and then offer to connect the resulting parent to
 * the lost+found.  We have to do loop detection; if we ever discover
 * a loop, we treat that as a disconnected directory and offer to
 * reparent it to lost+found.
 *
 * However, loop detection is expensive, because for very large
 * filesystems, the inode_loop_detect bitmap is huge, and clearing it
 * is non-trivial.  Loops in filesystems are also a rare error case,
 * and we shouldn't optimize for error cases.  So we try two passes of
 * the algorithm.  The first time, we ignore loop detection and merely
 * increment a counter; if the counter exceeds some extreme threshold,
 * then we try again with the loop detection bitmap enabled.
 */
static int check_directory(e2fsck_t ctx, ext2_ino_t dir,
			   struct problem_context *pctx)
{
	ext2_filsys 	fs = ctx->fs;
	ext2_ino_t	ino = dir, parent;
	int		loop_pass = 0, parent_count = 0;

	while (1) {
		/*
		 * Mark this inode as being "done"; by the time we
		 * return from this function, the inode we either be
		 * verified as being connected to the directory tree,
		 * or we will have offered to reconnect this to
		 * lost+found.
		 *
		 * If it was marked done already, then we've reached a
		 * parent we've already checked.
		 */
		if (ext2fs_mark_inode_bitmap2(inode_done_map, ino))
			break;

		if (e2fsck_dir_info_get_parent(ctx, ino, &parent)) {
			fix_problem(ctx, PR_3_NO_DIRINFO, pctx);
			return 0;
		}

		/*
		 * If this directory doesn't have a parent, or we've
		 * seen the parent once already, then offer to
		 * reparent it to lost+found
		 */
		if (!parent ||
		    (loop_pass &&
		     ext2fs_test_inode_bitmap2(inode_loop_detect, parent))) {
			pctx->ino = ino;
			if (parent)
				pctx->dir = parent;
			else
				(void) ext2fs_lookup(fs, ino, "..", 2, NULL,
						     &pctx->dir);
			if (fix_problem(ctx, !parent ? PR_3_UNCONNECTED_DIR :
						       PR_3_LOOPED_DIR, pctx)) {
				if (e2fsck_reconnect_file(ctx, pctx->ino)) {
					ext2fs_unmark_valid(fs);
				} else {
					fix_dotdot(ctx, pctx->ino,
						   ctx->lost_and_found);
					parent = ctx->lost_and_found;
				}
			}
			break;
		}
		ino = parent;
		if (loop_pass) {
			ext2fs_mark_inode_bitmap2(inode_loop_detect, ino);
		} else if (parent_count++ > 2048) {
			/*
			 * If we've run into a path depth that's
			 * greater than 2048, try again with the inode
			 * loop bitmap turned on and start from the
			 * top.
			 */
			loop_pass = 1;
			if (inode_loop_detect)
				ext2fs_clear_inode_bitmap(inode_loop_detect);
			else {
				pctx->errcode = e2fsck_allocate_inode_bitmap(fs, _("inode loop detection bitmap"), EXT2FS_BMAP64_AUTODIR, "inode_loop_detect", &inode_loop_detect);
				if (pctx->errcode) {
					pctx->num = 1;
					fix_problem(ctx,
				    PR_3_ALLOCATE_IBITMAP_ERROR, pctx);
					ctx->flags |= E2F_FLAG_ABORT;
					return -1;
				}
			}
			ino = dir;
		}
	}

	/*
	 * Make sure that .. and the parent directory are the same;
	 * offer to fix it if not.
	 */
	pctx->ino = dir;
	if (e2fsck_dir_info_get_dotdot(ctx, dir, &pctx->ino2) ||
	    e2fsck_dir_info_get_parent(ctx, dir, &pctx->dir)) {
		fix_problem(ctx, PR_3_NO_DIRINFO, pctx);
		return 0;
	}
	if (pctx->ino2 != pctx->dir) {
		if (fix_problem(ctx, PR_3_BAD_DOT_DOT, pctx))
			fix_dotdot(ctx, dir, pctx->dir);
	}
	return 0;
}

/*
 * This routine gets the lost_and_found inode, making it a directory
 * if necessary
 */
ext2_ino_t e2fsck_get_lost_and_found(e2fsck_t ctx, int fix)
{
	ext2_filsys fs = ctx->fs;
	ext2_ino_t			ino;
	blk64_t			blk;
	errcode_t		retval;
	struct ext2_inode_large	inode;
	char *			block;
	static const char	name[] = "lost+found";
	struct 	problem_context	pctx;
	int			will_rehash, flags;

	if (ctx->lost_and_found)
		return ctx->lost_and_found;

	clear_problem_context(&pctx);

	will_rehash = e2fsck_dir_will_be_rehashed(ctx, EXT2_ROOT_INO);
	if (will_rehash) {
		flags = ctx->fs->flags;
		ctx->fs->flags |= EXT2_FLAG_IGNORE_CSUM_ERRORS;
	}
	retval = ext2fs_lookup(fs, EXT2_ROOT_INO, name,
			       sizeof(name)-1, 0, &ino);
	if (will_rehash)
		ctx->fs->flags = (flags & EXT2_FLAG_IGNORE_CSUM_ERRORS) |
			(ctx->fs->flags & ~EXT2_FLAG_IGNORE_CSUM_ERRORS);
	if (retval && !fix)
		return 0;
	if (!retval) {
		/* Lost+found shouldn't have inline data */
		retval = ext2fs_read_inode_full(fs, ino, EXT2_INODE(&inode),
						sizeof(inode));
		if (fix && retval)
			return 0;

		if (fix && (inode.i_flags & EXT4_INLINE_DATA_FL)) {
			if (!fix_problem(ctx, PR_3_LPF_INLINE_DATA, &pctx))
				return 0;
			goto unlink;
		}

		if (fix && (inode.i_flags & EXT4_ENCRYPT_FL)) {
			if (!fix_problem(ctx, PR_3_LPF_ENCRYPTED, &pctx))
				return 0;
			goto unlink;
		}

		if (ext2fs_check_directory(fs, ino) == 0) {
			ctx->lost_and_found = ino;
			return ino;
		}

		/* Lost+found isn't a directory! */
		if (!fix)
			return 0;
		pctx.ino = ino;
		if (!fix_problem(ctx, PR_3_LPF_NOTDIR, &pctx))
			return 0;

unlink:
		/* OK, unlink the old /lost+found file. */
		pctx.errcode = ext2fs_unlink(fs, EXT2_ROOT_INO, name, ino, 0);
		if (pctx.errcode) {
			pctx.str = "ext2fs_unlink";
			fix_problem(ctx, PR_3_CREATE_LPF_ERROR, &pctx);
			return 0;
		}
		(void) e2fsck_dir_info_set_parent(ctx, ino, 0);
		e2fsck_adjust_inode_count(ctx, ino, -1);
		/*
		 * If the old lost+found was a directory, we've just
		 * disconnected it from the directory tree, which
		 * means we need to restart the directory tree scan.
		 * The simplest way to do this is restart the whole
		 * e2fsck operation.
		 */
		if (LINUX_S_ISDIR(inode.i_mode))
			ctx->flags |= E2F_FLAG_RESTART;
	} else if (retval != EXT2_ET_FILE_NOT_FOUND) {
		pctx.errcode = retval;
		fix_problem(ctx, PR_3_ERR_FIND_LPF, &pctx);
	}
	if (!fix_problem(ctx, PR_3_NO_LF_DIR, 0))
		return 0;

	/*
	 * Read the inode and block bitmaps in; we'll be messing with
	 * them.
	 */
	e2fsck_read_bitmaps(ctx);

	/*
	 * First, find a free block
	 */
	if (ctx->lnf_repair_block) {
		blk = ctx->lnf_repair_block;
		ctx->lnf_repair_block = 0;
		goto skip_new_block;
	}
	retval = ext2fs_new_block2(fs, 0, ctx->block_found_map, &blk);
	if (retval == EXT2_ET_BLOCK_ALLOC_FAIL &&
	    fix_problem(ctx, PR_3_LPF_NO_SPACE, &pctx)) {
		fix_problem(ctx, PR_3_NO_SPACE_TO_RECOVER, &pctx);
		ctx->lost_and_found = EXT2_ROOT_INO;
		return 0;
	}
	if (retval) {
		pctx.errcode = retval;
		fix_problem(ctx, PR_3_ERR_LPF_NEW_BLOCK, &pctx);
		return 0;
	}
	ext2fs_mark_block_bitmap2(ctx->block_found_map, blk);
skip_new_block:
	ext2fs_block_alloc_stats2(fs, blk, +1);

	/*
	 * Next find a free inode.
	 */
	retval = ext2fs_new_inode(fs, EXT2_ROOT_INO, 040700,
				  ctx->inode_used_map, &ino);
	if (retval == EXT2_ET_INODE_ALLOC_FAIL &&
	    fix_problem(ctx, PR_3_LPF_NO_SPACE, &pctx)) {
		fix_problem(ctx, PR_3_NO_SPACE_TO_RECOVER, &pctx);
		ctx->lost_and_found = EXT2_ROOT_INO;
		return 0;
	}
	if (retval) {
		pctx.errcode = retval;
		fix_problem(ctx, PR_3_ERR_LPF_NEW_INODE, &pctx);
		return 0;
	}
	ext2fs_mark_inode_bitmap2(ctx->inode_used_map, ino);
	ext2fs_mark_inode_bitmap2(ctx->inode_dir_map, ino);
	ext2fs_inode_alloc_stats2(fs, ino, +1, 1);

	/*
	 * Set up the inode structure
	 */
	memset(&inode, 0, sizeof(inode));
	inode.i_mode = 040700;
	inode.i_size = fs->blocksize;
	ext2fs_inode_xtime_set(&inode, i_atime, ctx->now);
	ext2fs_inode_xtime_set(&inode, i_ctime, ctx->now);
	ext2fs_inode_xtime_set(&inode, i_mtime, ctx->now);
	inode.i_links_count = 2;
	ext2fs_iblk_set(fs, EXT2_INODE(&inode), 1);
	inode.i_block[0] = blk;

	/*
	 * Next, write out the inode.
	 */
	pctx.errcode = ext2fs_write_new_inode(fs, ino, EXT2_INODE(&inode));
	if (pctx.errcode) {
		pctx.str = "ext2fs_write_inode";
		fix_problem(ctx, PR_3_CREATE_LPF_ERROR, &pctx);
		return 0;
	}

	/*
	 * Now let's create the actual data block for the inode.
	 * Due to metadata_csum, the directory block MUST be written
	 * after the inode is written to disk!
	 */
	retval = ext2fs_new_dir_block(fs, ino, EXT2_ROOT_INO, &block);
	if (retval) {
		pctx.errcode = retval;
		fix_problem(ctx, PR_3_ERR_LPF_NEW_DIR_BLOCK, &pctx);
		return 0;
	}

	retval = ext2fs_write_dir_block4(fs, blk, block, 0, ino);
	ext2fs_free_mem(&block);
	if (retval) {
		pctx.errcode = retval;
		fix_problem(ctx, PR_3_ERR_LPF_WRITE_BLOCK, &pctx);
		return 0;
	}

	/*
	 * Finally, create the directory link
	 */
	pctx.errcode = ext2fs_link(fs, EXT2_ROOT_INO, name, ino, EXT2_FT_DIR);
	if (pctx.errcode == EXT2_ET_DIR_NO_SPACE) {
		pctx.errcode = ext2fs_expand_dir(fs, EXT2_ROOT_INO);
		if (pctx.errcode)
			goto link_error;
		pctx.errcode = ext2fs_link(fs, EXT2_ROOT_INO, name, ino,
					   EXT2_FT_DIR);
	}
	if (pctx.errcode) {
link_error:
		pctx.str = "ext2fs_link";
		fix_problem(ctx, PR_3_CREATE_LPF_ERROR, &pctx);
		return 0;
	}

	/*
	 * Miscellaneous bookkeeping that needs to be kept straight.
	 */
	e2fsck_add_dir_info(ctx, ino, EXT2_ROOT_INO);
	e2fsck_adjust_inode_count(ctx, EXT2_ROOT_INO, 1);
	ext2fs_icount_store(ctx->inode_count, ino, 2);
	ext2fs_icount_store(ctx->inode_link_info, ino, 2);
	ctx->lost_and_found = ino;
	quota_data_add(ctx->qctx, &inode, ino, EXT2_CLUSTER_SIZE(fs->super));
	quota_data_inodes(ctx->qctx, &inode, ino, +1);
#if 0
	printf("/lost+found created; inode #%lu\n", ino);
#endif
	return ino;
}

/*
 * This routine will connect a file to lost+found
 */
int e2fsck_reconnect_file(e2fsck_t ctx, ext2_ino_t ino)
{
	ext2_filsys fs = ctx->fs;
	errcode_t	retval;
	char		name[80];
	struct problem_context	pctx;
	struct ext2_inode 	inode;
	int		file_type = 0;

	clear_problem_context(&pctx);
	pctx.ino = ino;

	if (!ctx->bad_lost_and_found && !ctx->lost_and_found) {
		if (e2fsck_get_lost_and_found(ctx, 1) == 0)
			ctx->bad_lost_and_found++;
	}
	if (ctx->bad_lost_and_found) {
		fix_problem(ctx, PR_3_NO_LPF, &pctx);
		return 1;
	}

	sprintf(name, "#%u", ino);
	if (ext2fs_read_inode(fs, ino, &inode) == 0)
		file_type = ext2_file_type(inode.i_mode);
	retval = ext2fs_link(fs, ctx->lost_and_found, name, ino, file_type);
	if (retval == EXT2_ET_DIR_NO_SPACE) {
		if (!fix_problem(ctx, PR_3_EXPAND_LF_DIR, &pctx))
			return 1;
		retval = e2fsck_expand_directory(ctx, ctx->lost_and_found,
						 1, 0);
		if (retval) {
			pctx.errcode = retval;
			fix_problem(ctx, PR_3_CANT_EXPAND_LPF, &pctx);
			return 1;
		}
		retval = ext2fs_link(fs, ctx->lost_and_found, name,
				     ino, file_type);
	}
	if (retval) {
		pctx.errcode = retval;
		fix_problem(ctx, PR_3_CANT_RECONNECT, &pctx);
		return 1;
	}
	e2fsck_adjust_inode_count(ctx, ino, 1);

	return 0;
}

/*
 * Utility routine to adjust the inode counts on an inode.
 */
errcode_t e2fsck_adjust_inode_count(e2fsck_t ctx, ext2_ino_t ino, int adj)
{
	ext2_filsys fs = ctx->fs;
	errcode_t		retval;
	struct ext2_inode 	inode;

	if (!ino)
		return 0;

	retval = ext2fs_read_inode(fs, ino, &inode);
	if (retval)
		return retval;

#if 0
	printf("Adjusting link count for inode %lu by %d (from %d)\n", ino, adj,
	       inode.i_links_count);
#endif

	if (adj == 1) {
		ext2fs_icount_increment(ctx->inode_count, ino, 0);
		if (inode.i_links_count == (__u16) ~0)
			return 0;
		ext2fs_icount_increment(ctx->inode_link_info, ino, 0);
		inode.i_links_count++;
	} else if (adj == -1) {
		ext2fs_icount_decrement(ctx->inode_count, ino, 0);
		if (inode.i_links_count == 0)
			return 0;
		ext2fs_icount_decrement(ctx->inode_link_info, ino, 0);
		inode.i_links_count--;
	}

	retval = ext2fs_write_inode(fs, ino, &inode);
	if (retval)
		return retval;

	return 0;
}

/*
 * Fix parent --- this routine fixes up the parent of a directory.
 */
struct fix_dotdot_struct {
	ext2_filsys	fs;
	ext2_ino_t	parent;
	int		done;
	e2fsck_t	ctx;
};

static int fix_dotdot_proc(struct ext2_dir_entry *dirent,
			   int	offset EXT2FS_ATTR((unused)),
			   int	blocksize EXT2FS_ATTR((unused)),
			   char	*buf EXT2FS_ATTR((unused)),
			   void	*priv_data)
{
	struct fix_dotdot_struct *fp = (struct fix_dotdot_struct *) priv_data;
	errcode_t	retval;
	struct problem_context pctx;

	if (ext2fs_dirent_name_len(dirent) != 2)
		return 0;
	if (strncmp(dirent->name, "..", 2))
		return 0;

	clear_problem_context(&pctx);

	retval = e2fsck_adjust_inode_count(fp->ctx, dirent->inode, -1);
	if (retval) {
		pctx.errcode = retval;
		fix_problem(fp->ctx, PR_3_ADJUST_INODE, &pctx);
	}
	retval = e2fsck_adjust_inode_count(fp->ctx, fp->parent, 1);
	if (retval) {
		pctx.errcode = retval;
		fix_problem(fp->ctx, PR_3_ADJUST_INODE, &pctx);
	}
	dirent->inode = fp->parent;
	if (ext2fs_has_feature_filetype(fp->ctx->fs->super))
		ext2fs_dirent_set_file_type(dirent, EXT2_FT_DIR);
	else
		ext2fs_dirent_set_file_type(dirent, EXT2_FT_UNKNOWN);

	fp->done++;
	return DIRENT_ABORT | DIRENT_CHANGED;
}

static void fix_dotdot(e2fsck_t ctx, ext2_ino_t ino, ext2_ino_t parent)
{
	ext2_filsys fs = ctx->fs;
	errcode_t	retval;
	struct fix_dotdot_struct fp;
	struct problem_context pctx;
	int		flags, will_rehash;

	fp.fs = fs;
	fp.parent = parent;
	fp.done = 0;
	fp.ctx = ctx;

#if 0
	printf("Fixing '..' of inode %lu to be %lu...\n", ino, parent);
#endif

	clear_problem_context(&pctx);
	pctx.ino = ino;
	will_rehash = e2fsck_dir_will_be_rehashed(ctx, ino);
	if (will_rehash) {
		flags = ctx->fs->flags;
		ctx->fs->flags |= EXT2_FLAG_IGNORE_CSUM_ERRORS;
	}
	retval = ext2fs_dir_iterate(fs, ino, DIRENT_FLAG_INCLUDE_EMPTY,
				    0, fix_dotdot_proc, &fp);
	if (will_rehash)
		ctx->fs->flags = (flags & EXT2_FLAG_IGNORE_CSUM_ERRORS) |
			(ctx->fs->flags & ~EXT2_FLAG_IGNORE_CSUM_ERRORS);
	if (retval || !fp.done) {
		pctx.errcode = retval;
		fix_problem(ctx, retval ? PR_3_FIX_PARENT_ERR :
			    PR_3_FIX_PARENT_NOFIND, &pctx);
		ext2fs_unmark_valid(fs);
	}
	(void) e2fsck_dir_info_set_dotdot(ctx, ino, parent);
	if (e2fsck_dir_info_set_parent(ctx, ino, ctx->lost_and_found))
		fix_problem(ctx, PR_3_NO_DIRINFO, &pctx);

	return;
}

/*
 * These routines are responsible for expanding a /lost+found if it is
 * too small.
 */

struct expand_dir_struct {
	blk64_t			num;
	e2_blkcnt_t		guaranteed_size;
	blk64_t			newblocks;
	blk64_t			last_block;
	errcode_t		err;
	e2fsck_t		ctx;
	ext2_ino_t		dir;
};

static int expand_dir_proc(ext2_filsys fs,
			   blk64_t	*blocknr,
			   e2_blkcnt_t	blockcnt,
			   blk64_t ref_block EXT2FS_ATTR((unused)),
			   int ref_offset EXT2FS_ATTR((unused)),
			   void	*priv_data)
{
	struct expand_dir_struct *es = (struct expand_dir_struct *) priv_data;
	blk64_t	new_blk;
	static blk64_t	last_blk = 0;
	char		*block;
	errcode_t	retval;
	e2fsck_t	ctx;

	ctx = es->ctx;

	if (es->guaranteed_size && blockcnt >= es->guaranteed_size)
		return BLOCK_ABORT;

	if (blockcnt > 0)
		es->last_block = blockcnt;
	if (*blocknr) {
		last_blk = *blocknr;
		return 0;
	}

	if (blockcnt &&
	    (EXT2FS_B2C(fs, last_blk) == EXT2FS_B2C(fs, last_blk + 1)))
		new_blk = last_blk + 1;
	else {
		last_blk &= ~EXT2FS_CLUSTER_MASK(fs);
		retval = ext2fs_new_block2(fs, last_blk, ctx->block_found_map,
					  &new_blk);
		if (retval) {
			es->err = retval;
			return BLOCK_ABORT;
		}
		es->newblocks++;
		ext2fs_block_alloc_stats2(fs, new_blk, +1);
	}
	last_blk = new_blk;

	if (blockcnt > 0) {
		retval = ext2fs_new_dir_block(fs, 0, 0, &block);
		if (retval) {
			es->err = retval;
			return BLOCK_ABORT;
		}
		es->num--;
		retval = ext2fs_write_dir_block4(fs, new_blk, block, 0,
						 es->dir);
		ext2fs_free_mem(&block);
	} else
		retval = ext2fs_zero_blocks2(fs, new_blk, 1, NULL, NULL);
	if (retval) {
		es->err = retval;
		return BLOCK_ABORT;
	}
	*blocknr = new_blk;
	ext2fs_mark_block_bitmap2(ctx->block_found_map, new_blk);

	if (es->num == 0)
		return (BLOCK_CHANGED | BLOCK_ABORT);
	else
		return BLOCK_CHANGED;
}

errcode_t e2fsck_expand_directory(e2fsck_t ctx, ext2_ino_t dir,
				  int num, int guaranteed_size)
{
	ext2_filsys fs = ctx->fs;
	errcode_t	retval;
	struct expand_dir_struct es;
	struct ext2_inode_large	inode;
	blk64_t		sz;

	if (!(fs->flags & EXT2_FLAG_RW))
		return EXT2_ET_RO_FILSYS;

	/*
	 * Read the inode and block bitmaps in; we'll be messing with
	 * them.
	 */
	e2fsck_read_bitmaps(ctx);

	retval = ext2fs_check_directory(fs, dir);
	if (retval)
		return retval;

	es.num = num;
	es.guaranteed_size = guaranteed_size;
	es.last_block = 0;
	es.err = 0;
	es.newblocks = 0;
	es.ctx = ctx;
	es.dir = dir;

	retval = ext2fs_block_iterate3(fs, dir, BLOCK_FLAG_APPEND,
				       0, expand_dir_proc, &es);

	if (es.err)
		return es.err;

	/*
	 * Update the size and block count fields in the inode.
	 */
	retval = ext2fs_read_inode_full(fs, dir,
					EXT2_INODE(&inode), sizeof(inode));
	if (retval)
		return retval;

	sz = (es.last_block + 1) * fs->blocksize;
	retval = ext2fs_inode_size_set(fs, EXT2_INODE(&inode), sz);
	if (retval)
		return retval;
	ext2fs_iblk_add_blocks(fs, EXT2_INODE(&inode), es.newblocks);
	quota_data_add(ctx->qctx, &inode, dir,
		       es.newblocks * EXT2_CLUSTER_SIZE(fs->super));

	e2fsck_write_inode_full(ctx, dir, EXT2_INODE(&inode),
				sizeof(inode), "expand_directory");

	return 0;
}

