/*
 * main.c --- ext2 resizer main program
 *
 * Copyright (C) 1997 Theodore Ts'o
 * 
 * %Begin-Header%
 * All rights reserved.
 * %End-Header%
 */

#ifdef HAVE_GETOPT_H
#include <getopt.h>
#endif
#include <fcntl.h>
#include <sys/ioctl.h>


#include "resize2fs.h"

#define E2FSPROGS_VERSION "1.10"
#define E2FSPROGS_DATE "27-Apr-97"

char *program_name, *device_name;

static volatile void usage (char *prog)
{
	fprintf (stderr, "usage: %s [-d debug_flags] [-p] [-F] device new-size\n", prog);
	exit (1);
}

static void resize_progress_func(ext2_resize_t rfs, int pass,
				 unsigned long cur, unsigned long max)
{
	ext2_sim_progmeter progress;
	const char	*label;
	errcode_t	retval;

	progress = (ext2_sim_progmeter) rfs->prog_data;
	if (cur == 0) {
		if (progress)
			ext2fs_progress_close(progress);
		progress = 0;
		switch (pass) {
		case E2_RSZ_ADJUST_SUPERBLOCK_PASS:
			label = "Initializing inode table";
			break;
		case E2_RSZ_BLOCK_RELOC_PASS:
			label = "Relocating blocks";
			break;
		case E2_RSZ_BLOCK_REF_UPD_PASS:
			label = "Updating block references";
			break;
		case E2_RSZ_INODE_FIND_DIR_PASS:
			label = "Finding directories";
			break;
		case E2_RSZ_INODE_RELOC_PASS:
			label = "Moving inodes";
			break;
		case E2_RSZ_INODE_REF_UPD_PASS:
			label = "Updating inode references";
			break;
		case E2_RSZ_MOVE_ITABLE_PASS:
			label = "Moving inode table";
			break;
		}
		printf("Begin pass %d (max = %lu)\n", pass, max);
		retval = ext2fs_progress_init(&progress, label, 30,
					      40, max, 0);
		if (retval)
			progress = 0;
		rfs->prog_data = (void *) progress;
	}
	if (progress)
		ext2fs_progress_update(progress, cur);
	if (cur >= max) {
		if (progress)
			ext2fs_progress_close(progress);
		progress = 0;
		rfs->prog_data = 0;
	}
}

void main (int argc, char ** argv)
{
	errcode_t	retval;
	ext2_filsys	fs;
	int		c;
	int		flags = 0;
	int		flush = 0;
	int		fd;
	blk_t		new_size;
	io_manager	io_ptr;

	fprintf (stderr, "resize2fs %s, %s for EXT2 FS %s, %s\n",
		 E2FSPROGS_VERSION, E2FSPROGS_DATE,
		 EXT2FS_VERSION, EXT2FS_DATE);
	if (argc && *argv)
		program_name = *argv;
	
	while ((c = getopt (argc, argv, "d:Fhp")) != EOF) {
		switch (c) {
		case 'h':
			usage(program_name);
			break;
		case 'F':
			flush = 1;
			break;
		case 'd':
			flags |= atoi(optarg);
			break;
		case 'p':
			flags |= RESIZE_PERCENT_COMPLETE;
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

	if (flush) {
#ifdef BLKFLSBUF
		fd = open(device_name, O_RDONLY, 0);

		if (fd < 0) {
			com_err("open", errno, "while opening %s for flushing",
				device_name);
			exit(1);
		}
		if (ioctl(fd, BLKFLSBUF, 0) < 0) {
			com_err("BLKFLSBUF", errno, "while trying to flush %s",
				device_name);
			exit(1);
		}
		close(fd);
#else
		fprintf(stderr, "BLKFLSBUF not supported");
		exit(1);
#endif /* BLKFLSBUF */
	}

	if (flags & RESIZE_DEBUG_IO) {
		io_ptr = test_io_manager;
		test_io_backing_manager = unix_io_manager;
	} else 
		io_ptr = unix_io_manager;

	retval = ext2fs_open (device_name, EXT2_FLAG_RW, 0, 0,
			      io_ptr, &fs);
	if (retval) {
		com_err (program_name, retval, "while trying to open %s",
			 device_name);
		printf ("Couldn't find valid filesystem superblock.\n");
		exit (1);
	}
	retval = resize_fs(fs, new_size, flags, resize_progress_func);
	if (retval) {
		com_err(program_name, retval, "while trying to resize %s",
			device_name);
		ext2fs_close (fs);
	}
	exit (0);
}
