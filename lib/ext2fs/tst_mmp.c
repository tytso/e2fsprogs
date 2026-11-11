/*
 * tst_mmp.c --- unit tests for mmp.c
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

#ifdef CONFIG_MMP
static void test_mmp_ops(void)
{
	struct ext2_super_block	param;
	ext2_filsys		fs = NULL;
	struct mmp_struct	*mmp_blk_buf = NULL, *mmp_s;
	blk64_t			saved_mmp_blk;
	unsigned int		seq;
	char			path[64];
	unsigned char		flipped;
	int			fd, i;

	/* 1. ext2fs_mmp_new_seq stays within valid active sequence range */
	for (i = 0; i < 16; i++) {
		seq = ext2fs_mmp_new_seq();
		CHECK(seq <= EXT4_MMP_SEQ_MAX,
		      "mmp_new_seq <= EXT4_MMP_SEQ_MAX");
	}

	/* 2. Set up a 4K-block filesystem with MMP and metadata_csum */
	strcpy(path, "/tmp/tst_mmp.XXXXXX");
	fd = mkstemp(path);
	if (fd < 0) {
		perror("mkstemp");
		fail_count++;
		return;
	}
	if (ftruncate(fd, (off_t) 512 * 4096) < 0) {
		perror("ftruncate");
		close(fd);
		unlink(path);
		fail_count++;
		return;
	}

	memset(&param, 0, sizeof(param));
	ext2fs_blocks_count_set(&param, 512);
	param.s_log_block_size = 2;
	param.s_log_cluster_size = 2;
	param.s_inodes_count = 64;
	param.s_rev_level = EXT2_DYNAMIC_REV;
	param.s_inode_size = 256;
	param.s_desc_size = EXT2_MIN_DESC_SIZE_64BIT;
	ext2fs_set_feature_64bit(&param);
	ext2fs_set_feature_extents(&param);
	ext2fs_set_feature_mmp(&param);
	ext2fs_set_feature_metadata_csum(&param);

	if (ext2fs_initialize(path, EXT2_FLAG_RW | EXT2_FLAG_64BITS,
			      &param, unix_io_manager, &fs) ||
	    ext2fs_allocate_tables(fs)) {
		fprintf(stderr, "Failed to initialize test filesystem\n");
		close(fd);
		unlink(path);
		fail_count++;
		return;
	}
	memset(fs->super->s_uuid, 0x5a, sizeof(fs->super->s_uuid));
	fs->super->s_checksum_type = EXT2_CRC32C_CHKSUM;
	ext2fs_init_csum_seed(fs);
	CHECK_RC(ext2fs_set_gdt_csum(fs), 0, "set_gdt_csum");

	CHECK_RC(ext2fs_get_memzero(fs->blocksize, &mmp_blk_buf), 0,
		 "alloc mmp_blk_buf");

	/* 3. ext2fs_mmp_init validation and allocation */
	fs->super->s_mmp_update_interval = EXT4_MMP_MAX_UPDATE_INTERVAL + 1;
	CHECK_RC(ext2fs_mmp_init(fs), EXT2_ET_INVALID_ARGUMENT,
		 "mmp_init rejects interval > MAX_UPDATE_INTERVAL");

	fs->super->s_mmp_update_interval = 0;
	CHECK_RC(ext2fs_mmp_init(fs), 0, "mmp_init");
	CHECK(fs->super->s_mmp_update_interval == EXT4_MMP_UPDATE_INTERVAL,
	      "mmp_init sets default update interval");
	saved_mmp_blk = fs->super->s_mmp_block;
	CHECK(saved_mmp_blk > fs->super->s_first_data_block &&
	      saved_mmp_blk < ext2fs_blocks_count(fs->super) &&
	      ext2fs_test_block_bitmap2(fs->block_map, saved_mmp_blk) != 0,
	      "mmp_init allocated and marked s_mmp_block");

	/* 4. ext2fs_mmp_read and ext2fs_mmp_write bounds, csum, and magic */
	CHECK_RC(ext2fs_mmp_read(fs, 0, mmp_blk_buf), EXT2_ET_MMP_BAD_BLOCK,
		 "mmp_read block 0 -> MMP_BAD_BLOCK");
	CHECK_RC(ext2fs_mmp_read(fs, ext2fs_blocks_count(fs->super),
				 mmp_blk_buf),
		 EXT2_ET_MMP_BAD_BLOCK,
		 "mmp_read block == blocks_count -> MMP_BAD_BLOCK");

	/* A rejected mmp_write must not touch the MMP state */
	fs->super->s_mmp_block = ext2fs_blocks_count(fs->super) + 1;
	fs->mmp_last_written = 0;
	CHECK_RC(ext2fs_mmp_write(fs, fs->super->s_mmp_block, fs->mmp_buf),
		 EXT2_ET_MMP_BAD_BLOCK,
		 "mmp_write invalid s_mmp_block -> MMP_BAD_BLOCK");
	CHECK(fs->mmp_last_written == 0,
	      "rejected mmp_write leaves mmp_last_written alone");

	/* mmp_write checks the block it writes, using mmp_read's bounds */
	fs->super->s_mmp_block = saved_mmp_blk;
	CHECK_RC(ext2fs_mmp_write(fs, fs->super->s_first_data_block,
				  fs->mmp_buf),
		 EXT2_ET_MMP_BAD_BLOCK,
		 "mmp_write block == s_first_data_block -> MMP_BAD_BLOCK");
	CHECK_RC(ext2fs_mmp_write(fs, ext2fs_blocks_count(fs->super),
				  fs->mmp_buf),
		 EXT2_ET_MMP_BAD_BLOCK,
		 "mmp_write block == blocks_count -> MMP_BAD_BLOCK");
	fs->super->s_mmp_block = ext2fs_blocks_count(fs->super) + 1;
	CHECK_RC(ext2fs_mmp_write(fs, saved_mmp_blk, fs->mmp_buf), 0,
		 "mmp_write valid block with invalid s_mmp_block");

	/* mmp_clear must not write to a corrupted s_mmp_block */
	fs->super->s_mmp_block = fs->super->s_first_data_block;
	CHECK_RC(ext2fs_mmp_clear(fs), EXT2_ET_MMP_BAD_BLOCK,
		 "mmp_clear with s_mmp_block == s_first_data_block");
	fs->super->s_mmp_block = ext2fs_blocks_count(fs->super);
	CHECK_RC(ext2fs_mmp_clear(fs), EXT2_ET_MMP_BAD_BLOCK,
		 "mmp_clear with s_mmp_block == blocks_count");
	fs->super->s_mmp_block = saved_mmp_blk;

	CHECK_RC(ext2fs_mmp_read(fs, saved_mmp_blk, mmp_blk_buf), 0,
		 "mmp_read clean block");
	CHECK(mmp_blk_buf->mmp_magic == EXT4_MMP_MAGIC &&
	      mmp_blk_buf->mmp_seq == EXT4_MMP_SEQ_CLEAN &&
	      mmp_blk_buf->mmp_check_interval >= EXT4_MMP_MIN_CHECK_INTERVAL,
	      "mmp_read returned clean MMP struct");

	/* Corrupt a byte on disk to trigger EXT2_ET_MMP_CSUM_INVALID */
	CHECK(pread(fd, &flipped, 1,
		    (off_t) saved_mmp_blk * fs->blocksize + 16) == 1,
	      "pread byte from MMP block");
	flipped ^= 0xff;
	CHECK(pwrite(fd, &flipped, 1,
		     (off_t) saved_mmp_blk * fs->blocksize + 16) == 1,
	      "corrupt MMP block byte on disk");
	CHECK_RC(ext2fs_mmp_read(fs, saved_mmp_blk, mmp_blk_buf),
		 EXT2_ET_MMP_CSUM_INVALID,
		 "mmp_read detects bad metadata_csum");

	fs->flags |= EXT2_FLAG_IGNORE_CSUM_ERRORS;
	CHECK_RC(ext2fs_mmp_read(fs, saved_mmp_blk, mmp_blk_buf), 0,
		 "mmp_read ignores bad csum with EXT2_FLAG_IGNORE_CSUM_ERRORS");
	fs->flags &= ~EXT2_FLAG_IGNORE_CSUM_ERRORS;

	/* Corrupt mmp_magic with a valid checksum -> EXT2_ET_MMP_MAGIC_INVALID */
	memset(mmp_blk_buf, 0, fs->blocksize);
	mmp_blk_buf->mmp_magic = 0x12345678U;
	CHECK_RC(ext2fs_mmp_write(fs, saved_mmp_blk, mmp_blk_buf), 0,
		 "write bad-magic MMP block with valid csum");
	CHECK_RC(ext2fs_mmp_read(fs, saved_mmp_blk, mmp_blk_buf),
		 EXT2_ET_MMP_MAGIC_INVALID,
		 "mmp_read detects invalid mmp_magic");

	/* 5. ext2fs_mmp_clear RO rejection and RW reset */
	fs->flags &= ~EXT2_FLAG_RW;
	CHECK_RC(ext2fs_mmp_clear(fs), EXT2_ET_RO_FILSYS,
		 "mmp_clear on RO fs returns RO_FILSYS");
	fs->flags |= EXT2_FLAG_RW;
	CHECK_RC(ext2fs_mmp_clear(fs), 0, "mmp_clear resets MMP block");
	CHECK_RC(ext2fs_mmp_read(fs, saved_mmp_blk, mmp_blk_buf), 0,
		 "mmp_read after mmp_clear");
	CHECK(mmp_blk_buf->mmp_seq == EXT4_MMP_SEQ_CLEAN,
	      "mmp_clear restored EXT4_MMP_SEQ_CLEAN");

	/* 6. Fast non-sleeping ext2fs_mmp_start paths */
	/* (a) Read-only open on SEQ_CLEAN returns 0 immediately */
	fs->flags &= ~EXT2_FLAG_RW;
	CHECK_RC(ext2fs_mmp_start(fs), 0,
		 "mmp_start on RO fs with SEQ_CLEAN succeeds immediately");
	fs->flags |= EXT2_FLAG_RW;

	/* (b) SEQ_FSCK returns EXT2_ET_MMP_FSCK_ON immediately */
	mmp_s = fs->mmp_buf;
	mmp_s->mmp_seq = EXT4_MMP_SEQ_FSCK;
	CHECK_RC(ext2fs_mmp_write(fs, saved_mmp_blk, fs->mmp_buf), 0,
		 "write SEQ_FSCK");
	CHECK_RC(ext2fs_mmp_start(fs), EXT2_ET_MMP_FSCK_ON,
		 "mmp_start on SEQ_FSCK returns MMP_FSCK_ON");

	/* (c) Unknown seq (> SEQ_FSCK) returns EXT2_ET_MMP_UNKNOWN_SEQ */
	mmp_s->mmp_seq = EXT4_MMP_SEQ_FSCK + 1;
	CHECK_RC(ext2fs_mmp_write(fs, saved_mmp_blk, fs->mmp_buf), 0,
		 "write unknown seq");
	CHECK_RC(ext2fs_mmp_start(fs), EXT2_ET_MMP_UNKNOWN_SEQ,
		 "mmp_start on unknown seq returns MMP_UNKNOWN_SEQ");

	/* 7. ext2fs_mmp_update / ext2fs_mmp_update2 and ext2fs_mmp_stop */
	CHECK_RC(ext2fs_mmp_clear(fs), 0, "reset clean before update tests");
	CHECK_RC(ext2fs_mmp_read(fs, saved_mmp_blk, fs->mmp_buf), 0,
		 "sync fs->mmp_buf and fs->mmp_cmp");

	/* SKIP_MMP flag short-circuits update2 */
	fs->flags |= EXT2_FLAG_SKIP_MMP;
	CHECK_RC(ext2fs_mmp_update2(fs, 1), 0, "mmp_update2 with SKIP_MMP");
	fs->flags &= ~EXT2_FLAG_SKIP_MMP;
	CHECK_RC(ext2fs_mmp_read(fs, saved_mmp_blk, mmp_blk_buf), 0,
		 "read after SKIP_MMP update2");
	CHECK(mmp_blk_buf->mmp_seq == EXT4_MMP_SEQ_CLEAN,
	      "SKIP_MMP left mmp_seq untouched");

	/* Throttled update (immediately == 0) skips write when recent */
	CHECK_RC(ext2fs_mmp_update(fs), 0, "throttled mmp_update");
	CHECK_RC(ext2fs_mmp_read(fs, saved_mmp_blk, mmp_blk_buf), 0,
		 "read after throttled mmp_update");
	CHECK(mmp_blk_buf->mmp_seq == EXT4_MMP_SEQ_CLEAN,
	      "throttled mmp_update did not rewrite recent MMP block");

	/* Immediate update2 (immediately == 1) writes EXT4_MMP_SEQ_FSCK */
	CHECK_RC(ext2fs_mmp_update2(fs, 1), 0, "mmp_update2(immediately=1)");
	CHECK_RC(ext2fs_mmp_read(fs, saved_mmp_blk, mmp_blk_buf), 0,
		 "read after immediate mmp_update2");
	CHECK(mmp_blk_buf->mmp_seq == EXT4_MMP_SEQ_FSCK,
	      "immediate mmp_update2 wrote EXT4_MMP_SEQ_FSCK");

	/* Expired interval triggers unforced ext2fs_mmp_update() write */
	CHECK_RC(ext2fs_mmp_clear(fs), 0, "clear before expired update");
	CHECK_RC(ext2fs_mmp_read(fs, saved_mmp_blk, fs->mmp_buf), 0,
		 "sync mmp_buf before expired update");
	fs->mmp_last_written = 0;
	CHECK_RC(ext2fs_mmp_update(fs), 0, "expired-interval mmp_update");
	CHECK_RC(ext2fs_mmp_read(fs, saved_mmp_blk, mmp_blk_buf), 0,
		 "read after expired-interval mmp_update");
	CHECK(mmp_blk_buf->mmp_seq == EXT4_MMP_SEQ_FSCK,
	      "expired-interval mmp_update wrote EXT4_MMP_SEQ_FSCK");

	/* External modification triggers EXT2_ET_MMP_CHANGE_ABORT */
	memcpy(mmp_blk_buf, fs->mmp_buf, fs->blocksize);
	mmp_blk_buf->mmp_seq = 0x1234U;
	CHECK_RC(ext2fs_mmp_write(fs, saved_mmp_blk, mmp_blk_buf), 0,
		 "simulate external MMP write");
	CHECK_RC(ext2fs_mmp_update2(fs, 1), EXT2_ET_MMP_CHANGE_ABORT,
		 "mmp_update2 detects external MMP modification");
	CHECK_RC(ext2fs_mmp_stop(fs), EXT2_ET_MMP_CHANGE_ABORT,
		 "mmp_stop detects external MMP modification");
	CHECK(fs->mmp_fd == -1, "mmp_stop closed mmp_fd on error");

	/* Clean ext2fs_mmp_stop restores SEQ_CLEAN and closes mmp_fd */
	CHECK_RC(ext2fs_mmp_read(fs, saved_mmp_blk, fs->mmp_buf), 0,
		 "re-sync fs->mmp_buf with on-disk MMP block");
	CHECK(fs->mmp_fd > 0, "mmp_read reopened mmp_fd");
	CHECK_RC(ext2fs_mmp_stop(fs), 0, "clean mmp_stop");
	CHECK(fs->mmp_fd == -1, "clean mmp_stop closed mmp_fd");
	CHECK_RC(ext2fs_mmp_read(fs, saved_mmp_blk, fs->mmp_buf), 0,
		 "read after clean mmp_stop");
	mmp_s = fs->mmp_buf;
	CHECK(mmp_s->mmp_seq == EXT4_MMP_SEQ_CLEAN,
	      "clean mmp_stop restored EXT4_MMP_SEQ_CLEAN");

	ext2fs_free_mem(&mmp_blk_buf);
	CHECK_RC(ext2fs_close_free(&fs), 0, "close_free MMP test fs");

	/*
	 * 8. Opening with EXT2_FLAG_SKIP_MMP skips ext2fs_mmp_start(), so
	 * there is no fs->mmp_buf.  ext2fs_mmp_update2() must cope with
	 * that when the flag is cleared later on.
	 */
	CHECK_RC(ext2fs_open(path, EXT2_FLAG_RW | EXT2_FLAG_SKIP_MMP, 0, 0,
			     unix_io_manager, &fs),
		 0, "reopen MMP test fs with EXT2_FLAG_SKIP_MMP");
	if (fs) {
		CHECK(fs->mmp_buf == NULL, "SKIP_MMP open left mmp_buf unset");
		fs->flags &= ~EXT2_FLAG_SKIP_MMP;
		CHECK_RC(ext2fs_mmp_update2(fs, 1), 0,
			 "mmp_update2 without mmp_buf");
		CHECK_RC(ext2fs_close_free(&fs), 0,
			 "close_free reopened MMP test fs");
	}
	close(fd);
	unlink(path);
}
#else
static void test_mmp_unsupported(void)
{
	struct struct_ext2_filsys	dummy_fs;
	struct ext2_super_block		dummy_sb;

	memset(&dummy_fs, 0, sizeof(dummy_fs));
	memset(&dummy_sb, 0, sizeof(dummy_sb));
	dummy_fs.super = &dummy_sb;

	CHECK_RC(ext2fs_mmp_init(&dummy_fs), EXT2_ET_OP_NOT_SUPPORTED,
		 "mmp_init unsupported without CONFIG_MMP");
	CHECK_RC(ext2fs_mmp_read(&dummy_fs, 1, NULL), EXT2_ET_OP_NOT_SUPPORTED,
		 "mmp_read unsupported without CONFIG_MMP");
	CHECK_RC(ext2fs_mmp_write(&dummy_fs, 1, NULL), EXT2_ET_OP_NOT_SUPPORTED,
		 "mmp_write unsupported without CONFIG_MMP");
	CHECK_RC(ext2fs_mmp_clear(&dummy_fs), EXT2_ET_OP_NOT_SUPPORTED,
		 "mmp_clear unsupported without CONFIG_MMP");
	CHECK_RC(ext2fs_mmp_start(&dummy_fs), EXT2_ET_OP_NOT_SUPPORTED,
		 "mmp_start unsupported without CONFIG_MMP");
}
#endif

int main(int argc EXT2FS_ATTR((unused)), char **argv EXT2FS_ATTR((unused)))
{
	add_error_table(&et_ext2_error_table);

#ifdef CONFIG_MMP
	test_mmp_ops();
#else
	test_mmp_unsupported();
#endif

	if (fail_count) {
		fprintf(stderr, "tst_mmp: %d/%d checks FAILED\n",
			fail_count, test_count);
		return 1;
	}
	printf("tst_mmp: all %d checks passed\n", test_count);
	return 0;
}
