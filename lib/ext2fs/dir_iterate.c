/*
 * dir_iterate.c --- ext2fs directory iteration operations
 * 
 * Copyright (C) 1993, 1994, 1994, 1995, 1996, 1997 Theodore Ts'o.
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 */

#include <stdio.h>
#include <string.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#if HAVE_ERRNO_H
#include <errno.h>
#endif

#include "ext2_fs.h"
#include "ext2fsP.h"

errcode_t ext2fs_dir_iterate(ext2_filsys fs,
			     ext2_ino_t dir,
			     int flags,
			     char *block_buf,
			     int (*func)(struct ext2_dir_entry *dirent,
					 int	offset,
					 int	blocksize,
					 char	*buf,
					 void	*priv_data),
			     void *priv_data)
{
	struct		dir_context	ctx;
	errcode_t	retval;
	
	EXT2_CHECK_MAGIC(fs, EXT2_ET_MAGIC_EXT2FS_FILSYS);

	retval = ext2fs_check_directory(fs, dir);
	if (retval)
		return retval;
	
	ctx.dir = dir;
	ctx.flags = flags;
	if (block_buf)
		ctx.buf = block_buf;
	else {
		retval = ext2fs_get_mem(fs->blocksize, (void **) &ctx.buf);
		if (retval)
			return retval;
	}
	ctx.func = func;
	ctx.func2 = 0;
	ctx.priv_data = priv_data;
	ctx.errcode = 0;
	retval = ext2fs_block_iterate2(fs, dir, 0, 0,
				       ext2fs_process_dir_block, &ctx);
	if (!block_buf)
		ext2fs_free_mem((void **) &ctx.buf);
	if (retval)
		return retval;
	return ctx.errcode;
}

/*
 * Helper function which is private to this module.  Used by
 * ext2fs_dir_iterate() and ext2fs_dblist_dir_iterate()
 */
int ext2fs_process_dir_block(ext2_filsys  	fs,
			     blk_t		*blocknr,
			     e2_blkcnt_t		blockcnt,
			     blk_t		ref_block,
			     int			ref_offset,
			     void		*priv_data)
{
	struct dir_context *ctx = (struct dir_context *) priv_data;
	int		offset = 0;
	int		ret = 0;
	int		changed = 0;
	int		do_abort = 0;
	int		entry;
	struct ext2_dir_entry *dirent;

	if (blockcnt < 0)
		return 0;

	entry = blockcnt ? DIRENT_OTHER_FILE : DIRENT_DOT_FILE;
	
	ctx->errcode = ext2fs_read_dir_block(fs, *blocknr, ctx->buf);
	if (ctx->errcode)
		return BLOCK_ABORT;

	while (offset < fs->blocksize) {
		dirent = (struct ext2_dir_entry *) (ctx->buf + offset);
		if (((offset + dirent->rec_len) > fs->blocksize) ||
		    (dirent->rec_len < 8) ||
		    ((dirent->rec_len % 4) != 0) ||
		    (((dirent->name_len & 0xFF)+8) > dirent->rec_len)) {
			ctx->errcode = EXT2_ET_DIR_CORRUPTED;
			return BLOCK_ABORT;
		}
		if (!dirent->inode &&
		    !(ctx->flags & DIRENT_FLAG_INCLUDE_EMPTY))
			goto next;

		if (ctx->func)
			ret = (ctx->func)(dirent, offset, fs->blocksize,
					  ctx->buf, ctx->priv_data);
		else if (ctx->func2) {
			ret = (ctx->func2)(ctx->dir, entry, dirent, offset,
					   fs->blocksize, ctx->buf,
					   ctx->priv_data);
			if (entry < DIRENT_OTHER_FILE)
				entry++;
		}
			
		if (ret & DIRENT_CHANGED)
			changed++;
		if (ret & DIRENT_ABORT) {
			do_abort++;
			break;
		}
next:		
		offset += dirent->rec_len;
	}

	if (changed) {
		ctx->errcode = ext2fs_write_dir_block(fs, *blocknr, ctx->buf);
		if (ctx->errcode)
			return BLOCK_ABORT;
	}
	if (do_abort)
		return BLOCK_ABORT;
	return 0;
}

