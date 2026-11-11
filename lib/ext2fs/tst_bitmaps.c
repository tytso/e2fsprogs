/*
 * tst_bitmaps.c
 *
 * Copyright (C) 2011 Theodore Ts'o.
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Library
 * General Public License, version 2.
 * %End-Header%
 */

#include "config.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#ifdef HAVE_GETOPT_H
#include <getopt.h>
#endif
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "ss/ss.h"

#include "ext2_fs.h"
#include "ext2fs.h"
#include "ext2fsP.h"

extern ss_request_table tst_bitmaps_cmds;

static char subsystem_name[] = "tst_bitmaps";
static char version[] = "1.0";

ext2_filsys	test_fs;
int		exit_status = 0;
static unsigned int saved_type = EXT2FS_BMAP64_BITARRAY;
static int	saved_flags = EXT2_FLAG_64BITS;

static int source_file(const char *cmd_file, int sci_idx)
{
	FILE		*f;
	char		buf[256];
	char		*cp;
	int		retval;
	int 		noecho;

	if (strcmp(cmd_file, "-") == 0)
		f = stdin;
	else {
		f = fopen(cmd_file, "r");
		if (!f) {
			perror(cmd_file);
			exit(1);
		}
	}
	fflush(stdout);
	fflush(stderr);
	setbuf(stdout, NULL);
	setbuf(stderr, NULL);
	while (!feof(f)) {
		if (fgets(buf, sizeof(buf), f) == NULL)
			break;
		if (buf[0] == '#')
			continue;
		noecho = 0;
		if (buf[0] == '-') {
			noecho = 1;
			buf[0] = ' ';
		}
		cp = strchr(buf, '\n');
		if (cp)
			*cp = 0;
		cp = strchr(buf, '\r');
		if (cp)
			*cp = 0;
		if (!noecho)
			printf("%s: %s\n", subsystem_name, buf);
		retval = ss_execute_line(sci_idx, buf);
		if (retval) {
			ss_perror(sci_idx, retval, buf);
			exit_status++;
		}
	}
	return exit_status;
}


/*
 * This function resets the libc getopt() function, which keeps
 * internal state.  Bad design!  Stupid libc API designers!  No
 * biscuit!
 *
 * BSD-derived getopt() functions require that optind be reset to 1 in
 * order to reset getopt() state.  This used to be generally accepted
 * way of resetting getopt().  However, glibc's getopt()
 * has additional getopt() state beyond optind, and requires that
 * optind be set zero to reset its state.  So the unfortunate state of
 * affairs is that BSD-derived versions of getopt() misbehave if
 * optind is set to 0 in order to reset getopt(), and glibc's getopt()
 * will core dump if optind is set 1 in order to reset getopt().
 *
 * More modern versions of BSD require that optreset be set to 1 in
 * order to reset getopt().   Sigh.  Standards, anyone?
 *
 * We hide the hair here.
 */
void reset_getopt(void)
{
#if defined(__GLIBC__) || defined(__linux__)
	optind = 0;
#else
	optind = 1;
#endif
#ifdef HAVE_OPTRESET
	optreset = 1;		/* Makes BSD getopt happy */
#endif
}

/*
 * This function will convert a string to an unsigned long, printing
 * an error message if it fails, and returning success or failure in err.
 */
unsigned long parse_ulong(const char *str, const char *cmd,
			  const char *descr, int *err)
{
	char		*tmp;
	unsigned long	ret;

	ret = strtoul(str, &tmp, 0);
	if (*tmp == 0) {
		if (err)
			*err = 0;
		return ret;
	}
	com_err(cmd, 0, "Bad %s - %s", descr, str);
	if (err)
		*err = 1;
	else
		exit(1);
	return 0;
}


int check_fs_open(const char *name)
{
	if (!test_fs) {
		com_err(name, 0, "Filesystem not open");
		return 1;
	}
	return 0;
}

static void setup_filesystem(const char *name,
			     unsigned int blocks, unsigned int inodes,
			     unsigned int type, int flags)
{
	struct ext2_super_block param;
	errcode_t retval;

	memset(&param, 0, sizeof(param));
	ext2fs_blocks_count_set(&param, blocks);
	param.s_inodes_count = inodes;

	retval = ext2fs_initialize("test fs", flags, &param,
				   test_io_manager, &test_fs);

	if (retval) {
		com_err(name, retval, "while initializing filesystem");
		return;
	}
	test_fs->default_bitmap_type = type;
	ext2fs_free_block_bitmap(test_fs->block_map);
	test_fs->block_map = 0;
	ext2fs_free_inode_bitmap(test_fs->inode_map);
	test_fs->inode_map = 0;
	retval = ext2fs_allocate_block_bitmap(test_fs, "block bitmap",
					      &test_fs->block_map);
	if (retval) {
		com_err(name, retval, "while allocating block bitmap");
		goto errout;
	}
	retval = ext2fs_allocate_inode_bitmap(test_fs, "inode bitmap",
					      &test_fs->inode_map);
	if (retval) {
		com_err(name, retval, "while allocating inode bitmap");
		goto errout;
	}
	return;

errout:
	ext2fs_close_free(&test_fs);
}

void setup_cmd(int argc, ss_argv_t argv,
	       int sci_idx EXT2FS_ATTR((unused)),
	       void *infop EXT2FS_ATTR((unused)))
{
	int		c, err;
	unsigned int	blocks = 128;
	unsigned int	inodes = 0;
	unsigned int	type = saved_type;
	int		flags = saved_flags;

	if (test_fs)
		ext2fs_close_free(&test_fs);

	reset_getopt();
	while ((c = getopt(argc, argv, "b:i:lt:")) != EOF) {
		switch (c) {
		case 'b':
			blocks = parse_ulong(optarg, argv[0],
					     "number of blocks", &err);
			if (err)
				return;
			break;
		case 'i':
			inodes = parse_ulong(optarg, argv[0],
					     "number of blocks", &err);
			if (err)
				return;
			break;
		case 'l':	/* Legacy bitmaps */
			flags = 0;
			break;
		case 't':
			type = parse_ulong(optarg, argv[0],
					   "bitmap backend type", &err);
			if (err)
				return;
			break;
		default:
			fprintf(stderr, "%s: usage: setup [-b blocks] "
				"[-i inodes] [-t type]\n", argv[0]);
			return;
		}
	}
	setup_filesystem(argv[0], blocks, inodes, type, flags);
}

void close_cmd(int argc, char **argv, int sci_idx EXT2FS_ATTR((unused)),
	       void *infop EXT2FS_ATTR((unused)))
{
	if (check_fs_open(argv[0]))
		return;

	ext2fs_close_free(&test_fs);
}


void dump_bitmap(ext2fs_generic_bitmap bmap, unsigned int start, unsigned num)
{
	unsigned char	*buf;
	errcode_t	retval;
	int		i, len = (num - start + 7) / 8;

	buf = malloc(len);
	if (!buf) {
		com_err("dump_bitmap", 0, "couldn't allocate buffer");
		return;
	}
	memset(buf, 0, len);
	retval = ext2fs_get_generic_bmap_range(bmap, (__u64) start, num, buf);
	if (retval) {
		com_err("dump_bitmap", retval, 
			"while calling ext2fs_generic_bmap_range");
		free(buf);
		return;
	}
	for (i=0; i < len; i++)
		printf("%02x", buf[i]);
	printf("\n");
	printf("bits set: %u\n", ext2fs_bitcount(buf, len));
	free(buf);
}

void dump_inode_bitmap_cmd(int argc, ss_argv_t argv,
			   int sci_idx EXT2FS_ATTR((unused)),
			   void *infop EXT2FS_ATTR((unused)))
{
	if (check_fs_open(argv[0]))
		return;

	printf("inode bitmap: ");
	dump_bitmap(test_fs->inode_map, 1, test_fs->super->s_inodes_count);
}
	
void dump_block_bitmap_cmd(int argc, ss_argv_t argv,
			   int sci_idx EXT2FS_ATTR((unused)),
			   void *infop EXT2FS_ATTR((unused)))
{
	if (check_fs_open(argv[0]))
		return;

	printf("block bitmap: ");
	dump_bitmap(test_fs->block_map, test_fs->super->s_first_data_block,
		    test_fs->super->s_blocks_count);
}
	
void do_setb(int argc, ss_argv_t argv,
	     int sci_idx EXT2FS_ATTR((unused)),
	     void *infop EXT2FS_ATTR((unused)))
{
	unsigned int block, num;
	int err;
	int test_result, op_result;

	if (check_fs_open(argv[0]))
		return;

	if (argc != 2 && argc != 3) {
		com_err(argv[0], 0, "Usage: setb <block> [num]");
		return;
	}

	block = parse_ulong(argv[1], argv[0], "block", &err);
	if (err)
		return;

	if (argc == 3) {
		num = parse_ulong(argv[2], argv[0], "num", &err);
		if (err)
			return;

		ext2fs_mark_block_bitmap_range2(test_fs->block_map,
						block, num);
		printf("Marking blocks %u to %u\n", block, block + num - 1);
		return;
	}

	test_result = ext2fs_test_block_bitmap2(test_fs->block_map, block);
	op_result = ext2fs_mark_block_bitmap2(test_fs->block_map, block);
	printf("Setting block %u, was %s before\n", block, op_result ?
	       "set" : "clear");
	if (!test_result != !op_result)
		com_err(argv[0], 0, "*ERROR* test_result different! (%d, %d)",
			test_result, op_result);
}

void do_clearb(int argc, ss_argv_t argv,
	       int sci_idx EXT2FS_ATTR((unused)),
	       void *infop EXT2FS_ATTR((unused)))
{
	unsigned int block, num;
	int err;
	int test_result, op_result;

	if (check_fs_open(argv[0]))
		return;

	if (argc != 2 && argc != 3) {
		com_err(argv[0], 0, "Usage: clearb <block> [num]");
		return;
	}

	block = parse_ulong(argv[1], argv[0], "block", &err);
	if (err)
		return;

	if (argc == 3) {
		num = parse_ulong(argv[2], argv[0], "num", &err);
		if (err)
			return;

		ext2fs_unmark_block_bitmap_range2(test_fs->block_map,
						block, num);
		printf("Clearing blocks %u to %u\n", block, block + num - 1);
		return;
	}

	test_result = ext2fs_test_block_bitmap2(test_fs->block_map, block);
	op_result = ext2fs_unmark_block_bitmap2(test_fs->block_map, block);
	printf("Clearing block %u, was %s before\n", block, op_result ?
	       "set" : "clear");
	if (!test_result != !op_result)
		com_err(argv[0], 0, "*ERROR* test_result different! (%d, %d)",
			test_result, op_result);
}

void do_testb(int argc, ss_argv_t argv,
	      int sci_idx EXT2FS_ATTR((unused)),
	      void *infop EXT2FS_ATTR((unused)))
{
	unsigned int block, num;
	int err;
	int test_result;

	if (check_fs_open(argv[0]))
		return;

	if (argc != 2 && argc != 3) {
		com_err(argv[0], 0, "Usage: testb <block> [num]");
		return;
	}

	block = parse_ulong(argv[1], argv[0], "block", &err);
	if (err)
		return;

	if (argc == 3) {
		num = parse_ulong(argv[2], argv[0], "num", &err);
		if (err)
			return;

		test_result =
			ext2fs_test_block_bitmap_range2(test_fs->block_map,
							block, num);
		printf("Blocks %u to %u are %sall clear.\n",
		       block, block + num - 1, test_result ? "" : "NOT ");
		return;
	}

	test_result = ext2fs_test_block_bitmap2(test_fs->block_map, block);
	printf("Block %u is %s\n", block, test_result ? "set" : "clear");
}

void do_ffzb(int argc, ss_argv_t argv,
	     int sci_idx EXT2FS_ATTR((unused)),
	     void *infop EXT2FS_ATTR((unused)))
{
	unsigned int start, end;
	int err;
	errcode_t retval;
	blk64_t out;

	if (check_fs_open(argv[0]))
		return;

	if (argc != 3 && argc != 3) {
		com_err(argv[0], 0, "Usage: ffzb <start> <end>");
		return;
	}

	start = parse_ulong(argv[1], argv[0], "start", &err);
	if (err)
		return;

	end = parse_ulong(argv[2], argv[0], "end", &err);
	if (err)
		return;

	retval = ext2fs_find_first_zero_block_bitmap2(test_fs->block_map,
						      start, end, &out);
	if (retval) {
		printf("ext2fs_find_first_zero_block_bitmap2() returned %s\n",
		       error_message(retval));
		return;
	}
	printf("First unmarked block is %llu\n", (unsigned long long) out);
}

void do_ffsb(int argc, ss_argv_t argv,
	     int sci_idx EXT2FS_ATTR((unused)),
	     void *infop EXT2FS_ATTR((unused)))
{
	unsigned int start, end;
	int err;
	errcode_t retval;
	blk64_t out;

	if (check_fs_open(argv[0]))
		return;

	if (argc != 3 && argc != 3) {
		com_err(argv[0], 0, "Usage: ffsb <start> <end>");
		return;
	}

	start = parse_ulong(argv[1], argv[0], "start", &err);
	if (err)
		return;

	end = parse_ulong(argv[2], argv[0], "end", &err);
	if (err)
		return;

	retval = ext2fs_find_first_set_block_bitmap2(test_fs->block_map,
						      start, end, &out);
	if (retval) {
		printf("ext2fs_find_first_set_block_bitmap2() returned %s\n",
		       error_message(retval));
		return;
	}
	printf("First marked block is %llu\n", (unsigned long long) out);
}


void do_zerob(int argc, ss_argv_t argv,
	      int sci_idx EXT2FS_ATTR((unused)),
	      void *infop EXT2FS_ATTR((unused)))
{
	if (check_fs_open(argv[0]))
		return;

	printf("Clearing block bitmap.\n");
	ext2fs_clear_block_bitmap(test_fs->block_map);
}

void do_seti(int argc, ss_argv_t argv,
	     int sci_idx EXT2FS_ATTR((unused)),
	     void *infop EXT2FS_ATTR((unused)))
{
	unsigned int inode;
	int err;
	int test_result, op_result;

	if (check_fs_open(argv[0]))
		return;

	if (argc != 2) {
		com_err(argv[0], 0, "Usage: seti <inode>");
		return;
	}

	inode = parse_ulong(argv[1], argv[0], "inode", &err);
	if (err)
		return;

	test_result = ext2fs_test_inode_bitmap2(test_fs->inode_map, inode);
	op_result = ext2fs_mark_inode_bitmap2(test_fs->inode_map, inode);
	printf("Setting inode %u, was %s before\n", inode, op_result ?
	       "set" : "clear");
	if (!test_result != !op_result) {
		com_err(argv[0], 0, "*ERROR* test_result different! (%d, %d)",
			test_result, op_result);
		exit_status++;
	}
}

void do_cleari(int argc, ss_argv_t argv,
	       int sci_idx EXT2FS_ATTR((unused)),
	       void *infop EXT2FS_ATTR((unused)))
{
	unsigned int inode;
	int err;
	int test_result, op_result;

	if (check_fs_open(argv[0]))
		return;

	if (argc != 2) {
		com_err(argv[0], 0, "Usage: clearb <inode>");
		return;
	}

	inode = parse_ulong(argv[1], argv[0], "inode", &err);
	if (err)
		return;

	test_result = ext2fs_test_inode_bitmap2(test_fs->inode_map, inode);
	op_result = ext2fs_unmark_inode_bitmap2(test_fs->inode_map, inode);
	printf("Clearing inode %u, was %s before\n", inode, op_result ?
	       "set" : "clear");
	if (!test_result != !op_result) {
		com_err(argv[0], 0, "*ERROR* test_result different! (%d, %d)",
			test_result, op_result);
		exit_status++;
	}
}

void do_testi(int argc, ss_argv_t argv,
	      int sci_idx EXT2FS_ATTR((unused)),
	      void *infop EXT2FS_ATTR((unused)))
{
	unsigned int inode;
	int err;
	int test_result;

	if (check_fs_open(argv[0]))
		return;

	if (argc != 2) {
		com_err(argv[0], 0, "Usage: testb <inode>");
		return;
	}

	inode = parse_ulong(argv[1], argv[0], "inode", &err);
	if (err)
		return;

	test_result = ext2fs_test_inode_bitmap2(test_fs->inode_map, inode);
	printf("Inode %u is %s\n", inode, test_result ? "set" : "clear");
}

void do_ffzi(int argc, ss_argv_t argv,
	     int sci_idx EXT2FS_ATTR((unused)),
	     void *infop EXT2FS_ATTR((unused)))
{
	unsigned int start, end;
	int err;
	errcode_t retval;
	ext2_ino_t out;

	if (check_fs_open(argv[0]))
		return;

	if (argc != 3 && argc != 3) {
		com_err(argv[0], 0, "Usage: ffzi <start> <end>");
		return;
	}

	start = parse_ulong(argv[1], argv[0], "start", &err);
	if (err)
		return;

	end = parse_ulong(argv[2], argv[0], "end", &err);
	if (err)
		return;

	retval = ext2fs_find_first_zero_inode_bitmap2(test_fs->inode_map,
						      start, end, &out);
	if (retval) {
		printf("ext2fs_find_first_zero_inode_bitmap2() returned %s\n",
		       error_message(retval));
		return;
	}
	printf("First unmarked inode is %u\n", out);
}

void do_ffsi(int argc, ss_argv_t argv,
	     int sci_idx EXT2FS_ATTR((unused)),
	     void *infop EXT2FS_ATTR((unused)))
{
	unsigned int start, end;
	int err;
	errcode_t retval;
	ext2_ino_t out;

	if (check_fs_open(argv[0]))
		return;

	if (argc != 3 && argc != 3) {
		com_err(argv[0], 0, "Usage: ffsi <start> <end>");
		return;
	}

	start = parse_ulong(argv[1], argv[0], "start", &err);
	if (err)
		return;

	end = parse_ulong(argv[2], argv[0], "end", &err);
	if (err)
		return;

	retval = ext2fs_find_first_set_inode_bitmap2(test_fs->inode_map,
						     start, end, &out);
	if (retval) {
		printf("ext2fs_find_first_set_inode_bitmap2() returned %s\n",
		       error_message(retval));
		return;
	}
	printf("First marked inode is %u\n", out);
}

void do_zeroi(int argc, ss_argv_t argv,
	      int sci_idx EXT2FS_ATTR((unused)),
	      void *infop EXT2FS_ATTR((unused)))
{
	if (check_fs_open(argv[0]))
		return;

	printf("Clearing inode bitmap.\n");
	ext2fs_clear_inode_bitmap(test_fs->inode_map);
}

void do_copy_cmp(int argc EXT2FS_ATTR((unused)), ss_argv_t argv,
		 int sci_idx EXT2FS_ATTR((unused)),
		 void *infop EXT2FS_ATTR((unused)))
{
	ext2fs_block_bitmap bcopy = NULL;
	ext2fs_inode_bitmap icopy = NULL;
	errcode_t retval;

	if (check_fs_open(argv[0]))
		return;

	retval = ext2fs_copy_bitmap(test_fs->block_map, &bcopy);
	if (retval) {
		com_err(argv[0], retval, "copying block bitmap");
		exit_status++;
		return;
	}
	retval = ext2fs_copy_bitmap(test_fs->inode_map, &icopy);
	if (retval) {
		com_err(argv[0], retval, "copying inode bitmap");
		ext2fs_free_block_bitmap(bcopy);
		exit_status++;
		return;
	}

	retval = ext2fs_compare_block_bitmap(test_fs->block_map, bcopy);
	if (retval) {
		com_err(argv[0], retval, "comparing identical block bitmaps");
		exit_status++;
	}
	retval = ext2fs_compare_inode_bitmap(test_fs->inode_map, icopy);
	if (retval) {
		com_err(argv[0], retval, "comparing identical inode bitmaps");
		exit_status++;
	}

	/* Flip interior bit 3 in both copies and verify mismatch error codes */
	if (ext2fs_test_block_bitmap2(bcopy, 3))
		ext2fs_unmark_block_bitmap2(bcopy, 3);
	else
		ext2fs_mark_block_bitmap2(bcopy, 3);
	retval = ext2fs_compare_block_bitmap(test_fs->block_map, bcopy);
	if (retval != EXT2_ET_NEQ_BLOCK_BITMAP) {
		com_err(argv[0], retval, "expected EXT2_ET_NEQ_BLOCK_BITMAP");
		exit_status++;
	}

	if (ext2fs_test_inode_bitmap2(icopy, 3))
		ext2fs_unmark_inode_bitmap2(icopy, 3);
	else
		ext2fs_mark_inode_bitmap2(icopy, 3);
	retval = ext2fs_compare_inode_bitmap(test_fs->inode_map, icopy);
	if (retval != EXT2_ET_NEQ_INODE_BITMAP) {
		com_err(argv[0], retval, "expected EXT2_ET_NEQ_INODE_BITMAP");
		exit_status++;
	}

	ext2fs_free_block_bitmap(bcopy);
	ext2fs_free_inode_bitmap(icopy);
	printf("Copy and compare block/inode bitmaps: OK\n");
}

void do_fudge_b(int argc, ss_argv_t argv,
		int sci_idx EXT2FS_ATTR((unused)),
		void *infop EXT2FS_ATTR((unused)))
{
	unsigned int end;
	int err;
	blk64_t oend = 0;
	errcode_t retval;

	if (check_fs_open(argv[0]))
		return;
	if (argc != 2) {
		com_err(argv[0], 0, "Usage: fudge_b <end>");
		exit_status++;
		return;
	}
	end = parse_ulong(argv[1], argv[0], "end", &err);
	if (err)
		return;
	retval = ext2fs_fudge_block_bitmap_end2(test_fs->block_map, end, &oend);
	if (retval) {
		printf("fudge_b(%u) returned %s\n", end, error_message(retval));
		return;
	}
	printf("Fudged block bitmap end from %llu to %llu\n",
	       (unsigned long long) oend,
	       (unsigned long long) ext2fs_get_block_bitmap_end2(
		       test_fs->block_map));
}

void do_fudge_i(int argc, ss_argv_t argv,
		int sci_idx EXT2FS_ATTR((unused)),
		void *infop EXT2FS_ATTR((unused)))
{
	unsigned int end;
	int err;
	ext2_ino_t oend = 0;
	errcode_t retval;

	if (check_fs_open(argv[0]))
		return;
	if (argc != 2) {
		com_err(argv[0], 0, "Usage: fudge_i <end>");
		exit_status++;
		return;
	}
	end = parse_ulong(argv[1], argv[0], "end", &err);
	if (err)
		return;
	retval = ext2fs_fudge_inode_bitmap_end(test_fs->inode_map, end, &oend);
	if (retval) {
		printf("fudge_i(%u) returned %s\n", end, error_message(retval));
		return;
	}
	printf("Fudged inode bitmap end from %u to %llu\n", oend,
	       (unsigned long long) ext2fs_get_inode_bitmap_end2(
		       test_fs->inode_map));
}

void do_resize_b(int argc, ss_argv_t argv,
		 int sci_idx EXT2FS_ATTR((unused)),
		 void *infop EXT2FS_ATTR((unused)))
{
	unsigned int new_end, new_real_end;
	int err;
	errcode_t retval;

	if (check_fs_open(argv[0]))
		return;
	if (argc != 3) {
		com_err(argv[0], 0, "Usage: resize_b <new_end> <new_real_end>");
		exit_status++;
		return;
	}
	new_end = parse_ulong(argv[1], argv[0], "new_end", &err);
	if (err)
		return;
	new_real_end = parse_ulong(argv[2], argv[0], "new_real_end", &err);
	if (err)
		return;
	retval = ext2fs_resize_block_bitmap2(new_end, new_real_end,
					     test_fs->block_map);
	if (retval) {
		com_err(argv[0], retval, "resizing block bitmap");
		exit_status++;
		return;
	}
	ext2fs_blocks_count_set(test_fs->super, new_end + 1);
	printf("Resized block bitmap to end=%u, real_end=%u\n",
	       new_end, new_real_end);
}

void do_resize_i(int argc, ss_argv_t argv,
		 int sci_idx EXT2FS_ATTR((unused)),
		 void *infop EXT2FS_ATTR((unused)))
{
	unsigned int new_end, new_real_end;
	int err;
	errcode_t retval;

	if (check_fs_open(argv[0]))
		return;
	if (argc != 3) {
		com_err(argv[0], 0, "Usage: resize_i <new_end> <new_real_end>");
		exit_status++;
		return;
	}
	new_end = parse_ulong(argv[1], argv[0], "new_end", &err);
	if (err)
		return;
	new_real_end = parse_ulong(argv[2], argv[0], "new_real_end", &err);
	if (err)
		return;
	retval = ext2fs_resize_inode_bitmap2(new_end, new_real_end,
					     test_fs->inode_map);
	if (retval) {
		com_err(argv[0], retval, "resizing inode bitmap");
		exit_status++;
		return;
	}
	test_fs->super->s_inodes_count = new_end;
	printf("Resized inode bitmap to end=%u, real_end=%u\n",
	       new_end, new_real_end);
}

void do_pad_b(int argc, ss_argv_t argv,
	      int sci_idx EXT2FS_ATTR((unused)),
	      void *infop EXT2FS_ATTR((unused)))
{
	unsigned int num;
	int err;

	if (check_fs_open(argv[0]))
		return;
	if (argc != 2) {
		com_err(argv[0], 0, "Usage: pad_b <dump_bits>");
		exit_status++;
		return;
	}
	num = parse_ulong(argv[1], argv[0], "dump_bits", &err);
	if (err)
		return;
	ext2fs_set_bitmap_padding(test_fs->block_map);
	printf("padded block bitmap: ");
	dump_bitmap(test_fs->block_map, test_fs->super->s_first_data_block,
		    num);
}

void do_set_range_b(int argc, ss_argv_t argv,
		    int sci_idx EXT2FS_ATTR((unused)),
		    void *infop EXT2FS_ATTR((unused)))
{
	unsigned int num, fill, bytes;
	unsigned char *inbuf, *outbuf;
	int err;
	errcode_t retval;

	if (check_fs_open(argv[0]))
		return;
	if (argc != 3) {
		com_err(argv[0], 0, "Usage: set_range_b <num> <byte>");
		exit_status++;
		return;
	}
	num = parse_ulong(argv[1], argv[0], "num", &err);
	if (err)
		return;
	fill = parse_ulong(argv[2], argv[0], "byte", &err) & 0xff;
	if (err)
		return;
	bytes = (num + 7) / 8;
	inbuf = malloc(bytes);
	outbuf = calloc(1, bytes);
	if (!inbuf || !outbuf) {
		free(inbuf);
		free(outbuf);
		exit(1);
	}
	memset(inbuf, (int) fill, bytes);
	ext2fs_clear_block_bitmap(test_fs->block_map);
	retval = ext2fs_set_block_bitmap_range2(test_fs->block_map,
			test_fs->super->s_first_data_block, num, inbuf);
	if (retval) {
		com_err(argv[0], retval, "set_block_bitmap_range2");
		exit_status++;
		goto out;
	}
	retval = ext2fs_get_block_bitmap_range2(test_fs->block_map,
			test_fs->super->s_first_data_block, num, outbuf);
	if (retval) {
		com_err(argv[0], retval, "get_block_bitmap_range2");
		exit_status++;
		goto out;
	}
	if (memcmp(inbuf, outbuf, bytes) != 0) {
		com_err(argv[0], 0, "range mismatch after set/get!");
		exit_status++;
		goto out;
	}
	printf("Set/get block bitmap range (%u bits, 0x%02x): OK\n",
	       num, fill);
out:
	free(inbuf);
	free(outbuf);
}

void do_count_b(int argc, ss_argv_t argv,
		int sci_idx EXT2FS_ATTR((unused)),
		void *infop EXT2FS_ATTR((unused)))
{
	unsigned int start, end;
	int err;
	blk64_t out = 0;
	errcode_t retval;

	if (check_fs_open(argv[0]))
		return;
	if (argc != 3) {
		com_err(argv[0], 0, "Usage: count_b <start> <end>");
		exit_status++;
		return;
	}
	start = parse_ulong(argv[1], argv[0], "start", &err);
	if (err)
		return;
	end = parse_ulong(argv[2], argv[0], "end", &err);
	if (err)
		return;
	retval = ext2fs_count_used_blocks(test_fs, start, end, &out);
	if (retval) {
		printf("count_b(%u, %u) returned %s\n",
		       start, end, error_message(retval));
		return;
	}
	printf("Used blocks in [%u..%u]: %llu\n", start, end,
	       (unsigned long long) out);
}

static int run_subcluster_tests(void)
{
	static const unsigned int types[] = {
		EXT2FS_BMAP64_BITARRAY,
		EXT2FS_BMAP64_RBTREE,
		EXT2FS_BMAP64_AUTODIR,
	};
	struct ext2_super_block param;
	ext2_filsys fs = NULL;
	ext2fs_block_bitmap submap = NULL;
	blk64_t used_blks = 0, used_clusters = 0;
	errcode_t retval;
	size_t t;
	int fails = 0;

	for (t = 0; t < sizeof(types) / sizeof(types[0]); t++) {
		memset(&param, 0, sizeof(param));
		ext2fs_blocks_count_set(&param, 256);
		param.s_inodes_count = 32;
		param.s_rev_level = EXT2_DYNAMIC_REV;
		param.s_log_block_size = 0;
		param.s_log_cluster_size = 2; /* 4 blocks per cluster */
		ext2fs_set_feature_bigalloc(&param);

		retval = ext2fs_initialize("bigalloc test", EXT2_FLAG_64BITS,
					   &param, test_io_manager, &fs);
		if (retval) {
			com_err("subcluster", retval, "ext2fs_initialize");
			return 1;
		}
		fs->default_bitmap_type = types[t];
		ext2fs_free_block_bitmap(fs->block_map);
		fs->block_map = NULL;
		retval = ext2fs_allocate_block_bitmap(fs, "cluster map",
						      &fs->block_map);
		if (retval || ext2fs_get_bitmap_granularity(fs->block_map) != 2) {
			fprintf(stderr, "FAIL: cluster granularity type %u\n",
				types[t]);
			fails++;
		}

		retval = ext2fs_allocate_subcluster_bitmap(fs, "subcluster map",
							   &submap);
		if (retval || ext2fs_get_bitmap_granularity(submap) != 0) {
			fprintf(stderr, "FAIL: subcluster alloc type %u\n",
				types[t]);
			fails++;
		}

		/* Mark individual blocks in clusters [4..7], [16..19], [40..43] */
		ext2fs_mark_block_bitmap2(submap, 5);
		ext2fs_mark_block_bitmap2(submap, 16);
		ext2fs_mark_block_bitmap2(submap, 19);
		ext2fs_mark_block_bitmap2(submap, 42);
		if (!ext2fs_test_block_bitmap2(submap, 5) ||
		    ext2fs_test_block_bitmap2(submap, 4) ||
		    ext2fs_test_block_bitmap2(submap, 6)) {
			fprintf(stderr, "FAIL: subcluster bit isolation %u\n",
				types[t]);
			fails++;
		}

		retval = ext2fs_convert_subcluster_bitmap(fs, &submap);
		if (retval || ext2fs_get_bitmap_granularity(submap) != 2) {
			fprintf(stderr, "FAIL: convert_subcluster type %u\n",
				types[t]);
			fails++;
		}
		if (!ext2fs_test_block_bitmap2(submap, 4) ||
		    !ext2fs_test_block_bitmap2(submap, 7) ||
		    !ext2fs_test_block_bitmap2(submap, 17) ||
		    !ext2fs_test_block_bitmap2(submap, 40) ||
		    ext2fs_test_block_bitmap2(submap, 8) ||
		    ext2fs_test_block_bitmap2(submap, 20)) {
			fprintf(stderr, "FAIL: converted cluster bits %u\n",
				types[t]);
			fails++;
		}

		ext2fs_free_block_bitmap(fs->block_map);
		fs->block_map = submap;
		submap = NULL;
		if (ext2fs_count_used_blocks(fs, 1, 63, &used_blks) != 0 ||
		    used_blks != 12 ||
		    ext2fs_count_used_clusters(fs, 1, 63, &used_clusters) != 0 ||
		    used_clusters != 3) {
			fprintf(stderr, "FAIL: count_used_clusters(%llu,%llu)\n",
				(unsigned long long) used_blks,
				(unsigned long long) used_clusters);
			fails++;
		}

		/* Legacy and journal_dev error checks */
		fs->flags &= ~EXT2_FLAG_64BITS;
		if (ext2fs_allocate_subcluster_bitmap(fs, "legacy", &submap) !=
		    EXT2_ET_CANT_USE_LEGACY_BITMAPS) {
			fprintf(stderr, "FAIL: expected CANT_USE_LEGACY\n");
			fails++;
		}
		fs->flags |= EXT2_FLAG_64BITS;
		ext2fs_set_feature_journal_dev(fs->super);
		if (ext2fs_allocate_subcluster_bitmap(fs, "jdev", &submap) !=
		    EXT2_ET_EXTERNAL_JOURNAL_NOSUPP) {
			fprintf(stderr, "FAIL: expected JOURNAL_NOSUPP\n");
			fails++;
		}
		ext2fs_clear_feature_journal_dev(fs->super);
		ext2fs_close_free(&fs);
	}
	printf("tst_bitmaps subcluster tests: %s\n", fails ? "FAILED" : "OK");
	return fails ? 1 : 0;
}

int main(int argc, char **argv)
{
	unsigned int	blocks = 128;
	unsigned int	inodes = 0;
	unsigned int	type = EXT2FS_BMAP64_BITARRAY;
	int		c, err, code;
	char		*request = (char *)NULL;
	char		*cmd_file = 0;
	int		sci_idx;
	int		flags = EXT2_FLAG_64BITS;
	int		subcluster_mode = 0;

	add_error_table(&et_ss_error_table);
	add_error_table(&et_ext2_error_table);
	while ((c = getopt (argc, argv, "b:ci:lt:R:f:")) != EOF) {
		switch (c) {
		case 'b':
			blocks = parse_ulong(optarg, argv[0],
					     "number of blocks", &err);
			if (err)
				exit(1);
			break;
		case 'c':
			subcluster_mode = 1;
			break;
		case 'i':
			inodes = parse_ulong(optarg, argv[0],
					     "number of blocks", &err);
			if (err)
				exit(1);
			break;
		case 'l':	/* Legacy bitmaps */
			flags = 0;
			break;
		case 't':
			type = parse_ulong(optarg, argv[0],
					   "bitmap backend type", &err);
			if (err)
				exit(1);
			break;
		case 'R':
			request = optarg;
			break;
		case 'f':
			cmd_file = optarg;
			break;
		default:
			com_err(argv[0], 0, "Usage: %s [-R request] "
				"[-f cmd_file]", subsystem_name);
			exit(1);
		}
	}

	if (subcluster_mode)
		exit(run_subcluster_tests());

	saved_type = type;
	saved_flags = flags;

	sci_idx = ss_create_invocation(subsystem_name, version,
				       (char *)NULL, &tst_bitmaps_cmds, &code);
	if (code) {
		ss_perror(sci_idx, code, "creating invocation");
		exit(1);
	}

	(void) ss_add_request_table (sci_idx, &ss_std_requests, 1, &code);
	if (code) {
		ss_perror(sci_idx, code, "adding standard requests");
		exit (1);
	}

	printf("%s %s.  Type '?' for a list of commands.\n\n",
	       subsystem_name, version);

	setup_filesystem(argv[0], blocks, inodes, type, flags);

	if (request) {
		code = ss_execute_line(sci_idx, request);
		if (code) {
			ss_perror(sci_idx, code, request);
			exit_status++;
		}
	} else if (cmd_file) {
		exit_status = source_file(cmd_file, sci_idx);
	} else {
		ss_listen(sci_idx);
	}

	exit(exit_status);
}

