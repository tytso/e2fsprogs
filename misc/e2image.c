/*
 * e2image.c --- Program which writes an image file backing up
 * critical metadata for the filesystem.
 *
 * Copyright 2000 by Theodore Ts'o.
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 */

#include <fcntl.h>
#include <grp.h>
#ifdef HAVE_GETOPT_H
#include <getopt.h>
#else
extern char *optarg;
extern int optind;
#endif
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <linux/ext2_fs.h>

#include "ext2fs/ext2fs.h"
#include "et/com_err.h"
#include "uuid/uuid.h"
#include "e2p/e2p.h"
#include "ext2fs/e2image.h"

#include "../version.h"
#include "nls-enable.h"

const char * program_name = "e2image";
char * device_name = NULL;

static void usage(void)
{
	fprintf(stderr, _("Usage: %s device file\n"), program_name);
	exit (1);
}

static const char *please_fsck = N_("Please run e2fsck on the filesystem.\n");

static void write_header(int fd, struct ext2_image_hdr *hdr)
{
	char header_buf[4096];
	int actual;

	if (lseek(fd, 0, SEEK_SET) < 0) {
		perror("lseek while writing header");
		exit(1);
	}
	memset(header_buf, 0, sizeof(header_buf));
	
	if (hdr)
		memcpy(header_buf, hdr, sizeof(struct ext2_image_hdr));
	
	actual = write(fd, header_buf, sizeof(header_buf));
	if (actual < 0) {
		perror("write header");
		exit(1);
	}
	if (actual != sizeof(header_buf)) {
		fprintf(stderr, _("short write (only %d bytes) for"
				  "writing image header"), actual);
		exit(1);
	}
}


int main (int argc, char ** argv)
{
	int c;
	char * tmp;
	errcode_t retval;
	ext2_filsys fs;
	int open_flag = 0;
	int raw_flag = 0;
	int fd = 0;
	char *features_cmd = 0;
	struct ext2_image_hdr hdr;

#ifdef ENABLE_NLS
	setlocale(LC_MESSAGES, "");
	bindtextdomain(NLS_CAT_NAME, LOCALEDIR);
	textdomain(NLS_CAT_NAME);
#endif
	fprintf (stderr, _("e2image %s, %s for EXT2 FS %s, %s\n"),
		 E2FSPROGS_VERSION, E2FSPROGS_DATE,
		 EXT2FS_VERSION, EXT2FS_DATE);
	if (argc && *argv)
		program_name = *argv;
	initialize_ext2_error_table();
	while ((c = getopt (argc, argv, "r")) != EOF)
		switch (c) {
		case 'r':
			raw_flag++;
			break;
		default:
			usage();
		}
	if (optind != argc - 2 )
		usage();
	device_name = argv[optind];
	retval = ext2fs_open (device_name, open_flag, 0, 0,
			      unix_io_manager, &fs);
        if (retval) {
		com_err (program_name, retval, _("while trying to open %s"),
			 device_name);
		printf(_("Couldn't find valid filesystem superblock.\n"));
		exit(1);
	}

	fd = open(argv[optind+1], O_CREAT|O_TRUNC|O_RDWR, 0600);
	if (fd < 0) {
		com_err(program_name, errno, _("while trying to open %s"),
			argv[optind+1]);
		exit(1);
	}

	write_header(fd, NULL);
	memset(&hdr, 0, sizeof(struct ext2_image_hdr));
	
	hdr.offset_super = lseek(fd, 0, SEEK_CUR);
	retval = ext2fs_image_super_write(fs, fd, 0);
	if (retval) {
		com_err(program_name, retval, _("while writing superblock"));
		exit(1);
	}
	
	hdr.offset_inode = lseek(fd, 0, SEEK_CUR);
	retval = ext2fs_image_inode_write(fs, fd, IMAGER_FLAG_SPARSEWRITE);
	if (retval) {
		com_err(program_name, retval, _("while writing inode table"));
		exit(1);
	}
	
	hdr.offset_blockmap = lseek(fd, 0, SEEK_CUR);
	retval = ext2fs_image_bitmap_write(fs, fd, 0);
	if (retval) {
		com_err(program_name, retval, _("while writing block bitmap"));
		exit(1);
	}

	hdr.offset_inodemap = lseek(fd, 0, SEEK_CUR);
	retval = ext2fs_image_bitmap_write(fs, fd, IMAGER_FLAG_INODEMAP);
	if (retval) {
		com_err(program_name, retval, _("while writing inode bitmap"));
		exit(1);
	}
	
	ext2fs_close (fs);
	exit (0);
}
