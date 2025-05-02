/*
 * main.c --- ext2 resizer main program
 *
 * Copyright (C) 1997, 1998 by Theodore Ts'o and
 * 	PowerQuest, Inc.
 *
 * Copyright (C) 1999, 2000, 2001, 2002, 2003, 2004 by Theodore Ts'o
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 */

#ifndef _LARGEFILE_SOURCE
#define _LARGEFILE_SOURCE
#endif
#ifndef _LARGEFILE64_SOURCE
#define _LARGEFILE64_SOURCE
#endif

#include "config.h"
#ifdef HAVE_GETOPT_H
#include <getopt.h>
#else
extern char *optarg;
extern int optind;
#endif
#include <unistd.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <libgen.h>

#include "e2p/e2p.h"

#include "resize2fs.h"

#include "../version.h"

char *program_name;
static char *device_name, *io_options;

static void usage (char *prog)
{
	fprintf (stderr, _("Usage: %s [-d debug_flags] [-f] [-F] [-M] [-P] "
			   "[-p] device [-b|-s|new_size] [-S RAID-stride] "
			   "[-z undo_file]\n\n"),
		 prog ? prog : "resize2fs");

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

static void determine_fs_stride(ext2_filsys fs)
{
	unsigned int	group;
	unsigned long long sum;
	unsigned int	has_sb, prev_has_sb = 0, num;
	unsigned int	flexbg_size = 1U << fs->super->s_log_groups_per_flex;
	int		i_stride, b_stride;

	if (fs->stride)
		return;
	num = 0; sum = 0;
	for (group = 0; group < fs->group_desc_count; group++) {
		has_sb = ext2fs_bg_has_super(fs, group);
		if (group == 0 || has_sb != prev_has_sb)
			goto next;
		b_stride = ext2fs_block_bitmap_loc(fs, group) -
			ext2fs_block_bitmap_loc(fs, group - 1) -
			fs->super->s_blocks_per_group;
		i_stride = ext2fs_inode_bitmap_loc(fs, group) -
			ext2fs_inode_bitmap_loc(fs, group - 1) -
			fs->super->s_blocks_per_group;
		if (b_stride != i_stride ||
		    b_stride < 0 ||
		    (flexbg_size > 1 && (group % flexbg_size == 0)))
			goto next;

		/* printf("group %d has stride %d\n", group, b_stride); */
		sum += b_stride;
		num++;

	next:
		prev_has_sb = has_sb;
	}

	if (fs->group_desc_count > 12 && num < 3)
		sum = 0;

	if (num)
		fs->stride = sum / num;
	else
		fs->stride = 0;

	fs->super->s_raid_stride = fs->stride;
	ext2fs_mark_super_dirty(fs);

#if 0
	if (fs->stride)
		printf("Using RAID stride of %d\n", fs->stride);
#endif
}

static void bigalloc_check(ext2_filsys fs, int force)
{
	if (!force && ext2fs_has_feature_bigalloc(fs->super)) {
		fprintf(stderr, "%s", _("\nResizing bigalloc file systems has "
					"not been fully tested.  Proceed at\n"
					"your own risk!  Use the force option "
					"if you want to go ahead anyway.\n\n"));
		exit(1);
	}
}

static int resize2fs_setup_tdb(const char *device, char *undo_file,
			       io_manager *io_ptr)
{
	errcode_t retval = ENOMEM;
	const char *tdb_dir = NULL;
	char *tdb_file = NULL;
	char *dev_name, *tmp_name;

	/* (re)open a specific undo file */
	if (undo_file && undo_file[0] != 0) {
		retval = set_undo_io_backing_manager(*io_ptr);
		if (retval)
			goto err;
		*io_ptr = undo_io_manager;
		retval = set_undo_io_backup_file(undo_file);
		if (retval)
			goto err;
		printf(_("Overwriting existing filesystem; this can be undone "
			 "using the command:\n"
			 "    e2undo %s %s\n\n"),
			undo_file, device);
		return retval;
	}

	/*
	 * Configuration via a conf file would be
	 * nice
	 */
	tdb_dir = getenv("E2FSPROGS_UNDO_DIR");
	if (!tdb_dir)
		tdb_dir = "/var/lib/e2fsprogs";

	if (!strcmp(tdb_dir, "none") || (tdb_dir[0] == 0) ||
	    access(tdb_dir, W_OK))
		return 0;

	tmp_name = strdup(device);
	if (!tmp_name)
		goto errout;
	dev_name = basename(tmp_name);
	tdb_file = malloc(strlen(tdb_dir) + 11 + strlen(dev_name) + 7 + 1);
	if (!tdb_file) {
		free(tmp_name);
		goto errout;
	}
	sprintf(tdb_file, "%s/resize2fs-%s.e2undo", tdb_dir, dev_name);
	free(tmp_name);

	if ((unlink(tdb_file) < 0) && (errno != ENOENT)) {
		retval = errno;
		com_err(program_name, retval,
			_("while trying to delete %s"), tdb_file);
		goto errout;
	}

	retval = set_undo_io_backing_manager(*io_ptr);
	if (retval)
		goto errout;
	*io_ptr = undo_io_manager;
	retval = set_undo_io_backup_file(tdb_file);
	if (retval)
		goto errout;
	printf(_("Overwriting existing filesystem; this can be undone "
		 "using the command:\n"
		 "    e2undo %s %s\n\n"), tdb_file, device);

	free(tdb_file);
	return 0;
errout:
	free(tdb_file);
err:
	com_err(program_name, retval, "%s",
		_("while trying to setup undo file\n"));
	return retval;
}

int main (int argc, char ** argv)
{
	errcode_t	retval;
	ext2_filsys	fs;
	int		c;
	int		flags = 0;
	int		flush = 0;
	int		force = 0;
	int		io_flags = 0;
	int		force_min_size = 0;
	int		print_min_size = 0;
	int		fd, ret;
	int		open_flags = O_RDWR;
	blk64_t		new_size = 0;
	blk64_t		max_size = 0;
	blk64_t		min_size = 0;
	io_manager	io_ptr;
	char		*new_size_str = 0;
	int		use_stride = -1;
	ext2fs_struct_stat st_buf;
	__s64		new_file_size;
	unsigned int	sys_page_size = 4096;
	unsigned int	blocksize;
	long		sysval;
	int		len, mount_flags;
	char		*mtpt, *undo_file = NULL;

#ifdef ENABLE_NLS
	setlocale(LC_MESSAGES, "");
	setlocale(LC_CTYPE, "");
	bindtextdomain(NLS_CAT_NAME, LOCALEDIR);
	textdomain(NLS_CAT_NAME);
	set_com_err_gettext(gettext);
#endif

	add_error_table(&et_ext2_error_table);

	fprintf (stderr, "resize2fs %s (%s)\n",
		 E2FSPROGS_VERSION, E2FSPROGS_DATE);
	if (argc && *argv)
		program_name = *argv;
	else
		usage(NULL);

	while ((c = getopt(argc, argv, "d:fFhMPpS:bsz:")) != EOF) {
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
		case 'M':
			force_min_size = 1;
			break;
		case 'P':
			print_min_size = 1;
			break;
		case 'd':
			flags |= atoi(optarg);
			break;
		case 'p':
			flags |= RESIZE_PERCENT_COMPLETE;
			break;
		case 'S':
			use_stride = atoi(optarg);
			break;
		case 'b':
			flags |= RESIZE_ENABLE_64BIT;
			break;
		case 's':
			flags |= RESIZE_DISABLE_64BIT;
			break;
		case 'z':
			undo_file = optarg;
			break;
		default:
			usage(program_name);
		}
	}
	if (optind == argc)
		usage(program_name);

	device_name = argv[optind++];
	if (optind < argc)
		new_size_str = argv[optind++];
	if (optind < argc)
		usage(program_name);

	io_options = strchr(device_name, '?');
	if (io_options)
		*io_options++ = 0;

	/*
	 * Figure out whether or not the device is mounted, and if it is
	 * where it is mounted.
	 */
	len=80;
	while (1) {
		mtpt = malloc(len);
		if (!mtpt)
			return ENOMEM;
		mtpt[len-1] = 0;
		retval = ext2fs_check_mount_point(device_name, &mount_flags,
						  mtpt, len);
		if (retval) {
			com_err("ext2fs_check_mount_point", retval,
				_("while determining whether %s is mounted."),
				device_name);
			exit(1);
		}
		if (!(mount_flags & EXT2_MF_MOUNTED) || (mtpt[len-1] == 0))
			break;
		free(mtpt);
		len = 2 * len;
	}

	if (print_min_size)
		open_flags = O_RDONLY;

	fd = ext2fs_open_file(device_name, open_flags, 0);
	if (fd < 0) {
		com_err("open", errno, _("while opening %s"),
			device_name);
		exit(1);
	}

	ret = ext2fs_fstat(fd, &st_buf);
	if (ret < 0) {
		com_err("open", errno,
			_("while getting stat information for %s"),
			device_name);
		exit(1);
	}

	if (flush) {
		retval = ext2fs_sync_device(fd, 1);
		if (retval) {
			com_err(argv[0], retval,
				_("while trying to flush %s"),
				device_name);
			exit(1);
		}
	}

	if (!S_ISREG(st_buf.st_mode )) {
		close(fd);
		fd = -1;
	}

#ifdef CONFIG_TESTIO_DEBUG
	if (getenv("TEST_IO_FLAGS") || getenv("TEST_IO_BLOCK")) {
		io_ptr = test_io_manager;
		test_io_backing_manager = unix_io_manager;
	} else
#endif
		io_ptr = unix_io_manager;

	if (!(mount_flags & EXT2_MF_MOUNTED) && !print_min_size)
		io_flags = EXT2_FLAG_RW | EXT2_FLAG_EXCLUSIVE;
	if (mount_flags & EXT2_MF_MOUNTED)
		io_flags |= EXT2_FLAG_DIRECT_IO;

	io_flags |= EXT2_FLAG_64BITS | EXT2_FLAG_THREADS;
	if (undo_file) {
		retval = resize2fs_setup_tdb(device_name, undo_file, &io_ptr);
		if (retval)
			exit(1);
	}
	retval = ext2fs_open2(device_name, io_options, io_flags,
			      0, 0, io_ptr, &fs);
	if (retval) {
		com_err(program_name, retval, _("while trying to open %s"),
			device_name);
		printf("%s", _("Couldn't find valid filesystem superblock.\n"));
		exit (1);
	}
	fs->default_bitmap_type = EXT2FS_BMAP64_RBTREE;

	/*
	 * Before acting on an unmounted filesystem, make sure it's ok,
	 * unless the user is forcing it.
	 *
	 * We do ERROR and VALID checks even if we're only printing the
	 * minimum size, because traversal of a badly damaged filesystem
	 * can cause issues as well.  We don't require it to be fscked after
	 * the last mount time in this case, though, as this is a bit less
	 * risky.
	 */
	if (!force && !(mount_flags & EXT2_MF_MOUNTED)) {
		int checkit = 0;

		if (fs->super->s_state & EXT2_ERROR_FS)
			checkit = 1;

		if ((fs->super->s_state & EXT2_VALID_FS) == 0)
			checkit = 1;

		if ((fs->super->s_lastcheck < fs->super->s_mtime) &&
		    !print_min_size)
			checkit = 1;

		if ((ext2fs_free_blocks_count(fs->super) >
		     ext2fs_blocks_count(fs->super)) ||
		    (fs->super->s_free_inodes_count > fs->super->s_inodes_count))
			checkit = 1;

		if ((fs->super->s_last_orphan != 0) ||
		    ext2fs_has_feature_journal_needs_recovery(fs->super))
			checkit = 1;

		if (checkit) {
			fprintf(stderr,
				_("Please run 'e2fsck -f %s' first.\n\n"),
				device_name);
			goto errout;
		}
	}

	/*
	 * Check for compatibility with the feature sets.  We need to
	 * be more stringent than ext2fs_open().
	 */
	if (fs->super->s_feature_compat & ~EXT2_LIB_FEATURE_COMPAT_SUPP) {
		com_err(program_name, EXT2_ET_UNSUPP_FEATURE,
			"(%s)", device_name);
		goto errout;
	}

	min_size = calculate_minimum_resize_size(fs, flags);

	if (print_min_size) {
		printf(_("Estimated minimum size of the filesystem: %llu\n"),
		       (unsigned long long) min_size);
	success_exit:
		(void) ext2fs_close_free(&fs);
		remove_error_table(&et_ext2_error_table);
		exit(0);
	}

	/* Determine the system page size if possible */
#ifdef HAVE_SYSCONF
#if (!defined(_SC_PAGESIZE) && defined(_SC_PAGE_SIZE))
#define _SC_PAGESIZE _SC_PAGE_SIZE
#endif
#ifdef _SC_PAGESIZE
	sysval = sysconf(_SC_PAGESIZE);
	if (sysval > 0)
		sys_page_size = sysval;
#endif /* _SC_PAGESIZE */
#endif /* HAVE_SYSCONF */

	/*
	 * Get the size of the containing partition, and use this for
	 * defaults and for making sure the new filesystem doesn't
	 * exceed the partition size.
	 */
	blocksize = fs->blocksize;
	retval = ext2fs_get_device_size2(device_name, blocksize,
					 &max_size);
	if (retval) {
		com_err(program_name, retval, "%s",
			_("while trying to determine filesystem size"));
		goto errout;
	}
	if (force_min_size)
		new_size = min_size;
	else if (new_size_str) {
		new_size = parse_num_blocks2(new_size_str,
					     fs->super->s_log_block_size);
		if (new_size == 0) {
			com_err(program_name, 0,
				_("Invalid new size: %s\n"), new_size_str);
			goto errout;
		}
	} else {
		new_size = max_size;
		/* Round down to an even multiple of a pagesize */
		if (sys_page_size > blocksize)
			new_size &= ~((blk64_t)((sys_page_size / blocksize)-1));
	}
	/* If changing 64bit, don't change the filesystem size. */
	if (flags & (RESIZE_DISABLE_64BIT | RESIZE_ENABLE_64BIT)) {
		new_size = ext2fs_blocks_count(fs->super);
	}
	if (!ext2fs_has_feature_64bit(fs->super)) {
		/* Take 16T down to 2^32-1 blocks */
		if (new_size == (1ULL << 32))
			new_size--;
		else if (new_size > (1ULL << 32)) {
			com_err(program_name, 0, "%s",
				_("New size too large to be "
				  "expressed in 32 bits\n"));
			goto errout;
		}
	}

	/* If using cluster allocations, trim down to a cluster boundary */
	if (ext2fs_has_feature_bigalloc(fs->super)) {
		new_size &= ~((blk64_t)(1ULL << fs->cluster_ratio_bits) - 1);
	}

	if (!ext2fs_has_feature_meta_bg(fs->super)) {
		dgrp_t		new_group_desc_count;
		unsigned long	new_desc_blocks;

		new_group_desc_count = ext2fs_div64_ceil(new_size -
					fs->super->s_first_data_block,
					EXT2_BLOCKS_PER_GROUP(fs->super));
		new_desc_blocks = ext2fs_div_ceil(new_group_desc_count,
					EXT2_DESC_PER_BLOCK(fs->super));
		if ((new_desc_blocks + fs->super->s_first_data_block) >
		    EXT2_BLOCKS_PER_GROUP(fs->super)) {
			com_err(program_name, 0,
				_("New size results in too many block group "
				  "descriptors.\n"));
			goto errout;
		}
	}

	if (!force && new_size < min_size) {
		com_err(program_name, 0,
			_("New size smaller than minimum (%llu)\n"),
			(unsigned long long) min_size);
		goto errout;
	}
	if (use_stride >= 0) {
		if (use_stride >= (int) fs->super->s_blocks_per_group) {
			com_err(program_name, 0, "%s",
				_("Invalid stride length"));
			goto errout;
		}
		fs->stride = fs->super->s_raid_stride = use_stride;
		ext2fs_mark_super_dirty(fs);
	} else
		  determine_fs_stride(fs);

	/*
	 * If we are resizing a plain file, and it's not big enough,
	 * automatically extend it in a sparse fashion by writing the
	 * last requested block.
	 */
	new_file_size = ((__u64) new_size) * blocksize;
	if ((__u64) new_file_size >
	    (((__u64) 1) << (sizeof(st_buf.st_size)*8 - 1)) - 1)
		fd = -1;
	if ((new_file_size > st_buf.st_size) &&
	    (fd > 0)) {
		if ((ext2fs_llseek(fd, new_file_size-1, SEEK_SET) >= 0) &&
		    (write(fd, "0", 1) == 1))
			max_size = new_size;
	}
	if (!force && (new_size > max_size)) {
		fprintf(stderr, _("The containing partition (or device)"
			" is only %llu (%dk) blocks.\nYou requested a new size"
			" of %llu blocks.\n\n"), (unsigned long long) max_size,
			blocksize / 1024, (unsigned long long) new_size);
		goto errout;
	}
	if ((flags & RESIZE_DISABLE_64BIT) && (flags & RESIZE_ENABLE_64BIT)) {
		fprintf(stderr, _("Cannot set and unset 64bit feature.\n"));
		goto errout;
	} else if (flags & (RESIZE_DISABLE_64BIT | RESIZE_ENABLE_64BIT)) {
		if (new_size >= (1ULL << 32)) {
			fprintf(stderr, _("Cannot change the 64bit feature "
				"on a filesystem that is larger than "
				"2^32 blocks.\n"));
			goto errout;
		}
		if (mount_flags & EXT2_MF_MOUNTED) {
			fprintf(stderr, _("Cannot change the 64bit feature "
				"while the filesystem is mounted.\n"));
			goto errout;
		}
		if (flags & RESIZE_ENABLE_64BIT &&
		    !ext2fs_has_feature_extents(fs->super)) {
			fprintf(stderr, _("Please enable the extents feature "
				"with tune2fs before enabling the 64bit "
				"feature.\n"));
			goto errout;
		}
	} else {
		adjust_new_size(fs, &new_size);
		if (new_size == ext2fs_blocks_count(fs->super)) {
			fprintf(stderr, _("The filesystem is already "
					  "%llu (%dk) blocks long.  "
					  "Nothing to do!\n\n"),
				(unsigned long long) new_size,
				blocksize / 1024);
			goto success_exit;
		}
	}
	if ((flags & RESIZE_ENABLE_64BIT) &&
	    ext2fs_has_feature_64bit(fs->super)) {
		fprintf(stderr, _("The filesystem is already 64-bit.\n"));
		goto success_exit;
	}
	if ((flags & RESIZE_DISABLE_64BIT) &&
	    !ext2fs_has_feature_64bit(fs->super)) {
		fprintf(stderr, _("The filesystem is already 32-bit.\n"));
		goto success_exit;
	}
	if (new_size < ext2fs_blocks_count(fs->super) &&
	    ext2fs_has_feature_stable_inodes(fs->super)) {
		fprintf(stderr, _("Cannot shrink this filesystem "
			"because it has the stable_inodes feature flag.\n"));
		goto errout;
	}
	if (mount_flags & EXT2_MF_MOUNTED) {
		retval = online_resize_fs(fs, mtpt, &new_size, flags);
	} else {
		bigalloc_check(fs, force);
		if (flags & RESIZE_ENABLE_64BIT)
			printf(_("Converting the filesystem to 64-bit.\n"));
		else if (flags & RESIZE_DISABLE_64BIT)
			printf(_("Converting the filesystem to 32-bit.\n"));
		else
			printf(_("Resizing the filesystem on "
				 "%s to %llu (%dk) blocks.\n"),
			       device_name, (unsigned long long) new_size,
			       blocksize / 1024);
		retval = resize_fs(fs, &new_size, flags,
				   ((flags & RESIZE_PERCENT_COMPLETE) ?
				    resize_progress_func : 0));
	}
	free(mtpt);
	if (retval) {
		com_err(program_name, retval, _("while trying to resize %s"),
			device_name);
		fprintf(stderr,
			_("Please run 'e2fsck -fy %s' to fix the filesystem\n"
			  "after the aborted resize operation.\n"),
			device_name);
		goto errout;
	}
	printf(_("The filesystem on %s is now %llu (%dk) blocks long.\n\n"),
	       device_name, (unsigned long long) new_size, blocksize / 1024);

	if ((st_buf.st_size > new_file_size) &&
	    (fd > 0)) {
#ifdef HAVE_FTRUNCATE64
		retval = ftruncate64(fd, new_file_size);
#else
		retval = 0;
		/* Only truncate if new_file_size doesn't overflow off_t */
		if (((off_t) new_file_size) == new_file_size)
			retval = ftruncate(fd, (off_t) new_file_size);
#endif
		if (retval)
			com_err(program_name, retval,
				_("while trying to truncate %s"),
				device_name);
	}
	if (fd > 0)
		close(fd);
	remove_error_table(&et_ext2_error_table);
	return 0;
errout:
	(void) ext2fs_close_free(&fs);
	remove_error_table(&et_ext2_error_table);
	return 1;
}
