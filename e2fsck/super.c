/*
 * e2fsck.c - superblock checks
 * 
 * Copyright (C) 1993, 1994, 1995, 1996, 1997 Theodore Ts'o.
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 */

#include <stdio.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#include <string.h>
#include <fcntl.h>
#include <ctype.h>
#include <termios.h>
#include <time.h>
#ifdef HAVE_GETOPT_H
#include <getopt.h>
#endif
#include <unistd.h>
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#ifdef HAVE_MNTENT_H
#include <mntent.h>
#endif
#include <sys/ioctl.h>
#include <malloc.h>

#include "uuid/uuid.h"
#include "e2fsck.h"
#include "problem.h"
#include "../version.h"

#define MIN_CHECK 1
#define MAX_CHECK 2

static void check_super_value(e2fsck_t ctx, const char *descr,
			      unsigned long value, int flags,
			      unsigned long min, unsigned long max)
{
	struct		problem_context pctx;

	if (((flags & MIN_CHECK) && (value < min)) ||
	    ((flags & MAX_CHECK) && (value > max))) {
		clear_problem_context(&pctx);
		pctx.num = value;
		pctx.str = descr;
		fix_problem(ctx, PR_0_MISC_CORRUPT_SUPER, &pctx);
		fatal_error(0);	/* never get here! */
	}
}

void check_super_block(e2fsck_t ctx)
{
	ext2_filsys fs = ctx->fs;
	blk_t	first_block, last_block;
	struct ext2fs_sb *s = (struct ext2fs_sb *) fs->super;
	blk_t	blocks_per_group = fs->super->s_blocks_per_group;
	int	i;
	blk_t	should_be;
	struct problem_context	pctx;

	ctx->invalid_inode_bitmap_flag = allocate_memory(sizeof(int) *
					       fs->group_desc_count,
					       "invalid_inode_bitmap");
	ctx->invalid_block_bitmap_flag = allocate_memory(sizeof(int) *
					       fs->group_desc_count,
					       "invalid_block_bitmap");
	ctx->invalid_inode_table_flag = allocate_memory(sizeof(int) *
					      fs->group_desc_count,
					      "invalid_inode_table");
		
	clear_problem_context(&pctx);

	/*
	 * Verify the super block constants...
	 */
	check_super_value(ctx, "inodes_count", s->s_inodes_count,
			  MIN_CHECK, 1, 0);
	check_super_value(ctx, "blocks_count", s->s_blocks_count,
			  MIN_CHECK, 1, 0);
	check_super_value(ctx, "first_data_block", s->s_first_data_block,
			  MAX_CHECK, 0, s->s_blocks_count);
	check_super_value(ctx, "log_frag_size", s->s_log_frag_size,
			  MAX_CHECK, 0, 2);
	check_super_value(ctx, "log_block_size", s->s_log_block_size,
			  MIN_CHECK | MAX_CHECK, s->s_log_frag_size,
			  2);
	check_super_value(ctx, "frags_per_group", s->s_frags_per_group,
			  MIN_CHECK | MAX_CHECK, 1, 8 * EXT2_BLOCK_SIZE(s));
	check_super_value(ctx, "blocks_per_group", s->s_blocks_per_group,
			  MIN_CHECK | MAX_CHECK, 1, 8 * EXT2_BLOCK_SIZE(s));
	check_super_value(ctx, "inodes_per_group", s->s_inodes_per_group,
			  MIN_CHECK, 1, 0);
	check_super_value(ctx, "r_blocks_count", s->s_r_blocks_count,
			  MAX_CHECK, 0, s->s_blocks_count);

	pctx.errcode = ext2fs_get_device_size(ctx->filesystem_name,
					      EXT2_BLOCK_SIZE(s),
					      &should_be);
	if (pctx.errcode) {
		fix_problem(ctx, PR_0_GETSIZE_ERROR, &pctx);
		fatal_error(0);
	}
	if (should_be < s->s_blocks_count) {
		pctx.blk = s->s_blocks_count;
		pctx.blk2 = should_be;
		if (fix_problem(ctx, PR_0_FS_SIZE_WRONG, &pctx))
			fatal_error(0);
	}

	if (s->s_log_block_size != s->s_log_frag_size) {
		pctx.blk = EXT2_BLOCK_SIZE(s);
		pctx.blk2 = EXT2_FRAG_SIZE(s);
		fix_problem(ctx, PR_0_NO_FRAGMENTS, &pctx);
		fatal_error(0);
	}

	should_be = s->s_frags_per_group /
		(s->s_log_block_size - s->s_log_frag_size + 1);
	if (s->s_blocks_per_group != should_be) {
		pctx.blk = s->s_blocks_per_group;
		pctx.blk2 = should_be;
		fix_problem(ctx, PR_0_BLOCKS_PER_GROUP, &pctx);
		fatal_error(0);
	}

	should_be = (s->s_log_block_size == 0) ? 1 : 0;
	if (s->s_first_data_block != should_be) {
		pctx.blk = s->s_first_data_block;
		pctx.blk2 = should_be;
		fix_problem(ctx, PR_0_FIRST_DATA_BLOCK, &pctx);
		fatal_error(0);
	}

	/*
	 * Verify the group descriptors....
	 */
	first_block =  fs->super->s_first_data_block;
	last_block = first_block + blocks_per_group;

	for (i = 0; i < fs->group_desc_count; i++) {
		pctx.group = i;
		
		if (i == fs->group_desc_count - 1)
			last_block = fs->super->s_blocks_count;
		if ((fs->group_desc[i].bg_block_bitmap < first_block) ||
		    (fs->group_desc[i].bg_block_bitmap >= last_block)) {
			pctx.blk = fs->group_desc[i].bg_block_bitmap;
			if (fix_problem(ctx, PR_0_BB_NOT_GROUP, &pctx)) {
				fs->group_desc[i].bg_block_bitmap = 0;
				ctx->invalid_block_bitmap_flag[i]++;
				ctx->invalid_bitmaps++;
			}
		}
		if ((fs->group_desc[i].bg_inode_bitmap < first_block) ||
		    (fs->group_desc[i].bg_inode_bitmap >= last_block)) {
			pctx.blk = fs->group_desc[i].bg_inode_bitmap;
			if (fix_problem(ctx, PR_0_IB_NOT_GROUP, &pctx)) {
				fs->group_desc[i].bg_inode_bitmap = 0;
				ctx->invalid_inode_bitmap_flag[i]++;
				ctx->invalid_bitmaps++;
			}
		}
		if ((fs->group_desc[i].bg_inode_table < first_block) ||
		    ((fs->group_desc[i].bg_inode_table +
		      fs->inode_blocks_per_group - 1) >= last_block)) {
			pctx.blk = fs->group_desc[i].bg_inode_table;
			if (fix_problem(ctx, PR_0_ITABLE_NOT_GROUP, &pctx)) {
				fs->group_desc[i].bg_inode_table = 0;
				ctx->invalid_inode_table_flag[i]++;
				ctx->invalid_bitmaps++;
			}
		}
		first_block += fs->super->s_blocks_per_group;
		last_block += fs->super->s_blocks_per_group;
	}
	/*
	 * If we have invalid bitmaps, set the error state of the
	 * filesystem.
	 */
	if (ctx->invalid_bitmaps && !(ctx->options & E2F_OPT_READONLY)) {
		fs->super->s_state &= ~EXT2_VALID_FS;
		ext2fs_mark_super_dirty(fs);
	}

	/*
	 * If the UUID field isn't assigned, assign it.
	 */
	if (!(ctx->options & E2F_OPT_READONLY) && uuid_is_null(s->s_uuid)) {
		clear_problem_context(&pctx);
		if (fix_problem(ctx, PR_0_ADD_UUID, &pctx)) {
			uuid_generate(s->s_uuid);
			ext2fs_mark_super_dirty(fs);
		}
	}
	return;
}

