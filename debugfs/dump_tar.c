/*
 * dump_tar.c --- dump the contents of inodes to libarchive
 *
 * Copyright (C) 1994 Theodore Ts'o, (C) 2023 Johannes Schauer Marin
 * Rodrigues <josch@debian.org>, (C) 2026 Likai Liu <likai@likai.org>.
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public License.
 * %End-Header%
 *
 * Known limitations:
 *
 *  - Writing sparse file to tar is not supported by libarchive.
 *  - Extended attributes are not dumped for now (can be a todo).
 *  - Timestamps are only in seconds (better precision can be a todo).
 */

#include "archive_entry.h"
#define _LARGEFILE64_SOURCE 1
#define _GNU_SOURCE 1

#include "config.h"
#include "support/nls-enable.h"

#if (!(defined(CONFIG_DLOPEN_LIBARCHIVE) || defined(HAVE_ARCHIVE_H)) || \
     defined(CONFIG_DISABLE_LIBARCHIVE))

/* If ./configure was run with --without-libarchive, then only
 * do_rdump_tar() remains in this file and will return an error. */

void do_rdump_tar(int argc EXT2FS_ATTR((unused)),
		  ss_argv_t argv EXT2FS_ATTR((unused)),
		  int sci_idx EXT2FS_ATTR((unused)),
		  void *infop EXT2FS_ATTR((unused)))
{
	com_err("rdump_tar", 0,
		_("rdump to tar requires compiling e2fsprogs without "
		  "--without-libarchive"));
	return;
}

#else

/* If ./configure was NOT run with --without-libarchive, then build with
 * support for dlopen()-ing libarchive at runtime. This will also work even
 * if archive.h is not available at compile-time. See the comment there. */

#include "support/dl_archive_write.h"

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
	{ LINUX_S_ISUID, S_ISUID },
	{ LINUX_S_ISGID, S_ISGID },
	{ LINUX_S_ISVTX, S_ISVTX },
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

static void rdump_tar_file(const char *cmdname, ext2_ino_t ino,
			   struct archive_entry *entry, struct archive *a)
{
	errcode_t retval;
	struct ext2_inode	inode;
	char		*buf = 0;
	ext2_file_t	e2_file;
	la_ssize_t	nbytes;
	unsigned int	got, blocksize = current_fs->blocksize;

	if (debugfs_read_inode(ino, &inode, cmdname))
		return;

	retval = ext2fs_file_open(current_fs, ino, 0, &e2_file);
	if (retval) {
		com_err(cmdname, retval, _("while opening ext2 file"));
		return;
	}
	retval = ext2fs_get_mem(blocksize, &buf);
	if (retval) {
		com_err(cmdname, retval, _("while allocating memory"));
		return;
	}

	dl_archive_entry_set_filetype(entry, AE_IFREG);
	if (dl_archive_write_header(a, entry) != ARCHIVE_OK) {
		com_err(cmdname, retval, _("while writing tar header: %s"),
			dl_archive_error_string(a));
		return;
	}

	while (1) {
		retval = ext2fs_file_read(e2_file, buf, blocksize, &got);
		if (retval) {
			com_err(cmdname, retval, _("while reading ext2 file"));
			return;
		}
		if (got == 0)
			break;
		nbytes = dl_archive_write_data(a, buf, got);
		if ((unsigned) nbytes != got)
			com_err(cmdname, errno, _("while writing tar file: %s"),
				dl_archive_error_string(a));
	}
	if (buf)
		ext2fs_free_mem(&buf);
	retval = ext2fs_file_close(e2_file);
	if (retval) {
		com_err(cmdname, retval, _("while closing ext2 file"));
		return;
	}
	return;
}

static void rdump_tar_symlink(ext2_ino_t ino, struct ext2_inode *inode,
			      struct archive_entry *entry, struct archive *a)
{
	ext2_file_t e2_file;
	char *buf;
	errcode_t retval;

	buf = malloc(inode->i_size + 1);
	if (!buf) {
		com_err("rdump_tar", errno, _("while allocating for symlink"));
		goto errout;
	}

	if (ext2fs_is_fast_symlink(inode))
		strcpy(buf, (char *) inode->i_block);
	else {
		unsigned bytes = inode->i_size;
		char *p = buf;
		retval = ext2fs_file_open(current_fs, ino, 0, &e2_file);
		if (retval) {
			com_err("rdump_tar", retval, _("while opening symlink"));
			goto errout;
		}
		for (;;) {
			unsigned int got;
			retval = ext2fs_file_read(e2_file, p, bytes, &got);
			if (retval) {
				com_err("rdump_tar", retval, _("while reading symlink"));
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
			com_err("rdump_tar", retval, _("while closing symlink"));
	}

	dl_archive_entry_set_filetype(entry, AE_IFLNK);
	dl_archive_entry_copy_symlink(entry, buf);
	if (dl_archive_write_header(a, entry) != ARCHIVE_OK) {
		com_err("rdump_tar", 0, _("while writing tar header: %s"),
			dl_archive_error_string(a));
	}

errout:
	free(buf);
}

static void rdump_tar_mknod(struct archive_entry *entry, struct archive *a,
			    unsigned int filetype) {
	dl_archive_entry_set_filetype(entry, filetype);
	if (dl_archive_write_header(a, entry) != ARCHIVE_OK) {
		com_err("rdump_tar", 0, _("while writing tar header: %s"),
			dl_archive_error_string(a));
	}
}

struct rdump_tar_dirent_private {
	const char *fullname;
	struct archive *a;
};

static int rdump_tar_dirent(struct ext2_dir_entry *, int, int, char *, void *);

static void rdump_tar_inode(ext2_ino_t ino, struct ext2_inode *inode,
			    const char *name, const char *dumproot,
			    struct archive *a)
{
	char *fullname;
	struct archive_entry *entry;
	struct stat st;
	memset(&st, 0, sizeof(st));

	/* dumproot is NULL for the topmost call, and "" the next
	 * level. Do not make entries with a leading "/". */
	if (dumproot && dumproot[0]) {
		/* There are more efficient ways to do this, but this method
		 * requires only minimal debugging. */
		fullname = malloc(strlen(dumproot) + strlen(name) + 2);
		if (fullname) {
			sprintf(fullname, "%s/%s", dumproot, name);
		}
	} else {
		fullname = strdup(name);
	}
	if (!fullname) {
		com_err("rdump_tar", errno, _("while allocating memory"));
		return;
	}

	entry = dl_archive_entry_new();
	if (!entry) {
		com_err("rdump_tar", 0, _("while creating tar entry: %s"),
			dl_archive_error_string(a));
		goto errout;
	}
	dl_archive_entry_copy_pathname(entry, fullname);

	st.st_mode = mode_xlate(inode->i_mode);
	st.st_uid = inode_uid(*inode);
	st.st_gid = inode_gid(*inode);
	st.st_size = EXT2_I_SIZE(inode);
	st.st_atime = ext2fs_inode_xtime_get(inode, i_atime);
	st.st_mtime = ext2fs_inode_xtime_get(inode, i_mtime);
	st.st_ctime = ext2fs_inode_xtime_get(inode, i_ctime);
	if (LINUX_S_ISBLK(inode->i_mode) || LINUX_S_ISCHR(inode->i_mode)) {
		int major, minor;
		if (inode->i_block[0]) {
			major = (inode->i_block[0] >> 8) & 255;
			minor = inode->i_block[0] & 255;
		} else {
			major = (inode->i_block[1] & 0xfff00) >> 8;
			minor = ((inode->i_block[1] & 0xff) |
				 ((inode->i_block[1] >> 12) & 0xfff00));
		}
		st.st_rdev = makedev(major, minor);
	}
	dl_archive_entry_copy_stat(entry, &st);
	// Inhibit the LIBARCHIVE.creationtime extended header which is
	// not recognized by gnutar.
	dl_archive_entry_unset_birthtime(entry);

	if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
		/* ignore "." and ".." */
	}
	else if (LINUX_S_ISREG(inode->i_mode)) {
		rdump_tar_file("rdump_tar", ino, entry, a);
	}
	else if (LINUX_S_ISDIR(inode->i_mode)) {
		errcode_t retval;
		struct rdump_tar_dirent_private private;
		private.a = a;
		private.fullname = fullname;

		/* fullname may be a zero-length string if the user
		 * runs "rdump_tar /"; only write the header if the
		 * fullname is not zero-length. */
		if (*fullname) {
			dl_archive_entry_set_filetype(entry, AE_IFDIR);
			if (dl_archive_write_header(a, entry) != ARCHIVE_OK) {
				com_err("rdump_tar", 0,
					_("while writing tar header: %s"),
					dl_archive_error_string(a));
				goto errout;
			}
		}
		retval = ext2fs_dir_iterate(current_fs, ino, 0, 0,
					    rdump_tar_dirent, &private);
		if (retval)
			com_err("rdump_tar", retval, _("while dumping %s"), fullname);
	}
	else if (LINUX_S_ISCHR(inode->i_mode)) {
		rdump_tar_mknod(entry, a, AE_IFCHR);
	}
	else if (LINUX_S_ISBLK(inode->i_mode)) {
		rdump_tar_mknod(entry, a, AE_IFBLK);
	}
	else if (LINUX_S_ISFIFO(inode->i_mode)) {
		rdump_tar_mknod(entry, a, AE_IFIFO);
	}
	else if (LINUX_S_ISSOCK(inode->i_mode)) {
		rdump_tar_mknod(entry, a, AE_IFSOCK);
	}
	else if (LINUX_S_ISLNK(inode->i_mode)) {
		rdump_tar_symlink(ino, inode, entry, a);
	}
	else {
		com_err("rdump_tar", 0,
			_("warning: not dumping file with mode %o: %s"),
			inode->i_mode, fullname);
	}

errout:
	dl_archive_entry_free(entry);
	free(fullname);
}

static int rdump_tar_dirent(struct ext2_dir_entry *dirent,
			    int offset EXT2FS_ATTR((unused)),
			    int blocksize EXT2FS_ATTR((unused)),
			    char *buf EXT2FS_ATTR((unused)), void *p)
{
	char name[EXT2_NAME_LEN + 1];
	int thislen;
	struct rdump_tar_dirent_private *private = p;
	struct ext2_inode inode;

	thislen = ext2fs_dirent_name_len(dirent);
	strncpy(name, dirent->name, thislen);
	name[thislen] = 0;

	if (debugfs_read_inode(dirent->inode, &inode, name))
		return 0;

	rdump_tar_inode(dirent->inode, &inode, name,
			private->fullname, private->a);

	return 0;
}

void do_rdump_tar(int argc, ss_argv_t argv, int sci_idx EXT2FS_ATTR((unused)),
		  void *infop EXT2FS_ATTR((unused)))
{
	struct stat st;
	char *dest_tar;
	int i;

	int opt;
	int compression;
	const char *format;
	struct archive *a;
	int (*filter_func)(struct archive *);

	if (!libarchive_available()) {
		com_err("rdump_tar", 0,
			_("libarchive is required to write tarballs"));
		return;
	}

	compression = 0;
	format = "posix";
	reset_getopt();
	while ((opt = getopt(argc, argv, "H:JjZz")) != EOF) {
		switch (opt) {
		case '?':
			goto usage;
		case 'H':
			format = optarg;
			continue;
		default:
			compression = opt;
		}
	}
	if (optind + 2 > argc) {
		goto usage;
	}

	/* Pull out last argument */
	dest_tar = argv[argc - 1];
	argc--;

	/* Ensure last arg does not exist yet. */
	if (stat(dest_tar, &st) != -1) {
		com_err("rdump_tar", 0, _("%s already exists"), dest_tar);
		return;
	}

	a = dl_archive_write_new();
	if (a == NULL) {
		com_err("rdump_tar", 0, _("while creating archive writer"));
		return;
	}

	switch (compression) {
	case 0:
		filter_func = dl_archive_write_add_filter_none;
		break;
	case 'j':
		filter_func = dl_archive_write_add_filter_bzip2;
		break;
	case 'J':
		filter_func = dl_archive_write_add_filter_xz;
		break;
	case 'Z':
		filter_func = dl_archive_write_add_filter_compress;
		break;
	case 'z':
		filter_func = dl_archive_write_add_filter_gzip;
		break;
	default:
		filter_func = NULL;
	}
	if (filter_func == NULL) {
		com_err("rdump_tar", 0,
			_("tar compression filter for -%c is not available"),
			compression);
		goto out;
	}
	if (filter_func(a) != ARCHIVE_OK) {
		com_err("rdump_tar", 0,
			_("tar compression filter for -%c is not usable: %s"),
			compression, dl_archive_error_string(a));
		goto out;
	}

	if (dl_archive_write_set_format_by_name(a, format) != ARCHIVE_OK) {
		com_err("rdump_tar", 0, _("tar cannot set format to %s: %s"),
			format, dl_archive_error_string(a));
		goto out;
	}

	if (dl_archive_write_open_filename(a, dest_tar) != ARCHIVE_OK) {
		com_err("rdump_tar", 0, _("tar cannot open: %s: %s"),
			dest_tar, dl_archive_error_string(a));
		goto out;
	}

	for (i = optind; i < argc; i++) {
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

		rdump_tar_inode(ino, &inode, basename, NULL, a);
	}

out:
	dl_archive_write_close(a);
	dl_archive_write_free(a);
	return;

usage:
	com_err("rdump_tar", 0,
		_("Usage: dump_tar [-H bsdtar|gnutar|pax|posix|ustar|v7] [-JjyZz] <directory>... <native tar file>"));
	return;
}

#endif /* CONFIG_DISABLE_LIBARCHIVE */
