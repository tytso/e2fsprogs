/*
 * dump.c --- dump the contents of an inode out to a file
 *
 * Copyright (C) 1994 Theodore Ts'o.  This file may be redistributed
 * under the terms of the GNU Public License.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE /* for O_LARGEFILE */
#endif

#include "config.h"
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <time.h>
#include <utime.h>
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <utime.h>
#ifdef HAVE_GETOPT_H
#include <getopt.h>
#else
extern int optind;
extern char *optarg;
#endif

#include "debugfs.h"

#ifndef O_LARGEFILE
#define O_LARGEFILE 0
#endif

/*
 * The mode_xlate function translates a linux mode into a native-OS mode_t.
 */
static struct {
	__u16 lmask;
	mode_t mask;
} mode_table[] = {
	{ LINUX_S_IRUSR, S_IRUSR },
	{ LINUX_S_IWUSR, S_IWUSR },
	{ LINUX_S_IXUSR, S_IXUSR },
	{ LINUX_S_IRGRP, S_IRGRP },
	{ LINUX_S_IWGRP, S_IWGRP },
	{ LINUX_S_IXGRP, S_IXGRP },
	{ LINUX_S_IROTH, S_IROTH },
	{ LINUX_S_IWOTH, S_IWOTH },
	{ LINUX_S_IXOTH, S_IXOTH },
	{ 0, 0 }
};

static mode_t mode_xlate(__u16 lmode)
{
	mode_t	mode = 0;
	int	i;

	for (i=0; mode_table[i].lmask; i++) {
		if (lmode & mode_table[i].lmask)
			mode |= mode_table[i].mask;
	}
	return mode;
}

static void fix_attrs(const char *cmd, const struct ext2_inode *inode,
		      int fd, const char *name, int preserve)
{
	int i;

#ifndef HAVE_UTIMENSAT
	i = utime(name, &((struct utimbuf) {
			.actime = inode->i_atime,
			.modtime = inode->i_mtime
		}));
#else
	i = utimensat(AT_FDCWD, name, (struct timespec []) {
			[0] = { .tv_sec = inode->i_atime },
			[1] = { .tv_sec = inode->i_mtime }
		}, AT_SYMLINK_NOFOLLOW);
#endif
	if (i == -1)
		com_err(cmd, errno, "while setting times of %s", name);

	if (!S_ISLNK(inode->i_mode)) {
		if (fd != -1) {
			if (preserve)
				i = fchmod(fd, mode_xlate(inode->i_mode));
			else
				i = fchmod(fd, mode_xlate(inode->i_mode) & ~umask(0));
		} else {
			if (preserve)
				i = chmod(name, mode_xlate(inode->i_mode));
			else
				i = chmod(name, mode_xlate(inode->i_mode) & ~umask(0));
		}
		if (i == -1)
			com_err(cmd, errno, "while setting permissions of %s", name);
	}

	if (preserve) {
#ifndef HAVE_FCHOWN
		i = lchown(name, inode_uid(*inode), inode_gid(*inode));
#else
		if (fd != -1)
			i = fchown(fd, inode_uid(*inode), inode_gid(*inode));
		else
			i = lchown(name, inode_uid(*inode), inode_gid(*inode));
#endif
		if (i == -1)
			com_err(cmd, errno, "while changing ownership of %s", name);
	}
}

static void dump_file(const char *cmdname, ext2_ino_t ino, int fd,
		      char *outname, int preserve)
{
	errcode_t retval;
	struct ext2_inode inode;
	char *buf = 0;
	ext2_file_t e2_file;
	int nbytes;
	unsigned int got, blocksize = current_fs->blocksize;

	if (debugfs_read_inode(ino, &inode, cmdname))
		return;

	retval = ext2fs_file_open(current_fs, ino, 0, &e2_file);
	if (retval) {
		com_err(cmdname, retval, "while opening ext2 file");
		return;
	}
	retval = ext2fs_get_mem(blocksize, &buf);
	if (retval) {
		com_err(cmdname, retval, "while allocating memory");
		return;
	}
	while (1) {
		retval = ext2fs_file_read(e2_file, buf, blocksize, &got);
		if (retval) {
			com_err(cmdname, retval, "while reading ext2 file");
			return;
		}
		if (got == 0)
			break;
		nbytes = write(fd, buf, got);
		if ((unsigned) nbytes != got)
			com_err(cmdname, errno, "while writing file");
	}
	if (buf)
		ext2fs_free_mem(&buf);
	retval = ext2fs_file_close(e2_file);
	if (retval) {
		com_err(cmdname, retval, "while closing ext2 file");
		return;
	}

	/* Don't touch attributes if fd is stdout (for cat command) */
	if (fd != 1)
		fix_attrs("dump_file", &inode, fd, outname, preserve);

	return;
}

void do_dump(int argc, ss_argv_t argv, int sci_idx EXT2FS_ATTR((unused)),
	     void *infop EXT2FS_ATTR((unused)))
{
	ext2_ino_t inode;
	int fd;
	int c;
	int preserve = 0;
	char *in_fn, *out_fn;

	reset_getopt();
	while ((c = getopt (argc, argv, "p")) != EOF) {
		switch (c) {
		case 'p':
			preserve++;
			break;
		default:
		print_usage:
			com_err(argv[0], 0, "Usage: dump_inode [-p] "
				"<file> <output_file>");
			return;
		}
	}
	if (optind != argc-2)
		goto print_usage;

	if (check_fs_open(argv[0]))
		return;

	in_fn = argv[optind];
	out_fn = argv[optind+1];

	inode = string_to_inode(in_fn);
	if (!inode)
		return;

	fd = open(out_fn, O_CREAT | O_WRONLY | O_TRUNC | O_LARGEFILE, 0666);
	if (fd < 0) {
		com_err(argv[0], errno, "while opening %s for dump_inode",
			out_fn);
		return;
	}

	dump_file(argv[0], inode, fd, out_fn, preserve);
	if (close(fd) != 0) {
		com_err(argv[0], errno, "while closing %s for dump_inode",
			out_fn);
		return;
	}

	return;
}

static void rdump_symlink(ext2_ino_t ino, struct ext2_inode *inode,
			  const char *fullname, int preserve)
{
	ext2_file_t e2_file;
	char *buf;
	errcode_t retval;

	buf = malloc(inode->i_size + 1);
	if (!buf) {
		com_err("rdump", errno, "while allocating for symlink");
		goto errout;
	}

	if (ext2fs_is_fast_symlink(inode))
		strcpy(buf, (char *) inode->i_block);
	else {
		unsigned bytes = inode->i_size;
		char *p = buf;
		retval = ext2fs_file_open(current_fs, ino, 0, &e2_file);
		if (retval) {
			com_err("rdump", retval, "while opening symlink");
			goto errout;
		}
		for (;;) {
			unsigned int got;
			retval = ext2fs_file_read(e2_file, p, bytes, &got);
			if (retval) {
				com_err("rdump", retval, "while reading symlink");
				goto errout;
			}
			bytes -= got;
			p += got;
			if (got == 0 || bytes == 0)
				break;
		}
		buf[inode->i_size] = 0;
		retval = ext2fs_file_close(e2_file);
		if (retval)
			com_err("rdump", retval, "while closing symlink");
	}

	if (symlink(buf, fullname) == -1) {
		com_err("rdump", errno, "while creating symlink %s -> %s", buf, fullname);
		goto errout;
	}

	fix_attrs("rdump_symlink", inode, -1, fullname, preserve);

errout:
	free(buf);
}

struct rdump_dirctx {
	const char *dumproot;
	int preserve;
};

static int rdump_dirent(struct ext2_dir_entry *, int, int, char *, void *);

static void rdump_inode(ext2_ino_t ino, struct ext2_inode *inode,
			const char *name, const char *dumproot, int preserve)
{
	char *fullname;

	/* There are more efficient ways to do this, but this method
	 * requires only minimal debugging. */
	fullname = malloc(strlen(dumproot) + strlen(name) + 2);
	if (!fullname) {
		com_err("rdump", errno, "while allocating memory");
		return;
	}
	sprintf(fullname, "%s/%s", dumproot, name);

	if (LINUX_S_ISLNK(inode->i_mode))
		rdump_symlink(ino, inode, fullname, preserve);
	else if (LINUX_S_ISREG(inode->i_mode)) {
		int fd;
		fd = open(fullname, O_WRONLY | O_CREAT | O_TRUNC | O_LARGEFILE, S_IRWXU);
		if (fd == -1) {
			com_err("rdump", errno, "while opening %s", fullname);
			goto errout;
		}
		dump_file("rdump", ino, fd, fullname, preserve);
		if (close(fd) != 0) {
			com_err("rdump", errno, "while closing %s", fullname);
			goto errout;
		}
	}
	else if (LINUX_S_ISDIR(inode->i_mode) && strcmp(name, ".") && strcmp(name, "..")) {
		errcode_t retval;
		struct rdump_dirctx ctx = {
			.dumproot = fullname,
			.preserve = preserve
		};

		/* Create the directory with 0700 permissions, because we
		 * expect to have to create entries it.  Then fix its perms
		 * once we've done the traversal. */
		if (name[0] && mkdir(fullname, S_IRWXU) == -1) {
			com_err("rdump", errno, "while making directory %s", fullname);
			goto errout;
		}

		retval = ext2fs_dir_iterate(current_fs, ino, 0, 0,
					    rdump_dirent, (void *) &ctx);
		if (retval)
			com_err("rdump", retval, "while dumping %s", fullname);

		fix_attrs("rdump", inode, -1, fullname, preserve);
	}
	/* else do nothing (don't dump device files, sockets, fifos, etc.) */

errout:
	free(fullname);
}

static int rdump_dirent(struct ext2_dir_entry *dirent,
			int offset EXT2FS_ATTR((unused)),
			int blocksize EXT2FS_ATTR((unused)),
			char *buf EXT2FS_ATTR((unused)), void *private)
{
	char name[EXT2_NAME_LEN + 1];
	int thislen;
	struct rdump_dirctx *ctx = private;
	const char *dumproot = ctx->dumproot;
	int preserve = ctx->preserve;
	struct ext2_inode inode;

	thislen = ext2fs_dirent_name_len(dirent);
	strncpy(name, dirent->name, thislen);
	name[thislen] = 0;

	if (debugfs_read_inode(dirent->inode, &inode, name))
		return 0;

	rdump_inode(dirent->inode, &inode, name, dumproot, preserve);

	return 0;
}

void do_rdump(int argc, ss_argv_t argv, int sci_idx EXT2FS_ATTR((unused)),
	      void *infop EXT2FS_ATTR((unused)))
{
	struct stat st;
	char *dest_dir;
	int preserve = 0;
	int c;
	int i;

	reset_getopt();
	while ((c = getopt (argc, argv, "p")) != EOF) {
		switch (c) {
		case 'p':
			preserve++;
			break;
		default:
		print_usage:
			com_err(argv[0], 0, "Usage: rdump [-p] "
				"<directory>... <native directory>");
			return;
		}
	}

	argc -= optind;
	argv += optind;
	if (common_args_process(argc, argv, 2, INT_MAX, "rdump",
				"[-p] <directory>... <native directory>", 0))
		return;

	/* Pull out last argument */
	dest_dir = argv[argc - 1];
	argc--;

	/* Ensure last arg is a directory. */
	if (stat(dest_dir, &st) == -1) {
		com_err("rdump", errno, "while statting %s", dest_dir);
		return;
	}
	if (!S_ISDIR(st.st_mode)) {
		com_err("rdump", 0, "%s is not a directory", dest_dir);
		return;
	}

	for (i = 0; i < argc; i++) {
		char *arg = argv[i], *basename;
		struct ext2_inode inode;
		ext2_ino_t ino = string_to_inode(arg);
		if (!ino)
			continue;

		if (debugfs_read_inode(ino, &inode, arg))
			continue;

		basename = strrchr(arg, '/');
		if (basename)
			basename++;
		else
			basename = arg;

		rdump_inode(ino, &inode, basename, dest_dir, preserve);
	}
}

void do_cat(int argc, ss_argv_t argv, int sci_idx EXT2FS_ATTR((unused)),
	    void *infop EXT2FS_ATTR((unused)))
{
	ext2_ino_t inode;

	if (common_inode_args_process(argc, argv, &inode, 0))
		return;

	fflush(stdout);
	fflush(stderr);
	dump_file(argv[0], inode, 1, argv[2], 0);

	return;
}

