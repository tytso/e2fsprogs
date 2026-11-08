/*
 * unlink.c --- delete links in a ext2fs directory
 *
 * Copyright (C) 1993, 1994, 1997 Theodore Ts'o.
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Library
 * General Public License, version 2.
 * %End-Header%
 */

#include "config.h"
#include <stdio.h>
#include <string.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "ext2_fs.h"
#include "ext2fs.h"

struct link_struct  {
	ext2_filsys	fs;
	const char	*name;
	int		namelen;
	ext2_ino_t	inode;
	int		flags;
	struct ext2_dir_entry *prev;
	int		done;
	errcode_t	err;
};

#ifdef __TURBOC__
 #pragma argsused
#endif
static int unlink_proc(struct ext2_dir_entry *dirent,
		     int	offset,
		     int	blocksize EXT2FS_ATTR((unused)),
		     char	*buf EXT2FS_ATTR((unused)),
		     void	*priv_data)
{
	struct link_struct *ls = (struct link_struct *) priv_data;
	struct ext2_dir_entry *prev;

	prev = ls->prev;
	ls->prev = dirent;

	if (ls->name) {
		if (ext2fs_dirent_name_len(dirent) != ls->namelen)
			return 0;
		if (strncmp(ls->name, dirent->name, ext2fs_dirent_name_len(dirent)))
			return 0;
	}
	if (!(ls->flags & EXT2FS_UNLINK_FORCE) && ls->inode) {
		if (dirent->inode != ls->inode)
			return 0;
	} else {
		if (!dirent->inode)
			return 0;
	}

	if (offset) {
		unsigned int prev_len, cur_len;

		ls->err = ext2fs_get_rec_len(ls->fs, prev, &prev_len);
		if (ls->err)
			return DIRENT_ABORT;
		ls->err = ext2fs_get_rec_len(ls->fs, dirent, &cur_len);
		if (ls->err)
			return DIRENT_ABORT;
		ls->err = ext2fs_set_rec_len(ls->fs, prev_len + cur_len, prev);
		if (ls->err)
			return DIRENT_ABORT;
	} else
		dirent->inode = 0;
	ls->done++;
	return DIRENT_ABORT|DIRENT_CHANGED;
}

#ifdef __TURBOC__
 #pragma argsused
#endif
errcode_t ext2fs_unlink(ext2_filsys fs, ext2_ino_t dir,
			const char *name, ext2_ino_t ino,
			int flags)
{
	errcode_t	retval;
	struct link_struct ls;

	EXT2_CHECK_MAGIC(fs, EXT2_ET_MAGIC_EXT2FS_FILSYS);

	if (!name && !ino)
		return EXT2_ET_INVALID_ARGUMENT;

	if (!(fs->flags & EXT2_FLAG_RW))
		return EXT2_ET_RO_FILSYS;

	ls.fs = fs;
	ls.name = name;
	ls.namelen = name ? strlen(name) : 0;
	ls.inode = ino;
	ls.flags = flags;
	ls.done = 0;
	ls.prev = 0;
	ls.err = 0;

	retval = ext2fs_dir_iterate(fs, dir, DIRENT_FLAG_INCLUDE_EMPTY,
				    0, unlink_proc, &ls);
	if (retval)
		return retval;
	if (ls.err)
		return ls.err;

	return (ls.done) ? 0 : EXT2_ET_DIR_NO_SPACE;
}

