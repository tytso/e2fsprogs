/*
 * main.c --- ext2 resizer main program
 *
 * Copyright (C) 1997 Theodore Ts'o
 * 
 * %Begin-Header%
 * All rights reserved.
 * %End-Header%
 */

#include "resize2fs.h"

#define E2FSPROGS_VERSION "1.10"
#define E2FSPROGS_DATE "27-Apr-97"

char *program_name, *device_name;

static volatile void usage (char *program_name)
{
	fprintf (stderr, "usage: %s device new-size\n", program_name);
	exit (1);
}

void main (int argc, char ** argv)
{
	errcode_t	retval;
	ext2_filsys	fs;
	int		c;
	blk_t		new_size;
	io_manager	io_ptr;

	fprintf (stderr, "resize2fs %s, %s for EXT2 FS %s, %s\n",
		 E2FSPROGS_VERSION, E2FSPROGS_DATE,
		 EXT2FS_VERSION, EXT2FS_DATE);
	if (argc && *argv)
		program_name = *argv;
	
	while ((c = getopt (argc, argv, "h")) != EOF) {
		switch (c) {
		case 'h':
			usage(program_name);
			break;
		default:
			usage (program_name);
		}
	}
	if (optind > argc - 2)
		usage (program_name);
	device_name = argv[optind++];
	new_size = atoi(argv[optind++]);
	initialize_ext2_error_table();
#if 1
	io_ptr = unix_io_manager;
#else
	io_ptr = test_io_manager;
	test_io_backing_manager = unix_io_manager;
#endif
	retval = ext2fs_open (device_name, 0, 0, 0,
			      io_ptr, &fs);
	if (retval) {
		com_err (program_name, retval, "while trying to open %s",
			 device_name);
		printf ("Couldn't find valid filesystem superblock.\n");
		exit (1);
	}
	retval = ext2fs_read_bitmaps(fs);
	if (retval) {
		com_err (program_name, retval,
			 "while trying to read the bitmaps", device_name);
		ext2fs_close (fs);
		exit (1);
	}
	resize_fs(fs, new_size);
	ext2fs_close (fs);
	exit (0);
}
