/*
 * tst_filsys.c --- unit tests for initialize.c, openfs.c, closefs.c,
 *                  check_desc.c, rw_bitmaps.c, dupfs.c, and res_gdt.c
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
#include "e2image.h"

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

static int make_temp_file(char *path_out, off_t size)
{
	int fd;

	strcpy(path_out, "/tmp/tst_filsys.XXXXXX");
	fd = mkstemp(path_out);
	if (fd < 0) {
		perror("mkstemp");
		exit(1);
	}
	if (ftruncate(fd, size) < 0) {
		perror("ftruncate");
		close(fd);
		unlink(path_out);
		exit(1);
	}
	return fd;
}

static void test_initialize_and_backups(void)
{
	struct ext2_super_block	param;
	ext2_filsys		fs = NULL;
	char			path[64];
	int			fd;
	dgrp_t			three, five, seven, grp;
	dgrp_t			backups[8];
	int			n;
	blk64_t			sb_blk, old_desc, new_desc;
	blk_t			used_blks;

	fd = make_temp_file(path, (off_t) 200000 * 1024);

	/* 1. Parameter validation error paths */
	CHECK_RC(ext2fs_initialize(path, EXT2_FLAG_RW, NULL,
				   unix_io_manager, &fs),
		 EXT2_ET_INVALID_ARGUMENT, "init NULL param");

	memset(&param, 0, sizeof(param));
	CHECK_RC(ext2fs_initialize(path, EXT2_FLAG_RW, &param,
				   unix_io_manager, &fs),
		 EXT2_ET_INVALID_ARGUMENT, "init zero blocks");

	memset(&param, 0, sizeof(param));
	ext2fs_blocks_count_set(&param, 4096);
	ext2fs_r_blocks_count_set(&param, 4096);
	CHECK_RC(ext2fs_initialize(path, EXT2_FLAG_RW, &param,
				   unix_io_manager, &fs),
		 EXT2_ET_INVALID_ARGUMENT, "init r_blocks == blocks");

	memset(&param, 0, sizeof(param));
	ext2fs_blocks_count_set(&param, 4096);
	param.s_feature_incompat = 0x80000000U;
	CHECK_RC(ext2fs_initialize(path, EXT2_FLAG_RW, &param,
				   unix_io_manager, &fs),
		 EXT2_ET_UNSUPP_FEATURE, "init unsupp incompat");

	memset(&param, 0, sizeof(param));
	ext2fs_blocks_count_set(&param, 4096);
	param.s_feature_ro_compat = 0x80000000U;
	CHECK_RC(ext2fs_initialize(path, EXT2_FLAG_RW, &param,
				   unix_io_manager, &fs),
		 EXT2_ET_RO_UNSUPP_FEATURE, "init unsupp ro_compat");

	memset(&param, 0, sizeof(param));
	ext2fs_blocks_count_set(&param, 8);
	CHECK_RC(ext2fs_initialize(path, EXT2_FLAG_RW, &param,
				   unix_io_manager, &fs),
		 EXT2_ET_TOOSMALL, "init too small");

	memset(&param, 0, sizeof(param));
	ext2fs_blocks_count_set(&param, 1024);
	param.s_inodes_count = 65536;
	CHECK_RC(ext2fs_initialize(path, EXT2_FLAG_RW, &param,
				   unix_io_manager, &fs),
		 EXT2_ET_TOO_MANY_INODES, "init too many inodes");

	memset(&param, 0, sizeof(param));
	ext2fs_blocks_count_set(&param, 16384);
	ext2fs_set_feature_resize_inode(&param);
	param.s_reserved_gdt_blocks = 1000;
	CHECK_RC(ext2fs_initialize(path, EXT2_FLAG_RW, &param,
				   unix_io_manager, &fs),
		 EXT2_ET_RES_GDT_BLOCKS, "init too many reserved gdt blocks");

	/* 2. Dense superblocks + dynamic rev upgrade */
	memset(&param, 0, sizeof(param));
	ext2fs_blocks_count_set(&param, 32768);
	param.s_rev_level = EXT2_GOOD_OLD_REV;
	CHECK_RC(ext2fs_initialize(path, EXT2_FLAG_RW, &param,
				   unix_io_manager, &fs),
		 0, "init dense rev0");
	CHECK(fs->group_desc_count == 4, "dense 4 groups");
	CHECK(ext2fs_bg_has_super(fs, 0) == 1 &&
	      ext2fs_bg_has_super(fs, 2) == 1 &&
	      ext2fs_bg_has_super(fs, 3) == 1, "dense bg_has_super all 1");
	three = 1; five = 5; seven = 7;
	CHECK(ext2fs_list_backups(fs, &three, &five, &seven) == 1 &&
	      ext2fs_list_backups(fs, &three, &five, &seven) == 2 &&
	      ext2fs_list_backups(fs, &three, &five, &seven) == 3,
	      "dense list_backups 1,2,3");
	ext2fs_update_dynamic_rev(fs);
	CHECK(fs->super->s_rev_level == EXT2_DYNAMIC_REV &&
	      fs->super->s_first_ino == EXT2_GOOD_OLD_FIRST_INO &&
	      fs->super->s_inode_size == EXT2_GOOD_OLD_INODE_SIZE,
	      "update_dynamic_rev fields");
	ext2fs_free(fs);
	fs = NULL;

	/* 3. sparse_super backup group layout */
	memset(&param, 0, sizeof(param));
	ext2fs_blocks_count_set(&param, 81920);
	param.s_rev_level = EXT2_DYNAMIC_REV;
	ext2fs_set_feature_sparse_super(&param);
	CHECK_RC(ext2fs_initialize(path, EXT2_FLAG_RW | EXT2_FLAG_64BITS,
				   &param, unix_io_manager, &fs),
		 0, "init sparse_super 10 groups");
	CHECK(ext2fs_bg_has_super(fs, 0) == 1 &&
	      ext2fs_bg_has_super(fs, 1) == 1 &&
	      ext2fs_bg_has_super(fs, 2) == 0 &&
	      ext2fs_bg_has_super(fs, 3) == 1 &&
	      ext2fs_bg_has_super(fs, 4) == 0 &&
	      ext2fs_bg_has_super(fs, 5) == 1 &&
	      ext2fs_bg_has_super(fs, 7) == 1 &&
	      ext2fs_bg_has_super(fs, 8) == 0 &&
	      ext2fs_bg_has_super(fs, 9) == 1,
	      "sparse_super bg_has_super powers of 3,5,7");
	three = 1; five = 5; seven = 7;
	n = 0;
	while ((grp = ext2fs_list_backups(fs, &three, &five, &seven)) <
	       fs->group_desc_count && n < 8)
		backups[n++] = grp;
	CHECK(n == 5 && backups[0] == 1 && backups[1] == 3 &&
	      backups[2] == 5 && backups[3] == 7 && backups[4] == 9,
	      "sparse_super list_backups == 1,3,5,7,9");
	ext2fs_free(fs);
	fs = NULL;

	/* 4. sparse_super2 sorting + backup locations */
	memset(&param, 0, sizeof(param));
	ext2fs_blocks_count_set(&param, 65536);
	param.s_rev_level = EXT2_DYNAMIC_REV;
	ext2fs_set_feature_sparse_super2(&param);
	param.s_backup_bgs[0] = 5;
	param.s_backup_bgs[1] = 2;
	CHECK_RC(ext2fs_initialize(path, EXT2_FLAG_RW | EXT2_FLAG_64BITS,
				   &param, unix_io_manager, &fs),
		 0, "init sparse_super2");
	CHECK(fs->super->s_backup_bgs[0] == 2 &&
	      fs->super->s_backup_bgs[1] == 5,
	      "sparse_super2 sorted s_backup_bgs");
	CHECK(ext2fs_bg_has_super(fs, 0) == 1 &&
	      ext2fs_bg_has_super(fs, 1) == 0 &&
	      ext2fs_bg_has_super(fs, 2) == 1 &&
	      ext2fs_bg_has_super(fs, 5) == 1 &&
	      ext2fs_bg_has_super(fs, 7) == 0,
	      "sparse_super2 bg_has_super");
	three = 1; five = 5; seven = 7;
	CHECK(ext2fs_list_backups(fs, &three, &five, &seven) == 2 &&
	      ext2fs_list_backups(fs, &three, &five, &seven) == 5 &&
	      ext2fs_list_backups(fs, &three, &five, &seven) >= fs->group_desc_count,
	      "sparse_super2 list_backups == 2,5");
	ext2fs_free(fs);
	fs = NULL;

	/* 5. meta_bg descriptor location via ext2fs_super_and_bgd_loc2 */
	memset(&param, 0, sizeof(param));
	ext2fs_blocks_count_set(&param, 163840); /* 20 groups @ 1K */
	param.s_rev_level = EXT2_DYNAMIC_REV;
	param.s_desc_size = EXT2_MIN_DESC_SIZE_64BIT; /* 16 groups/block */
	ext2fs_set_feature_64bit(&param);
	ext2fs_set_feature_sparse_super(&param);
	ext2fs_set_feature_meta_bg(&param);
	param.s_first_meta_bg = 1;
	CHECK_RC(ext2fs_initialize(path, EXT2_FLAG_RW | EXT2_FLAG_64BITS,
				   &param, unix_io_manager, &fs),
		 0, "init meta_bg 20 groups");
	sb_blk = old_desc = new_desc = 0;
	used_blks = 0;
	CHECK_RC(ext2fs_super_and_bgd_loc2(fs, 16, &sb_blk, &old_desc,
					   &new_desc, &used_blks),
		 0, "super_and_bgd_loc2 meta_bg group 16");
	CHECK(old_desc == 0 && new_desc != 0 && used_blks == 1,
	      "meta_bg group 16 uses new_desc_blk");
	ext2fs_free(fs);

	close(fd);
	unlink(path);
}

static void test_open_flush_close(void)
{
	struct ext2_super_block	param;
	struct ext2_image_hdr	hdr;
	ext2_filsys		fs = NULL, fs2 = NULL, img_fs = NULL;
	io_channel		orig_io = NULL, clone_io = NULL;
	char			path[64], path2[64], img_path[64], opt_name[96];
	char			copy_buf[16384];
	int			fd, fd2, img_fd;
	__u16			orig_magic, bad_magic = 0;
	__u8			orig_byte, flipped;

	fd = make_temp_file(path, (off_t) 32768 * 1024);

	memset(&param, 0, sizeof(param));
	ext2fs_blocks_count_set(&param, 24576);
	param.s_inodes_count = 192;
	param.s_rev_level = EXT2_DYNAMIC_REV;
	param.s_inode_size = 256;
	param.s_desc_size = EXT2_MIN_DESC_SIZE_64BIT;
	param.s_checksum_type = EXT2_CRC32C_CHKSUM;
	memset(param.s_uuid, 0x41, sizeof(param.s_uuid));
	ext2fs_set_feature_sparse_super(&param);
	ext2fs_set_feature_extents(&param);
	ext2fs_set_feature_64bit(&param);
	ext2fs_set_feature_metadata_csum(&param);

	CHECK_RC(ext2fs_initialize(path, EXT2_FLAG_RW | EXT2_FLAG_64BITS,
				   &param, unix_io_manager, &fs),
		 0, "init 3-group fs");
	memset(fs->super->s_uuid, 0x41, sizeof(fs->super->s_uuid));
	fs->super->s_checksum_type = EXT2_CRC32C_CHKSUM;
	ext2fs_init_csum_seed(fs);
	CHECK_RC(ext2fs_allocate_tables(fs), 0, "alloc_tables");
	CHECK_RC(ext2fs_set_gdt_csum(fs), 0, "set_gdt_csum");
	CHECK_RC(ext2fs_flush2(fs, 0), 0, "flush primary and backup SBs");
	CHECK_RC(ext2fs_close_free(&fs), 0, "close_free");
	CHECK(fs == NULL, "close_free zeroes pointer");

	/* Open error arguments */
	CHECK_RC(ext2fs_open2(path, NULL, EXT2_FLAG_64BITS, 8193, 0,
			      unix_io_manager, &fs),
		 EXT2_ET_INVALID_ARGUMENT,
		 "open backup superblock without block_size");
	CHECK_RC(ext2fs_open2(path, NULL, EXT2_FLAG_64BITS, 0, 4096,
			      unix_io_manager, &fs),
		 EXT2_ET_UNEXPECTED_BLOCK_SIZE,
		 "open wrong expected block_size");

	/* SUPER_ONLY open skips group descriptors */
	CHECK_RC(ext2fs_open2(path, NULL,
			      EXT2_FLAG_64BITS | EXT2_FLAG_SUPER_ONLY, 0, 0,
			      unix_io_manager, &fs),
		 0, "open SUPER_ONLY");
	CHECK(fs != NULL && fs->group_desc == NULL,
	      "SUPER_ONLY leaves group_desc NULL");
	CHECK_RC(ext2fs_close_free(&fs), 0, "close SUPER_ONLY");

	/* Corrupt primary superblock magic, verify backup superblock open */
	CHECK(pread(fd, &orig_magic, 2, 1024 + 56) == 2, "pread s_magic");
	CHECK(pwrite(fd, &bad_magic, 2, 1024 + 56) == 2, "corrupt s_magic");
	CHECK_RC(ext2fs_open2(path, NULL, EXT2_FLAG_64BITS, 0, 0,
			      unix_io_manager, &fs),
		 EXT2_ET_BAD_MAGIC, "primary open fails on bad magic");
	CHECK_RC(ext2fs_open2(path, NULL, EXT2_FLAG_64BITS, 8193, 1024,
			      unix_io_manager, &fs),
		 0, "backup superblock open succeeds");
	CHECK_RC(ext2fs_close_free(&fs), 0, "close backup open");
	CHECK(pwrite(fd, &orig_magic, 2, 1024 + 56) == 2, "restore s_magic");

	/* Corrupt primary superblock csum -> SB_CSUM_INVALID + IGNORE bypass */
	CHECK(pread(fd, &orig_byte, 1, 1024 + 120) == 1, "pread volname byte");
	flipped = orig_byte ^ 0x5a;
	CHECK(pwrite(fd, &flipped, 1, 1024 + 120) == 1, "flip volname byte");
	CHECK_RC(ext2fs_open2(path, NULL, EXT2_FLAG_64BITS, 0, 0,
			      unix_io_manager, &fs),
		 EXT2_ET_SB_CSUM_INVALID, "open detects bad superblock csum");
	CHECK_RC(ext2fs_open2(path, NULL,
			      EXT2_FLAG_RW | EXT2_FLAG_64BITS |
			      EXT2_FLAG_IGNORE_CSUM_ERRORS,
			      0, 0, unix_io_manager, &fs),
		 0, "open with IGNORE_CSUM_ERRORS succeeds");
	ext2fs_mark_super_dirty(fs);
	CHECK_RC(ext2fs_close_free(&fs), 0, "close flushes repaired csum");
	CHECK_RC(ext2fs_open2(path, NULL, EXT2_FLAG_64BITS, 0, 0,
			      unix_io_manager, &fs),
		 0, "reopen after csum repair succeeds");

	/* Non-image fs rejects get_data_io / set_data_io / rewrite_to_io */
	CHECK_RC(ext2fs_get_data_io(fs, &orig_io), EXT2_ET_NOT_IMAGE_FILE,
		 "get_data_io rejects non-image fs");
	CHECK_RC(ext2fs_set_data_io(fs, fs->io), EXT2_ET_NOT_IMAGE_FILE,
		 "set_data_io rejects non-image fs");
	CHECK_RC(ext2fs_rewrite_to_io(fs, fs->io), EXT2_ET_NOT_IMAGE_FILE,
		 "rewrite_to_io rejects non-image fs");

	/* Build an e2image file, open with EXT2_FLAG_IMAGE_FILE, rewrite_to_io */
	CHECK_RC(ext2fs_read_bitmaps(fs), 0, "read bitmaps for e2image");
	img_fd = make_temp_file(img_path, 0);
	memset(&hdr, 0, sizeof(hdr));
	hdr.magic_number = ext2fs_cpu_to_le32(EXT2_ET_MAGIC_E2IMAGE);
	strcpy(hdr.magic_descriptor, "Ext2 Image 1.0");
	hdr.fs_blocksize = ext2fs_cpu_to_le32(fs->blocksize);
	hdr.offset_super = ext2fs_cpu_to_le32(fs->blocksize);
	CHECK(lseek(img_fd, fs->blocksize, SEEK_SET) == (off_t) fs->blocksize,
	      "seek offset_super");
	CHECK_RC(ext2fs_image_super_write(fs, img_fd, 0), 0,
		 "image_super_write");
	hdr.offset_inodemap = ext2fs_cpu_to_le32((__u32) lseek(img_fd, 0,
							       SEEK_CUR));
	CHECK_RC(ext2fs_image_bitmap_write(fs, img_fd, IMAGER_FLAG_INODEMAP), 0,
		 "image_bitmap_write inodemap");
	hdr.offset_blockmap = ext2fs_cpu_to_le32((__u32) lseek(img_fd, 0,
							       SEEK_CUR));
	CHECK_RC(ext2fs_image_bitmap_write(fs, img_fd, 0), 0,
		 "image_bitmap_write blockmap");
	CHECK(pwrite(img_fd, &hdr, sizeof(hdr), 0) == (ssize_t) sizeof(hdr),
	      "write e2image hdr");
	CHECK_RC(ext2fs_close_free(&fs), 0, "close orig fs");

	CHECK_RC(ext2fs_open2(img_path, NULL,
			      EXT2_FLAG_IMAGE_FILE | EXT2_FLAG_64BITS,
			      0, 0, unix_io_manager, &img_fs),
		 0, "open EXT2_FLAG_IMAGE_FILE");
	CHECK_RC(ext2fs_read_bitmaps(img_fs), 0, "read bitmaps from e2image");
	CHECK_RC(ext2fs_get_data_io(img_fs, &orig_io), 0,
		 "get_data_io on e2image");
	CHECK(orig_io == NULL, "e2image initial data_io is NULL");

	fd2 = make_temp_file(path2, (off_t) 32768 * 1024);
	CHECK_RC(unix_io_manager->open(path2, IO_FLAG_RW, &clone_io), 0,
		 "open clone_io");
	CHECK_RC(ext2fs_set_data_io(img_fs, clone_io), 0,
		 "set_data_io to clone_io");
	CHECK_RC(ext2fs_get_data_io(img_fs, &orig_io), 0,
		 "get_data_io after set");
	CHECK(orig_io == clone_io, "get_data_io returns clone_io");
	CHECK_RC(ext2fs_set_data_io(img_fs, NULL), 0,
		 "reset data_io back to image_io");
	img_fs->flags &= ~EXT2_FLAG_MASTER_SB_ONLY;
	CHECK_RC(ext2fs_rewrite_to_io(img_fs, clone_io), 0, "rewrite_to_io");
	/* rewrite_to_io transferred ownership of clone_io to img_fs */
	CHECK_RC(ext2fs_close_free(&img_fs), 0, "flush & close rewritten fs");

	CHECK_RC(ext2fs_open2(path2, NULL, EXT2_FLAG_64BITS, 0, 0,
			      unix_io_manager, &fs2),
		 0, "open rewritten fs on path2");
	CHECK_RC(ext2fs_check_desc(fs2), 0, "check_desc on rewritten fs");
	CHECK_RC(ext2fs_close_free(&fs2), 0, "close rewritten fs");
	close(img_fd);
	unlink(img_path);
	close(fd2);
	unlink(path2);

	/* Embedded ?offset= option in device name + journal_dev open */
	fd2 = make_temp_file(path2, (off_t) 1024 * 4096);
	memset(&param, 0, sizeof(param));
	ext2fs_blocks_count_set(&param, 1024);
	param.s_log_block_size = 2; /* 4K */
	param.s_log_cluster_size = 2;
	param.s_rev_level = EXT2_DYNAMIC_REV;
	ext2fs_set_feature_journal_dev(&param);
	CHECK_RC(ext2fs_initialize(path2, EXT2_FLAG_RW, &param,
				   unix_io_manager, &fs),
		 0, "init journal_dev");
	CHECK_RC(ext2fs_close_free(&fs), 0, "close journal_dev");
	CHECK(pread(fd2, copy_buf, sizeof(copy_buf), 0) ==
	      (ssize_t) sizeof(copy_buf), "read journal_dev header");
	CHECK(pwrite(fd, copy_buf, sizeof(copy_buf), 65536) ==
	      (ssize_t) sizeof(copy_buf), "write journal_dev at offset 65536");
	close(fd2);
	unlink(path2);

	snprintf(opt_name, sizeof(opt_name), "%s?offset=65536", path);

	CHECK_RC(ext2fs_open2(opt_name, NULL, 0, 0, 0, unix_io_manager, &fs),
		 EXT2_ET_UNSUPP_FEATURE,
		 "open journal_dev without JOURNAL_DEV_OK fails");
	CHECK_RC(ext2fs_open2(opt_name, NULL, EXT2_FLAG_JOURNAL_DEV_OK, 0, 0,
			      unix_io_manager, &fs),
		 0, "open journal_dev with ?offset= and JOURNAL_DEV_OK");
	CHECK_RC(ext2fs_rw_bitmaps(fs, EXT2FS_BITMAPS_BLOCK, 1),
		 EXT2_ET_EXTERNAL_JOURNAL_NOSUPP,
		 "rw_bitmaps rejects journal_dev");
	CHECK_RC(ext2fs_close_free(&fs), 0, "close journal_dev open");

	close(fd);
	unlink(path);
}

static void test_check_desc(void)
{
	struct ext2_super_block	param;
	ext2_filsys		fs = NULL;
	char			path[64];
	int			fd;
	blk64_t			orig_bmap, orig_imap, orig_itable;
	__u16			orig_desc_size;

	fd = make_temp_file(path, (off_t) 32768 * 1024);

	/* Standard FS + corruptions */
	memset(&param, 0, sizeof(param));
	ext2fs_blocks_count_set(&param, 16384);
	param.s_inodes_count = 128;
	param.s_rev_level = EXT2_DYNAMIC_REV;
	param.s_inode_size = 256;
	param.s_desc_size = EXT2_MIN_DESC_SIZE_64BIT;
	ext2fs_set_feature_sparse_super(&param);
	ext2fs_set_feature_64bit(&param);
	CHECK_RC(ext2fs_initialize(path, EXT2_FLAG_RW | EXT2_FLAG_64BITS,
				   &param, unix_io_manager, &fs),
		 0, "init std fs for check_desc");
	CHECK_RC(ext2fs_allocate_tables(fs), 0, "alloc_tables std");
	CHECK_RC(ext2fs_check_desc(fs), 0, "check_desc valid std fs");

	orig_desc_size = fs->super->s_desc_size;
	fs->super->s_desc_size = 48; /* non-power-of-2 */
	CHECK_RC(ext2fs_check_desc(fs), EXT2_ET_BAD_DESC_SIZE,
		 "check_desc bad non-pow2 s_desc_size");
	fs->super->s_desc_size = orig_desc_size;

	orig_bmap = ext2fs_block_bitmap_loc(fs, 0);
	ext2fs_block_bitmap_loc_set(fs, 0, 1); /* collides with superblock */
	CHECK_RC(ext2fs_check_desc(fs), EXT2_ET_GDESC_BAD_BLOCK_MAP,
		 "check_desc bmap at superblock");
	ext2fs_block_bitmap_loc_set(fs, 0, 99999);
	CHECK_RC(ext2fs_check_desc(fs), EXT2_ET_GDESC_BAD_BLOCK_MAP,
		 "check_desc bmap OOB");
	ext2fs_block_bitmap_loc_set(fs, 0, orig_bmap);

	orig_imap = ext2fs_inode_bitmap_loc(fs, 0);
	ext2fs_inode_bitmap_loc_set(fs, 0, orig_bmap);
	CHECK_RC(ext2fs_check_desc(fs), EXT2_ET_GDESC_BAD_INODE_MAP,
		 "check_desc imap == bmap");
	ext2fs_inode_bitmap_loc_set(fs, 0, 99999);
	CHECK_RC(ext2fs_check_desc(fs), EXT2_ET_GDESC_BAD_INODE_MAP,
		 "check_desc imap OOB");
	ext2fs_inode_bitmap_loc_set(fs, 0, orig_imap);

	orig_itable = ext2fs_inode_table_loc(fs, 0);
	ext2fs_inode_table_loc_set(fs, 0, orig_imap);
	CHECK_RC(ext2fs_check_desc(fs), EXT2_ET_GDESC_BAD_INODE_TABLE,
		 "check_desc itable overlaps imap");
	ext2fs_inode_table_loc_set(fs, 0, 16380);
	CHECK_RC(ext2fs_check_desc(fs), EXT2_ET_GDESC_BAD_INODE_TABLE,
		 "check_desc itable extends past group");
	ext2fs_inode_table_loc_set(fs, 0, orig_itable);

	CHECK_RC(ext2fs_check_desc(fs), 0, "check_desc restored valid");
	ext2fs_free(fs);
	fs = NULL;

	/* flex_bg allows cross-group metadata */
	memset(&param, 0, sizeof(param));
	ext2fs_blocks_count_set(&param, 32768);
	param.s_inodes_count = 256;
	param.s_rev_level = EXT2_DYNAMIC_REV;
	param.s_inode_size = 256;
	param.s_desc_size = EXT2_MIN_DESC_SIZE_64BIT;
	param.s_log_groups_per_flex = 2;
	ext2fs_set_feature_sparse_super(&param);
	ext2fs_set_feature_64bit(&param);
	ext2fs_set_feature_flex_bg(&param);
	CHECK_RC(ext2fs_initialize(path, EXT2_FLAG_RW | EXT2_FLAG_64BITS,
				   &param, unix_io_manager, &fs),
		 0, "init flex_bg fs");
	CHECK_RC(ext2fs_allocate_tables(fs), 0, "alloc_tables flex_bg");
	CHECK(ext2fs_block_bitmap_loc(fs, 1) < 8192,
	      "flex_bg placed group 1 bmap in group 0");
	CHECK_RC(ext2fs_check_desc(fs), 0, "check_desc valid flex_bg fs");
	ext2fs_free(fs);

	close(fd);
	unlink(path);
}

static void test_rw_bitmaps_and_stats(void)
{
	struct ext2_super_block	param;
	ext2_filsys		fs = NULL;
	char			path[64], *buf = NULL;
	int			fd;
	blk64_t			expect_free_blks, bmap_blk, imap_blk;
	ext2_ino_t		expect_free_inos;
	__u8			byte;

	fd = make_temp_file(path, (off_t) 65536 * 4096);

	/* 4-group non-flex_bg 1K FS with 64 inodes/group and metadata_csum */
	memset(&param, 0, sizeof(param));
	ext2fs_blocks_count_set(&param, 32768);
	param.s_inodes_count = 256; /* 64 per group */
	param.s_rev_level = EXT2_DYNAMIC_REV;
	param.s_inode_size = 256;
	param.s_desc_size = EXT2_MIN_DESC_SIZE_64BIT;
	param.s_checksum_type = EXT2_CRC32C_CHKSUM;
	memset(param.s_uuid, 0x52, sizeof(param.s_uuid));
	ext2fs_set_feature_sparse_super(&param);
	ext2fs_set_feature_extents(&param);
	ext2fs_set_feature_64bit(&param);
	ext2fs_set_feature_metadata_csum(&param);

	CHECK_RC(ext2fs_initialize(path, EXT2_FLAG_RW | EXT2_FLAG_64BITS,
				   &param, unix_io_manager, &fs),
		 0, "init 4-group fs for rw_bitmaps");
	memset(fs->super->s_uuid, 0x52, sizeof(fs->super->s_uuid));
	fs->super->s_checksum_type = EXT2_CRC32C_CHKSUM;
	ext2fs_init_csum_seed(fs);
	CHECK_RC(ext2fs_allocate_tables(fs), 0, "alloc_tables 4-group");

	/* Mark blocks and interior inodes (< s_inodes_per_group per group) */
	ext2fs_mark_block_bitmap2(fs->block_map, 500);
	ext2fs_mark_block_bitmap2(fs->block_map, 9000);
	ext2fs_mark_inode_bitmap2(fs->inode_map, 15); /* grp 0, offset 15 */
	ext2fs_mark_inode_bitmap2(fs->inode_map, 80); /* grp 1, offset 16 */

	CHECK_RC(ext2fs_calculate_summary_stats(fs, 0), 0,
		 "calculate_summary_stats full");
	expect_free_blks = ext2fs_free_blocks_count(fs->super);
	expect_free_inos = fs->super->s_free_inodes_count;
	CHECK(ext2fs_bg_itable_unused(fs, 0) == 64 - 15,
	      "group 0 itable_unused == 49");
	CHECK(ext2fs_bg_itable_unused(fs, 1) == 64 - 16,
	      "group 1 itable_unused == 48");
	CHECK(ext2fs_bg_free_inodes_count(fs, 2) == 64 &&
	      ext2fs_bg_flags_test(fs, 2, EXT2_BG_INODE_UNINIT),
	      "group 2 completely unused & INODE_UNINIT");

	/* Verify super_only = 1 recalculation */
	ext2fs_free_blocks_count_set(fs->super, 1);
	fs->super->s_free_inodes_count = 1;
	CHECK_RC(ext2fs_calculate_summary_stats(fs, 1), 0,
		 "calculate_summary_stats super_only");
	CHECK(ext2fs_free_blocks_count(fs->super) == expect_free_blks &&
	      fs->super->s_free_inodes_count == expect_free_inos,
	      "super_only restores superblock free counts");

	/*
	 * Allocating the final inode in group 0 (ino == s_inodes_per_group)
	 * must yield bg_itable_unused == 0, not s_inodes_per_group.
	 */
	ext2fs_mark_inode_bitmap2(fs->inode_map, 64);
	CHECK_RC(ext2fs_calculate_summary_stats(fs, 0), 0,
		 "calculate_summary_stats with last group inode allocated");
	CHECK(ext2fs_bg_itable_unused(fs, 0) == 0,
	      "group 0 last inode allocated -> itable_unused == 0");
	ext2fs_unmark_inode_bitmap2(fs->inode_map, 64);
	CHECK_RC(ext2fs_calculate_summary_stats(fs, 0), 0,
		 "restore summary stats after unmarking ino 64");

	/*
	 * Verify ext2fs_calculate_summary_stats() on a bigalloc filesystem
	 * counts bg_free_blocks_count in clusters and s_free_blocks_count
	 * in blocks without multiplying by cluster_ratio twice.
	 */
	{
		struct ext2_super_block ba_param;
		ext2_filsys ba_fs = NULL;
		blk64_t used_clusters = 0, expect_clusters;

		memset(&ba_param, 0, sizeof(ba_param));
		ext2fs_blocks_count_set(&ba_param, 8192);
		ba_param.s_inodes_count = 128;
		ba_param.s_rev_level = EXT2_DYNAMIC_REV;
		ba_param.s_inode_size = 256;
		ba_param.s_log_block_size = 0;   /* 1 KiB blocks */
		ba_param.s_log_cluster_size = 2; /* 4 KiB clusters (ratio=4) */
		ba_param.s_desc_size = EXT2_MIN_DESC_SIZE_64BIT;
		ext2fs_set_feature_sparse_super(&ba_param);
		ext2fs_set_feature_extents(&ba_param);
		ext2fs_set_feature_64bit(&ba_param);
		ext2fs_set_feature_bigalloc(&ba_param);
		ext2fs_set_feature_metadata_csum(&ba_param);
		CHECK_RC(ext2fs_initialize("bigalloc_mem",
					   EXT2_FLAG_RW | EXT2_FLAG_64BITS,
					   &ba_param, test_io_manager, &ba_fs),
			 0, "init bigalloc fs");
		CHECK_RC(ext2fs_allocate_tables(ba_fs), 0,
			 "alloc_tables bigalloc fs");
		CHECK_RC(ext2fs_convert_subcluster_bitmap(ba_fs,
							  &ba_fs->block_map),
			 0, "convert subcluster bitmap");
		CHECK_RC(ext2fs_count_used_clusters(ba_fs,
				ba_fs->super->s_first_data_block,
				ext2fs_blocks_count(ba_fs->super) - 1,
				&used_clusters),
			 0, "count_used_clusters bigalloc");
		expect_clusters = (8192 / 4) - used_clusters;
		ext2fs_free_blocks_count_set(ba_fs->super, 1);
		ext2fs_bg_free_blocks_count_set(ba_fs, 0, 1);
		CHECK_RC(ext2fs_calculate_summary_stats(ba_fs, 0), 0,
			 "calculate_summary_stats bigalloc");
		CHECK(ext2fs_bg_free_blocks_count(ba_fs, 0) == expect_clusters,
		      "bigalloc bg_free_blocks_count counted in clusters");
		CHECK(ext2fs_free_blocks_count(ba_fs->super) ==
		      expect_clusters * 4,
		      "bigalloc s_free_blocks_count matches exact blocks");
		ext2fs_free(ba_fs);
	}

	/* Write bitmaps via ext2fs_rw_bitmaps(WRITE) + flush */
	CHECK_RC(ext2fs_rw_bitmaps(fs, 0x4000, 1), EXT2_ET_INVALID_ARGUMENT,
		 "rw_bitmaps rejects invalid flag");
	ext2fs_mark_bb_dirty(fs);
	ext2fs_mark_ib_dirty(fs);
	CHECK_RC(ext2fs_rw_bitmaps(fs,
				   EXT2FS_BITMAPS_WRITE |
				   EXT2FS_BITMAPS_BLOCK |
				   EXT2FS_BITMAPS_INODE, 1),
		 0, "rw_bitmaps explicit write");
	bmap_blk = ext2fs_block_bitmap_loc(fs, 0);
	imap_blk = ext2fs_inode_bitmap_loc(fs, 0);
	CHECK_RC(ext2fs_close_free(&fs), 0, "close 4-group fs");

	/* Single-threaded read_bitmaps verification */
	CHECK_RC(ext2fs_open2(path, NULL, EXT2_FLAG_64BITS, 0, 0,
			      unix_io_manager, &fs),
		 0, "reopen single-threaded");
	CHECK_RC(ext2fs_read_bitmaps(fs), 0, "read_bitmaps single-threaded");
	CHECK(ext2fs_test_block_bitmap2(fs->block_map, 500) &&
	      ext2fs_test_block_bitmap2(fs->block_map, 9000) &&
	      !ext2fs_test_block_bitmap2(fs->block_map, 501) &&
	      !ext2fs_test_block_bitmap2(fs->block_map, 20000),
	      "block bitmap round-trip (incl UNINIT group)");
	CHECK(ext2fs_test_inode_bitmap2(fs->inode_map, 15) &&
	      ext2fs_test_inode_bitmap2(fs->inode_map, 80) &&
	      !ext2fs_test_inode_bitmap2(fs->inode_map, 16) &&
	      !ext2fs_test_inode_bitmap2(fs->inode_map, 150),
	      "inode bitmap round-trip (incl UNINIT group)");
	CHECK_RC(ext2fs_close_free(&fs), 0, "close single-threaded");

	/* Multi-threaded rw_bitmaps(num_threads = 2) with EXT2_FLAG_THREADS */
	CHECK_RC(ext2fs_open2(path, NULL,
			      EXT2_FLAG_64BITS | EXT2_FLAG_THREADS,
			      0, 0, unix_io_manager, &fs),
		 0, "reopen with EXT2_FLAG_THREADS");
	CHECK_RC(ext2fs_rw_bitmaps(fs,
				   EXT2FS_BITMAPS_BLOCK | EXT2FS_BITMAPS_INODE,
				   2),
		 0, "rw_bitmaps 2 threads");
	CHECK(ext2fs_test_block_bitmap2(fs->block_map, 500) &&
	      ext2fs_test_block_bitmap2(fs->block_map, 9000) &&
	      ext2fs_test_inode_bitmap2(fs->inode_map, 15) &&
	      ext2fs_test_inode_bitmap2(fs->inode_map, 80) &&
	      !ext2fs_test_inode_bitmap2(fs->inode_map, 200),
	      "multi-threaded bitmaps match");

	/*
	 * Exercise num_threads == group_desc_count (4) and uneven 3-thread
	 * partitioning alongside an out-of-range s_log_groups_per_flex value.
	 */
	fs->super->s_log_groups_per_flex = 40;
	CHECK_RC(ext2fs_rw_bitmaps(fs,
				   EXT2FS_BITMAPS_BLOCK | EXT2FS_BITMAPS_INODE,
				   4),
		 0, "rw_bitmaps 4 threads on 4 groups with garbage log_flex");
	CHECK(ext2fs_test_block_bitmap2(fs->block_map, 500) &&
	      ext2fs_test_block_bitmap2(fs->block_map, 9000) &&
	      ext2fs_test_inode_bitmap2(fs->inode_map, 15) &&
	      ext2fs_test_inode_bitmap2(fs->inode_map, 80) &&
	      !ext2fs_test_block_bitmap2(fs->block_map, 25000),
	      "4-thread full-group slice bitmaps match");
	CHECK_RC(ext2fs_rw_bitmaps(fs,
				   EXT2FS_BITMAPS_BLOCK | EXT2FS_BITMAPS_INODE,
				   3),
		 0, "rw_bitmaps 3 threads on 4 groups");
	CHECK(ext2fs_test_block_bitmap2(fs->block_map, 500) &&
	      ext2fs_test_block_bitmap2(fs->block_map, 9000) &&
	      ext2fs_test_inode_bitmap2(fs->inode_map, 15) &&
	      ext2fs_test_inode_bitmap2(fs->inode_map, 80),
	      "3-thread uneven slice bitmaps match");
	ext2fs_set_feature_flex_bg(fs->super);
	CHECK_RC(ext2fs_rw_bitmaps(fs,
				   EXT2FS_BITMAPS_BLOCK | EXT2FS_BITMAPS_INODE,
				   4),
		 0, "rw_bitmaps falls back cleanly on flex_bg log_flex > 30");
	CHECK(ext2fs_test_block_bitmap2(fs->block_map, 500) &&
	      ext2fs_test_inode_bitmap2(fs->inode_map, 80),
	      "flex_bg fallback bitmaps match");
	CHECK_RC(ext2fs_close_free(&fs), 0, "close multi-threaded");

	/* Non-0xff inode bitmap tail padding with valid checksum -> TAIL flag */
	CHECK_RC(ext2fs_open2(path, NULL, EXT2_FLAG_RW | EXT2_FLAG_64BITS,
			      0, 0, unix_io_manager, &fs),
		 0, "open rw for tail padding test");
	CHECK_RC(ext2fs_get_mem(fs->blocksize, &buf), 0, "alloc buf");
	CHECK_RC(io_channel_read_blk64(fs->io, imap_blk, 1, buf), 0,
		 "read group 0 imap block");
	buf[100] = 0x00; /* byte 100 is well past 64 inodes (8 bytes) */
	CHECK_RC(ext2fs_inode_bitmap_csum_set(fs, 0, buf,
					      EXT2_INODES_PER_GROUP(fs->super) / 8),
		 0, "update imap csum");
	ext2fs_group_desc_csum_set(fs, 0);
	CHECK_RC(io_channel_write_blk64(fs->io, imap_blk, 1, buf), 0,
		 "write bad-tail imap block");
	ext2fs_mark_super_dirty(fs);
	CHECK_RC(ext2fs_close_free(&fs), 0, "flush gdt for tail test");

	CHECK_RC(ext2fs_open2(path, NULL, EXT2_FLAG_64BITS, 0, 0,
			      unix_io_manager, &fs),
		 0, "open to verify IBITMAP_TAIL_PROBLEM");
	CHECK_RC(ext2fs_read_inode_bitmap(fs), 0,
		 "read_inode_bitmap with bad tail");
	CHECK((fs->flags & EXT2_FLAG_IBITMAP_TAIL_PROBLEM) != 0,
	      "EXT2_FLAG_IBITMAP_TAIL_PROBLEM set");
	CHECK_RC(ext2fs_close_free(&fs), 0, "close tail test");
	ext2fs_free_mem(&buf);

	/* Corrupt block & inode bitmap checksums on disk */
	CHECK(pread(fd, &byte, 1, (off_t) bmap_blk * 1024 + 60) == 1,
	      "pread bmap byte");
	byte ^= 0x01;
	CHECK(pwrite(fd, &byte, 1, (off_t) bmap_blk * 1024 + 60) == 1,
	      "corrupt bmap byte");
	CHECK(pread(fd, &byte, 1, (off_t) imap_blk * 1024 + 2) == 1,
	      "pread imap byte");
	byte ^= 0x01;
	CHECK(pwrite(fd, &byte, 1, (off_t) imap_blk * 1024 + 2) == 1,
	      "corrupt imap byte");

	CHECK_RC(ext2fs_open2(path, NULL, EXT2_FLAG_64BITS, 0, 0,
			      unix_io_manager, &fs),
		 0, "open with corrupt bitmap csums");
	CHECK_RC(ext2fs_read_block_bitmap(fs),
		 EXT2_ET_BLOCK_BITMAP_CSUM_INVALID,
		 "detect bad block bitmap csum");
	CHECK_RC(ext2fs_read_inode_bitmap(fs),
		 EXT2_ET_INODE_BITMAP_CSUM_INVALID,
		 "detect bad inode bitmap csum");
	CHECK_RC(ext2fs_close_free(&fs), 0, "close corrupt bitmap fs");

	CHECK_RC(ext2fs_open2(path, NULL,
			      EXT2_FLAG_64BITS | EXT2_FLAG_IGNORE_CSUM_ERRORS,
			      0, 0, unix_io_manager, &fs),
		 0, "open corrupt bitmaps with IGNORE_CSUM_ERRORS");
	CHECK_RC(ext2fs_read_bitmaps(fs), 0,
		 "read_bitmaps succeeds with IGNORE_CSUM_ERRORS");
	CHECK_RC(ext2fs_close_free(&fs), 0, "close ignore csum fs");

	/* bigalloc cluster bitmap R/W + legacy bitmap rejection */
	ext2fs_zero_blocks2(NULL, 0, 0, NULL, NULL);
	memset(&param, 0, sizeof(param));
	ext2fs_blocks_count_set(&param, 65536);
	param.s_inodes_count = 256;
	param.s_log_block_size = 2; /* 4K */
	param.s_log_cluster_size = 4; /* 16K (4 blocks/cluster) */
	param.s_rev_level = EXT2_DYNAMIC_REV;
	param.s_inode_size = 256;
	param.s_desc_size = EXT2_MIN_DESC_SIZE_64BIT;
	ext2fs_set_feature_sparse_super(&param);
	ext2fs_set_feature_extents(&param);
	ext2fs_set_feature_64bit(&param);
	ext2fs_set_feature_bigalloc(&param);
	CHECK_RC(ext2fs_initialize(path, EXT2_FLAG_RW | EXT2_FLAG_64BITS,
				   &param, unix_io_manager, &fs),
		 0, "init bigalloc fs");
	CHECK_RC(ext2fs_allocate_tables(fs), 0, "alloc_tables bigalloc");
	CHECK_RC(ext2fs_convert_subcluster_bitmap(fs, &fs->block_map), 0,
		 "convert_subcluster_bitmap");
	ext2fs_mark_block_bitmap2(fs->block_map, 400);
	ext2fs_mark_bb_dirty(fs);
	CHECK_RC(ext2fs_close_free(&fs), 0, "flush & close bigalloc");

	CHECK_RC(ext2fs_open2(path, NULL, 0, 0, 0, unix_io_manager, &fs),
		 EXT2_ET_CANT_USE_LEGACY_BITMAPS,
		 "bigalloc open without EXT2_FLAG_64BITS fails");
	CHECK_RC(ext2fs_open2(path, NULL, EXT2_FLAG_64BITS, 0, 0,
			      unix_io_manager, &fs),
		 0, "bigalloc open with EXT2_FLAG_64BITS");
	CHECK_RC(ext2fs_read_bitmaps(fs), 0, "bigalloc read_bitmaps");
	CHECK(ext2fs_test_block_bitmap2(fs->block_map, 400) &&
	      ext2fs_test_block_bitmap2(fs->block_map, 403) &&
	      !ext2fs_test_block_bitmap2(fs->block_map, 404),
	      "bigalloc cluster 400..403 marked");
	CHECK_RC(ext2fs_close_free(&fs), 0, "close bigalloc");
	ext2fs_zero_blocks2(NULL, 0, 0, NULL, NULL);

	close(fd);
	unlink(path);
}

static void test_dup_handle_and_resize_inode(void)
{
	struct ext2_super_block	param;
	ext2_filsys		src = NULL, dest = NULL, rfs = NULL;
	struct ext2_inode	inode;
	char			path[64], *dindir_buf = NULL;
	int			fd;
	__u32			*entries;

	fd = make_temp_file(path, (off_t) 32768 * 1024);

	/* 1. ext2fs_dup_handle with bitmaps, badblocks, dblist, and icache */
	memset(&param, 0, sizeof(param));
	ext2fs_blocks_count_set(&param, 16384);
	param.s_inodes_count = 128;
	param.s_rev_level = EXT2_DYNAMIC_REV;
	param.s_inode_size = 256;
	ext2fs_set_feature_sparse_super(&param);
	CHECK_RC(ext2fs_initialize(path, EXT2_FLAG_RW | EXT2_FLAG_64BITS,
				   &param, unix_io_manager, &src),
		 0, "init src for dup_handle");
	CHECK_RC(ext2fs_allocate_tables(src), 0, "alloc_tables src");
	ext2fs_mark_block_bitmap2(src->block_map, 600);
	ext2fs_mark_bb_dirty(src);
	ext2fs_mark_ib_dirty(src);
	CHECK_RC(ext2fs_close_free(&src), 0, "flush & close initial src");

	/* Open via ext2fs_open2 so orig_super is populated */
	CHECK_RC(ext2fs_open2(path, NULL, EXT2_FLAG_RW | EXT2_FLAG_64BITS,
			      0, 0, unix_io_manager, &src),
		 0, "reopen src for dup_handle");
	CHECK_RC(ext2fs_read_bitmaps(src), 0, "read_bitmaps src");

	CHECK_RC(ext2fs_badblocks_list_create(&src->badblocks, 4), 0,
		 "create badblocks");
	CHECK_RC(ext2fs_badblocks_list_add(src->badblocks, 777), 0,
		 "add badblock 777");
	CHECK_RC(ext2fs_init_dblist(src, &src->dblist), 0, "init dblist");
	CHECK_RC(ext2fs_add_dir_block2(src->dblist, EXT2_ROOT_INO, 200, 0), 0,
		 "add dir block");
	CHECK_RC(ext2fs_create_inode_cache(src, 4), 0, "create icache");

	CHECK_RC(ext2fs_dup_handle(src, &dest), 0, "ext2fs_dup_handle");
	CHECK(dest != NULL && dest != src, "dest distinct from src");
	CHECK(ext2fs_test_block_bitmap2(dest->block_map, 600) != 0,
	      "dest inherited marked block 600");
	CHECK(dest->badblocks != NULL &&
	      ext2fs_badblocks_list_test(dest->badblocks, 777) == 1,
	      "dest copied badblocks");
	CHECK(dest->dblist != NULL &&
	      ext2fs_dblist_count2(dest->dblist) == 1,
	      "dest copied dblist");

	/* Mutate src; dest copy must remain independent, and closing dest
	 * must leave refcounted io_channel & icache usable on src */
	ext2fs_mark_block_bitmap2(src->block_map, 601);
	CHECK(!ext2fs_test_block_bitmap2(dest->block_map, 601),
	      "dest block_map independent of src mutation");
	CHECK_RC(ext2fs_close_free(&dest), 0, "close_free dest");
	CHECK_RC(ext2fs_read_inode(src, EXT2_ROOT_INO, &inode), 0,
		 "src io_channel & icache still usable after closing dest");
	CHECK_RC(ext2fs_close_free(&src), 0, "close_free src");

	/* 2. ext2fs_create_resize_inode: creation, idempotency, corruption */
	memset(&param, 0, sizeof(param));
	ext2fs_blocks_count_set(&param, 32768); /* 4 groups @ 1K */
	param.s_inodes_count = 256;
	param.s_rev_level = EXT2_DYNAMIC_REV;
	param.s_inode_size = 128;
	param.s_reserved_gdt_blocks = 4;
	ext2fs_set_feature_sparse_super(&param);
	ext2fs_set_feature_resize_inode(&param);
	CHECK_RC(ext2fs_initialize(path, EXT2_FLAG_RW | EXT2_FLAG_64BITS,
				   &param, unix_io_manager, &rfs),
		 0, "init fs with resize_inode");
	CHECK_RC(ext2fs_allocate_tables(rfs), 0, "alloc_tables resize_inode");
	CHECK_RC(ext2fs_create_resize_inode(rfs), 0, "create_resize_inode");

	CHECK_RC(ext2fs_read_inode(rfs, EXT2_RESIZE_INO, &inode), 0,
		 "read EXT2_RESIZE_INO");
	CHECK(inode.i_block[EXT2_DIND_BLOCK] != 0,
	      "resize inode DIND block allocated");
	CHECK(inode.i_links_count == 1, "resize inode links_count == 1");

	/* Re-running on valid resize inode is idempotent */
	CHECK_RC(ext2fs_create_resize_inode(rfs), 0,
		 "create_resize_inode idempotent");

	/* Corrupt a reserved GDT entry in DIND block -> RESIZE_INODE_CORRUPT */
	CHECK_RC(ext2fs_get_mem(rfs->blocksize, &dindir_buf), 0,
		 "alloc dindir_buf");
	CHECK_RC(ext2fs_read_ind_block(rfs, inode.i_block[EXT2_DIND_BLOCK],
				       dindir_buf),
		 0, "read resize DIND block");
	entries = (__u32 *) dindir_buf;
	entries[1 % (rfs->blocksize / 4)] = 99999; /* wrong gdt_blk */
	CHECK_RC(ext2fs_write_ind_block(rfs, inode.i_block[EXT2_DIND_BLOCK],
					dindir_buf),
		 0, "write corrupted resize DIND block");
	CHECK_RC(ext2fs_create_resize_inode(rfs), EXT2_ET_RESIZE_INODE_CORRUPT,
		 "create_resize_inode detects corrupt DIND entry");

	ext2fs_free_mem(&dindir_buf);
	CHECK_RC(ext2fs_close_free(&rfs), 0, "close resize_inode fs");

	close(fd);
	unlink(path);
}

int main(int argc EXT2FS_ATTR((unused)), char **argv EXT2FS_ATTR((unused)))
{
	add_error_table(&et_ext2_error_table);

	test_initialize_and_backups();
	test_open_flush_close();
	test_check_desc();
	test_rw_bitmaps_and_stats();
	test_dup_handle_and_resize_inode();

	remove_error_table(&et_ext2_error_table);

	if (fail_count) {
		fprintf(stderr, "tst_filsys: %d/%d checks FAILED\n",
			fail_count, test_count);
		return 1;
	}
	printf("tst_filsys: all %d checks passed\n", test_count);
	return 0;
}
