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

#include <string.h>
#include <fcntl.h>
#include <ctype.h>
#include <termios.h>
#include <time.h>
#include <getopt.h>
#include <unistd.h>
#include <stdlib.h>
#include <mntent.h>
#include <malloc.h>
#include <sys/ioctl.h>
#include <linux/ext2_fs.h>
#include <linux/fs.h>

#include "et/com_err.h"
#include "ext2fs/ext2fs.h"
#include "../version.h"

#define STRIDE_LENGTH 8

extern int isatty(int);

const char * program_name = "mke2fs";
const char * device_name = NULL;

/* Command line options */
int	cflag = 0;
int	verbose = 0;
int	quiet = 0;
char	*bad_blocks_filename = 0;

struct ext2_super_block param;

static void usage(NOARGS)
{
	fprintf(stderr,
		"Usage: %s [-c|-t|-l filename] [-b block-size] "
		"[-f fragment-size]\n\t[-i bytes-per-inode] "
		"[-m reserved-blocks-percentage] [-v]\n"
		"\tdevice [blocks-count]\n",
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

static long valid_offset (int fd, int offset)
{
	char ch;

	if (lseek (fd, offset, 0) < 0)
		return 0;
	if (read (fd, &ch, 1) < 1)
		return 0;
	return 1;
}

static int count_blocks (int fd)
{
	int high, low;

	low = 0;
	for (high = 1; valid_offset (fd, high); high *= 2)
		low = high;
	while (low < high - 1)
	{
		const int mid = (low + high) / 2;

		if (valid_offset (fd, mid))
			low = mid;
		else
			high = mid;
	}
	valid_offset (fd, 0);
	return (low + 1) / 1024;
}

static int get_size(const char  *file)
{
	int	fd;
	int	size;

	fd = open(file, O_RDWR);
	if (fd < 0) {
		com_err("open", errno, "while trying to determine size of %s",
			file);
		exit(1);
	}
	if (ioctl(fd, BLKGETSIZE, &size) >= 0) {
		close(fd);
		return size / (EXT2_BLOCK_SIZE(&param) / 512);
	}
		
	size = count_blocks(fd);
	close(fd);
	return size;
}

static void check_mount(NOARGS)
{
	FILE * f;
	struct mntent * mnt;

	if ((f = setmntent (MOUNTED, "r")) == NULL)
		return;
	while ((mnt = getmntent (f)) != NULL)
		if (strcmp (device_name, mnt->mnt_fsname) == 0)
			break;
	endmntent (f);
	if (!mnt)
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
	printf("Bad block %lu out of range; ignored.\n", blk);
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

	sprintf(buf, "badblocks %s%s %ld", quiet ? "" : "-s ",
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
	fclose (f);
	if (retval) {
		com_err("ext2fs_read_bb_FILE", retval,
			"while processing list of bad blocks from program");
		exit(1);
	}
}

static void handle_bad_blocks(ext2_filsys fs, badblocks_list bb_list)
{
	int			i;
	int			must_be_good;
	blk_t			blk;
	badblocks_iterate	bb_iter;
	errcode_t		retval;

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
			fprintf(stderr, "Blocks %ld through %d must be good "
				"in order to build a filesystem.\n",
				fs->super->s_first_data_block, must_be_good);
			fprintf(stderr, "Aborting....\n");
			exit(1);
		}
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
		ext2fs_mark_block_bitmap(fs, fs->block_map, blk);
	badblocks_list_iterate_end(bb_iter);
}

static void new_table_block(ext2_filsys fs, blk_t first_block,
			    const char *name, int num, const char *buf,
			    blk_t *new_block)
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
	blk = *new_block;
	for (i=0; i < num; i += STRIDE_LENGTH, blk += STRIDE_LENGTH) {
		if (num-i > STRIDE_LENGTH)
			count = STRIDE_LENGTH;
		else
			count = num - i;
		retval = io_channel_write_blk(fs->io, blk, count, buf);
		if (retval)
			printf("Warning: could not write %d blocks starting "
			       "at %ld for %s: %s\n",
			       count, blk, name, error_message(retval));
	}
	blk = *new_block;
	for (i = 0; i < num; i++, blk++)
		ext2fs_mark_block_bitmap(fs, fs->block_map, blk);
}	

static void alloc_tables(ext2_filsys fs)
{
	blk_t	group_blk;
	int	i;
	char	*buf;
	int	numblocks;

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
		new_table_block(fs, group_blk, "block bitmap", 1, buf,
				&fs->group_desc[i].bg_block_bitmap);
		new_table_block(fs, group_blk, "inode bitmap", 1, buf,
				&fs->group_desc[i].bg_inode_bitmap);
		new_table_block(fs, group_blk, "inode table",
				fs->inode_blocks_per_group, buf,
				&fs->group_desc[i].bg_inode_table);
		
		if (i == fs->group_desc_count-1) {
			numblocks = (fs->super->s_blocks_count -
				     fs->super->s_first_data_block) %
					     fs->super->s_blocks_per_group;
			if (!numblocks)
				numblocks = fs->super->s_blocks_per_group;
		} else
			numblocks = fs->super->s_blocks_per_group;
		numblocks -= 3 + fs->desc_blocks + fs->inode_blocks_per_group;
		
		fs->group_desc[i].bg_free_blocks_count = numblocks;
		fs->group_desc[i].bg_free_inodes_count =
			fs->super->s_inodes_per_group;
		fs->group_desc[i].bg_used_dirs_count = 0;
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

	retval = ext2fs_mkdir(fs, EXT2_ROOT_INO, EXT2_ROOT_INO, 0);
	if (retval) {
		com_err("ext2fs_mkdir", retval, "while creating root dir");
		exit(1);
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
	
	ext2fs_mark_inode_bitmap(fs, fs->inode_map, EXT2_BAD_INO);
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
		ext2fs_mark_inode_bitmap (fs, fs->inode_map, i);
		group = ext2fs_group_of_ino(fs, i);
		fs->group_desc[group].bg_free_inodes_count--;
		fs->super->s_free_inodes_count--;
	}
	ext2fs_mark_ib_dirty(fs);
}

static void show_stats(ext2_filsys fs)
{
	struct ext2_super_block	*s = fs->super;
	blk_t			group_block;
	int			i, col_left;
	
	if (param.s_blocks_count != s->s_blocks_count)
		printf("warning: %ld blocks unused.\n\n",
		       param.s_blocks_count - s->s_blocks_count);
	
	printf("%lu inodes, %lu blocks\n", s->s_inodes_count,
	       s->s_blocks_count);
	printf("%lu blocks (%2.2f%%) reserved for the super user\n",
		s->s_r_blocks_count,
	       100.0 * s->s_r_blocks_count / s->s_blocks_count);
	printf("First data block=%lu\n", s->s_first_data_block);
	printf("Block size=%u (log=%lu)\n", fs->blocksize,
		s->s_log_block_size);
	printf("Fragment size=%u (log=%lu)\n", fs->fragsize,
		s->s_log_frag_size);
	printf("%lu block group%s\n", fs->group_desc_count,
		(fs->group_desc_count > 1) ? "s" : "");
	printf("%lu blocks per group, %lu fragments per group\n",
	       s->s_blocks_per_group, s->s_frags_per_group);
	printf("%lu inodes per group\n", s->s_inodes_per_group);

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
		printf("%lu", group_block);
		if (i != fs->group_desc_count - 1)
			printf(", ");
	}
	printf("\n\n");
}

static void PRS(int argc, char *argv[])
{
	char	c;
	int	size;
	char	* tmp;
	char *oldpath, newpath[PATH_MAX];
	int	inode_ratio = 4096;
	int	reserved_ratio = 5;

	/* Update our PATH to include /sbin  */
	strcpy(newpath, "PATH=/sbin:");
	if ((oldpath = getenv("PATH")) != NULL)
		strcat(newpath, oldpath);
	putenv(newpath);

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);
	initialize_ext2_error_table();
	memset(&param, 0, sizeof(struct ext2_super_block));
	
	fprintf (stderr, "mke2fs %s, %s for EXT2 FS %s, %s\n",
		 E2FSPROGS_VERSION, E2FSPROGS_DATE,
		 EXT2FS_VERSION, EXT2FS_DATE);
	if (argc && *argv)
		program_name = *argv;
	while ((c = getopt (argc, argv, "b:cf:g:i:l:m:qtv")) != EOF)
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
			if (param.s_blocks_per_group < 256 ||
			    param.s_blocks_per_group > 8192 || *tmp) {
				com_err(program_name, 0,
					"bad blocks per group count - %s",
					optarg);
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
			bad_blocks_filename = strdup(optarg);
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
	param.s_log_frag_size = param.s_log_block_size;

	if (!param.s_blocks_count)
		param.s_blocks_count = get_size(device_name);

	/*
	 * Calculate number of inodes based on the inode ratio
	 */
	param.s_inodes_count =
		(param.s_blocks_count * EXT2_BLOCK_SIZE(&param)) / inode_ratio;

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

	check_mount();

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
	create_root_dir(fs);
	create_lost_and_found(fs);
	reserve_inodes(fs);
	create_bad_block_inode(fs, bb_list);
	
	if (!quiet)
		printf("Writing superblocks and "
		       "filesystem accounting information: ");
	ext2fs_close(fs);
	if (!quiet)
		printf("done\n");
	return 0;
}
