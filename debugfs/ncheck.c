/*
 * ncheck.c --- given a list of inodes, generate a list of names
 *
 * Copyright (C) 1994 Theodore Ts'o.  This file may be redistributed
 * under the terms of the GNU Public License.
 */

#include "config.h"
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <time.h>
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#include <sys/types.h>
#ifdef HAVE_GETOPT_H
#include <getopt.h>
#else
extern int optind;
extern char *optarg;
#endif

#include "debugfs.h"

struct inode_walk_struct {
	ext2_ino_t		dir;
	ext2_ino_t		*iarray;
	int			names_left;
	int			num_inodes;
	int			position;
	char			*parent;
	unsigned int		get_pathname_failed:1;
	unsigned int		check_dirent:1;
};

static int ncheck_proc(struct ext2_dir_entry *dirent,
		       int	offset EXT2FS_ATTR((unused)),
		       int	blocksize EXT2FS_ATTR((unused)),
		       char	*buf EXT2FS_ATTR((unused)),
		       void	*private)
{
	struct inode_walk_struct *iw = (struct inode_walk_struct *) private;
	struct ext2_inode inode;
	errcode_t	retval;
	int		filetype = ext2fs_dirent_file_type(dirent);
	int		i;

	iw->position++;
	if (iw->position <= 2)
		return 0;
	for (i=0; i < iw->num_inodes; i++) {
		if (iw->iarray[i] == dirent->inode) {
			if (!iw->parent && !iw->get_pathname_failed) {
				retval = ext2fs_get_pathname(current_fs,
							     iw->dir,
							     0, &iw->parent);
				if (retval) {
					com_err("ncheck", retval,
		"while calling ext2fs_get_pathname for inode #%u", iw->dir);
					iw->get_pathname_failed = 1;
				}
			}
			if (iw->parent)
				printf("%u\t%s/%.*s", iw->iarray[i],
				       iw->parent,
				       ext2fs_dirent_name_len(dirent),
				       dirent->name);
			else
				printf("%u\t<%u>/%.*s", iw->iarray[i],
				       iw->dir,
				       ext2fs_dirent_name_len(dirent),
				       dirent->name);
			if (iw->check_dirent && filetype) {
				if (!debugfs_read_inode(dirent->inode, &inode,
							"ncheck") &&
				    filetype != ext2_file_type(inode.i_mode)) {
					printf("  <--- BAD FILETYPE");
				}
			}
			putc('\n', stdout);
			iw->names_left--;
		}
	}
	if (!iw->names_left)
		return DIRENT_ABORT;

	return 0;
}

void do_ncheck(int argc, ss_argv_t argv, int sci_idx EXT2FS_ATTR((unused)),
	       void *infop EXT2FS_ATTR((unused)))
{
	struct inode_walk_struct iw;
	int			c, i;
	ext2_inode_scan		scan = 0;
	ext2_ino_t		ino;
	struct ext2_inode	inode;
	errcode_t		retval;
	char			*tmp;

	iw.check_dirent = 0;

	reset_getopt();
	while ((c = getopt (argc, argv, "c")) != EOF) {
		switch (c) {
		case 'c':
			iw.check_dirent = 1;
			break;
		default:
			goto print_usage;
		}
	}

	if (argc <= 1) {
	print_usage:
		com_err(argv[0], 0, "Usage: ncheck [-c] <inode number> ...");
		return;
	}
	if (check_fs_open(argv[0]))
		return;

	argc -= optind;
	argv += optind;
	iw.iarray = malloc(sizeof(ext2_ino_t) * argc);
	if (!iw.iarray) {
		com_err("ncheck", ENOMEM,
			"while allocating inode number array");
		return;
	}
	memset(iw.iarray, 0, sizeof(ext2_ino_t) * argc);

	iw.names_left = 0;
	for (i=0; i < argc; i++) {
		char *str = argv[i];
		int len = strlen(str);

		if ((len > 2) && (str[0] == '<') && (str[len - 1] == '>'))
			str++;
		iw.iarray[i] = strtol(str, &tmp, 0);
		if (*tmp && (str == argv[i] || *tmp != '>')) {
			com_err("ncheck", 0, "Invalid inode number - '%s'",
				argv[i]);
			goto error_out;
		}
		if (debugfs_read_inode(iw.iarray[i], &inode, *argv))
			goto error_out;
		if (LINUX_S_ISDIR(inode.i_mode))
			iw.names_left += 1;
		else
			iw.names_left += inode.i_links_count;
	}

	iw.num_inodes = argc;

	retval = ext2fs_open_inode_scan(current_fs, 0, &scan);
	if (retval) {
		com_err("ncheck", retval, "while opening inode scan");
		goto error_out;
	}

	do {
		retval = ext2fs_get_next_inode(scan, &ino, &inode);
	} while (retval == EXT2_ET_BAD_BLOCK_IN_INODE_TABLE);
	if (retval) {
		com_err("ncheck", retval, "while starting inode scan");
		goto error_out;
	}

	printf("Inode\tPathname\n");
	while (ino) {
		if (!inode.i_links_count)
			goto next;
		/*
		 * To handle filesystems touched by 0.3c extfs; can be
		 * removed later.
		 */
		if (inode.i_dtime)
			goto next;
		/* Ignore anything that isn't a directory */
		if (!LINUX_S_ISDIR(inode.i_mode))
			goto next;

		iw.position = 0;
		iw.parent = 0;
		iw.dir = ino;
		iw.get_pathname_failed = 0;

		retval = ext2fs_dir_iterate(current_fs, ino, 0, 0,
					    ncheck_proc, &iw);
		ext2fs_free_mem(&iw.parent);
		if (retval) {
			com_err("ncheck", retval,
				"while calling ext2_dir_iterate");
			goto next;
		}

		if (iw.names_left == 0)
			break;

	next:
		do {
			retval = ext2fs_get_next_inode(scan, &ino, &inode);
		} while (retval == EXT2_ET_BAD_BLOCK_IN_INODE_TABLE);

		if (retval) {
			com_err("ncheck", retval,
				"while doing inode scan");
			goto error_out;
		}
	}

error_out:
	free(iw.iarray);
	if (scan)
		ext2fs_close_inode_scan(scan);
	return;
}



