/*
 * block.c --- iterate over all blocks in an inode
 * 
 * Copyright (C) 1993 Theodore Ts'o.  This file may be redistributed
 * under the terms of the GNU Public License.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#if HAVE_ERRNO_H
#include <errno.h>
#endif

#include <linux/ext2_fs.h>

#include "ext2fs.h"

struct block_context {
	ext2_filsys	fs;
	int (*func)(ext2_filsys	fs,
		    blk_t	*blocknr,
		    int		bcount,
		    void	*private);
	int		bcount;
	int		bsize;
	int		flags;
	errcode_t	errcode;
	char	*ind_buf;
	char	*dind_buf;
	char	*tind_buf;
	void	*private;
};

static int block_iterate_ind(blk_t *ind_block, struct block_context *ctx)
{
	int	ret = 0, changed = 0;
	int	i, flags, limit;
	blk_t	*block_nr;

	if (!(ctx->flags & BLOCK_FLAG_DEPTH_TRAVERSE) &&
	    !(ctx->flags & BLOCK_FLAG_DATA_ONLY))
		ret = (*ctx->func)(ctx->fs, ind_block,
				   BLOCK_COUNT_IND, ctx->private);
	if (!*ind_block || (ret & BLOCK_ABORT))
		return ret;
	if (*ind_block >= ctx->fs->super->s_blocks_count ||
	    *ind_block < ctx->fs->super->s_first_data_block) {
		ctx->errcode = EXT2_ET_BAD_IND_BLOCK;
		ret |= BLOCK_ERROR;
		return ret;
	}
	ctx->errcode = io_channel_read_blk(ctx->fs->io, *ind_block,
					   1, ctx->ind_buf);
	if (ctx->errcode) {
		ret |= BLOCK_ERROR;
		return ret;
	}
	limit = ctx->fs->blocksize >> 2;
	if ((ctx->fs->flags & EXT2_FLAG_SWAP_BYTES) ||
	    (ctx->fs->flags & EXT2_FLAG_SWAP_BYTES_READ)) {
		block_nr = (blk_t *) ctx->ind_buf;
		for (i = 0; i < limit; i++, block_nr++)
			*block_nr = ext2fs_swab32(*block_nr);
	}
	block_nr = (blk_t *) ctx->ind_buf;
	if (ctx->flags & BLOCK_FLAG_APPEND) {
		for (i = 0; i < limit; i++, ctx->bcount++, block_nr++) {
			flags = (*ctx->func)(ctx->fs, block_nr, ctx->bcount,
					     ctx->private);
			changed	|= flags;
			if (flags & BLOCK_ABORT) {
				ret |= BLOCK_ABORT;
				break;
			}
		}
	} else {
		for (i = 0; i < limit; i++, ctx->bcount++, block_nr++) {
			if (*block_nr == 0)
				continue;
			flags = (*ctx->func)(ctx->fs, block_nr, ctx->bcount,
					     ctx->private);
			changed	|= flags;
			if (flags & BLOCK_ABORT) {
				ret |= BLOCK_ABORT;
				break;
			}
		}
	}
	if (changed & BLOCK_CHANGED) {
		if ((ctx->fs->flags & EXT2_FLAG_SWAP_BYTES) ||
		    (ctx->fs->flags & EXT2_FLAG_SWAP_BYTES_WRITE)) {
			block_nr = (blk_t *) ctx->ind_buf;
			for (i = 0; i < limit; i++, block_nr++)
				*block_nr = ext2fs_swab32(*block_nr);
		}
		ctx->errcode = io_channel_write_blk(ctx->fs->io, *ind_block,
						    1, ctx->ind_buf);
		if (ctx->errcode)
			ret |= BLOCK_ERROR | BLOCK_ABORT;
	}
	if ((ctx->flags & BLOCK_FLAG_DEPTH_TRAVERSE) &&
	    !(ctx->flags & BLOCK_FLAG_DATA_ONLY) &&
	    !(ret & BLOCK_ABORT))
		ret |= (*ctx->func)(ctx->fs, ind_block,
				    BLOCK_COUNT_IND, ctx->private);
	return ret;
}
	
static int block_iterate_dind(blk_t *dind_block, struct block_context *ctx)
{
	int	ret = 0, changed = 0;
	int	i, flags, limit;
	blk_t	*block_nr;

	if (!(ctx->flags & BLOCK_FLAG_DEPTH_TRAVERSE) &&
	    !(ctx->flags & BLOCK_FLAG_DATA_ONLY))
		ret = (*ctx->func)(ctx->fs, dind_block,
				   BLOCK_COUNT_DIND, ctx->private);
	if (!*dind_block || (ret & BLOCK_ABORT))
		return ret;
	if (*dind_block >= ctx->fs->super->s_blocks_count ||
	    *dind_block < ctx->fs->super->s_first_data_block) {
		ctx->errcode = EXT2_ET_BAD_DIND_BLOCK;
		ret |= BLOCK_ERROR;
		return ret;
	}
	ctx->errcode = io_channel_read_blk(ctx->fs->io, *dind_block,
					   1, ctx->dind_buf);
	if (ctx->errcode) {
		ret |= BLOCK_ERROR;
		return ret;
	}
	limit = ctx->fs->blocksize >> 2;
	if ((ctx->fs->flags & EXT2_FLAG_SWAP_BYTES) ||
	    (ctx->fs->flags & EXT2_FLAG_SWAP_BYTES_READ)) {
		block_nr = (blk_t *) ctx->dind_buf;
		for (i = 0; i < limit; i++, block_nr++)
			*block_nr = ext2fs_swab32(*block_nr);
	}
	block_nr = (blk_t *) ctx->dind_buf;
	if (ctx->flags & BLOCK_FLAG_APPEND) {
		for (i = 0; i < limit; i++, block_nr++) {
			flags = block_iterate_ind(block_nr, ctx);
			changed |= flags;
			if (flags & (BLOCK_ABORT | BLOCK_ERROR)) {
				ret |= flags & (BLOCK_ABORT | BLOCK_ERROR);
				break;
			}
		}
	} else {
		for (i = 0; i < limit; i++, block_nr++) {
			if (*block_nr == 0)
				continue;
			flags = block_iterate_ind(block_nr, ctx);
			changed |= flags;
			if (flags & (BLOCK_ABORT | BLOCK_ERROR)) {
				ret |= flags & (BLOCK_ABORT | BLOCK_ERROR);
				break;
			}
		}
	}
	if (changed & BLOCK_CHANGED) {
		if ((ctx->fs->flags & EXT2_FLAG_SWAP_BYTES) ||
		    (ctx->fs->flags & EXT2_FLAG_SWAP_BYTES_WRITE)) {
			block_nr = (blk_t *) ctx->dind_buf;
			for (i = 0; i < limit; i++, block_nr++)
				*block_nr = ext2fs_swab32(*block_nr);
		}
		ctx->errcode = io_channel_write_blk(ctx->fs->io, *dind_block,
						    1, ctx->dind_buf);
		if (ctx->errcode)
			ret |= BLOCK_ERROR | BLOCK_ABORT;
	}
	if ((ctx->flags & BLOCK_FLAG_DEPTH_TRAVERSE) &&
	    !(ctx->flags & BLOCK_FLAG_DATA_ONLY) &&
	    !(ret & BLOCK_ABORT))
		ret |= (*ctx->func)(ctx->fs, dind_block,
				    BLOCK_COUNT_DIND, ctx->private);
	return ret;
}
	
static int block_iterate_tind(blk_t *tind_block, struct block_context *ctx)
{
	int	ret = 0, changed = 0;
	int	i, flags, limit;
	blk_t	*block_nr;

	if (!(ctx->flags & BLOCK_FLAG_DEPTH_TRAVERSE) &&
	    !(ctx->flags & BLOCK_FLAG_DATA_ONLY))
		ret = (*ctx->func)(ctx->fs, tind_block,
				   BLOCK_COUNT_TIND, ctx->private);
	if (!*tind_block || (ret & BLOCK_ABORT))
		return ret;
	if (*tind_block >= ctx->fs->super->s_blocks_count ||
	    *tind_block < ctx->fs->super->s_first_data_block) {
		ctx->errcode = EXT2_ET_BAD_TIND_BLOCK;
		ret |= BLOCK_ERROR;
		return ret;
	}
	ctx->errcode = io_channel_read_blk(ctx->fs->io, *tind_block,
					   1, ctx->tind_buf);
	if (ctx->errcode) {
		ret |= BLOCK_ERROR;
		return ret;
	}
	limit = ctx->fs->blocksize >> 2;
	if ((ctx->fs->flags & EXT2_FLAG_SWAP_BYTES) ||
	    (ctx->fs->flags & EXT2_FLAG_SWAP_BYTES_READ)) {
		block_nr = (blk_t *) ctx->tind_buf;
		for (i = 0; i < limit; i++, block_nr++)
			*block_nr = ext2fs_swab32(*block_nr);
	}
	block_nr = (blk_t *) ctx->tind_buf;
	if (ctx->flags & BLOCK_FLAG_APPEND) {
		for (i = 0; i < limit; i++, block_nr++) {
			flags = block_iterate_dind(block_nr, ctx);
			changed |= flags;
			if (flags & (BLOCK_ABORT | BLOCK_ERROR)) {
				ret |= flags & (BLOCK_ABORT | BLOCK_ERROR);
				break;
			}
		}
	} else {
		for (i = 0; i < limit; i++, block_nr++) {
			if (*block_nr == 0)
				continue;
			flags = block_iterate_dind(block_nr, ctx);
			changed |= flags;
			if (flags & (BLOCK_ABORT | BLOCK_ERROR)) {
				ret |= flags & (BLOCK_ABORT | BLOCK_ERROR);
				break;
			}
		}
	}
	if (changed & BLOCK_CHANGED) {
		if ((ctx->fs->flags & EXT2_FLAG_SWAP_BYTES) ||
		    (ctx->fs->flags & EXT2_FLAG_SWAP_BYTES_WRITE)) {
			block_nr = (blk_t *) ctx->tind_buf;
			for (i = 0; i < limit; i++, block_nr++)
				*block_nr = ext2fs_swab32(*block_nr);
		}
		ctx->errcode = io_channel_write_blk(ctx->fs->io, *tind_block,
						    1, ctx->tind_buf);
		if (ctx->errcode)
			ret |= BLOCK_ERROR | BLOCK_ABORT;
	}
	if ((ctx->flags & BLOCK_FLAG_DEPTH_TRAVERSE) &&
	    !(ctx->flags & BLOCK_FLAG_DATA_ONLY) &&
	    !(ret & BLOCK_ABORT))
		ret |= (*ctx->func)(ctx->fs, tind_block,
				    BLOCK_COUNT_TIND, ctx->private);
	
	return ret;
}
	
errcode_t ext2fs_block_iterate(ext2_filsys fs,
			       ino_t	ino,
			       int	flags,
			       char *block_buf,
			       int (*func)(ext2_filsys fs,
					   blk_t	*blocknr,
					   int	blockcnt,
					   void	*private),
			       void *private)
{
	int	i;
	int	got_inode = 0;
	int	ret = 0;
	struct block_context ctx;
	blk_t	blocks[EXT2_N_BLOCKS];	/* directory data blocks */
	struct ext2_inode inode;
	errcode_t	retval;
	
	EXT2_CHECK_MAGIC(fs, EXT2_ET_MAGIC_EXT2FS_FILSYS);

	ret = ext2fs_get_blocks(fs, ino, blocks);
	if (ret)
		return ret;

	ctx.fs = fs;
	ctx.func = func;
	ctx.private = private;
	ctx.bcount = 0;
	ctx.flags = flags;
	if (block_buf) {
		ctx.ind_buf = block_buf;
	} else {
		ctx.ind_buf = malloc(fs->blocksize * 3);
		if (!ctx.ind_buf)
			return ENOMEM;
	}
	ctx.dind_buf = ctx.ind_buf + fs->blocksize;
	ctx.tind_buf = ctx.dind_buf + fs->blocksize;

	/*
	 * Iterate over the HURD translator block (if present)
	 */
	if ((fs->super->s_creator_os == EXT2_OS_HURD) &&
	    !(flags & BLOCK_FLAG_DATA_ONLY)) {
		ctx.errcode = ext2fs_read_inode(fs, ino, &inode);
		if (ctx.errcode)
			goto abort;
		got_inode = 1;
		if (inode.osd1.hurd1.h_i_translator) {
			ret |= (*func)(fs, &inode.osd1.hurd1.h_i_translator,
				       BLOCK_COUNT_TRANSLATOR, private);
			if (ret & BLOCK_ABORT)
				goto abort;
		}
	}
	
	/*
	 * Iterate over normal data blocks
	 */
	for (i = 0; i < EXT2_NDIR_BLOCKS ; i++, ctx.bcount++) {
		if (blocks[i] || (flags & BLOCK_FLAG_APPEND)) {
			ret |= (*func)(fs, &blocks[i], ctx.bcount, private);
			if (ret & BLOCK_ABORT)
				goto abort;
		}
	}
	if (*(blocks + EXT2_IND_BLOCK) || (flags & BLOCK_FLAG_APPEND)) {
		ret |= block_iterate_ind(blocks + EXT2_IND_BLOCK, &ctx);
		if (ret & BLOCK_ABORT)
			goto abort;
	}
	if (*(blocks + EXT2_DIND_BLOCK) || (flags & BLOCK_FLAG_APPEND)) {
		ret |= block_iterate_dind(blocks + EXT2_DIND_BLOCK, &ctx);
		if (ret & BLOCK_ABORT)
			goto abort;
	}
	if (*(blocks + EXT2_TIND_BLOCK) || (flags & BLOCK_FLAG_APPEND)) {
		ret |= block_iterate_tind(blocks + EXT2_TIND_BLOCK, &ctx);
		if (ret & BLOCK_ABORT)
			goto abort;
	}

abort:
	if (ret & BLOCK_CHANGED) {
		if (!got_inode) {
			retval = ext2fs_read_inode(fs, ino, &inode);
			if (retval)
				return retval;
		}
		for (i=0; i < EXT2_N_BLOCKS; i++)
			inode.i_block[i] = blocks[i];
		retval = ext2fs_write_inode(fs, ino, &inode);
		if (retval)
			return retval;
	}

	if (!block_buf)
		free(ctx.ind_buf);

	return (ret & BLOCK_ERROR) ? ctx.errcode : 0;
}
