/*
 * mke2fs.c - Make a ext2fs filesystem.
 * 
 * Copyright (C) 1994 Theodore Ts'o.  This file may be redistributed
 * under the terms of the GNU Public License.
 */

/* Usage: mke2fs [options] device
 * 
 * The device may be a block device or a image of one, but this isn't
 * enforced (but it's not much fun on a character device :-). 
 */

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <ctype.h>
#include <termios.h>
#include <time.h>
#ifdef HAVE_GETOPT_H
#include <getopt.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#ifdef HAVE_MNTENT_H
#include <mntent.h>
#endif
#include <malloc.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>

#ifdef HAVE_LINUX_FS_H
#include <linux/fs.h>
#endif
#include <linux/ext2_fs.h>
#ifdef HAVE_LINUX_MAJOR_H
#include <linux/major.h>
#endif

#include "et/com_err.h"
#include "ext2fs/ext2fs.h"
#include "../version.h"

#define STRIDE_LENGTH 8

extern int isatty(int);
extern FILE *fpopen(const char *cmd, const char *mode);

const char * program_name = "mke2fs";
const char * device_name = NULL;

/* Command line options */
int	cflag = 0;
int	verbose = 0;
int	quiet = 0;
int	super_only = 0;
int	force = 0;
char	*bad_blocks_filename = 0;

struct ext2_super_block param;

static void usage(NOARGS)
{
	fprintf(stderr,
		"Usage: %s [-c|-t|-l filename] [-b block-size] "
		"[-f fragment-size]\n\t[-i bytes-per-inode] "
		"[-m reserved-blocks-percentage] [-qvS]\n"
		"\t[-g blocks-per-group] device [blocks-count]\n",
		program_name);
	exit(1);
}

static int log2(int arg)
{
	int	l = 0;

	arg >>= 1;
	while (arg) {
		l++;
		arg >>= 1;
	}
	return l;
}

static void check_plausibility(NOARGS)
{
#ifdef HAVE_LINUX_MAJOR_H
	int val;
	struct stat s;
	
	val = stat(device_name, &s);
	
	if(val == -1) {
		perror("stat");
		exit(1);
	}
	if(!S_ISBLK(s.st_mode)) {
		printf("%s is not a block special device.\n", device_name);
		printf("Proceed anyway? (y,n) ");
		if (getchar() != 'y')
			exit(1);
		return;
	}
	if ((MAJOR(s.st_rdev) == HD_MAJOR && MINOR(s.st_rdev)%64 == 0) ||
	    (MAJOR(s.st_rdev) == SCSI_DISK_MAJOR &&
	     MINOR(s.st_rdev)%16 == 0)) {
		printf("%s is entire device, not just one partition!\n", 
		       device_name);
		printf("Proceed anyway? (y,n) ");
		if (getchar() != 'y')
			exit(1);
		return;
	}
#endif
}

static void check_mount(NOARGS)
{
	errcode_t	retval;
	int		mount_flags;

	retval = ext2fs_check_if_mounted(device_name, &mount_flags);
	if (retval) {
		com_err("ext2fs_check_if_mount", retval,
			"while determining whether %s is mounted.",
			device_name);
		return;
	}
	if (!(mount_flags & EXT2_MF_MOUNTED))
		return;
	
	fprintf(stderr, "%s is mounted; will not make a filesystem here!\n",
		device_name);
	exit(1);
}

/*
 * Helper function for read_bb_file and test_disk
 */
static void invalid_block(ext2_filsys fs, blk_t blk)
{
	printf("Bad block %u out of range; ignored.\n", blk);
	return;
}

/*
 * Reads the bad blocks list from a file
 */
static void read_bb_file(ext2_filsys fs, badblocks_list *bb_list,
			 const char *bad_blocks_file)
{
	FILE		*f;
	errcode_t	retval;

	f = fopen(bad_blocks_file, "r");
	if (!f) {
		com_err("read_bad_blocks_file", errno,
			"while trying to open %s", bad_blocks_file);
		exit(1);
	}
	retval = ext2fs_read_bb_FILE(fs, f, bb_list, invalid_block);
	fclose (f);
	if (retval) {
		com_err("ext2fs_read_bb_FILE", retval,
			"while reading in list of bad blocks from file");
		exit(1);
	}
}

/*
 * Runs the badblocks program to test the disk
 */
static void test_disk(ext2_filsys fs, badblocks_list *bb_list)
{
	FILE		*f;
	errcode_t	retval;
	char		buf[1024];

	sprintf(buf, "badblocks %s%s %d", quiet ? "" : "-s ",
		fs->device_name,
		fs->super->s_blocks_count);
	if (verbose)
		printf("Running command: %s\n", buf);
	f = popen(buf, "r");
	if (!f) {
		com_err("popen", errno,
			"while trying run '%s'", buf);
		exit(1);
	}
	retval = ext2fs_read_bb_FILE(fs, f, bb_list, invalid_block);
	pclose(f);
	if (retval) {
		com_err("ext2fs_read_bb_FILE", retval,
			"while processing list of bad blocks from program");
		exit(1);
	}
}

static void handle_bad_blocks(ext2_filsys fs, badblocks_list bb_list)
{
	int			i, j;
	int			must_be_good;
	blk_t			blk;
	badblocks_iterate	bb_iter;
	errcode_t		retval;
	blk_t			group_block;
	int			group;
	int			group_bad;

	if (!bb_list)
		return;
	
	/*
	 * The primary superblock and group descriptors *must* be
	 * good; if not, abort.
	 */
	must_be_good = fs->super->s_first_data_block + 1 + fs->desc_blocks;
	for (i = fs->super->s_first_data_block; i <= must_be_good; i++) {
		if (badblocks_list_test(bb_list, i)) {
			fprintf(stderr, "Block %d in primary superblock/group "
				"descriptor area bad.\n", i);
			fprintf(stderr, "Blocks %d through %d must be good "
				"in order to build a filesystem.\n",
				fs->super->s_first_data_block, must_be_good);
			fprintf(stderr, "Aborting....\n");
			exit(1);
		}
	}

	/*
	 * See if any of the bad blocks are showing up in the backup
	 * superblocks and/or group descriptors.  If so, issue a
	 * warning and adjust the block counts appropriately.
	 */
	group_block = fs->super->s_first_data_block +
		fs->super->s_blocks_per_group;
	group_bad = 0;
	
	for (i = 1; i < fs->group_desc_count; i++) {
		for (j=0; j < fs->desc_blocks+1; j++) {
			if (badblocks_list_test(bb_list, group_block +
						j)) {
				if (!group_bad) 
					fprintf(stderr,
"Warning: the backup superblock/group descriptors at block %d contain\n"
"	bad blocks.\n\n",
						group_block);
				group_bad++;
				group = ext2fs_group_of_blk(fs, group_block+j);
				fs->group_desc[group].bg_free_blocks_count++;
				fs->super->s_free_blocks_count++;
			}
		}
		group_block += fs->super->s_blocks_per_group;
	}
	
	/*
	 * Mark all the bad blocks as used...
	 */
	retval = badblocks_list_iterate_begin(bb_list, &bb_iter);
	if (retval) {
		com_err("badblocks_list_iterate_begin", retval,
			"while marking bad blocks as used");
		exit(1);
	}
	while (badblocks_list_iterate(bb_iter, &blk)) 
		ext2fs_mark_block_bitmap(fs->block_map, blk);
	badblocks_list_iterate_end(bb_iter);
}

static void new_table_block(ext2_filsys fs, blk_t first_block,
			    const char *name, int num, int initialize,
			    const char *buf, blk_t *new_block)
{
	errcode_t	retval;
	blk_t		blk;
	int		i;
	int		count;
	
	retval = ext2fs_get_free_blocks(fs, first_block,
			first_block + fs->super->s_blocks_per_group,
					num, fs->block_map, new_block);
	if (retval) {
		printf("Could not allocate %d block(s) for %s: %s\n",
		       num, name, error_message(retval));
		ext2fs_unmark_valid(fs);
		return;
	}
	if (initialize) {
		blk = *new_block;
		for (i=0; i < num; i += STRIDE_LENGTH, blk += STRIDE_LENGTH) {
			if (num-i > STRIDE_LENGTH)
				count = STRIDE_LENGTH;
			else
				count = num - i;
			retval = io_channel_write_blk(fs->io, blk, count, buf);
			if (retval)
				printf("Warning: could not write %d blocks "
				       "starting at %d for %s: %s\n",
				       count, blk, name,
				       error_message(retval));
		}
	}
	blk = *new_block;
	for (i = 0; i < num; i++, blk++)
		ext2fs_mark_block_bitmap(fs->block_map, blk);
}	

static void alloc_tables(ext2_filsys fs)
{
	blk_t	group_blk;
	int	i;
	char	*buf;

	buf = malloc(fs->blocksize * STRIDE_LENGTH);
	if (!buf) {
		com_err("malloc", ENOMEM, "while allocating zeroizing buffer");
		exit(1);
	}
	memset(buf, 0, fs->blocksize * STRIDE_LENGTH);
	
	group_blk = fs->super->s_first_data_block;
	if (!quiet)
		printf("Writing inode tables: ");
	for (i = 0; i < fs->group_desc_count; i++) {
		if (!quiet)
			printf("%4d/%4ld", i, fs->group_desc_count);
		new_table_block(fs, group_blk, "block bitmap", 1, 0, buf,
				&fs->group_desc[i].bg_block_bitmap);
		new_table_block(fs, group_blk, "inode bitmap", 1, 0, buf,
				&fs->group_desc[i].bg_inode_bitmap);
		new_table_block(fs, group_blk, "inode table",
				fs->inode_blocks_per_group,
				!super_only, buf,
				&fs->group_desc[i].bg_inode_table);
		
		group_blk += fs->super->s_blocks_per_group;
		if (!quiet) 
			printf("\b\b\b\b\b\b\b\b\b");
	}
	if (!quiet)
		printf("done     \n");
}

static void create_root_dir(ext2_filsys fs)
{
	errcode_t	retval;
	struct ext2_inode	inode;

	retval = ext2fs_mkdir(fs, EXT2_ROOT_INO, EXT2_ROOT_INO, 0);
	if (retval) {
		com_err("ext2fs_mkdir", retval, "while creating root dir");
		exit(1);
	}
	if (geteuid()) {
		retval = ext2fs_read_inode(fs, EXT2_ROOT_INO, &inode);
		if (retval) {
			com_err("ext2fs_read_inode", retval,
				"while reading root inode");
			exit(1);
		}
		inode.i_uid = geteuid();
		retval = ext2fs_write_inode(fs, EXT2_ROOT_INO, &inode);
		if (retval) {
			com_err("ext2fs_write_inode", retval,
				"while setting root inode ownership");
			exit(1);
		}
	}
}

static void create_lost_and_found(ext2_filsys fs)
{
	errcode_t		retval;
	ino_t			ino;
	const char		*name = "lost+found";
	int			i;

	retval = ext2fs_mkdir(fs, EXT2_ROOT_INO, 0, name);
	if (retval) {
		com_err("ext2fs_mkdir", retval, "while creating /lost+found");
		exit(1);
	}

	retval = ext2fs_lookup(fs, EXT2_ROOT_INO, name, strlen(name), 0, &ino);
	if (retval) {
		com_err("ext2_lookup", retval, "while looking up /lost+found");
		exit(1);
	}
	
	for (i=1; i < EXT2_NDIR_BLOCKS; i++) {
		retval = ext2fs_expand_dir(fs, ino);
		if (retval) {
			com_err("ext2fs_expand_dir", retval,
				"while expanding /lost+found");
			exit(1);
		}
	}		
}

static void create_bad_block_inode(ext2_filsys fs, badblocks_list bb_list)
{
	errcode_t	retval;
	
	ext2fs_mark_inode_bitmap(fs->inode_map, EXT2_BAD_INO);
	fs->group_desc[0].bg_free_inodes_count--;
	fs->super->s_free_inodes_count--;
	retval = ext2fs_update_bb_inode(fs, bb_list);
	if (retval) {
		com_err("ext2fs_update_bb_inode", retval,
			"while setting bad block inode");
		exit(1);
	}

}

static void reserve_inodes(ext2_filsys fs)
{
	ino_t	i;
	int	group;

	for (i = EXT2_ROOT_INO + 1; i < EXT2_FIRST_INO; i++) {
		ext2fs_mark_inode_bitmap(fs->inode_map, i);
		group = ext2fs_group_of_ino(fs, i);
		fs->group_desc[group].bg_free_inodes_count--;
		fs->super->s_free_inodes_count--;
	}
	ext2fs_mark_ib_dirty(fs);
}

static void zap_bootblock(ext2_filsys fs)
{
	char buf[512];
	int retval;

	memset(buf, 0, 512);
	
	retval = io_channel_write_blk(fs->io, 0, -512, buf);
	if (retval)
		printf("Warning: could not erase block 0: %s\n", 
		       error_message(retval));
}
	

static void show_stats(ext2_filsys fs)
{
	struct ext2_super_block	*s = fs->super;
	blk_t			group_block;
	int			i, col_left;
	
	if (param.s_blocks_count != s->s_blocks_count)
		printf("warning: %d blocks unused.\n\n",
		       param.s_blocks_count - s->s_blocks_count);
	
	printf("%u inodes, %u blocks\n", s->s_inodes_count,
	       s->s_blocks_count);
	printf("%u blocks (%2.2f%%) reserved for the super user\n",
		s->s_r_blocks_count,
	       100.0 * s->s_r_blocks_count / s->s_blocks_count);
	printf("First data block=%u\n", s->s_first_data_block);
	printf("Block size=%u (log=%u)\n", fs->blocksize,
		s->s_log_block_size);
	printf("Fragment size=%u (log=%u)\n", fs->fragsize,
		s->s_log_frag_size);
	printf("%lu block group%s\n", fs->group_desc_count,
		(fs->group_desc_count > 1) ? "s" : "");
	printf("%u blocks per group, %u fragments per group\n",
	       s->s_blocks_per_group, s->s_frags_per_group);
	printf("%u inodes per group\n", s->s_inodes_per_group);

	if (fs->group_desc_count == 1) {
		printf("\n");
		return;
	}
	
	printf("Superblock backups stored on blocks: ");
	group_block = s->s_first_data_block;
	col_left = 0;
	for (i = 1; i < fs->group_desc_count; i++) {
		group_block += s->s_blocks_per_group;
		if (!col_left--) {
			printf("\n\t");
			col_left = 8;
		}
		printf("%u", group_block);
		if (i != fs->group_desc_count - 1)
			printf(", ");
	}
	printf("\n\n");
}

#define PATH_SET "PATH=/sbin"

static void PRS(int argc, char *argv[])
{
	char	c;
	int	size;
	char	* tmp;
	int	inode_ratio = 4096;
	int	reserved_ratio = 5;
	errcode_t	retval;
	char	*oldpath = getenv("PATH");

	/* Update our PATH to include /sbin  */
	if (oldpath) {
		char *newpath;
		
		newpath = malloc(sizeof (PATH_SET) + 1 + strlen (oldpath));
		strcpy (newpath, PATH_SET);
		strcat (newpath, ":");
		strcat (newpath, oldpath);
		putenv (newpath);
	} else
		putenv (PATH_SET);

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);
	initialize_ext2_error_table();
	memset(&param, 0, sizeof(struct ext2_super_block));
	
	fprintf (stderr, "mke2fs %s, %s for EXT2 FS %s, %s\n",
		 E2FSPROGS_VERSION, E2FSPROGS_DATE,
		 EXT2FS_VERSION, EXT2FS_DATE);
	if (argc && *argv)
		program_name = *argv;
	while ((c = getopt (argc, argv, "b:cf:g:i:l:m:qtvSF")) != EOF)
		switch (c) {
		case 'b':
			size = strtoul(optarg, &tmp, 0);
			if (size < 1024 || size > 4096 || *tmp) {
				com_err(program_name, 0, "bad block size - %s",
					optarg);
				exit(1);
			}
			param.s_log_block_size =
				log2(size >> EXT2_MIN_BLOCK_LOG_SIZE);
			break;
		case 'c':
		case 't':	/* Check for bad blocks */
			cflag = 1;
			break;
		case 'f':
			size = strtoul(optarg, &tmp, 0);
			if (size < 1024 || size > 4096 || *tmp) {
				com_err(program_name, 0, "bad fragment size - %s",
					optarg);
				exit(1);
			}
			param.s_log_frag_size =
				log2(size >> EXT2_MIN_BLOCK_LOG_SIZE);
			printf("Warning: fragments not supported.  "
			       "Ignoring -f option\n");
			break;
		case 'g':
			param.s_blocks_per_group = strtoul(optarg, &tmp, 0);
			if (*tmp) {
				com_err(program_name, 0,
					"Illegal number for blocks per group");
				exit(1);
			}
			if (param.s_blocks_per_group < 256 ||
			    param.s_blocks_per_group > 8192 || *tmp) {
				com_err(program_name, 0,
					"blocks per group count out of range");
				exit(1);
			}
			if ((param.s_blocks_per_group % 8) != 0) {
				com_err(program_name, 0,
				"blocks per group must be multiple of 8");
				exit(1);
			}
			break;
		case 'i':
			inode_ratio = strtoul(optarg, &tmp, 0);
			if (inode_ratio < 1024 || inode_ratio > 256 * 1024 ||
			    *tmp) {
				com_err(program_name, 0, "bad inode ratio - %s",
					optarg);
				exit(1);
			}
			break;
		case 'l':
			bad_blocks_filename = malloc(strlen(optarg)+1);
			if (!bad_blocks_filename) {
				com_err(program_name, ENOMEM,
					"in malloc for bad_blocks_filename");
				exit(1);
			}
			strcpy(bad_blocks_filename, optarg);
			break;
		case 'm':
			reserved_ratio = strtoul(optarg, &tmp, 0);
			if (reserved_ratio > 50 || *tmp) {
				com_err(program_name, 0,
					"bad reserved blocks percent - %s",
					optarg);
				exit(1);
			}
			break;
		case 'v':
			verbose = 1;
			break;
		case 'q':
			quiet = 1;
			break;
		case 'F':
			force = 1;
			break;
		case 'S':
			super_only = 1;
			break;
		default:
			usage();
		}
	if (optind == argc)
		usage();
	device_name = argv[optind];
	optind++;
	if (optind < argc) {
		param.s_blocks_count = strtoul(argv[optind++], &tmp, 0);
		if (*tmp) {
			com_err(program_name, 0, "bad blocks count - %s",
				argv[optind - 1]);
			exit(1);
		}
	}
	if (optind < argc)
		usage();

	if (!force)
		check_plausibility();
	check_mount();

	param.s_log_frag_size = param.s_log_block_size;

	if (!param.s_blocks_count) {
		retval = ext2fs_get_device_size(device_name,
						EXT2_BLOCK_SIZE(&param),
						&param.s_blocks_count);
		if (retval) {
			com_err(program_name, 0,
				"while trying to determine filesystem size");
			exit(1);
		}
	}

	/*
	 * Calculate number of inodes based on the inode ratio
	 */
	param.s_inodes_count =
		((long long) param.s_blocks_count * EXT2_BLOCK_SIZE(&param))
			/ inode_ratio;

	/*
	 * Calculate number of blocks to reserve
	 */
	param.s_r_blocks_count = (param.s_blocks_count * reserved_ratio) / 100;
}
					
int main (int argc, char *argv[])
{
	errcode_t	retval = 0;
	ext2_filsys	fs;
	badblocks_list	bb_list = 0;
	
	PRS(argc, argv);

	/*
	 * Initialize the superblock....
	 */
	retval = ext2fs_initialize(device_name, 0, &param,
				   unix_io_manager, &fs);
	if (retval) {
		com_err(device_name, retval, "while setting up superblock");
		exit(1);
	}

	if (!quiet)
		show_stats(fs);

	if (bad_blocks_filename)
		read_bb_file(fs, &bb_list, bad_blocks_filename);
	if (cflag)
		test_disk(fs, &bb_list);

	handle_bad_blocks(fs, bb_list);
	alloc_tables(fs);
	if (super_only) {
		fs->super->s_state |= EXT2_ERROR_FS;
		fs->flags &= ~(EXT2_FLAG_IB_DIRTY|EXT2_FLAG_BB_DIRTY);
	} else {
		create_root_dir(fs);
		create_lost_and_found(fs);
		reserve_inodes(fs);
		create_bad_block_inode(fs, bb_list);
		zap_bootblock(fs);
	}
	
	if (!quiet)
		printf("Writing superblocks and "
		       "filesystem accounting information: ");
	ext2fs_close(fs);
	if (!quiet)
		printf("done\n");
	return 0;
}
