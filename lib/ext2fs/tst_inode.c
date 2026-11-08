/*
 * tst_inode.c --- unit tests for inode.c
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
				  int inode_size, int meta_csum)
{
	ext2_filsys		fs = NULL;
	struct ext2_super_block	param;
	int			fd;
	dgrp_t			g;
	char			*zbuf = NULL;

	strcpy(path_out, "/tmp/tst_inode.XXXXXX");
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
	param.s_inode_size = inode_size;
	/* Fixed UUID so metadata_csum is 100% deterministic */
	memset(param.s_uuid, 0x5a, sizeof(param.s_uuid));
	if (meta_csum) {
		ext2fs_set_feature_metadata_csum(&param);
		ext2fs_set_feature_64bit(&param);
		param.s_desc_size = EXT2_MIN_DESC_SIZE_64BIT;
	}

	if (ext2fs_initialize(path_out, EXT2_FLAG_RW | EXT2_FLAG_64BITS,
			      &param, unix_io_manager, &fs))
		exit(1);
	fs->now = 1700000000;
	if (ext2fs_allocate_tables(fs))
		exit(1);

	/* Zero inode tables and stamp valid empty-inode csums if meta_csum */
	if (ext2fs_get_memzero((size_t) fs->blocksize *
			       fs->inode_blocks_per_group, &zbuf))
		exit(1);
	for (g = 0; g < fs->group_desc_count; g++) {
		blk64_t itbl = ext2fs_inode_table_loc(fs, g);

		if (io_channel_write_blk64(fs->io, itbl,
					   fs->inode_blocks_per_group, zbuf))
			exit(1);
		ext2fs_bg_flags_clear(fs, g, EXT2_BG_INODE_UNINIT);
		ext2fs_bg_itable_unused_set(fs, g, 0);
		ext2fs_group_desc_csum_set(fs, g);
	}
	ext2fs_free_mem(&zbuf);
	if (meta_csum) {
		ext2_ino_t ino;
		struct ext2_inode_large empty;

		memset(&empty, 0, sizeof(empty));
		for (ino = 1; ino <= fs->super->s_inodes_count; ino++) {
			if (ext2fs_write_inode_full(fs, ino,
					(struct ext2_inode *) &empty,
					sizeof(empty)))
				exit(1);
		}
		ext2fs_flush_icache(fs);
	}
	if (ext2fs_flush(fs))
		exit(1);
	return fs;
}

static errcode_t custom_read_cb(ext2_filsys fs EXT2FS_ATTR((unused)),
				ext2_ino_t ino,
				struct ext2_inode *inode)
{
	if (ino == 15) {
		memset(inode, 0, sizeof(*inode));
		inode->i_uid = 7777;
		return 0;
	}
	return EXT2_ET_CALLBACK_NOTHANDLED;
}

static int write_cb_seen;
static errcode_t custom_write_cb(ext2_filsys fs EXT2FS_ATTR((unused)),
				 ext2_ino_t ino,
				 struct ext2_inode *inode EXT2FS_ATTR((unused)))
{
	if (ino == 15) {
		write_cb_seen++;
		return 0;
	}
	return EXT2_ET_CALLBACK_NOTHANDLED;
}

/*
 * Suite 1: Single-inode R/W, error paths, write_new_inode, partial-size RMW,
 *          icache eviction/flush, and read/write override callbacks.
 */
static void test_inode_rw_and_icache(void)
{
	char			path[64];
	int			fd, i;
	ext2_filsys		fs;
	struct ext2_inode	ino128, out128;
	struct ext2_inode_large	ino256, out256;
	blk64_t			saved_itbl;

	fs = create_test_fs(path, &fd, 2048, 64, 256, 0);

	/* Error cases: journal_dev, OOB ino, RO fs, missing/bad itable */
	ext2fs_set_feature_journal_dev(fs->super);
	CHECK_RC(ext2fs_read_inode(fs, 2, &out128),
		 EXT2_ET_EXTERNAL_JOURNAL_NOSUPP,
		 "read_inode rejects journal_dev");
	CHECK_RC(ext2fs_write_inode(fs, 2, &out128),
		 EXT2_ET_EXTERNAL_JOURNAL_NOSUPP,
		 "write_inode rejects journal_dev");
	ext2fs_clear_feature_journal_dev(fs->super);

	CHECK_RC(ext2fs_read_inode(fs, 0, &out128), EXT2_ET_BAD_INODE_NUM,
		 "read_inode ino 0 rejected");
	CHECK_RC(ext2fs_read_inode(fs, 65, &out128), EXT2_ET_BAD_INODE_NUM,
		 "read_inode ino > s_inodes_count rejected");
	CHECK_RC(ext2fs_write_inode(fs, 0, &out128), EXT2_ET_BAD_INODE_NUM,
		 "write_inode ino 0 rejected");
	CHECK_RC(ext2fs_write_inode(fs, 65, &out128), EXT2_ET_BAD_INODE_NUM,
		 "write_inode ino > s_inodes_count rejected");

	saved_itbl = ext2fs_inode_table_loc(fs, 0);
	ext2fs_inode_table_loc_set(fs, 0, 0);
	ext2fs_flush_icache(fs);
	CHECK_RC(ext2fs_read_inode(fs, 12, &out128),
		 EXT2_ET_MISSING_INODE_TABLE, "read_inode missing itable");
	CHECK_RC(ext2fs_write_inode_full(fs, 12, (struct ext2_inode *) &ino256,
					 sizeof(ino256)),
		 EXT2_ET_MISSING_INODE_TABLE, "write_inode missing itable");

	ext2fs_inode_table_loc_set(fs, 0, 2045);
	CHECK_RC(ext2fs_read_inode(fs, 12, &out128),
		 EXT2_ET_GDESC_BAD_INODE_TABLE, "read_inode OOB itable");
	CHECK_RC(ext2fs_write_inode_full(fs, 12, (struct ext2_inode *) &ino256,
					 sizeof(ino256)),
		 EXT2_ET_GDESC_BAD_INODE_TABLE, "write_inode OOB itable");
	ext2fs_inode_table_loc_set(fs, 0, saved_itbl);

	/* RO filesystem rejects uncached write with EXT2_ET_RO_FILSYS */
	fs->flags &= ~EXT2_FLAG_RW;
	memset(&ino256, 0, sizeof(ino256));
	CHECK_RC(ext2fs_write_inode_full(fs, 13, (struct ext2_inode *) &ino256,
					 sizeof(ino256)),
		 EXT2_ET_RO_FILSYS, "write_inode on RO fs rejected");
	fs->flags |= EXT2_FLAG_RW;

	/* ext2fs_write_new_inode initializes extra_isize and timestamps */
	memset(&ino128, 0, sizeof(ino128));
	ino128.i_mode = LINUX_S_IFREG | 0640;
	ino128.i_uid = 1234;
	ino128.i_links_count = 1;
	CHECK_RC(ext2fs_write_new_inode(fs, 12, &ino128), 0,
		 "ext2fs_write_new_inode");
	memset(&out256, 0, sizeof(out256));
	CHECK_RC(ext2fs_read_inode_full(fs, 12, (struct ext2_inode *) &out256,
					sizeof(out256)), 0,
		 "read_inode_full after write_new_inode");
	CHECK(out256.i_uid == 1234 &&
	      out256.i_atime == 1700000000U &&
	      out256.i_mtime == 1700000000U &&
	      out256.i_crtime == 1700000000U &&
	      out256.i_extra_isize ==
	      sizeof(struct ext2_inode_large) - EXT2_GOOD_OLD_INODE_SIZE,
	      "write_new_inode populated timestamps and i_extra_isize");

	/* Partial 128B write_inode preserves extended fields in upper 128B */
	out256.i_projid = 4242;
	CHECK_RC(ext2fs_write_inode_full(fs, 12, (struct ext2_inode *) &out256,
					 sizeof(out256)), 0,
		 "write_inode_full with i_projid=4242");
	CHECK_RC(ext2fs_read_inode(fs, 12, &ino128), 0, "read 128B inode 12");
	ino128.i_uid = 5678;
	CHECK_RC(ext2fs_write_inode(fs, 12, &ino128), 0,
		 "128B write_inode RMW on 256B fs");
	ext2fs_flush_icache(fs);
	memset(&out256, 0, sizeof(out256));
	CHECK_RC(ext2fs_read_inode_full(fs, 12, (struct ext2_inode *) &out256,
					sizeof(out256)), 0,
		 "re-read 256B inode 12 from disk");
	CHECK(out256.i_uid == 5678 && out256.i_projid == 4242,
	      "128B write_inode updated i_uid and preserved upper i_projid");

	/* Exercise icache wrap-around (> 4 distinct inodes) */
	for (i = 20; i <= 26; i++) {
		memset(&ino128, 0, sizeof(ino128));
		ino128.i_mode = LINUX_S_IFREG | 0600;
		ino128.i_uid = 100 + i;
		CHECK_RC(ext2fs_write_new_inode(fs, i, &ino128), 0,
			 "write inode for icache wrap");
	}
	for (i = 20; i <= 26; i++) {
		CHECK_RC(ext2fs_read_inode(fs, i, &out128), 0,
			 "read inode across icache eviction");
		CHECK(out128.i_uid == (unsigned int) (100 + i),
		      "evicted/reloaded inode uid matches");
	}

	/* Override callbacks fs->read_inode and fs->write_inode */
	fs->read_inode = custom_read_cb;
	fs->write_inode = custom_write_cb;
	CHECK_RC(ext2fs_read_inode(fs, 15, &out128), 0,
		 "fs->read_inode handled ino 15");
	CHECK(out128.i_uid == 7777, "custom_read_cb value returned");
	CHECK_RC(ext2fs_read_inode(fs, 20, &out128), 0,
		 "fs->read_inode NOTHANDLED falls back to disk");
	CHECK(out128.i_uid == 120, "fallback disk read succeeded");
	CHECK_RC(ext2fs_write_inode(fs, 15, &out128), 0,
		 "fs->write_inode handled ino 15");
	CHECK(write_cb_seen == 1, "custom_write_cb invoked");
	fs->read_inode = NULL;
	fs->write_inode = NULL;

	CHECK_RC(ext2fs_close_free(&fs), 0, "close fs");
	close(fd);
	unlink(path);
	printf("  test_inode_rw_and_icache: ok\n");
}

/*
 * Suite 2: metadata_csum inode checksum verification, WRITE_INODE_NOCSUM,
 *          READ_INODE_NOCSUM, and EXT2_FLAG_IGNORE_CSUM_ERRORS.
 */
static void test_inode_csum_handling(void)
{
	char			path[64];
	int			fd;
	ext2_filsys		fs;
	struct ext2_inode_large	ino, out;

	fs = create_test_fs(path, &fd, 2048, 64, 256, 1);

	memset(&ino, 0, sizeof(ino));
	ino.i_mode = LINUX_S_IFREG | 0644;
	ino.i_uid = 3333;
	ino.i_links_count = 1;
	ino.i_extra_isize = sizeof(struct ext2_inode_large) -
			    EXT2_GOOD_OLD_INODE_SIZE;
	CHECK_RC(ext2fs_write_inode_full(fs, 14, (struct ext2_inode *) &ino,
					 sizeof(ino)), 0,
		 "write_inode_full stamps valid csum");
	ext2fs_flush_icache(fs);
	CHECK_RC(ext2fs_read_inode_full(fs, 14, (struct ext2_inode *) &out,
					sizeof(out)), 0,
		 "read_inode_full verifies valid csum");
	CHECK(out.i_uid == 3333, "valid csum inode data matches");

	/* Mutate i_uid and write with WRITE_INODE_NOCSUM -> stale checksum! */
	out.i_uid = 4444;
	CHECK_RC(ext2fs_write_inode2(fs, 14, (struct ext2_inode *) &out,
				     sizeof(out), WRITE_INODE_NOCSUM), 0,
		 "write_inode2 with WRITE_INODE_NOCSUM");
	ext2fs_flush_icache(fs);

	CHECK_RC(ext2fs_read_inode2(fs, 14, (struct ext2_inode *) &out,
				    sizeof(out), 0),
		 EXT2_ET_INODE_CSUM_INVALID,
		 "corrupt csum rejected with EXT2_ET_INODE_CSUM_INVALID");
	/* Repeat read to confirm corrupt inode was NOT cached in icache */
	CHECK_RC(ext2fs_read_inode2(fs, 14, (struct ext2_inode *) &out,
				    sizeof(out), 0),
		 EXT2_ET_INODE_CSUM_INVALID,
		 "corrupt inode not inserted into icache");

	/* READ_INODE_NOCSUM bypasses checksum failure */
	memset(&out, 0, sizeof(out));
	CHECK_RC(ext2fs_read_inode2(fs, 14, (struct ext2_inode *) &out,
				    sizeof(out), READ_INODE_NOCSUM), 0,
		 "READ_INODE_NOCSUM succeeds on bad csum");
	CHECK(out.i_uid == 4444, "READ_INODE_NOCSUM returned updated i_uid");

	/* EXT2_FLAG_IGNORE_CSUM_ERRORS also bypasses checksum failure */
	ext2fs_flush_icache(fs);
	fs->flags |= EXT2_FLAG_IGNORE_CSUM_ERRORS;
	CHECK_RC(ext2fs_read_inode_full(fs, 14, (struct ext2_inode *) &out,
					sizeof(out)), 0,
		 "EXT2_FLAG_IGNORE_CSUM_ERRORS bypasses bad csum");
	fs->flags &= ~EXT2_FLAG_IGNORE_CSUM_ERRORS;

	CHECK_RC(ext2fs_close_free(&fs), 0, "close meta_csum fs");
	close(fd);
	unlink(path);
	printf("  test_inode_csum_handling: ok\n");
}

struct scan_cb_ctx {
	dgrp_t	groups[8];
	int	count;
	dgrp_t	abort_on;
};

static errcode_t scan_done_group_cb(ext2_filsys fs EXT2FS_ATTR((unused)),
				    ext2_inode_scan scan EXT2FS_ATTR((unused)),
				    dgrp_t group, void *priv_data)
{
	struct scan_cb_ctx *ctx = priv_data;

	if (ctx->count < 8)
		ctx->groups[ctx->count++] = group;
	if (group == ctx->abort_on)
		return EXT2_ET_CALLBACK_NOTHANDLED;
	return 0;
}

/*
 * Suite 3: Inode scanner (BG_INODE_UNINIT lazy skip, bg_itable_unused,
 *          missing itable flag, goto_blockgroup, done_group callback, and
 *          EXT2_SF_WARN_GARBAGE_INODES).
 */
static void test_inode_scanner(void)
{
	char			path[64];
	int			fd, seen;
	ext2_filsys		fs;
	ext2_inode_scan		scan = NULL;
	ext2_ino_t		ino, ipg;
	struct ext2_inode	inode;
	struct ext2_inode_large	bad_ino;
	struct scan_cb_ctx	cb_ctx;
	blk64_t			saved_g2_itbl;
	unsigned int		i;

	/* 20000 1K blocks -> 3 groups (8192 blks/grp), 96 inodes (32/grp) */
	fs = create_test_fs(path, &fd, 20000, 96, 256, 1);
	ipg = EXT2_INODES_PER_GROUP(fs->super);
	CHECK(fs->group_desc_count == 3 && ipg == 32, "3 groups of 32 inodes");

	/*
	 * Configure groups:
	 *   Group 0: bg_itable_unused = 28 -> only first 4 inodes (1..4)
	 *   Group 1: EXT2_BG_INODE_UNINIT -> skipped under EXT2_SF_DO_LAZY
	 *   Group 2: bg_itable_unused = 30 -> only first 2 inodes (65..66)
	 */
	ext2fs_bg_itable_unused_set(fs, 0, 28);
	ext2fs_group_desc_csum_set(fs, 0);

	ext2fs_bg_flags_set(fs, 1, EXT2_BG_INODE_UNINIT);
	ext2fs_group_desc_csum_set(fs, 1);

	ext2fs_bg_itable_unused_set(fs, 2, 30);
	ext2fs_group_desc_csum_set(fs, 2);

	CHECK_RC(ext2fs_open_inode_scan(fs, 2, &scan), 0, "open_inode_scan");
	memset(&cb_ctx, 0, sizeof(cb_ctx));
	cb_ctx.abort_on = (dgrp_t) -1;
	ext2fs_set_inode_callback(scan, scan_done_group_cb, &cb_ctx);

	seen = 0;
	while (1) {
		CHECK_RC(ext2fs_get_next_inode(scan, &ino, &inode), 0,
			 "get_next_inode lazy scan");
		if (ino == 0)
			break;
		seen++;
	}
	/* Expect 4 inodes from group 0 + 0 from group 1 + 2 from group 2 = 6 */
	CHECK(seen == 6, "lazy scan yielded 6 active inodes across 3 groups");
	CHECK(cb_ctx.count == 3 && cb_ctx.groups[0] == 0 &&
	      cb_ctx.groups[1] == 1 && cb_ctx.groups[2] == 2,
	      "done_group callback invoked for groups 0, 1, 2");

	/* Test goto_blockgroup(2) */
	CHECK_RC(ext2fs_inode_scan_goto_blockgroup(scan, 2), 0,
		 "goto_blockgroup(2)");
	CHECK_RC(ext2fs_get_next_inode(scan, &ino, &inode), 0,
		 "first inode of group 2");
	CHECK(ino == 2 * ipg + 1, "goto_blockgroup positioned at group 2");
	ext2fs_close_inode_scan(scan);

	/* Test missing inode table with and without SKIP_MISSING_ITABLE */
	saved_g2_itbl = ext2fs_inode_table_loc(fs, 2);
	ext2fs_inode_table_loc_set(fs, 2, 0);
	CHECK_RC(ext2fs_open_inode_scan(fs, 0, &scan), 0, "open scan missing");
	CHECK_RC(ext2fs_inode_scan_goto_blockgroup(scan, 2), 0, "goto grp 2");
	CHECK_RC(ext2fs_get_next_inode(scan, &ino, &inode),
		 EXT2_ET_MISSING_INODE_TABLE,
		 "missing itable returns EXT2_ET_MISSING_INODE_TABLE");
	ext2fs_inode_scan_flags(scan, EXT2_SF_SKIP_MISSING_ITABLE, 0);
	CHECK_RC(ext2fs_get_next_inode(scan, &ino, &inode), 0,
		 "SKIP_MISSING_ITABLE skips group 2 cleanly");
	CHECK(ino == 0, "scan finished after skipping last group");
	ext2fs_close_inode_scan(scan);
	ext2fs_inode_table_loc_set(fs, 2, saved_g2_itbl);

	/* Test EXT2_SF_WARN_GARBAGE_INODES on insane block-mapped inodes */
	memset(&bad_ino, 0, sizeof(bad_ino));
	bad_ino.i_mode = LINUX_S_IFREG | 0644;
	bad_ino.i_links_count = 1;
	bad_ino.i_extra_isize = sizeof(struct ext2_inode_large) -
				EXT2_GOOD_OLD_INODE_SIZE;
	for (i = 0; i < 10; i++)
		bad_ino.i_block[i] = 999999U; /* out of bounds */
	/* 1K block / 256B inode = 4 inodes/block; make 3 of 4 insane (> 2) */
	for (ino = 1; ino <= 3; ino++)
		CHECK_RC(ext2fs_write_inode_full(fs, ino,
				(struct ext2_inode *) &bad_ino,
				sizeof(bad_ino)), 0, "write insane inode");

	CHECK_RC(ext2fs_open_inode_scan(fs, 1, &scan), 0, "open garbage scan");
	ext2fs_inode_scan_flags(scan, EXT2_SF_WARN_GARBAGE_INODES, 0);
	CHECK_RC(ext2fs_get_next_inode(scan, &ino, &inode),
		 EXT2_ET_INODE_IS_GARBAGE,
		 "EXT2_SF_WARN_GARBAGE_INODES flags insane inode block");
	ext2fs_close_inode_scan(scan);

	CHECK_RC(ext2fs_close_free(&fs), 0, "close scanner fs");
	close(fd);
	unlink(path);
	printf("  test_inode_scanner: ok\n");
}

static errcode_t custom_get_blocks_cb(ext2_filsys fs EXT2FS_ATTR((unused)),
				      ext2_ino_t ino, blk_t *blocks)
{
	int i;

	if (ino == 12) {
		for (i = 0; i < EXT2_N_BLOCKS; i++)
			blocks[i] = 500 + i;
		return 0;
	}
	return EXT2_ET_CALLBACK_NOTHANDLED;
}

static errcode_t custom_check_dir_cb(ext2_filsys fs EXT2FS_ATTR((unused)),
				     ext2_ino_t ino)
{
	if (ino == 12)
		return 0; /* pretend ino 12 is a directory */
	return EXT2_ET_CALLBACK_NOTHANDLED;
}

/*
 * Suite 4: ext2fs_get_blocks() and ext2fs_check_directory()
 */
static void test_get_blocks_and_check_dir(void)
{
	char			path[64];
	int			fd, i;
	ext2_filsys		fs;
	struct ext2_inode	reg_ino;
	blk_t			blocks[EXT2_N_BLOCKS];

	fs = create_test_fs(path, &fd, 1024, 64, 128, 0);
	CHECK_RC(ext2fs_mkdir(fs, EXT2_ROOT_INO, EXT2_ROOT_INO, 0), 0,
		 "create root dir");

	memset(&reg_ino, 0, sizeof(reg_ino));
	reg_ino.i_mode = LINUX_S_IFREG | 0644;
	reg_ino.i_links_count = 1;
	for (i = 0; i < EXT2_N_BLOCKS; i++)
		reg_ino.i_block[i] = 100 + i;
	CHECK_RC(ext2fs_write_new_inode(fs, 12, &reg_ino), 0, "write ino 12");

	/* ext2fs_get_blocks */
	CHECK_RC(ext2fs_get_blocks(fs, 999, blocks), EXT2_ET_BAD_INODE_NUM,
		 "get_blocks OOB ino rejected");
	memset(blocks, 0, sizeof(blocks));
	CHECK_RC(ext2fs_get_blocks(fs, 12, blocks), 0, "get_blocks ino 12");
	CHECK(blocks[0] == 100 && blocks[14] == 114,
	      "get_blocks returned all 15 i_block entries");

	fs->get_blocks = custom_get_blocks_cb;
	CHECK_RC(ext2fs_get_blocks(fs, 12, blocks), 0,
		 "get_blocks via custom callback");
	CHECK(blocks[0] == 500 && blocks[14] == 514,
	      "custom get_blocks values returned");
	fs->get_blocks = NULL;

	/* ext2fs_check_directory */
	CHECK_RC(ext2fs_check_directory(fs, 999), EXT2_ET_BAD_INODE_NUM,
		 "check_directory OOB ino rejected");
	CHECK_RC(ext2fs_check_directory(fs, EXT2_ROOT_INO), 0,
		 "check_directory succeeds on root dir");
	CHECK_RC(ext2fs_check_directory(fs, 12), EXT2_ET_NO_DIRECTORY,
		 "check_directory rejects regular file with NO_DIRECTORY");

	fs->check_directory = custom_check_dir_cb;
	CHECK_RC(ext2fs_check_directory(fs, 12), 0,
		 "check_directory override callback accepted ino 12");
	CHECK_RC(ext2fs_check_directory(fs, EXT2_ROOT_INO), 0,
		 "check_directory NOTHANDLED fell back to inode check");
	fs->check_directory = NULL;

	CHECK_RC(ext2fs_close_free(&fs), 0, "close fs");
	close(fd);
	unlink(path);
	printf("  test_get_blocks_and_check_dir: ok\n");
}

/*
 * Suite 5: Extended 34-bit inode timestamps (ext2fs_inode_xtime_set/get
 *          and ext2fs_write_new_inode post-2038 / pre-1970 handling).
 */
static void test_inode_timestamps(void)
{
	struct ext2_inode_large	large;
	struct ext2_inode	small;
#if (SIZEOF_TIME_T > 4)
	static const time_t cases[] = {
		(time_t) -2147483648LL,	/* 1901-12-13 (INT32_MIN) */
		(time_t) -86400,	/* 1969-12-31 */
		(time_t) 0x6af01234LL,	/* 2026 (bits 28..30 set) */
		(time_t) 0x7fffffffLL,	/* 2038-01-19 03:14:07 */
		(time_t) 0x80000000LL,	/* 2038-01-19 03:14:08 */
		(time_t) 0xffffffffLL,	/* 2106-02-07 06:28:15 */
		(time_t) 0x100000000LL,	/* 2106-02-07 06:28:16 (lo == 0) */
		(time_t) 0x180000000LL,	/* 2174-02-25 (extra == 2, bit 31) */
		(time_t) 0x37fffffffLL,	/* 2446-05-10 (max 34-bit signed) */
	};
	size_t			i;
	char			path[64];
	int			fd;
	ext2_filsys		fs;
	struct ext2_inode	new_ino;
	struct ext2_inode_large	disk_ino;
	time_t			future = (time_t) 0x90001234LL;
#endif

	memset(&large, 0, sizeof(large));
	large.i_extra_isize = sizeof(struct ext2_inode_large) -
			      EXT2_GOOD_OLD_INODE_SIZE;
	ext2fs_inode_xtime_set(&large, i_mtime, (time_t) 0x6af01234);
	CHECK(large.i_mtime == 0x6af01234U,
	      "xtime_set preserves bits 28..31 on large inode");
	CHECK(ext2fs_inode_xtime_get(&large, i_mtime) == (time_t) 0x6af01234,
	      "xtime_get round-trips 0x6af01234 on large inode");

	ext2fs_inode_xtime_set(&large, i_atime, (time_t) -86400);
	CHECK(ext2fs_inode_xtime_get(&large, i_atime) == (time_t) -86400,
	      "xtime_get sign-extends pre-1970 timestamp on large inode");

	memset(&small, 0, sizeof(small));
	ext2fs_inode_xtime_set(&small, i_mtime, (time_t) -86400);
	CHECK(ext2fs_inode_xtime_get(&small, i_mtime) == (time_t) -86400,
	      "xtime_get sign-extends pre-1970 timestamp on 128B inode");

#if (SIZEOF_TIME_T > 4)
	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		ext2fs_inode_xtime_set(&large, i_mtime, cases[i]);
		CHECK(ext2fs_inode_xtime_get(&large, i_mtime) == cases[i],
		      "large inode 34-bit mtime round-trip");
		ext2fs_inode_xtime_set(&large, i_crtime, cases[i]);
		CHECK(ext2fs_inode_xtime_get(&large, i_crtime) == cases[i],
		      "large inode 34-bit crtime round-trip");
	}

	ext2fs_inode_xtime_set(&small, i_ctime, future);
	CHECK(ext2fs_inode_xtime_get(&small, i_ctime) == (time_t) INT32_MAX,
	      "128B inode clamps post-2038 timestamp to INT32_MAX");

	fs = create_test_fs(path, &fd, 2048, 64, 256, 1);
	fs->now = future;
	memset(&new_ino, 0, sizeof(new_ino));
	new_ino.i_mode = LINUX_S_IFREG | 0644;
	new_ino.i_links_count = 1;
	CHECK_RC(ext2fs_write_new_inode(fs, 12, &new_ino), 0,
		 "write_new_inode with post-2038 fs->now");
	ext2fs_flush_icache(fs);
	memset(&disk_ino, 0, sizeof(disk_ino));
	CHECK_RC(ext2fs_read_inode_full(fs, 12, (struct ext2_inode *) &disk_ino,
					sizeof(disk_ino)), 0,
		 "read_inode_full post-2038 new inode");
	CHECK(ext2fs_inode_xtime_get(&disk_ino, i_atime) == future &&
	      ext2fs_inode_xtime_get(&disk_ino, i_ctime) == future &&
	      ext2fs_inode_xtime_get(&disk_ino, i_mtime) == future &&
	      ext2fs_inode_xtime_get(&disk_ino, i_crtime) == future,
	      "write_new_inode encodes 34-bit post-2038 atime/ctime/mtime/crtime");
	CHECK_RC(ext2fs_close_free(&fs), 0, "close post-2038 fs");
	close(fd);
	unlink(path);
#endif
	printf("  test_inode_timestamps: ok\n");
}

int main(int argc EXT2FS_ATTR((unused)), char *argv[] EXT2FS_ATTR((unused)))
{
	add_error_table(&et_ext2_error_table);
	printf("Running tst_inode unit tests...\n");

	test_inode_rw_and_icache();
	test_inode_csum_handling();
	test_inode_scanner();
	test_get_blocks_and_check_dir();
	test_inode_timestamps();

	remove_error_table(&et_ext2_error_table);
	printf("tst_inode: %d checks run, %d failed\n",
	       test_count, fail_count);
	return fail_count ? 1 : 0;
}
