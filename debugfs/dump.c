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

#include "debugfs.h"

struct dump_block_struct {
	int		fd;
	char		*buf;
	errcode_t	errcode;
};

static int dump_block(ext2_filsys fs, blk_t *blocknr, int blockcnt,
		      void *private)
{
	ssize_t nbytes;
	
	struct dump_block_struct *rec = (struct dump_block_struct *) private;
	
	if (blockcnt < 0)
		return 0;

	if (*blocknr) {
		rec->errcode = io_channel_read_blk(fs->io, *blocknr,
						   1, rec->buf);
		if (rec->errcode)
			return BLOCK_ABORT;
	} else
		memset(rec->buf, 0, fs->blocksize);

retry_write:
	nbytes = write(rec->fd, rec->buf, fs->blocksize);
	if (nbytes == -1) {
		if (errno == EINTR)
			goto retry_write;
		rec->errcode = errno;
		return BLOCK_ABORT;
	}
	if (nbytes != fs->blocksize) {
		/* XXX not quite right, but good enough */
		rec->errcode = EXT2_ET_SHORT_WRITE;
		return BLOCK_ABORT;
	}
	return 0;
}

static void dump_file(char *cmdname, ino_t inode, int fd, char *outname)
{
	errcode_t retval;
	struct dump_block_struct rec;

	rec.fd = fd;
	rec.errcode = 0;
	rec.buf = malloc(fs->blocksize);

	if (rec.buf == 0) {
		com_err(cmdname, ENOMEM, "while allocating block buffer for dump_inode");
		return;
	}
	
	retval = ext2fs_block_iterate(fs, inode, 0, NULL,
				      dump_block, &rec);
	if (retval) {
		com_err(cmdname, retval, "while iterating over blocks in %s",
			outname);
		goto cleanup;
	}
	if (rec.errcode) {
		com_err(cmdname, retval, "in dump_block while dumping %s",
			outname);
		goto cleanup;
	}
	
cleanup:
	free(rec.buf);
	return;
}

void do_dump(int argc, char **argv)
{
	ino_t	inode;
	int	fd;

	if (argc != 3) {
		com_err(argv[0], 0, "Usage: dump_inode <file> <output_file>");
		return;
	}

	if (check_fs_open(argv[0]))
		return;

	inode = string_to_inode(argv[1]);
	if (!inode) 
		return;

	fd = open(argv[2], O_CREAT | O_WRONLY | O_TRUNC, 0666);
	if (fd < 0) {
		com_err(argv[0], errno, "while opening %s for dump_inode",
			argv[2]);
		return;
	}

	dump_file(argv[0], inode, fd, argv[2]);

	close(fd);
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

	dump_file(argv[0], inode, 0, argv[2]);

	return;
}

