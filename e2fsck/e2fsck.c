/*
 * e2fsck.c - a consistency checker for the new extended file system.
 * 
 * Copyright (C) 1993, 1994 Theodore Ts'o.  This file may be
 * redistributed under the terms of the GNU Public License.
 */

/* Usage: e2fsck [-dfpnsvy] device
 *	-d -- debugging this program
 *	-f -- check the fs even if it is marked valid
 *	-p -- "preen" the filesystem
 * 	-n -- open the filesystem r/o mode; never try to fix problems
 *	-v -- verbose (tells how many files)
 * 	-y -- always answer yes to questions
 *
 * The device may be a block device or a image of one, but this isn't
 * enforced (but it's not much fun on a character device :-). 
 */

#include <string.h>
#include <fcntl.h>
#include <ctype.h>
#include <termios.h>
#include <time.h>
#include <getopt.h>
#include <unistd.h>
#include <mntent.h>
#include <sys/ioctl.h>
#include <malloc.h>

#include "et/com_err.h"
#include "e2fsck.h"
#include "../version.h"

extern int isatty(int);

const char * program_name = "e2fsck";
const char * device_name = NULL;

/* Command line options */
int nflag = 0;
int yflag = 0;
int tflag = 0;			/* Do timing */
int cflag = 0;			/* check disk */
int preen = 0;
int rwflag = 1;
int inode_buffer_blocks = 0;
blk_t superblock;
int blocksize = 0;
int verbose = 0;
int list = 0;
int debug = 0;
int force = 0;
static int show_version_only = 0;

static int replace_bad_blocks = 0;
static char *bad_blocks_file = 0;

static int possible_block_sizes[] = { 1024, 2048, 4096, 8192, 0};

struct resource_track	global_rtrack;

static int root_filesystem = 0;
static int read_only_root = 0;

static void usage(NOARGS)
{
	fprintf(stderr,
		"Usage: %s [-panyrdfvtFV] [-b superblock] [-B blocksize]\n"
		"\t\tdevice\n", program_name);
	exit(FSCK_USAGE);
}

static void show_stats(ext2_filsys fs)
{
	int inodes, inodes_used, blocks, blocks_used;
	int dir_links;
	int num_files, num_links;

	dir_links = 2 * fs_directory_count - 1;
	num_files = fs_total_count - dir_links;
	num_links = fs_links_count - dir_links;
	inodes = fs->super->s_inodes_count;
	inodes_used = (fs->super->s_inodes_count -
		       fs->super->s_free_inodes_count);
	blocks = fs->super->s_blocks_count;
	blocks_used = (fs->super->s_blocks_count -
		       fs->super->s_free_blocks_count);
	
	if (!verbose) {
		printf("%s: %d/%d files, %d/%d blocks\n", device_name,
		       inodes_used, inodes, blocks_used, blocks);
		return;
	}
	printf ("\n%6d inode%s used (%d%%)\n", inodes_used,
		(inodes_used != 1) ? "s" : "",
		100 * inodes_used / inodes);
	printf ("%6d block%s used (%d%%)\n"
		"%6d bad block%s\n", blocks_used,
		(blocks_used != 1) ? "s" : "",
		100 * blocks_used / blocks, fs_badblocks_count,
		fs_badblocks_count != 1 ? "s" : "");
	printf ("\n%6d regular file%s\n"
		"%6d director%s\n"
		"%6d character device file%s\n"
		"%6d block device file%s\n"
		"%6d fifo%s\n"
		"%6d link%s\n"
		"%6d symbolic link%s (%d fast symbolic link%s)\n"
		"%6d socket%s\n"
		"------\n"
		"%6d file%s\n",
		fs_regular_count, (fs_regular_count != 1) ? "s" : "",
		fs_directory_count, (fs_directory_count != 1) ? "ies" : "y",
		fs_chardev_count, (fs_chardev_count != 1) ? "s" : "",
		fs_blockdev_count, (fs_blockdev_count != 1) ? "s" : "",
		fs_fifo_count, (fs_fifo_count != 1) ? "s" : "",
		fs_links_count - dir_links,
		((fs_links_count - dir_links) != 1) ? "s" : "",
		fs_symlinks_count, (fs_symlinks_count != 1) ? "s" : "",
		fs_fast_symlinks_count, (fs_fast_symlinks_count != 1) ? "s" : "",
		fs_sockets_count, (fs_sockets_count != 1) ? "s" : "",
		fs_total_count - dir_links,
		((fs_total_count - dir_links) != 1) ? "s" : "");
}

static void check_mount(NOARGS)
{
	FILE * f;
	struct mntent * mnt;
	int cont;
	int fd;

	if ((f = setmntent (MOUNTED, "r")) == NULL)
		return;
	while ((mnt = getmntent (f)) != NULL)
		if (strcmp (device_name, mnt->mnt_fsname) == 0)
			break;
	endmntent (f);
	if (!mnt)
		return;

	if  (!strcmp(mnt->mnt_dir, "/"))
		root_filesystem = 1;

	/*
	 * If the root is mounted read-only, then /etc/mtab is
	 * probably not correct; so we won't issue a warning based on
	 * it.
	 */
	fd = open(MOUNTED, O_RDWR);
	if (fd < 0) {
		if (errno == EROFS) {
			read_only_root = 1;
			return;
		}
	} else
		close(fd);
	
	if (!rwflag) {
		printf("Warning!  %s is mounted.\n", device_name);
		return;
	}

	printf ("%s is mounted.  ", device_name);
	if (isatty (0) && isatty (1))
		cont = ask_yn("Do you really want to continue", -1);
	else
		cont = 0;
	if (!cont) {
		printf ("check aborted.\n");
		exit (0);
	}
	return;
}

static void sync_disks(NOARGS)
{
	sync();
	sync();
	sleep(1);
	sync();
}

static void check_super_block(ext2_filsys fs)
{
	blk_t	first_block, last_block;
	int	blocks_per_group = fs->super->s_blocks_per_group;
	int	i;

	first_block =  fs->super->s_first_data_block;
	last_block = first_block + blocks_per_group;

	for (i = 0; i < fs->group_desc_count; i++) {
		if ((fs->group_desc[i].bg_block_bitmap < first_block) ||
		    (fs->group_desc[i].bg_block_bitmap >= last_block)) {
			printf("Block bitmap %ld for group %d not in group.\n",
			       fs->group_desc[i].bg_block_bitmap, i);
			fatal_error(0);
		}
		if ((fs->group_desc[i].bg_inode_bitmap < first_block) ||
		    (fs->group_desc[i].bg_inode_bitmap >= last_block)) {
			printf("Inode bitmap %ld for group %d not in group.\n",
			       fs->group_desc[i].bg_inode_bitmap, i);
			fatal_error(0);
		}
		if ((fs->group_desc[i].bg_inode_table < first_block) ||
		    ((fs->group_desc[i].bg_inode_table +
		      fs->inode_blocks_per_group - 1) >= last_block)) {
			printf("Inode table %ld for group %d not in group.\n",
			       fs->group_desc[i].bg_inode_table, i);
			fatal_error(0);
		}
		first_block += fs->super->s_blocks_per_group;
		last_block += fs->super->s_blocks_per_group;
	}
	return;
}

/*
 * This routine checks to see if a filesystem can be skipped; if so,
 * it will exit with E2FSCK_OK.  Under some conditions it will print a
 * message explaining why a check is being forced.
 */
static void check_if_skip(ext2_filsys fs)
{
	const char *reason = NULL;
	
	if (force || bad_blocks_file || cflag)
		return;
	
	if (fs->super->s_state & EXT2_ERROR_FS)
		reason = "contains a file system with errors";
	else if (fs->super->s_mnt_count >=
		 (unsigned) fs->super->s_max_mnt_count)
		reason = "has reached maximal mount count";
	else if (fs->super->s_checkinterval &&
		 time(0) >= (fs->super->s_lastcheck +
			     fs->super->s_checkinterval))
		reason = "has gone too long without being checked";
	if (reason) {
		printf("%s %s, check forced.\n", device_name, reason);
		return;
	}
	if (fs->super->s_state & EXT2_VALID_FS) {
		printf("%s is clean, no check.\n", device_name);
		exit(FSCK_OK);
	}
}	

static void PRS(int argc, char *argv[])
{
	int flush = 0;
	char c;
#ifdef MTRACE
	extern void *mallwatch;
#endif
	char *oldpath, newpath[PATH_MAX];

	/* Update our PATH to include /sbin  */
	strcpy(newpath, "PATH=/sbin:");
	if ((oldpath = getenv("PATH")) != NULL)
		strcat(newpath, oldpath);
	putenv(newpath);

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);
	initialize_ext2_error_table();
	
	if (argc && *argv)
		program_name = *argv;
	while ((c = getopt (argc, argv, "panyrcB:dfvtFVM:b:I:P:l:L:")) != EOF)
		switch (c) {
		case 'p':
		case 'a':
			preen = 1;
			yflag = nflag = 0;
			break;
		case 'n':
			nflag = 1;
			preen = yflag = 0;
			break;
		case 'y':
			yflag = 1;
			preen = nflag = 0;
			break;
		case 't':
			tflag++;
			break;
		case 'c':
			cflag++;
			break;
		case 'r':
			/* What we do by default, anyway! */
			break;
		case 'b':
			superblock = atoi(optarg);
			break;
		case 'B':
			blocksize = atoi(optarg);
			break;
		case 'I':
			inode_buffer_blocks = atoi(optarg);
			break;
		case 'P':
			process_inode_size = atoi(optarg);
			break;
		case 'L':
			replace_bad_blocks++;
		case 'l':
			bad_blocks_file = malloc(strlen(optarg)+1);
			if (!bad_blocks_file)
				fatal_error("Couldn't malloc bad_blocks_file");
			strcpy(bad_blocks_file, optarg);
			break;
		case 'd':
			debug = 1;
			break;
		case 'f':
			force = 1;
			break;
		case 'F':
			flush = 1;
			break;
		case 'v':
			verbose = 1;
			break;
		case 'V':
			show_version_only = 1;
			break;
#ifdef MTRACE
		case 'M':
			mallwatch = (void *) strtol(optarg, NULL, 0);
			break;
#endif
		default:
			usage ();
		}
	if (show_version_only)
		return;
	if (optind != argc - 1)
		usage ();
	if (nflag && !bad_blocks_file && !cflag)
		rwflag = 0;
	device_name = argv[optind];
	if (flush) {
		int	fd = open(device_name, O_RDONLY, 0);

		if (fd < 0) {
			com_err("open", errno, "while opening %s for flushing",
				device_name);
			exit(FSCK_ERROR);
		}
		if (ioctl(fd, BLKFLSBUF, 0) < 0) {
			com_err("BLKFLSBUF", errno, "while trying to flush %s",
				device_name);
			exit(FSCK_ERROR);
		}
		close(fd);
	}
}
					
int main (int argc, char *argv[])
{
	errcode_t	retval = 0;
	int		exit_value = FSCK_OK;
	int		i;
	ext2_filsys	fs;
	
#ifdef MTRACE
	mtrace();
#endif
#ifdef MCHECK
	mcheck(0);
#endif
	
	init_resource_track(&global_rtrack);

	PRS(argc, argv);

	if (!preen)
		fprintf (stderr, "e2fsck %s, %s for EXT2 FS %s, %s\n",
			 E2FSPROGS_VERSION, E2FSPROGS_DATE,
			 EXT2FS_VERSION, EXT2FS_DATE);

	if (show_version_only)
		exit(0);
	
	check_mount();
	
	if (!preen && !nflag && !yflag) {
		if (!isatty (0) || !isatty (1))
			die ("need terminal for interactive repairs");
	}
	sync_disks();
	if (superblock && blocksize) {
		retval = ext2fs_open(device_name, rwflag ? EXT2_FLAG_RW : 0,
				     superblock, blocksize, unix_io_manager,
				     &fs);
	} else if (superblock) {
		for (i=0; possible_block_sizes[i]; i++) {
			retval = ext2fs_open(device_name,
					     rwflag ? EXT2_FLAG_RW : 0,
					     superblock,
					     possible_block_sizes[i],
					     unix_io_manager, &fs);
			if (!retval)
				break;
		}
	} else 
		retval = ext2fs_open(device_name, rwflag ? EXT2_FLAG_RW : 0,
				     0, 0, unix_io_manager, &fs);
	if (retval) {
		com_err(program_name, retval, "while trying to open %s",
			device_name);
		printf("Couldn't find valid filesystem superblock.\n");
		fatal_error(0);
	}
	/*
	 * If the user specified a specific superblock, presumably the
	 * master superblock has been trashed.  So we mark the
	 * superblock as dirty, so it can be written out.
	 */
	if (superblock && rwflag)
		ext2fs_mark_super_dirty(fs);

	ehandler_init(fs->io);

	check_super_block(fs);
	check_if_skip(fs);
	if (bad_blocks_file)
		read_bad_blocks_file(fs, bad_blocks_file, replace_bad_blocks);
	else if (cflag)
		test_disk(fs);

	/*
	 * Mark the system as valid, 'til proven otherwise
	 */
	ext2fs_mark_valid(fs);
	
	pass1(fs);
	pass2(fs);
	pass3(fs);
	pass4(fs);
	pass5(fs);

#ifdef MTRACE
	mtrace_print("Cleanup");
#endif
	if (ext2fs_test_changed(fs)) {
		exit_value = FSCK_NONDESTRUCT;
		if (!preen)
			printf("\n%s: ***** FILE SYSTEM WAS MODIFIED *****\n",
			       device_name);
		if (root_filesystem && !read_only_root) {
			printf("%s: ***** REBOOT LINUX *****\n", device_name);
			exit_value = FSCK_REBOOT;
		}
	}
	if (!ext2fs_test_valid(fs))
		exit_value = FSCK_UNCORRECTED;
	if (rwflag) {
		if (ext2fs_test_valid(fs))
			fs->super->s_state = EXT2_VALID_FS;
		else
			fs->super->s_state &= ~EXT2_VALID_FS;
		fs->super->s_mnt_count = 0;
		fs->super->s_lastcheck = time(NULL);
		ext2fs_mark_super_dirty(fs);
	}
	show_stats(fs);

	write_bitmaps(fs);
	ext2fs_close(fs);
	sync_disks();
	
	if (tflag)
		print_resource_track(&global_rtrack);
	
	return exit_value;
}
