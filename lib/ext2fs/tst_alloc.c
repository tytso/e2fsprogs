/*
 * tst_alloc.c --- unit tests for alloc.c, alloc_stats.c, alloc_sb.c,
 *                 and alloc_tables.c
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Library
 * General Public License, version 2.
 * %End-Header%
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif

#include "ext2_fs.h"
#include "ext2fs.h"
#include "ext2fsP.h"

static int test_count;
static int fail_count;

#define CHECK(cond, msg) do { \
	test_count++; \
	if (!(cond)) { \
		fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, msg); \
		fail_count++; \
	} \
} while (0)

#define CHECK_RC(rc, expected, msg) do { \
	errcode_t _rc = (rc); \
	errcode_t _exp = (expected); \
	test_count++; \
	if (_rc != _exp) { \
		fprintf(stderr, "FAIL [%s:%d]: %s (got %ld, expected %ld)\n", \
			__FILE__, __LINE__, msg, (long) _rc, (long) _exp); \
		fail_count++; \
	} \
} while (0)

static ext2_filsys create_test_fs(char *path_out, int *fd_out,
				  blk64_t blocks, ext2_ino_t inodes,
				  int flex_log, int bigalloc_log)
{
	ext2_filsys		fs = NULL;
	struct ext2_super_block	param;
	int			fd;

	strcpy(path_out, "/tmp/tst_alloc.XXXXXX");
	fd = mkstemp(path_out);
	if (fd < 0) {
		perror("mkstemp");
		exit(1);
	}
	if (ftruncate(fd, (off_t) blocks * 1024) < 0) {
		perror("ftruncate");
		close(fd);
		unlink(path_out);
		exit(1);
	}
	*fd_out = fd;

	memset(&param, 0, sizeof(param));
	ext2fs_blocks_count_set(&param, blocks);
	param.s_inodes_count = inodes;
	param.s_rev_level = EXT2_DYNAMIC_REV;
	param.s_inode_size = 256;
	memset(param.s_uuid, 0x3c, sizeof(param.s_uuid));
	ext2fs_set_feature_sparse_super(&param);
	ext2fs_set_feature_extents(&param);
	ext2fs_set_feature_metadata_csum(&param);
	ext2fs_set_feature_64bit(&param);
	param.s_desc_size = EXT2_MIN_DESC_SIZE_64BIT;

	if (flex_log > 0) {
		ext2fs_set_feature_flex_bg(&param);
		param.s_log_groups_per_flex = flex_log;
	}
	if (bigalloc_log > 0) {
		ext2fs_set_feature_bigalloc(&param);
		param.s_log_cluster_size = bigalloc_log;
	}

	if (ext2fs_initialize(path_out, EXT2_FLAG_RW | EXT2_FLAG_64BITS,
			      &param, unix_io_manager, &fs))
		exit(1);
	if (ext2fs_allocate_tables(fs))
		exit(1);
	return fs;
}

static void teardown_test_fs(ext2_filsys *fs, int fd, const char *path)
{
	ext2fs_zero_blocks2(NULL, 0, 0, NULL, NULL);
	ext2fs_close_free(fs);
	close(fd);
	unlink(path);
}

/*
 * Suite 1: ext2fs_new_inode() and ext2fs_inode_alloc_stats2()
 */
static void test_inode_alloc_and_stats(void)
{
	char			path[64];
	int			fd;
	ext2_filsys		fs;
	ext2fs_inode_bitmap	saved_imap;
	ext2_ino_t		ino = 0, ipg, i;
	__u32			free_before, g0_free_before;

	/* 20000 blocks -> 3 groups, 96 inodes -> 32 inodes/group */
	fs = create_test_fs(path, &fd, 20000, 96, 0, 0);
	ipg = EXT2_INODES_PER_GROUP(fs->super);

	/* Missing bitmap returns EXT2_ET_NO_INODE_BITMAP */
	saved_imap = fs->inode_map;
	fs->inode_map = NULL;
	CHECK_RC(ext2fs_new_inode(fs, EXT2_ROOT_INO, LINUX_S_IFREG, NULL, &ino),
		 EXT2_ET_NO_INODE_BITMAP, "new_inode NULL map rejected");
	fs->inode_map = saved_imap;

	/* Allocate in group 0 (first non-reserved inode == 11) */
	free_before = fs->super->s_free_inodes_count;
	g0_free_before = ext2fs_bg_free_inodes_count(fs, 0);
	ext2fs_bg_itable_unused_set(fs, 0, 20);
	ext2fs_group_desc_csum_set(fs, 0);

	CHECK_RC(ext2fs_new_inode(fs, EXT2_ROOT_INO, LINUX_S_IFDIR, NULL, &ino),
		 0, "new_inode in group 0");
	CHECK(ino == EXT2_GOOD_OLD_FIRST_INO, "allocated first free inode 11");

	/* Update stats as directory (+1, isdir=1) and check itable_unused */
	ext2fs_inode_alloc_stats2(fs, 20, +1, 1);
	CHECK(ext2fs_test_inode_bitmap2(fs->inode_map, 20) != 0,
	      "inode 20 marked in bitmap");
	CHECK(fs->super->s_free_inodes_count == free_before - 1 &&
	      ext2fs_bg_free_inodes_count(fs, 0) == g0_free_before - 1 &&
	      ext2fs_bg_used_dirs_count(fs, 0) == 1 &&
	      ext2fs_bg_itable_unused(fs, 0) == ipg - 20,
	      "inode_alloc_stats2 updated free, used_dirs, and itable_unused");

	/* Freeing (-1, isdir=1) restores counters */
	ext2fs_inode_alloc_stats2(fs, 20, -1, 1);
	CHECK(ext2fs_test_inode_bitmap2(fs->inode_map, 20) == 0 &&
	      fs->super->s_free_inodes_count == free_before &&
	      ext2fs_bg_used_dirs_count(fs, 0) == 0,
	      "inode_alloc_stats2(-1) restored counters");

	/*
	 * Test check_inode_uninit(): poison group 1's bitmap to all-1s, set
	 * EXT2_BG_INODE_UNINIT, and request an inode with dir in group 1.
	 */
	for (i = ipg + 1; i <= 2 * ipg; i++)
		ext2fs_mark_inode_bitmap2(fs->inode_map, i);
	ext2fs_bg_flags_set(fs, 1, EXT2_BG_INODE_UNINIT | EXT2_BG_BLOCK_UNINIT);
	ext2fs_group_desc_csum_set(fs, 1);

	CHECK_RC(ext2fs_new_inode(fs, ipg + 2, LINUX_S_IFREG, NULL, &ino), 0,
		 "new_inode on BG_INODE_UNINIT group 1");
	CHECK(ino == ipg + 1, "allocated first inode of uninit group 1");
	CHECK(!ext2fs_bg_flags_test(fs, 1, EXT2_BG_INODE_UNINIT) &&
	      !ext2fs_bg_flags_test(fs, 1, EXT2_BG_BLOCK_UNINIT),
	      "check_inode_uninit cleared INODE_UNINIT and BLOCK_UNINIT");

	/* Fill groups 1 and 2 -> dir in group 1 wraps around to group 0! */
	for (i = ipg + 1; i <= fs->super->s_inodes_count; i++)
		ext2fs_mark_inode_bitmap2(fs->inode_map, i);
	CHECK_RC(ext2fs_new_inode(fs, ipg + 5, LINUX_S_IFREG, NULL, &ino), 0,
		 "new_inode wraps around to group 0");
	CHECK(ino == EXT2_GOOD_OLD_FIRST_INO, "wrapped around to inode 11");

	/* Mark remaining group 0 inodes -> EXT2_ET_INODE_ALLOC_FAIL */
	for (i = 1; i <= ipg; i++)
		ext2fs_mark_inode_bitmap2(fs->inode_map, i);
	CHECK_RC(ext2fs_new_inode(fs, EXT2_ROOT_INO, LINUX_S_IFREG, NULL, &ino),
		 EXT2_ET_INODE_ALLOC_FAIL, "full inode map fails cleanly");

	teardown_test_fs(&fs, fd, path);
	printf("  test_inode_alloc_and_stats: ok\n");
}

static int custom_alloc_cb_calls;
static errcode_t custom_alloc_blk_cb(ext2_filsys fs, blk64_t goal, blk64_t *ret)
{
	custom_alloc_cb_calls++;
	/* Call ext2fs_new_block2 with NULL map to test anti-recursion guard */
	return ext2fs_new_block2(fs, goal + 5, NULL, ret);
}

static blk64_t last_stats_blk;
static int last_stats_inuse;
static void stats_cb(ext2_filsys fs EXT2FS_ATTR((unused)), blk64_t blk,
		     int inuse)
{
	last_stats_blk = blk;
	last_stats_inuse = inuse;
}

/*
 * Suite 2: ext2fs_new_block2/3, ext2fs_alloc_block2/3, ext2fs_get_free_blocks2,
 *          bigalloc cluster granularity, and callbacks.
 */
static void test_block_alloc_and_callbacks(void)
{
	char		path[64];
	char		buf[1024], rbuf[1024];
	int		fd;
	ext2_filsys	fs;
	blk64_t		blk = 0, run = 0;
	errcode_t	(*old_cb)(ext2_filsys, blk64_t, blk64_t *) = NULL;

	fs = create_test_fs(path, &fd, 2048, 64, 0, 0);
	ext2fs_set_block_alloc_stats_callback(fs, stats_cb, NULL);

	/* Dirty block 200 on disk via pwrite, then alloc_block2 zeros it */
	memset(buf, 0xaa, sizeof(buf));
	CHECK(pwrite(fd, buf, sizeof(buf), 200 * 1024) == sizeof(buf),
	      "dirty block 200 on disk");
	CHECK_RC(ext2fs_alloc_block2(fs, 200, NULL, &blk), 0,
		 "alloc_block2(buf=NULL) at goal 200");
	CHECK(blk == 200 && last_stats_blk == 200 && last_stats_inuse == 1,
	      "allocated block 200 and invoked block_alloc_stats callback");
	CHECK_RC(io_channel_read_blk64(fs->io, 200, 1, rbuf), 0, "read 200");
	CHECK(rbuf[0] == 0 && rbuf[1023] == 0, "alloc_block2 zeroed block 200");

	/* alloc_block2 with explicit block_buf */
	memset(buf, 0xbb, sizeof(buf));
	CHECK_RC(io_channel_write_blk64(fs->io, 205, 1, buf), 0, "dirty 205");
	CHECK_RC(ext2fs_alloc_block2(fs, 205, buf, &blk), 0,
		 "alloc_block2(block_buf) at goal 205");
	CHECK_RC(io_channel_read_blk64(fs->io, 205, 1, rbuf), 0, "read 205");
	CHECK(blk == 205 && rbuf[0] == 0 && rbuf[1023] == 0,
	      "alloc_block2 with block_buf zeroed block 205");

	/* Custom alloc_block callback with recursive ext2fs_new_block2 */
	ext2fs_set_alloc_block_callback(fs, custom_alloc_blk_cb, &old_cb);
	CHECK(old_cb == NULL, "previous alloc_block callback was NULL");
	CHECK_RC(ext2fs_new_block2(fs, 300, NULL, &blk), 0,
		 "new_block2 via custom_alloc_blk_cb anti-recursion");
	CHECK(custom_alloc_cb_calls == 1 && blk == 305,
	      "custom callback shifted goal +5 without infinite recursion");
	CHECK_RC(ext2fs_alloc_block2(fs, 320, NULL, &blk), 0,
		 "alloc_block2 via custom_alloc_blk_cb");
	CHECK(blk == 330, "alloc_block2 dispatched through get_alloc_block");
	ext2fs_set_alloc_block_callback(fs, NULL, NULL);

	/* ext2fs_get_free_blocks2: contiguous run and bounded failure */
	ext2fs_mark_block_bitmap2(fs->block_map, 401);
	CHECK_RC(ext2fs_get_free_blocks2(fs, 400, 410, 4, NULL, &run), 0,
		 "get_free_blocks2 skips 401 to find 4-block run");
	CHECK(run == 402, "found 4-block contiguous run at 402..405");

	ext2fs_mark_block_bitmap_range2(fs->block_map, 500, 10);
	CHECK_RC(ext2fs_get_free_blocks2(fs, 500, 508, 2, NULL, &run),
		 EXT2_ET_BLOCK_ALLOC_FAIL,
		 "get_free_blocks2 bounded range fails when full");

	teardown_test_fs(&fs, fd, path);

	/* Also verify bigalloc (cluster ratio = 4 blocks/cluster) */
	fs = create_test_fs(path, &fd, 4096, 64, 0, 2);
	{
		blk64_t free_before = ext2fs_free_blocks_count(fs->super);

		/* Unaligned goal 403 aligns down to cluster start 400 */
		CHECK_RC(ext2fs_new_block2(fs, 403, NULL, &blk), 0,
			 "new_block2 on bigalloc");
		CHECK((blk & 3) == 0 && blk == 400,
		      "bigalloc goal aligned to cluster boundary");
		ext2fs_block_alloc_stats2(fs, blk, +1);
		CHECK(ext2fs_free_blocks_count(fs->super) == free_before - 4,
		      "bigalloc block_alloc_stats2 decremented by 4 blocks");
	}
	teardown_test_fs(&fs, fd, path);
	printf("  test_block_alloc_and_callbacks: ok\n");
}

static blk64_t range_cb_blk;
static blk_t range_cb_num;
static int range_cb_inuse;
static void range_stats_cb(ext2_filsys fs EXT2FS_ATTR((unused)),
			   blk64_t blk, blk_t num, int inuse)
{
	range_cb_blk = blk;
	range_cb_num = num;
	range_cb_inuse = inuse;
}

/* A new_range handler which always returns exactly the requested range */
static errcode_t goal_new_range_cb(ext2_filsys fs EXT2FS_ATTR((unused)),
				   int flags EXT2FS_ATTR((unused)),
				   blk64_t goal, blk64_t len,
				   blk64_t *pblk, blk64_t *plen)
{
	*pblk = goal;
	*plen = len;
	return 0;
}

/*
 * Suite 3: ext2fs_find_inode_goal(), ext2fs_new_range(), ext2fs_alloc_range(),
 *          and ext2fs_block_alloc_stats_range().
 */
static void test_goal_and_range_alloc(void)
{
	char			path[64];
	char			buf[1024];
	int			fd;
	ext2_filsys		fs;
	struct ext2_inode	ind_ino, ext_ino, sym_ino;
	ext2_extent_handle_t	eh = NULL;
	blk64_t			pblk = 0, plen = 0;

	/* 20000 blocks -> 3 groups, flex_bg size = 4 (log=2) */
	fs = create_test_fs(path, &fd, 20000, 96, 2, 0);
	ext2fs_set_block_alloc_stats_range_callback(fs, range_stats_cb, NULL);

	/* ext2fs_find_inode_goal: NULL / fast symlink -> flex_bg base (blk 1) */
	CHECK(ext2fs_find_inode_goal(fs, 40, NULL, 0) ==
	      ext2fs_group_first_block2(fs, 0),
	      "NULL inode in group 1 returns flex_bg base group 0 block");

	memset(&sym_ino, 0, sizeof(sym_ino));
	sym_ino.i_mode = LINUX_S_IFLNK | 0777;
	sym_ino.i_size = 10;
	sym_ino.i_block[0] = 0x12345678;
	CHECK(ext2fs_find_inode_goal(fs, 40, &sym_ino, 0) ==
	      ext2fs_group_first_block2(fs, 0),
	      "fast symlink ignores i_block[0] target string");

	/* Block-mapped inode returns i_block[0] */
	memset(&ind_ino, 0, sizeof(ind_ino));
	ind_ino.i_mode = LINUX_S_IFREG | 0644;
	ind_ino.i_block[0] = 777;
	CHECK(ext2fs_find_inode_goal(fs, 12, &ind_ino, 5) == 777,
	      "block-mapped inode returns i_block[0]");

	/* Extent-mapped inode interpolates extent.e_pblk + (lblk - e_lblk) */
	{
		struct ext2fs_extent ex;

		memset(&ext_ino, 0, sizeof(ext_ino));
		ext_ino.i_mode = LINUX_S_IFREG | 0644;
		ext_ino.i_links_count = 1;
		ext_ino.i_flags = EXT4_EXTENTS_FL;
		CHECK_RC(ext2fs_write_new_inode(fs, 12, &ext_ino), 0,
			 "write ext_ino");
		CHECK_RC(ext2fs_extent_open(fs, 12, &eh), 0, "extent_open 12");
		memset(&ex, 0, sizeof(ex));
		ex.e_lblk = 10;
		ex.e_pblk = 1500;
		ex.e_len = 10;
		ex.e_flags = EXT2_EXTENT_FLAGS_UNINIT;
		CHECK_RC(ext2fs_extent_insert(eh, EXT2_EXTENT_INSERT_AFTER,
					      &ex), 0,
			 "insert 10-block extent [10..19] -> 1500");
		ext2fs_extent_free(eh);
		CHECK_RC(ext2fs_read_inode(fs, 12, &ext_ino), 0,
			 "re-read ext_ino");
		CHECK(ext2fs_find_inode_goal(fs, 12, &ext_ino, 14) == 1504,
		      "extent-mapped inode returns interpolated goal 1504");
	}

	/* ext2fs_new_range & ext2fs_alloc_range validation */
	CHECK_RC(ext2fs_new_range(fs, 0, 100, 0, NULL, &pblk, &plen),
		 EXT2_ET_INVALID_ARGUMENT, "new_range len=0 rejected");
	CHECK_RC(ext2fs_new_range(fs, 0x8000, 100, 4, NULL, &pblk, &plen),
		 EXT2_ET_INVALID_ARGUMENT, "new_range bad flags rejected");
	CHECK_RC(ext2fs_alloc_range(fs, 0, 100, 0, &pblk),
		 EXT2_ET_INVALID_ARGUMENT, "alloc_range len=0 rejected");

	/* Create a 3-block free island at 600..602 surrounded by 599 & 603 */
	ext2fs_mark_block_bitmap2(fs->block_map, 599);
	ext2fs_mark_block_bitmap2(fs->block_map, 603);

	/* Without MIN_LENGTH, requesting 6 at goal=600 returns shorter plen=3 */
	CHECK_RC(ext2fs_new_range(fs, 0, 600, 6, NULL, &pblk, &plen), 0,
		 "new_range without MIN_LENGTH");
	CHECK(pblk == 600 && plen == 3,
	      "new_range returned partial 3-block island");

	/* With MIN_LENGTH, skips 3-block island at 600 and lands at >= 604 */
	CHECK_RC(ext2fs_new_range(fs, EXT2_NEWRANGE_MIN_LENGTH, 600, 6,
				  NULL, &pblk, &plen), 0,
		 "new_range with MIN_LENGTH");
	CHECK(pblk == 604 && plen == 6,
	      "MIN_LENGTH skipped 3-block island to 604..609");

	/* FIXED_GOAL fails on 3-block island when requesting 6 */
	CHECK_RC(ext2fs_alloc_range(fs, EXT2_ALLOCRANGE_FIXED_GOAL, 600, 6,
				    &pblk),
		 EXT2_ET_BLOCK_ALLOC_FAIL,
		 "FIXED_GOAL fails when run at goal is too short");

	/*
	 * ext2fs_new_range() must clear BLOCK_UNINIT in every group touched
	 * by the returned range, also when the range does not start at the
	 * beginning of a group: 16382..16387 straddles groups 1 and 2.
	 */
	ext2fs_bg_flags_set(fs, 1, EXT2_BG_BLOCK_UNINIT);
	ext2fs_bg_flags_set(fs, 2, EXT2_BG_BLOCK_UNINIT);
	CHECK_RC(ext2fs_new_range(fs, EXT2_NEWRANGE_FIXED_GOAL |
				  EXT2_NEWRANGE_MIN_LENGTH, 16382, 6, NULL,
				  &pblk, &plen), 0,
		 "new_range straddling group 1 -> group 2");
	CHECK(pblk == 16382 && plen == 6, "new_range returned 16382..16387");
	CHECK(!ext2fs_bg_flags_test(fs, 1, EXT2_BG_BLOCK_UNINIT) &&
	      !ext2fs_bg_flags_test(fs, 2, EXT2_BG_BLOCK_UNINIT),
	      "new_range cleared BLOCK_UNINIT in groups 1 and 2");

	/* ... and so must ranges returned by a new_range handler */
	ext2fs_bg_flags_set(fs, 1, EXT2_BG_BLOCK_UNINIT);
	ext2fs_bg_flags_set(fs, 2, EXT2_BG_BLOCK_UNINIT);
	ext2fs_set_new_range_callback(fs, goal_new_range_cb, NULL);
	CHECK_RC(ext2fs_new_range(fs, 0, 16382, 6, NULL, &pblk, &plen), 0,
		 "new_range handler straddling group 1 -> group 2");
	ext2fs_set_new_range_callback(fs, NULL, NULL);
	CHECK(pblk == 16382 && plen == 6,
	      "new_range handler returned 16382..16387");
	CHECK(!ext2fs_bg_flags_test(fs, 1, EXT2_BG_BLOCK_UNINIT) &&
	      !ext2fs_bg_flags_test(fs, 2, EXT2_BG_BLOCK_UNINIT),
	      "new_range handler range cleared BLOCK_UNINIT in groups 1, 2");

	/*
	 * Straddle group 1 -> group 2 boundary (16382..16387, since group 2
	 * has no sparse_super backup and flex_bg placed its metadata in grp 0)
	 * with EXT2_ALLOCRANGE_ZERO_BLOCKS.
	 */
	memset(buf, 0xdd, sizeof(buf));
	CHECK(pwrite(fd, buf, sizeof(buf), 16383 * 1024) == sizeof(buf),
	      "dirty block 16383 on disk");
	CHECK_RC(ext2fs_alloc_range(fs,
			EXT2_ALLOCRANGE_FIXED_GOAL |
			EXT2_ALLOCRANGE_ZERO_BLOCKS,
			16382, 6, &pblk), 0,
		 "alloc_range straddling group 1 -> group 2");
	CHECK(pblk == 16382 && range_cb_blk == 16382 && range_cb_num == 6 &&
	      range_cb_inuse == 1,
	      "cross-group alloc_range succeeded and invoked range callback");
	CHECK_RC(io_channel_read_blk64(fs->io, 16383, 1, buf), 0, "read 16383");
	CHECK(buf[0] == 0 && buf[1023] == 0,
	      "EXT2_ALLOCRANGE_ZERO_BLOCKS zeroed disk blocks");

	/* Free the 6-block cross-group range and verify bitmap cleared */
	ext2fs_block_alloc_stats_range(fs, 16382, 6, -1);
	CHECK(ext2fs_test_block_bitmap2(fs->block_map, 16382) == 0 &&
	      ext2fs_test_block_bitmap2(fs->block_map, 16387) == 0 &&
	      range_cb_inuse == -1,
	      "block_alloc_stats_range(-1) freed cross-group blocks");

	teardown_test_fs(&fs, fd, path);
	printf("  test_goal_and_range_alloc: ok\n");
}

static int prog_init_seen, prog_updates, prog_close_seen;
static void mock_prog_init(ext2_filsys fs EXT2FS_ATTR((unused)),
			   struct ext2fs_numeric_progress_struct *p
			   EXT2FS_ATTR((unused)),
			   const char *label EXT2FS_ATTR((unused)),
			   __u64 max)
{
	if (max > 0)
		prog_init_seen++;
}
static void mock_prog_update(ext2_filsys fs EXT2FS_ATTR((unused)),
			     struct ext2fs_numeric_progress_struct *p
			     EXT2FS_ATTR((unused)),
			     __u64 val EXT2FS_ATTR((unused)))
{
	prog_updates++;
}
static void mock_prog_close(ext2_filsys fs EXT2FS_ATTR((unused)),
			    struct ext2fs_numeric_progress_struct *p
			    EXT2FS_ATTR((unused)),
			    const char *msg EXT2FS_ATTR((unused)))
{
	prog_close_seen++;
}

static struct ext2fs_progress_ops mock_progress = {
	.init	= mock_prog_init,
	.update	= mock_prog_update,
	.close	= mock_prog_close,
};

/*
 * Suite 4: alloc_sb.c (ext2fs_reserve_super_and_bgd / 2) and
 *          alloc_tables.c (stride, progress_ops, and flex_bg packing).
 */
static void test_reserve_sb_and_alloc_tables(void)
{
	char			path[64];
	int			fd;
	ext2_filsys		fs = NULL;
	struct ext2_super_block	param;
	blk_t			desc_blks = 0;

	strcpy(path, "/tmp/tst_alloc.XXXXXX");
	fd = mkstemp(path);
	if (fd < 0 || ftruncate(fd, 36000 * 1024) < 0)
		exit(1);

	/* 5 groups (36000 1K blocks) with sparse_super -> grp 2 has no backup */
	memset(&param, 0, sizeof(param));
	ext2fs_blocks_count_set(&param, 36000);
	param.s_inodes_count = 160;
	param.s_rev_level = EXT2_DYNAMIC_REV;
	param.s_inode_size = 128;
	ext2fs_set_feature_sparse_super(&param);

	CHECK_RC(ext2fs_initialize(path, EXT2_FLAG_RW | EXT2_FLAG_64BITS,
				   &param, unix_io_manager, &fs), 0,
		 "initialize 5-group fs");
	/* Clear block_map to test ext2fs_reserve_super_and_bgd2 per group */
	ext2fs_clear_block_bitmap(fs->block_map);
	CHECK_RC(ext2fs_reserve_super_and_bgd2(fs, 0, fs->block_map,
					       &desc_blks), 0,
		 "reserve_super_and_bgd2 grp 0");
	CHECK(desc_blks >= 2 &&
	      ext2fs_test_block_bitmap2(fs->block_map, 1) != 0,
	      "group 0 reserved primary superblock + GDT");

	CHECK_RC(ext2fs_reserve_super_and_bgd2(fs, 1, fs->block_map,
					       &desc_blks), 0,
		 "reserve_super_and_bgd2 backup grp 1");
	CHECK(desc_blks >= 2, "group 1 reserved backup superblock + GDT");

	CHECK_RC(ext2fs_reserve_super_and_bgd2(fs, 2, fs->block_map,
					       &desc_blks), 0,
		 "reserve_super_and_bgd2 non-backup grp 2");
	CHECK(desc_blks == 0, "sparse_super group 2 used 0 sb/bgd blocks");

	/* Allocate tables with fs->stride = 16 and progress_ops */
	{
		dgrp_t g;

		for (g = 0; g < fs->group_desc_count; g++)
			ext2fs_reserve_super_and_bgd(fs, g, fs->block_map);
	}
	fs->stride = 16;
	fs->progress_ops = &mock_progress;
	CHECK_RC(ext2fs_allocate_tables(fs), 0,
		 "allocate_tables with stride and progress_ops");
	CHECK(prog_init_seen == 1 && prog_updates == 5 && prog_close_seen == 1,
	      "progress_ops init/update(5)/close invoked");
	CHECK(ext2fs_block_bitmap_loc(fs, 2) > ext2fs_inode_table_loc(fs, 2),
	      "fs->stride offset bitmaps past inode table");
	teardown_test_fs(&fs, fd, path);

	/* Verify flex_bg packs group 0..3 metadata into group 0 */
	fs = create_test_fs(path, &fd, 36000, 160, 2, 0);
	CHECK(ext2fs_group_of_blk2(fs, ext2fs_block_bitmap_loc(fs, 0)) == 0 &&
	      ext2fs_group_of_blk2(fs, ext2fs_block_bitmap_loc(fs, 3)) == 0 &&
	      ext2fs_group_of_blk2(fs, ext2fs_inode_bitmap_loc(fs, 3)) == 0 &&
	      ext2fs_group_of_blk2(fs, ext2fs_inode_table_loc(fs, 3)) == 0,
	      "flex_bg packed groups 0..3 bitmaps and inode tables into grp 0");
	CHECK(ext2fs_block_bitmap_loc(fs, 1) ==
	      ext2fs_block_bitmap_loc(fs, 0) + 1 &&
	      ext2fs_inode_bitmap_loc(fs, 1) ==
	      ext2fs_inode_bitmap_loc(fs, 0) + 1,
	      "flex_bg allocated bitmaps contiguously");
	teardown_test_fs(&fs, fd, path);
	printf("  test_reserve_sb_and_alloc_tables: ok\n");
}

int main(int argc EXT2FS_ATTR((unused)), char *argv[] EXT2FS_ATTR((unused)))
{
	add_error_table(&et_ext2_error_table);
	printf("Running tst_alloc unit tests...\n");

	test_inode_alloc_and_stats();
	test_block_alloc_and_callbacks();
	test_goal_and_range_alloc();
	test_reserve_sb_and_alloc_tables();

	remove_error_table(&et_ext2_error_table);
	printf("tst_alloc: %d checks run, %d failed\n",
	       test_count, fail_count);
	return fail_count ? 1 : 0;
}
