/*
 * get_pathname.c --- do directry/inode -> name translation
 * 
 * Copyright (C) 1993, 1994, 1995 Theodore Ts'o.
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 *
 * 	ext2fs_get_pathname(fs, dir, ino, name)
 *
 * 	This function translates takes two inode numbers into a
 * 	string, placing the result in <name>.  <dir> is the containing
 * 	directory inode, and <ino> is the inode number itself.  If
 * 	<ino> is zero, then ext2fs_get_pathname will return pathname
 * 	of the the directory <dir>.
 * 
 */

#include <stdio.h>
#include <string.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <stdlib.h>

#include <linux/ext2_fs.h>

#include "ext2fs.h"

struct get_pathname_struct {
	ino_t		search_ino;
	ino_t		parent;
	char		*name;
	errcode_t	errcode;
};

#ifdef __TURBOC__
#pragma argsused
#endif
static int get_pathname_proc(struct ext2_dir_entry *dirent,
			     int	offset,
			     int	blocksize,
			     char	*buf,
			     void	*private)
{
	struct get_pathname_struct	*gp;

	gp = (struct get_pathname_struct *) private;

	if ((dirent->name_len == 2) &&
	    !strncmp(dirent->name, "..", 2))
		gp->parent = dirent->inode;
	if (dirent->inode == gp->search_ino) {
		gp->name = malloc(dirent->name_len + 1);
		if (!gp->name) {
			gp->errcode = EXT2_NO_MEMORY;
			return DIRENT_ABORT;
		}
		strncpy(gp->name, dirent->name, dirent->name_len);
		gp->name[dirent->name_len] = '\0';
		return DIRENT_ABORT;
	}
	return 0;
}

static errcode_t ext2fs_get_pathname_int(ext2_filsys fs, ino_t dir, ino_t ino,
					 int maxdepth, char *buf, char **name)
{
	struct get_pathname_struct gp;
	char	*parent_name, *ret;
	errcode_t	retval;

	if (dir == ino) {
		*name = malloc(2);
		if (!*name)
			return EXT2_NO_MEMORY;
		strcpy(*name, (dir == EXT2_ROOT_INO) ? "/" : ".");
		return 0;
	}

	if (!dir || (maxdepth < 0)) {
		*name = malloc(4);
		if (!*name)
			return EXT2_NO_MEMORY;
		strcpy(*name, "...");
		return 0;
	}

	gp.search_ino = ino;
	gp.parent = 0;
	gp.name = 0;
	gp.errcode = 0;
	
	retval = ext2fs_dir_iterate(fs, dir, 0, buf, get_pathname_proc, &gp);
	if (retval)
		goto cleanup;
	if (gp.errcode) {
		retval = gp.errcode;
		goto cleanup;
	}

	retval = ext2fs_get_pathname_int(fs, gp.parent, dir, maxdepth-1,
					 buf, &parent_name);
	if (retval)
		goto cleanup;
	if (!ino) {
		*name = parent_name;
		return 0;
	}
	
	if (gp.name) 
		ret = malloc(strlen(parent_name)+strlen(gp.name)+2);
	else
		ret = malloc(strlen(parent_name)+5); /* strlen("???") + 2 */
		
	if (!ret) {
		retval = EXT2_NO_MEMORY;
		goto cleanup;
	}
	ret[0] = 0;
	if (parent_name[1])
		strcat(ret, parent_name);
	strcat(ret, "/");
	if (gp.name)
		strcat(ret, gp.name);
	else
		strcat(ret, "???");
	*name = ret;
	free(parent_name);
	retval = 0;
	
cleanup:
	if (gp.name)
		free(gp.name);
	return retval;
}

errcode_t ext2fs_get_pathname(ext2_filsys fs, ino_t dir, ino_t ino,
			      char **name)
{
	char	*buf;
	errcode_t	retval;

	EXT2_CHECK_MAGIC(fs, EXT2_ET_MAGIC_EXT2FS_FILSYS);

	buf = malloc(fs->blocksize);
	if (!buf)
		return EXT2_NO_MEMORY;
	if (dir == ino)
		ino = 0;
	retval = ext2fs_get_pathname_int(fs, dir, ino, 32, buf, name);
	free(buf);
	return retval;
	
}
