/*
 * pass2.c --- check directory structure
 * 
 * Copyright (C) 1993, 1994, 1995, 1996, 1997 Theodore Ts'o
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 * 
 * Pass 2 of e2fsck iterates through all active directory inodes, and
 * applies to following tests to each directory entry in the directory
 * blocks in the inodes:
 *
 *	- The length of the directory entry (rec_len) should be at
 * 		least 8 bytes, and no more than the remaining space
 * 		left in the directory block.
 * 	- The length of the name in the directory entry (name_len)
 * 		should be less than (rec_len - 8).  
 *	- The inode number in the directory entry should be within
 * 		legal bounds.
 * 	- The inode number should refer to a in-use inode.
 *	- The first entry should be '.', and its inode should be
 * 		the inode of the directory.
 * 	- The second entry should be '..'.
 *
 * To minimize disk seek time, the directory blocks are processed in
 * sorted order of block numbers.
 *
 * Pass 2 also collects the following information:
 * 	- The inode numbers of the subdirectories for each directory.
 *
 * Pass 2 relies on the following information from previous passes:
 * 	- The directory information collected in pass 1.
 * 	- The inode_used_map bitmap
 * 	- The inode_bad_map bitmap
 * 	- The inode_dir_map bitmap
 *
 * Pass 2 frees the following data structures
 * 	- The inode_bad_map bitmap
 * 	- The inode_reg_map bitmap
 */

#include "e2fsck.h"
#include "problem.h"

#ifdef NO_INLINE_FUNCS
#define _INLINE_
#else
#define _INLINE_ inline
#endif

/*
 * Keeps track of how many times an inode is referenced.
 */
static void deallocate_inode(e2fsck_t ctx, ext2_ino_t ino, char* block_buf);
static int check_dir_block(ext2_filsys fs,
			   struct ext2_db_entry *dir_blocks_info,
			   void *priv_data);
static int allocate_dir_block(e2fsck_t ctx,
			      struct ext2_db_entry *dir_blocks_info,
			      char *buf, struct problem_context *pctx);
static int update_dir_block(ext2_filsys fs,
			    blk_t	*block_nr,
			    e2_blkcnt_t blockcnt,
			    blk_t	ref_block,
			    int		ref_offset, 
			    void	*priv_data);

struct check_dir_struct {
	char *buf;
	struct problem_context	pctx;
	int	count, max;
	e2fsck_t ctx;
};	

void e2fsck_pass2(e2fsck_t ctx)
{
	struct ext2_super_block *sb = ctx->fs->super;
	struct problem_context	pctx;
	ext2_filsys 		fs = ctx->fs;
	char			*buf;
#ifdef RESOURCE_TRACK
	struct resource_track	rtrack;
#endif
	struct dir_info 	*dir;
	struct check_dir_struct cd;
		
#ifdef RESOURCE_TRACK
	init_resource_track(&rtrack);
#endif

	clear_problem_context(&cd.pctx);

#ifdef MTRACE
	mtrace_print("Pass 2");
#endif

	if (!(ctx->options & E2F_OPT_PREEN))
		fix_problem(ctx, PR_2_PASS_HEADER, &cd.pctx);

	cd.pctx.errcode = ext2fs_create_icount2(fs, EXT2_ICOUNT_OPT_INCREMENT,
						0, ctx->inode_link_info,
						&ctx->inode_count);
	if (cd.pctx.errcode) {
		fix_problem(ctx, PR_2_ALLOCATE_ICOUNT, &cd.pctx);
		ctx->flags |= E2F_FLAG_ABORT;
		return;
	}
	buf = (char *) e2fsck_allocate_memory(ctx, fs->blocksize,
					      "directory scan buffer");

	/*
	 * Set up the parent pointer for the root directory, if
	 * present.  (If the root directory is not present, we will
	 * create it in pass 3.)
	 */
	dir = e2fsck_get_dir_info(ctx, EXT2_ROOT_INO);
	if (dir)
		dir->parent = EXT2_ROOT_INO;

	cd.buf = buf;
	cd.ctx = ctx;
	cd.count = 1;
	cd.max = ext2fs_dblist_count(fs->dblist);

	if (ctx->progress)
		(void) (ctx->progress)(ctx, 2, 0, cd.max);
	
	cd.pctx.errcode = ext2fs_dblist_iterate(fs->dblist, check_dir_block,
						&cd);
	if (ctx->flags & E2F_FLAG_SIGNAL_MASK)
		return;
	if (cd.pctx.errcode) {
		fix_problem(ctx, PR_2_DBLIST_ITERATE, &cd.pctx);
		ctx->flags |= E2F_FLAG_ABORT;
		return;
	}
	
	ext2fs_free_mem((void **) &buf);
	ext2fs_free_dblist(fs->dblist);

	if (ctx->inode_bad_map) {
		ext2fs_free_inode_bitmap(ctx->inode_bad_map);
		ctx->inode_bad_map = 0;
	}
	if (ctx->inode_reg_map) {
		ext2fs_free_inode_bitmap(ctx->inode_reg_map);
		ctx->inode_reg_map = 0;
	}

	clear_problem_context(&pctx);
	if (ctx->large_files) {
		if (!(sb->s_feature_ro_compat &
		      EXT2_FEATURE_RO_COMPAT_LARGE_FILE) &&
		    fix_problem(ctx, PR_2_FEATURE_LARGE_FILES, &pctx)) {
			sb->s_feature_ro_compat |=
				EXT2_FEATURE_RO_COMPAT_LARGE_FILE;
			ext2fs_mark_super_dirty(fs);
		}
		if (sb->s_rev_level == EXT2_GOOD_OLD_REV &&
		    fix_problem(ctx, PR_1_FS_REV_LEVEL, &pctx)) {
			ext2fs_update_dynamic_rev(fs);
			ext2fs_mark_super_dirty(fs);
		}
	} else if (!ctx->large_files &&
	    (sb->s_feature_ro_compat &
	      EXT2_FEATURE_RO_COMPAT_LARGE_FILE)) {
		if (fs->flags & EXT2_FLAG_RW) {
			sb->s_feature_ro_compat &= 
				~EXT2_FEATURE_RO_COMPAT_LARGE_FILE;
			ext2fs_mark_super_dirty(fs);
		}
	}
	
#ifdef RESOURCE_TRACK
	if (ctx->options & E2F_OPT_TIME2) {
		e2fsck_clear_progbar(ctx);
		print_resource_track("Pass 2", &rtrack);
	}
#endif
}

/*
 * Make sure the first entry in the directory is '.', and that the
 * directory entry is sane.
 */
static int check_dot(e2fsck_t ctx,
		     struct ext2_dir_entry *dirent,
		     ext2_ino_t ino, struct problem_context *pctx)
{
	struct ext2_dir_entry *nextdir;
	int	status = 0;
	int	created = 0;
	int	new_len;
	int	problem = 0;
	
	if (!dirent->inode)
		problem = PR_2_MISSING_DOT;
	else if (((dirent->name_len & 0xFF) != 1) ||
		 (dirent->name[0] != '.'))
		problem = PR_2_1ST_NOT_DOT;
	else if (dirent->name[1] != '\0')
		problem = PR_2_DOT_NULL_TERM;
	
	if (problem) {
		if (fix_problem(ctx, problem, pctx)) {
			if (dirent->rec_len < 12)
				dirent->rec_len = 12;
			dirent->inode = ino;
			dirent->name_len = 1;
			dirent->name[0] = '.';
			dirent->name[1] = '\0';
			status = 1;
			created = 1;
		}
	}
	if (dirent->inode != ino) {
		if (fix_problem(ctx, PR_2_BAD_INODE_DOT, pctx)) {
			dirent->inode = ino;
			status = 1;
		}
	}
	if (dirent->rec_len > 12) {
		new_len = dirent->rec_len - 12;
		if (new_len > 12) {
			if (created ||
			    fix_problem(ctx, PR_2_SPLIT_DOT, pctx)) {
				nextdir = (struct ext2_dir_entry *)
					((char *) dirent + 12);
				dirent->rec_len = 12;
				nextdir->rec_len = new_len;
				nextdir->inode = 0;
				nextdir->name_len = 0;
				status = 1;
			}
		}
	}
	return status;
}

/*
 * Make sure the second entry in the directory is '..', and that the
 * directory entry is sane.  We do not check the inode number of '..'
 * here; this gets done in pass 3.
 */
static int check_dotdot(e2fsck_t ctx,
			struct ext2_dir_entry *dirent,
			struct dir_info *dir, struct problem_context *pctx)
{
	int		problem = 0;
	
	if (!dirent->inode)
		problem = PR_2_MISSING_DOT_DOT;
	else if (((dirent->name_len & 0xFF) != 2) ||
		 (dirent->name[0] != '.') ||
		 (dirent->name[1] != '.'))
		problem = PR_2_2ND_NOT_DOT_DOT;
	else if (dirent->name[2] != '\0')
		problem = PR_2_DOT_DOT_NULL_TERM;

	if (problem) {
		if (fix_problem(ctx, problem, pctx)) {
			if (dirent->rec_len < 12)
				dirent->rec_len = 12;
			/*
			 * Note: we don't have the parent inode just
			 * yet, so we will fill it in with the root
			 * inode.  This will get fixed in pass 3.
			 */
			dirent->inode = EXT2_ROOT_INO;
			dirent->name_len = 2;
			dirent->name[0] = '.';
			dirent->name[1] = '.';
			dirent->name[2] = '\0';
			return 1;
		} 
		return 0;
	}
	dir->dotdot = dirent->inode;
	return 0;
}

/*
 * Check to make sure a directory entry doesn't contain any illegal
 * characters.
 */
static int check_name(e2fsck_t ctx,
		      struct ext2_dir_entry *dirent,
		      ext2_ino_t dir_ino, struct problem_context *pctx)
{
	int	i;
	int	fixup = -1;
	int	ret = 0;
	
	for ( i = 0; i < (dirent->name_len & 0xFF); i++) {
		if (dirent->name[i] == '/' || dirent->name[i] == '\0') {
			if (fixup < 0) {
				fixup = fix_problem(ctx, PR_2_BAD_NAME, pctx);
			}
			if (fixup) {
				dirent->name[i] = '.';
				ret = 1;
			}
		}
	}
	return ret;
}

/*
 * Check the directory filetype (if present)
 */
static _INLINE_ int check_filetype(e2fsck_t ctx,
		      struct ext2_dir_entry *dirent,
		      ext2_ino_t dir_ino, struct problem_context *pctx)
{
	int	filetype = dirent->name_len >> 8;
	int	should_be = EXT2_FT_UNKNOWN;
	struct ext2_inode	inode;

	if (!(ctx->fs->super->s_feature_incompat &
	      EXT2_FEATURE_INCOMPAT_FILETYPE)) {
		if (filetype == 0 ||
		    !fix_problem(ctx, PR_2_CLEAR_FILETYPE, pctx))
			return 0;
		dirent->name_len = dirent->name_len & 0xFF;
		return 1;
	}

	if (ext2fs_test_inode_bitmap(ctx->inode_dir_map, dirent->inode)) {
		should_be = EXT2_FT_DIR;
	} else if (ext2fs_test_inode_bitmap(ctx->inode_reg_map,
					    dirent->inode)) {
		should_be = EXT2_FT_REG_FILE;
	} else if (ctx->inode_bad_map &&
		   ext2fs_test_inode_bitmap(ctx->inode_bad_map,
					    dirent->inode))
		should_be = 0;
	else {
		e2fsck_read_inode(ctx, dirent->inode, &inode,
				  "check_filetype");
		should_be = ext2_file_type(inode.i_mode);
	}
	if (filetype == should_be)
		return 0;
	pctx->num = should_be;

	if (fix_problem(ctx, filetype ? PR_2_BAD_FILETYPE : PR_2_SET_FILETYPE,
			pctx) == 0)
		return 0;
			
	dirent->name_len = (dirent->name_len & 0xFF) | should_be << 8;
	return 1;
}


static int check_dir_block(ext2_filsys fs,
			   struct ext2_db_entry *db,
			   void *priv_data)
{
	struct dir_info		*subdir, *dir;
	struct ext2_dir_entry 	*dirent;
	int			offset = 0;
	int			dir_modified = 0;
	int			dot_state;
	blk_t			block_nr = db->blk;
	ext2_ino_t 		ino = db->ino;
	__u16			links;
	struct check_dir_struct	*cd;
	char 			*buf;
	e2fsck_t		ctx;
	int			problem;

	cd = (struct check_dir_struct *) priv_data;
	buf = cd->buf;
	ctx = cd->ctx;

	if (ctx->progress)
		if ((ctx->progress)(ctx, 2, cd->count++, cd->max))
			return DIRENT_ABORT;
	
	/*
	 * Make sure the inode is still in use (could have been 
	 * deleted in the duplicate/bad blocks pass.
	 */
	if (!(ext2fs_test_inode_bitmap(ctx->inode_used_map, ino))) 
		return 0;

	cd->pctx.ino = ino;
	cd->pctx.blk = block_nr;
	cd->pctx.blkcount = db->blockcnt;
	cd->pctx.ino2 = 0;
	cd->pctx.dirent = 0;
	cd->pctx.num = 0;

	if (db->blk == 0) {
		if (allocate_dir_block(ctx, db, buf, &cd->pctx))
			return 0;
		block_nr = db->blk;
	}
	
	if (db->blockcnt)
		dot_state = 2;
	else
		dot_state = 0;

#if 0
	printf("In process_dir_block block %lu, #%d, inode %lu\n", block_nr,
	       db->blockcnt, ino);
#endif
	
	cd->pctx.errcode = ext2fs_read_dir_block(fs, block_nr, buf);
	if (cd->pctx.errcode == EXT2_ET_DIR_CORRUPTED)
		cd->pctx.errcode = 0; /* We'll handle this ourselves */
	if (cd->pctx.errcode) {
		if (!fix_problem(ctx, PR_2_READ_DIRBLOCK, &cd->pctx)) {
			ctx->flags |= E2F_FLAG_ABORT;
			return DIRENT_ABORT;
		}
		memset(buf, 0, fs->blocksize);
	}

	do {
		dot_state++;
		problem = 0;
		dirent = (struct ext2_dir_entry *) (buf + offset);
		cd->pctx.dirent = dirent;
		cd->pctx.num = offset;
		if (((offset + dirent->rec_len) > fs->blocksize) ||
		    (dirent->rec_len < 12) ||
		    ((dirent->rec_len % 4) != 0) ||
		    (((dirent->name_len & 0xFF)+8) > dirent->rec_len)) {
			if (fix_problem(ctx, PR_2_DIR_CORRUPTED, &cd->pctx)) {
				dirent->rec_len = fs->blocksize - offset;
				dirent->name_len = 0;
				dirent->inode = 0;
				dir_modified++;
			} else
				return DIRENT_ABORT;
		}
		if ((dirent->name_len & 0xFF) > EXT2_NAME_LEN) {
			if (fix_problem(ctx, PR_2_FILENAME_LONG, &cd->pctx)) {
				dirent->name_len = EXT2_NAME_LEN;
				dir_modified++;
			}
		}

		if (dot_state == 1) {
			if (check_dot(ctx, dirent, ino, &cd->pctx))
				dir_modified++;
		} else if (dot_state == 2) {
			dir = e2fsck_get_dir_info(ctx, ino);
			if (!dir) {
				fix_problem(ctx, PR_2_NO_DIRINFO, &cd->pctx);
				ctx->flags |= E2F_FLAG_ABORT;
				return DIRENT_ABORT;
			}
			if (check_dotdot(ctx, dirent, dir, &cd->pctx))
				dir_modified++;
		} else if (dirent->inode == ino) {
			problem = PR_2_LINK_DOT;
			if (fix_problem(ctx, PR_2_LINK_DOT, &cd->pctx)) {
				dirent->inode = 0;
				dir_modified++;
				goto next;
			}
		}
		if (!dirent->inode) 
			goto next;
		
		/*
		 * Make sure the inode listed is a legal one.
		 */ 
		if (((dirent->inode != EXT2_ROOT_INO) &&
		     (dirent->inode < EXT2_FIRST_INODE(fs->super))) ||
		    (dirent->inode > fs->super->s_inodes_count)) {
			problem = PR_2_BAD_INO;
		} else if (!(ext2fs_test_inode_bitmap(ctx->inode_used_map,
					       dirent->inode))) {
			/*
			 * If the inode is unused, offer to clear it.
			 */
			problem = PR_2_UNUSED_INODE;
		} else if (ctx->inode_bb_map &&
			   (ext2fs_test_inode_bitmap(ctx->inode_bb_map,
						     dirent->inode))) {
			/*
			 * If the inode is in a bad block, offer to
			 * clear it.
			 */
			problem = PR_2_BB_INODE;
		} else if ((dot_state > 2) &&
			   ((dirent->name_len & 0xFF) == 1) &&
			   (dirent->name[0] == '.')) {
			/*
			 * If there's a '.' entry in anything other
			 * than the first directory entry, it's a
			 * duplicate entry that should be removed.
			 */
			problem = PR_2_DUP_DOT;
		} else if ((dot_state > 2) &&
			   ((dirent->name_len & 0xFF) == 2) &&
			   (dirent->name[0] == '.') && 
			   (dirent->name[1] == '.')) {
			/*
			 * If there's a '..' entry in anything other
			 * than the second directory entry, it's a
			 * duplicate entry that should be removed.
			 */
			problem = PR_2_DUP_DOT_DOT;
		} else if ((dot_state > 2) &&
			   (dirent->inode == EXT2_ROOT_INO)) {
			/*
			 * Don't allow links to the root directory.
			 * We check this specially to make sure we
			 * catch this error case even if the root
			 * directory hasn't been created yet.
			 */
			problem = PR_2_LINK_ROOT;
		} else if ((dot_state > 2) &&
			   (dirent->name_len & 0xFF) == 0) {
			/*
			 * Don't allow zero-length directory names.
			 */
			problem = PR_2_NULL_NAME;
		}

		if (problem) {
			if (fix_problem(ctx, problem, &cd->pctx)) {
				dirent->inode = 0;
				dir_modified++;
				goto next;
			} else {
				ext2fs_unmark_valid(fs);
				if (problem == PR_2_BAD_INO)
					goto next;
			}
		}

		/*
		 * If the inode was marked as having bad fields in
		 * pass1, process it and offer to fix/clear it.
		 * (We wait until now so that we can display the
		 * pathname to the user.)
		 */
		if (ctx->inode_bad_map &&
		    ext2fs_test_inode_bitmap(ctx->inode_bad_map,
					     dirent->inode)) {
			if (e2fsck_process_bad_inode(ctx, ino,
						     dirent->inode)) {
				dirent->inode = 0;
				dir_modified++;
				goto next;
			}
			if (ctx->flags & E2F_FLAG_SIGNAL_MASK)
				return DIRENT_ABORT;
		}

		if (check_name(ctx, dirent, ino, &cd->pctx))
			dir_modified++;

		if (check_filetype(ctx, dirent, ino, &cd->pctx))
			dir_modified++;

		/*
		 * If this is a directory, then mark its parent in its
		 * dir_info structure.  If the parent field is already
		 * filled in, then this directory has more than one
		 * hard link.  We assume the first link is correct,
		 * and ask the user if he/she wants to clear this one.
		 */
		if ((dot_state > 2) &&
		    (ext2fs_test_inode_bitmap(ctx->inode_dir_map,
					      dirent->inode))) {
			subdir = e2fsck_get_dir_info(ctx, dirent->inode);
			if (!subdir) {
				cd->pctx.ino = dirent->inode;
				fix_problem(ctx, PR_2_NO_DIRINFO, &cd->pctx);
				ctx->flags |= E2F_FLAG_ABORT;
				return DIRENT_ABORT;
			}
			if (subdir->parent) {
				cd->pctx.ino2 = subdir->parent;
				if (fix_problem(ctx, PR_2_LINK_DIR,
						&cd->pctx)) {
					dirent->inode = 0;
					dir_modified++;
					goto next;
				}
				cd->pctx.ino2 = 0;
			} else
				subdir->parent = ino;
		}
		
		ext2fs_icount_increment(ctx->inode_count, dirent->inode,
					&links);
		if (links > 1)
			ctx->fs_links_count++;
		ctx->fs_total_count++;
	next:
		offset += dirent->rec_len;
	} while (offset < fs->blocksize);
#if 0
	printf("\n");
#endif
	if (offset != fs->blocksize) {
		cd->pctx.num = dirent->rec_len - fs->blocksize + offset;
		if (fix_problem(ctx, PR_2_FINAL_RECLEN, &cd->pctx)) {
			dirent->rec_len = cd->pctx.num;
			dir_modified++;
		}
	}
	if (dir_modified) {
		cd->pctx.errcode = ext2fs_write_dir_block(fs, block_nr, buf);
		if (cd->pctx.errcode) {
			if (!fix_problem(ctx, PR_2_WRITE_DIRBLOCK,
					 &cd->pctx)) {
				ctx->flags |= E2F_FLAG_ABORT;
				return DIRENT_ABORT;
			}
		}
		ext2fs_mark_changed(fs);
	}
	return 0;
}

/*
 * This function is called to deallocate a block, and is an interator
 * functioned called by deallocate inode via ext2fs_iterate_block().
 */
static int deallocate_inode_block(ext2_filsys fs,
				  blk_t	*block_nr,
				  e2_blkcnt_t blockcnt,
				  blk_t ref_block,
				  int ref_offset, 
				  void *priv_data)
{
	e2fsck_t	ctx = (e2fsck_t) priv_data;
	
	if (HOLE_BLKADDR(*block_nr))
		return 0;
	ext2fs_unmark_block_bitmap(ctx->block_found_map, *block_nr);
	ext2fs_unmark_block_bitmap(fs->block_map, *block_nr);
	return 0;
}
		
/*
 * This fuction deallocates an inode
 */
static void deallocate_inode(e2fsck_t ctx, ext2_ino_t ino, char* block_buf)
{
	ext2_filsys fs = ctx->fs;
	struct ext2_inode	inode;
	struct problem_context	pctx;
	
	ext2fs_icount_store(ctx->inode_link_info, ino, 0);
	e2fsck_read_inode(ctx, ino, &inode, "deallocate_inode");
	inode.i_links_count = 0;
	inode.i_dtime = time(0);
	e2fsck_write_inode(ctx, ino, &inode, "deallocate_inode");
	clear_problem_context(&pctx);
	pctx.ino = ino;

	/*
	 * Fix up the bitmaps...
	 */
	e2fsck_read_bitmaps(ctx);
	ext2fs_unmark_inode_bitmap(ctx->inode_used_map, ino);
	ext2fs_unmark_inode_bitmap(ctx->inode_dir_map, ino);
	if (ctx->inode_bad_map)
		ext2fs_unmark_inode_bitmap(ctx->inode_bad_map, ino);
	ext2fs_unmark_inode_bitmap(fs->inode_map, ino);
	ext2fs_mark_ib_dirty(fs);

	if (!ext2fs_inode_has_valid_blocks(&inode))
		return;

	if (!LINUX_S_ISDIR(inode.i_mode) &&
	    (inode.i_size_high || inode.i_size & 0x80000000UL))
		ctx->large_files--;

	if (inode.i_file_acl) {
		ext2fs_unmark_block_bitmap(ctx->block_found_map,
					   inode.i_file_acl);
		ext2fs_unmark_block_bitmap(fs->block_map, inode.i_file_acl);
	}

	ext2fs_mark_bb_dirty(fs);
	pctx.errcode = ext2fs_block_iterate2(fs, ino, 0, block_buf,
					    deallocate_inode_block, ctx);
	if (pctx.errcode) {
		fix_problem(ctx, PR_2_DEALLOC_INODE, &pctx);
		ctx->flags |= E2F_FLAG_ABORT;
		return;
	}
}

extern int e2fsck_process_bad_inode(e2fsck_t ctx, ext2_ino_t dir,
				    ext2_ino_t ino)
{
	ext2_filsys fs = ctx->fs;
	struct ext2_inode	inode;
	int			inode_modified = 0;
	unsigned char		*frag, *fsize;
	struct problem_context	pctx;
	int	problem = 0;

	e2fsck_read_inode(ctx, ino, &inode, "process_bad_inode");

	clear_problem_context(&pctx);
	pctx.ino = ino;
	pctx.dir = dir;
	pctx.inode = &inode;

	if (!LINUX_S_ISDIR(inode.i_mode) && !LINUX_S_ISREG(inode.i_mode) &&
	    !LINUX_S_ISCHR(inode.i_mode) && !LINUX_S_ISBLK(inode.i_mode) &&
	    !LINUX_S_ISLNK(inode.i_mode) && !LINUX_S_ISFIFO(inode.i_mode) &&
	    !(LINUX_S_ISSOCK(inode.i_mode)))
		problem = PR_2_BAD_MODE;
	else if (LINUX_S_ISCHR(inode.i_mode)
		 && !e2fsck_pass1_check_device_inode(&inode))
		problem = PR_2_BAD_CHAR_DEV;
	else if (LINUX_S_ISBLK(inode.i_mode)
		 && !e2fsck_pass1_check_device_inode(&inode))
		problem = PR_2_BAD_BLOCK_DEV;
	else if (LINUX_S_ISFIFO(inode.i_mode)
		 && !e2fsck_pass1_check_device_inode(&inode))
		problem = PR_2_BAD_FIFO;
	else if (LINUX_S_ISSOCK(inode.i_mode)
		 && !e2fsck_pass1_check_device_inode(&inode))
		problem = PR_2_BAD_SOCKET;
	else if (LINUX_S_ISLNK(inode.i_mode)
		 && !e2fsck_pass1_check_symlink(fs, &inode)) {
		problem = PR_2_SYMLINK_SIZE;
	}

	if (problem) {
		if (fix_problem(ctx, problem, &pctx)) {
			deallocate_inode(ctx, ino, 0);
			if (ctx->flags & E2F_FLAG_SIGNAL_MASK)
				return 0;
			return 1;
		}
		problem = 0;
	}
		
	if (inode.i_faddr &&
	    fix_problem(ctx, PR_2_FADDR_ZERO, &pctx)) {
		inode.i_faddr = 0;
		inode_modified++;
	}

	switch (fs->super->s_creator_os) {
	    case EXT2_OS_LINUX:
		frag = &inode.osd2.linux2.l_i_frag;
		fsize = &inode.osd2.linux2.l_i_fsize;
		break;
	    case EXT2_OS_HURD:
		frag = &inode.osd2.hurd2.h_i_frag;
		fsize = &inode.osd2.hurd2.h_i_fsize;
		break;
	    case EXT2_OS_MASIX:
		frag = &inode.osd2.masix2.m_i_frag;
		fsize = &inode.osd2.masix2.m_i_fsize;
		break;
	    default:
		frag = fsize = 0;
	}
	if (frag && *frag) {
		pctx.num = *frag;
		if (fix_problem(ctx, PR_2_FRAG_ZERO, &pctx)) {
			*frag = 0;
			inode_modified++;
		}
		pctx.num = 0;
	}
	if (fsize && *fsize) {
		pctx.num = *fsize;
		if (fix_problem(ctx, PR_2_FSIZE_ZERO, &pctx)) {
			*fsize = 0;
			inode_modified++;
		}
		pctx.num = 0;
	}

	if (inode.i_file_acl &&
	    !(fs->super->s_feature_compat & EXT2_FEATURE_COMPAT_EXT_ATTR) &&
	    fix_problem(ctx, PR_2_FILE_ACL_ZERO, &pctx)) {
		inode.i_file_acl = 0;
		inode_modified++;
	}
	if (inode.i_file_acl &&
	    ((inode.i_file_acl < fs->super->s_first_data_block) ||
	     (inode.i_file_acl >= fs->super->s_blocks_count)) &&
	    fix_problem(ctx, PR_2_FILE_ACL_BAD, &pctx)) {
		inode.i_file_acl = 0;
		inode_modified++;
	}
	if (inode.i_dir_acl &&
	    LINUX_S_ISDIR(inode.i_mode) &&
	    fix_problem(ctx, PR_2_DIR_ACL_ZERO, &pctx)) {
		inode.i_dir_acl = 0;
		inode_modified++;
	}
	if (inode_modified)
		e2fsck_write_inode(ctx, ino, &inode, "process_bad_inode");
	return 0;
}


/*
 * allocate_dir_block --- this function allocates a new directory
 * 	block for a particular inode; this is done if a directory has
 * 	a "hole" in it, or if a directory has a illegal block number
 * 	that was zeroed out and now needs to be replaced.
 */
static int allocate_dir_block(e2fsck_t ctx,
			      struct ext2_db_entry *db,
			      char *buf, struct problem_context *pctx)
{
	ext2_filsys fs = ctx->fs;
	blk_t			blk;
	char			*block;
	struct ext2_inode	inode;

	if (fix_problem(ctx, PR_2_DIRECTORY_HOLE, pctx) == 0)
		return 1;

	/*
	 * Read the inode and block bitmaps in; we'll be messing with
	 * them.
	 */
	e2fsck_read_bitmaps(ctx);
	
	/*
	 * First, find a free block
	 */
	pctx->errcode = ext2fs_new_block(fs, 0, ctx->block_found_map, &blk);
	if (pctx->errcode) {
		pctx->str = "ext2fs_new_block";
		fix_problem(ctx, PR_2_ALLOC_DIRBOCK, pctx);
		return 1;
	}
	ext2fs_mark_block_bitmap(ctx->block_found_map, blk);
	ext2fs_mark_block_bitmap(fs->block_map, blk);
	ext2fs_mark_bb_dirty(fs);

	/*
	 * Now let's create the actual data block for the inode
	 */
	if (db->blockcnt)
		pctx->errcode = ext2fs_new_dir_block(fs, 0, 0, &block);
	else
		pctx->errcode = ext2fs_new_dir_block(fs, db->ino,
						     EXT2_ROOT_INO, &block);

	if (pctx->errcode) {
		pctx->str = "ext2fs_new_dir_block";
		fix_problem(ctx, PR_2_ALLOC_DIRBOCK, pctx);
		return 1;
	}

	pctx->errcode = ext2fs_write_dir_block(fs, blk, block);
	ext2fs_free_mem((void **) &block);
	if (pctx->errcode) {
		pctx->str = "ext2fs_write_dir_block";
		fix_problem(ctx, PR_2_ALLOC_DIRBOCK, pctx);
		return 1;
	}

	/*
	 * Update the inode block count
	 */
	e2fsck_read_inode(ctx, db->ino, &inode, "allocate_dir_block");
	inode.i_blocks += fs->blocksize / 512;
	if (inode.i_size < (db->blockcnt+1) * fs->blocksize)
		inode.i_size = (db->blockcnt+1) * fs->blocksize;
	e2fsck_write_inode(ctx, db->ino, &inode, "allocate_dir_block");

	/*
	 * Finally, update the block pointers for the inode
	 */
	db->blk = blk;
	pctx->errcode = ext2fs_block_iterate2(fs, db->ino, BLOCK_FLAG_HOLE,
				      0, update_dir_block, db);
	if (pctx->errcode) {
		pctx->str = "ext2fs_block_iterate";
		fix_problem(ctx, PR_2_ALLOC_DIRBOCK, pctx);
		return 1;
	}

	return 0;
}

/*
 * This is a helper function for allocate_dir_block().
 */
static int update_dir_block(ext2_filsys fs,
			    blk_t	*block_nr,
			    e2_blkcnt_t blockcnt,
			    blk_t ref_block,
			    int ref_offset, 
			    void *priv_data)
{
	struct ext2_db_entry *db;

	db = (struct ext2_db_entry *) priv_data;
	if (db->blockcnt == (int) blockcnt) {
		*block_nr = db->blk;
		return BLOCK_CHANGED;
	}
	return 0;
}
