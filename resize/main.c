/*
 * main.c --- ext2 resizer main program
 *
 * Copyright (C) 1997, 1998 by Theodore Ts'o and
 * 	PowerQuest, Inc.
 *
 * Copyright (C) 1999, 2000, 2001 by Theosore Ts'o
 * 
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 */

#ifdef HAVE_GETOPT_H
#include <getopt.h>
#else
extern char *optarg;
extern int optind;
#endif
#include <fcntl.h>
#include <sys/stat.h>

#include "resize2fs.h"

#include "../version.h"

char *program_name, *device_name;

static void usage (char *prog)
{
	fprintf (stderr, _("usage: %s [-d debug_flags] [-f] [-F] [-p] device [new-size]\n\n"), prog);

	exit (1);
}

static errcode_t resize_progress_func(ext2_resize_t rfs, int pass,
				      unsigned long cur, unsigned long max)
{
	ext2_sim_progmeter progress;
	const char	*label;
	errcode_t	retval;

	progress = (ext2_sim_progmeter) rfs->prog_data;
	if (max == 0)
		return 0;
	if (cur == 0) {
		if (progress)
			ext2fs_progress_close(progress);
		progress = 0;
		switch (pass) {
		case E2_RSZ_EXTEND_ITABLE_PASS:
			label = _("Extending the inode table");
			break;
		case E2_RSZ_BLOCK_RELOC_PASS:
			label = _("Relocating blocks");
			break;
		case E2_RSZ_INODE_SCAN_PASS:
			label = _("Scanning inode table");
			break;
		case E2_RSZ_INODE_REF_UPD_PASS:
			label = _("Updating inode references");
			break;
		case E2_RSZ_MOVE_ITABLE_PASS:
			label = _("Moving inode table");
			break;
		default:
			label = _("Unknown pass?!?");
			break;
		}
		printf(_("Begin pass %d (max = %lu)\n"), pass, max);
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
	return 0;
}

static void check_mount(char *device)
{
	errcode_t	retval;
	int		mount_flags;

	retval = ext2fs_check_if_mounted(device, &mount_flags);
	if (retval) {
		com_err(_("ext2fs_check_if_mount"), retval,
			_("while determining whether %s is mounted."),
			device);
		return;
	}
	if (!(mount_flags & EXT2_MF_MOUNTED))
		return;
	
	fprintf(stderr, _("%s is mounted; can't resize a "
		"mounted filesystem!\n\n"), device);
	exit(1);
}


int main (int argc, char ** argv)
{
	errcode_t	retval;
	ext2_filsys	fs;
	int		c;
	int		flags = 0;
	int		flush = 0;
	int		force = 0;
	int		fd;
	blk_t		new_size = 0;
	blk_t		max_size = 0;
	io_manager	io_ptr;
	char		*tmp;
	struct stat	st_buf;
	
	initialize_ext2_error_table();

	fprintf (stderr, _("resize2fs %s (%s)\n"),
		 E2FSPROGS_VERSION, E2FSPROGS_DATE);
	if (argc && *argv)
		program_name = *argv;

	while ((c = getopt (argc, argv, "d:fFhp")) != EOF) {
		switch (c) {
		case 'h':
			usage(program_name);
			break;
		case 'f':
			force = 1;
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
			usage(program_name);
		}
	}
	if (optind == argc)
		usage(program_name);

	device_name = argv[optind++];
	if (optind < argc) {
		new_size = strtoul(argv[optind++], &tmp, 0);
		if (*tmp) {
			com_err(program_name, 0, _("bad filesystem size - %s"),
				argv[optind - 1]);
			exit(1);
		}
	}
	if (optind < argc)
		usage(program_name);
	
	check_mount(device_name);
	
	if (flush) {
		fd = open(device_name, O_RDONLY, 0);

		if (fd < 0) {
			com_err("open", errno,
				_("while opening %s for flushing"),
				device_name);
			exit(1);
		}
		retval = ext2fs_sync_device(fd, 1);
		if (retval) {
			com_err(argv[0], retval, 
				_("while trying to flush %s"),
				device_name);
			exit(1);
		}
		close(fd);
	}

	if (flags & RESIZE_DEBUG_IO) {
		io_ptr = test_io_manager;
		test_io_backing_manager = unix_io_manager;
	} else 
		io_ptr = unix_io_manager;

	retval = ext2fs_open (device_name, EXT2_FLAG_RW, 0, 0,
			      io_ptr, &fs);
	if (retval) {
		com_err (program_name, retval, _("while trying to open %s"),
			 device_name);
		printf (_("Couldn't find valid filesystem superblock.\n"));
		exit (1);
	}
	/*
	 * Check for compatibility with the feature sets.  We need to
	 * be more stringent than ext2fs_open().
	 */
	if (fs->super->s_feature_compat & ~EXT2_LIB_FEATURE_COMPAT_SUPP) {
		com_err(program_name, EXT2_ET_UNSUPP_FEATURE,
			"(%s)", device_name);
		exit(1);
	}
	
	/*
	 * Get the size of the containing partition, and use this for
	 * defaults and for making sure the new filesystme doesn't
	 * exceed the partition size.
	 */
	retval = ext2fs_get_device_size(device_name, fs->blocksize,
					&max_size);
	if (retval) {
		com_err(program_name, retval,
			_("while trying to determine filesystem size"));
		exit(1);
	}
	if (!new_size)
		new_size = max_size;
	/*
	 * If we are resizing a plain file, and it's not big enough,
	 * automatically extend it in a sparse fashion by writing the
	 * last requested block.
	 */
	if ((new_size > max_size) &&
	    (stat(device_name, &st_buf) == 0) &&
	    S_ISREG(st_buf.st_mode) &&
	    ((tmp = malloc(fs->blocksize)) != 0)) {
		memset(tmp, 0, fs->blocksize);
		retval = io_channel_write_blk(fs->io, new_size-1, 1, tmp);
		if (retval == 0)
			max_size = new_size;
		free(tmp);
	}
	if (!force && (new_size > max_size)) {
		fprintf(stderr, _("The containing partition (or device)"
			" is only %d blocks.\nYou requested a new size"
			" of %d blocks.\n\n"), max_size,
			new_size);
		exit(1);
	}
	if (new_size == fs->super->s_blocks_count) {
		fprintf(stderr, _("The filesystem is already %d blocks "
			"long.  Nothing to do!\n\n"), new_size);
		exit(0);
	}
	if (!force && ((fs->super->s_lastcheck < fs->super->s_mtime) ||
		       (fs->super->s_state & EXT2_ERROR_FS) ||
		       ((fs->super->s_state & EXT2_VALID_FS) == 0))) {
		fprintf(stderr, _("Please run 'e2fsck -f %s' first.\n\n"),
			device_name);
		exit(1);
	}
	retval = resize_fs(fs, &new_size, flags,
			   ((flags & RESIZE_PERCENT_COMPLETE) ?
			    resize_progress_func : 0));
	if (retval) {
		com_err(program_name, retval, _("while trying to resize %s"),
			device_name);
		ext2fs_close (fs);
		exit(1);
	}
	printf(_("The filesystem on %s is now %d blocks long.\n\n"),
	       device_name, new_size);
	return (0);
}
