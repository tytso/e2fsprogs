/*
 * namei.c --- ext2fs directory lookup operations
 * 
 * Copyright (C) 1993, 1994 Theodore Ts'o.  This file may be
 * redistributed under the terms of the GNU Public License.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <linux/fs.h>
#include <linux/ext2_fs.h>

#include "ext2fs.h"

struct dir_context {
	ino_t		dir;
	int		flags;
	char		*buf;
	int (*func)(struct ext2_dir_entry *dirent,
		    int	offset,
		    int	blocksize,
		    char	*buf,
		    void	*private);
	void		*private;
	errcode_t	errcode;
};

static int process_dir_block(ext2_filsys fs,
			     blk_t	*blocknr,
			     int	blockcnt,
			     void	*private);

errcode_t ext2fs_dir_iterate(ext2_filsys fs,
			     ino_t dir,
			     int flags,
			     char *block_buf,
			     int (*func)(struct ext2_dir_entry *dirent,
					 int	offset,
					 int	blocksize,
					 char	*buf,
					 void	*private),
			     void *private)
{
	struct		dir_context	ctx;
	errcode_t	retval;
	
	retval = ext2fs_check_directory(fs, dir);
	if (retval)
		return retval;
	
	ctx.dir = dir;
	ctx.flags = flags;
	if (block_buf)
		ctx.buf = block_buf;
	else {
		ctx.buf = malloc(fs->blocksize);
		if (!ctx.buf)
			return ENOMEM;
	}
	ctx.func = func;
	ctx.private = private;
	ctx.errcode = 0;
	retval = ext2fs_block_iterate(fs, dir, 0, 0, process_dir_block, &ctx);
	if (!block_buf)
		free(ctx.buf);
	if (retval)
		return retval;
	return ctx.errcode;
}

static int process_dir_block(ext2_filsys  fs,
			     blk_t	*blocknr,
			     int	blockcnt,
			     void	*private)
{
	struct dir_context *ctx = (struct dir_context *) private;
	int		offset = 0;
	int		ret;
	int		changed = 0;
	int		do_abort = 0;
	struct ext2_dir_entry *dirent;

	if (blockcnt < 0)
		return 0;

	ctx->errcode = io_channel_read_blk(fs->io, *blocknr, 1, ctx->buf);
	if (ctx->errcode)
		return BLOCK_ABORT;
	
	while (offset < fs->blocksize) {
		dirent = (struct ext2_dir_entry *) (ctx->buf + offset);
		if (!dirent->inode &&
		    !(ctx->flags & DIRENT_FLAG_INCLUDE_EMPTY))
			goto next;

		ret = (ctx->func)(dirent, offset, fs->blocksize,
				  ctx->buf, ctx->private);
		if (ret & DIRENT_CHANGED)
			changed++;
		if (ret & DIRENT_ABORT) {
			do_abort++;
			break;
		}
next:		
		if (((offset + dirent->rec_len) > fs->blocksize) ||
		    (dirent->rec_len < 8) ||
		    ((dirent->name_len+8) > dirent->rec_len)) {
			ctx->errcode = EXT2_ET_DIR_CORRUPTED;
			return BLOCK_ABORT;
		}
		offset += dirent->rec_len;
	}

	if (changed) {
		ctx->errcode = io_channel_write_blk(fs->io, *blocknr, 1,
						    ctx->buf);
		if (ctx->errcode)
			return BLOCK_ABORT;
	}
	if (do_abort)
		return BLOCK_ABORT;
	return 0;
}

struct lookup_struct  {
	const char	*name;
	int		len;
	ino_t		*inode;
	int		found;
};	

static int lookup_proc(struct ext2_dir_entry *dirent,
		       int	offset,
		       int	blocksize,
		       char	*buf,
		       void	*private)
{
	struct lookup_struct *ls = (struct lookup_struct *) private;

	if (ls->len != dirent->name_len)
		return 0;
	if (strncmp(ls->name, dirent->name, dirent->name_len))
		return 0;
	*ls->inode = dirent->inode;
	ls->found++;
	return DIRENT_ABORT;
}


errcode_t ext2fs_lookup(ext2_filsys fs, ino_t dir, const char *name,
			int namelen, char *buf, ino_t *inode)
{
	errcode_t	retval;
	struct lookup_struct ls;

	ls.name = name;
	ls.len = namelen;
	ls.inode = inode;
	ls.found = 0;

	retval = ext2fs_dir_iterate(fs, dir, 0, buf, lookup_proc, &ls);
	if (retval)
		return retval;

	return (ls.found) ? 0 : ENOENT;
}

errcode_t ext2fs_namei(ext2_filsys fs, ino_t root, ino_t cwd, const char *name,
		       ino_t *inode)
{
	ino_t		dir = cwd;
	char		*buf;
	const char	*p = name, *q;
	int		len;
	errcode_t	retval;

	buf = malloc(fs->blocksize);
	if (!buf)
		return ENOMEM;
	if (*p == '/') {
		p++;
		dir = root;
	}
	while (*p) {
		q = strchr(p, '/');
		if (q)
			len = q - p;
		else
			len = strlen(p);
		if (len) {
			retval = ext2fs_lookup(fs, dir, p, len, buf, &dir);
			if (retval) {
				free(buf);
				return retval;
			}
		}
		if (q)
			p = q+1;
		else
			break;
	}
	*inode = dir;
	free(buf);
	return 0;
}
