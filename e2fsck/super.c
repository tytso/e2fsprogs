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

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif

#ifndef EXT2_SKIP_UUID
#include "uuid/uuid.h"
#endif
#include "e2fsck.h"
#include "problem.h"

#define MIN_CHECK 1
#define MAX_CHECK 2

static void check_super_value(e2fsck_t ctx, const char *descr,
			      unsigned long value, int flags,
			      unsigned long min_val, unsigned long max_val)
{
	struct		problem_context pctx;

	if (((flags & MIN_CHECK) && (value < min_val)) ||
	    ((flags & MAX_CHECK) && (value > max_val))) {
		clear_problem_context(&pctx);
		pctx.num = value;
		pctx.str = descr;
		fix_problem(ctx, PR_0_MISC_CORRUPT_SUPER, &pctx);
		ctx->flags |= E2F_FLAG_ABORT; /* never get here! */
	}
}

/*
 * This routine may get stubbed out in special compilations of the
 * e2fsck code..
 */
#ifndef EXT2_SPECIAL_DEVICE_SIZE
errcode_t e2fsck_get_device_size(e2fsck_t ctx)
{
	return (ext2fs_get_device_size(ctx->filesystem_name,
				       EXT2_BLOCK_SIZE(ctx->fs->super),
				       &ctx->num_blocks));
}
#endif

void check_super_block(e2fsck_t ctx)
{
	ext2_filsys fs = ctx->fs;
	blk_t	first_block, last_block;
	struct ext2fs_sb *s = (struct ext2fs_sb *) fs->super;
	blk_t	blocks_per_group = fs->super->s_blocks_per_group;
	int	inodes_per_block;
	dgrp_t	i;
	blk_t	should_be;
	struct problem_context	pctx;
	
	inodes_per_block = (EXT2_INODE_SIZE(fs->super) + 
			    EXT2_BLOCK_SIZE(fs->super) - 1) /
				    EXT2_BLOCK_SIZE(fs->super);

	ctx->invalid_inode_bitmap_flag = (int *) e2fsck_allocate_memory(ctx,
		 sizeof(int) * fs->group_desc_count, "invalid_inode_bitmap");
	ctx->invalid_block_bitmap_flag = (int *) e2fsck_allocate_memory(ctx,
		 sizeof(int) * fs->group_desc_count, "invalid_block_bitmap");
	ctx->invalid_inode_table_flag = (int *) e2fsck_allocate_memory(ctx,
		sizeof(int) * fs->group_desc_count, "invalid_inode_table");
		
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
			  MIN_CHECK | MAX_CHECK, 1,
			  inodes_per_block * blocks_per_group);
	check_super_value(ctx, "r_blocks_count", s->s_r_blocks_count,
			  MAX_CHECK, 0, s->s_blocks_count);

	if (!ctx->num_blocks) {
		pctx.errcode = e2fsck_get_device_size(ctx);
		if (pctx.errcode && pctx.errcode != EXT2_ET_UNIMPLEMENTED) {
			fix_problem(ctx, PR_0_GETSIZE_ERROR, &pctx);
			ctx->flags |= E2F_FLAG_ABORT;
			return;
		}
		if ((pctx.errcode != EXT2_ET_UNIMPLEMENTED) &&
		    (ctx->num_blocks < s->s_blocks_count)) {
			pctx.blk = s->s_blocks_count;
			pctx.blk2 = ctx->num_blocks;
			if (fix_problem(ctx, PR_0_FS_SIZE_WRONG, &pctx)) {
				ctx->flags |= E2F_FLAG_ABORT;
				return;
			}
		}
	}

	if (s->s_log_block_size != s->s_log_frag_size) {
		pctx.blk = EXT2_BLOCK_SIZE(s);
		pctx.blk2 = EXT2_FRAG_SIZE(s);
		fix_problem(ctx, PR_0_NO_FRAGMENTS, &pctx);
		ctx->flags |= E2F_FLAG_ABORT;
		return;
	}

	should_be = s->s_frags_per_group >>
		(s->s_log_block_size - s->s_log_frag_size);		
	if (s->s_blocks_per_group != should_be) {
		pctx.blk = s->s_blocks_per_group;
		pctx.blk2 = should_be;
		fix_problem(ctx, PR_0_BLOCKS_PER_GROUP, &pctx);
		ctx->flags |= E2F_FLAG_ABORT;
		return;
	}

	should_be = (s->s_log_block_size == 0) ? 1 : 0;
	if (s->s_first_data_block != should_be) {
		pctx.blk = s->s_first_data_block;
		pctx.blk2 = should_be;
		fix_problem(ctx, PR_0_FIRST_DATA_BLOCK, &pctx);
		ctx->flags |= E2F_FLAG_ABORT;
		return;
	}

	should_be = s->s_inodes_per_group * fs->group_desc_count;
	if (s->s_inodes_count != should_be) {
		pctx.ino = s->s_inodes_count;
		pctx.ino2 = should_be;
		if (fix_problem(ctx, PR_0_INODE_COUNT_WRONG, &pctx)) {
			s->s_inodes_count = should_be;
			ext2fs_mark_super_dirty(fs);
		}
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

	clear_problem_context(&pctx);
	
#ifndef EXT2_SKIP_UUID
	/*
	 * If the UUID field isn't assigned, assign it.
	 */
	if (!(ctx->options & E2F_OPT_READONLY) && uuid_is_null(s->s_uuid)) {
		if (fix_problem(ctx, PR_0_ADD_UUID, &pctx)) {
			uuid_generate(s->s_uuid);
			ext2fs_mark_super_dirty(fs);
		}
	}
#endif
	
	/*
	 * For the Hurd, check to see if the filetype option is set,
	 * since it doesn't support it.
	 */
	if (fs->super->s_creator_os == EXT2_OS_HURD &&
	    (fs->super->s_feature_incompat &
	     EXT2_FEATURE_INCOMPAT_FILETYPE)) {
		if (fix_problem(ctx, PR_0_HURD_CLEAR_FILETYPE, &pctx)) {
			fs->super->s_feature_incompat &=
				~EXT2_FEATURE_INCOMPAT_FILETYPE;
			ext2fs_mark_super_dirty(fs);

		}
	}
	return;
}
