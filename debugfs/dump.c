/*
 * dump.c --- dump the contents of an inode out to a file
 * 
 * Copyright (C) 1994 Theodore Ts'o.  This file may be redistributed
 * under the terms of the GNU Public License.
 */

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
#ifdef HAVE_OPTRESET
extern int optreset;		/* defined by BSD, but not others */
#endif

#include "debugfs.h"

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

static void dump_file(char *cmdname, ino_t ino, int fd, int preserve,
		      char *outname)
{
	errcode_t retval;
	struct ext2_inode	inode;
	struct utimbuf	ut;
	char 		buf[8192];
	ext2_file_t	e2_file;
	int		nbytes;
	unsigned int	got;
	
	retval = ext2fs_read_inode(current_fs, ino, &inode);
	if (retval) {
		com_err(cmdname, retval,
			"while reading inode %u in dump_file", ino);
		return;
	}

	retval = ext2fs_file_open(current_fs, ino, 0, &e2_file);
	if (retval) {
		com_err(cmdname, retval, "while opening ext2 file");
		return;
	}
	while (1) {
		retval = ext2fs_file_read(e2_file, buf, sizeof(buf), &got);
		if (retval) 
			com_err(cmdname, retval, "while reading ext2 file");
		if (got == 0)
			break;
		nbytes = write(fd, buf, got);
		if (nbytes != got)
			com_err(cmdname, errno, "while writing file");
	}
	retval = ext2fs_file_close(e2_file);
	if (retval) {
		com_err(cmdname, retval, "while closing ext2 file");
		return;
	}
		
	if (preserve) {
#ifdef HAVE_FCHOWN
		if (fchown(fd, inode.i_uid, inode.i_gid) < 0)
			com_err("dump_file", errno,
				"while changing ownership of %s", outname);
#else
		if (chown(outname, inode.i_uid, inode.i_gid) < 0)
			com_err("dump_file", errno,
				"while changing ownership of %s", outname);
			
#endif
		if (fchmod(fd, mode_xlate(inode.i_mode)) < 0)
			com_err("dump_file", errno,
				"while setting permissions of %s", outname);
		ut.actime = inode.i_atime;
		ut.modtime = inode.i_mtime;
		close(fd);
		if (utime(outname, &ut) < 0)
			com_err("dump_file", errno,
				"while setting times on %s", outname);
	} else if (fd != 1)
		close(fd);
				    
	return;
}

void do_dump(int argc, char **argv)
{
	ino_t	inode;
	int	fd;
	int	c;
	int	preserve = 0;
	const char *dump_usage = "Usage: dump_inode [-p] <file> <output_file>";
	char	*in_fn, *out_fn;
	
	optind = 0;
#ifdef HAVE_OPTRESET
	optreset = 1;		/* Makes BSD getopt happy */
#endif
	while ((c = getopt (argc, argv, "p")) != EOF) {
		switch (c) {
		case 'p':
			preserve++;
			break;
		default:
			com_err(argv[0], 0, dump_usage);
			return;
		}
	}
	if (optind != argc-2) {
		com_err(argv[0], 0, dump_usage);
		return;
	}

	if (check_fs_open(argv[0]))
		return;

	in_fn = argv[optind];
	out_fn = argv[optind+1];

	inode = string_to_inode(in_fn);
	if (!inode) 
		return;

	fd = open(out_fn, O_CREAT | O_WRONLY | O_TRUNC, 0666);
	if (fd < 0) {
		com_err(argv[0], errno, "while opening %s for dump_inode",
			out_fn);
		return;
	}

	dump_file(argv[0], inode, fd, preserve, out_fn);

	return;
}

void do_cat(int argc, char **argv)
{
	ino_t	inode;

	if (argc != 2) {
		com_err(argv[0], 0, "Usage: cat <file>");
		return;
	}

	if (check_fs_open(argv[0]))
		return;

	inode = string_to_inode(argv[1]);
	if (!inode) 
		return;

	fflush(stdout);
	fflush(stderr);
	dump_file(argv[0], inode, 1, 0, argv[2]); 

	return;
}

