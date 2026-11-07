/*
 * tst_mkjournal.c --- unit tests for mkjournal.c
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
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#ifdef HAVE_DIRENT_H
#include <dirent.h>
#endif

#include "ext2_fs.h"
#include "ext2fs.h"
#include "ext2fsP.h"
#include "kernel-jbd.h"

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

	strcpy(path_out, "/tmp/tst_mkjournal.XXXXXX");
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

static ext2_filsys create_fs(const char *path, blk64_t blocks,
			     int log_blksize, int use_extents,
			     int meta_csum, int flex_log, int journal_dev)
{
	struct ext2_super_block	param;
	ext2_filsys		fs = NULL;
	char			*zbuf = NULL;
	dgrp_t			g;

	ext2fs_zero_blocks2(NULL, 0, 0, NULL, NULL);

	memset(&param, 0, sizeof(param));
	ext2fs_blocks_count_set(&param, blocks);
	param.s_log_block_size = log_blksize;
	param.s_log_cluster_size = log_blksize;
	param.s_rev_level = EXT2_DYNAMIC_REV;
	if (journal_dev) {
		ext2fs_set_feature_journal_dev(&param);
	} else {
		param.s_inodes_count = 128;
		param.s_inode_size = 256;
		ext2fs_set_feature_sparse_super(&param);
		if (use_extents)
			ext2fs_set_feature_extents(&param);
		if (flex_log > 0) {
			param.s_log_groups_per_flex = flex_log;
			ext2fs_set_feature_flex_bg(&param);
		}
		if (meta_csum) {
			param.s_desc_size = EXT2_MIN_DESC_SIZE_64BIT;
			ext2fs_set_feature_64bit(&param);
			ext2fs_set_feature_metadata_csum(&param);
		}
	}

	if (ext2fs_initialize(path, EXT2_FLAG_RW | EXT2_FLAG_64BITS,
			      &param, unix_io_manager, &fs))
		exit(1);
	memset(fs->super->s_uuid, 0x73, sizeof(fs->super->s_uuid));
	if (meta_csum) {
		fs->super->s_checksum_type = EXT2_CRC32C_CHKSUM;
		ext2fs_init_csum_seed(fs);
	}
	fs->now = 1700000000;
	if (!journal_dev) {
		if (ext2fs_allocate_tables(fs))
			exit(1);
		if (ext2fs_get_memzero((size_t) fs->blocksize *
				       fs->inode_blocks_per_group, &zbuf))
			exit(1);
		for (g = 0; g < fs->group_desc_count; g++) {
			if (io_channel_write_blk64(fs->io,
					ext2fs_inode_table_loc(fs, g),
					fs->inode_blocks_per_group, zbuf))
				exit(1);
		}
		ext2fs_free_mem(&zbuf);
		if (meta_csum && ext2fs_set_gdt_csum(fs))
			exit(1);
	}
	return fs;
}

static void test_sizing_and_superblock(void)
{
	ext2_filsys			fs;
	struct ext2fs_journal_params	jparams;
	journal_superblock_t		*jsb;
	char				path[64], *raw_jsb = NULL;
	int				fd;

	/* 1. ext2fs_default_journal_size across all tiers */
	CHECK(ext2fs_default_journal_size(1000) == -1, "<2048 -> -1");
	CHECK(ext2fs_default_journal_size(4096) == 1024, "<32K -> 1024");
	CHECK(ext2fs_default_journal_size(65536) == 4096, "<256K -> 4096");
	CHECK(ext2fs_default_journal_size(300ULL * 1024) == 8192,
	      "<512K -> 8192");
	CHECK(ext2fs_default_journal_size(1024ULL * 1024) == 16384,
	      "<4M -> 16384");
	CHECK(ext2fs_default_journal_size(5000ULL * 1024) == 32768,
	      "<8M -> 32768");
	CHECK(ext2fs_default_journal_size(10000ULL * 1024) == 65536,
	      "<16M -> 65536");
	CHECK(ext2fs_default_journal_size(20000ULL * 1024) == 131072,
	      "<32M -> 131072");
	CHECK(ext2fs_default_journal_size(40000ULL * 1024) == 262144,
	      ">=32M -> 262144");

	/* 2. ext2fs_journal_sb_start */
	CHECK(ext2fs_journal_sb_start(1024) == 2, "sb_start(1024) == 2");
	CHECK(ext2fs_journal_sb_start(2048) == 1, "sb_start(2048) == 1");
	CHECK(ext2fs_journal_sb_start(4096) == 1, "sb_start(4096) == 1");

	/* 3. ext2fs_get_journal_params & ext2fs_create_journal_superblock2 */
	fd = make_temp_file(path, (off_t) 16384 * 4096);
	fs = create_fs(path, 8192, 0, 1, 1, 0, 0);

	ext2fs_blocks_count_set(fs->super, 1500);
	CHECK_RC(ext2fs_get_journal_params(&jparams, fs),
		 EXT2_ET_JOURNAL_TOO_SMALL,
		 "get_journal_params too small regular fs");

	ext2fs_blocks_count_set(fs->super, 8192);
	CHECK_RC(ext2fs_get_journal_params(&jparams, fs), 0,
		 "get_journal_params 8192 regular");
	CHECK(jparams.num_journal_blocks == 1024 &&
	      jparams.num_fc_blocks == 0,
	      "regular params without fast_commit");

	ext2fs_set_feature_fast_commit(fs->super);
	CHECK_RC(ext2fs_get_journal_params(&jparams, fs), 0,
		 "get_journal_params with fast_commit");
	CHECK(jparams.num_journal_blocks == 1024 &&
	      jparams.num_fc_blocks == 1024 / EXT2_JOURNAL_TO_FC_BLKS_RATIO,
	      "regular params with fast_commit (1024 + 16)");

	/* Too small superblock rejected */
	CHECK_RC(ext2fs_create_journal_superblock(fs, 512, 0, &raw_jsb),
		 EXT2_ET_JOURNAL_TOO_SMALL,
		 "create_journal_superblock < 1024 rejected");

	/* V2 superblock with fast_commit blocks */
	CHECK_RC(ext2fs_create_journal_superblock2(fs, &jparams, 0, &raw_jsb),
		 0, "create_journal_superblock2 V2");
	jsb = (journal_superblock_t *) raw_jsb;
	CHECK(ext2fs_be32_to_cpu(jsb->s_header.h_magic) == JBD2_MAGIC_NUMBER,
	      "V2 jsb magic");
	CHECK(ext2fs_be32_to_cpu(jsb->s_header.h_blocktype) ==
	      JBD2_SUPERBLOCK_V2, "V2 jsb blocktype");
	CHECK(ext2fs_be32_to_cpu(jsb->s_blocksize) == 1024, "V2 jsb blocksize");
	CHECK(ext2fs_be32_to_cpu(jsb->s_maxlen) == 1040, "V2 jsb maxlen 1040");
	CHECK(ext2fs_be32_to_cpu(jsb->s_num_fc_blks) == 16,
	      "V2 jsb num_fc_blks 16");
	CHECK(ext2fs_be32_to_cpu(jsb->s_nr_users) == 1 &&
	      ext2fs_be32_to_cpu(jsb->s_first) == 1 &&
	      ext2fs_be32_to_cpu(jsb->s_sequence) == 1,
	      "V2 jsb nr_users/first/seq == 1");
	CHECK(memcmp(jsb->s_uuid, fs->super->s_uuid, 16) == 0,
	      "V2 jsb uuid matches fs");
	ext2fs_free_mem(&raw_jsb);

	/* V1 superblock via legacy wrapper */
	CHECK_RC(ext2fs_create_journal_superblock(fs, 1024,
						  EXT2_MKJOURNAL_V1_SUPER,
						  &raw_jsb),
		 0, "create_journal_superblock V1");
	jsb = (journal_superblock_t *) raw_jsb;
	CHECK(ext2fs_be32_to_cpu(jsb->s_header.h_blocktype) ==
	      JBD2_SUPERBLOCK_V1, "V1 jsb blocktype");
	ext2fs_free_mem(&raw_jsb);
	ext2fs_free(fs);

	/* 4. External journal_dev params and superblock (1K and 4K) */
	fs = create_fs(path, 2048, 0, 0, 0, 0, 1); /* 1K journal_dev */
	ext2fs_blocks_count_set(fs->super, 512);
	CHECK_RC(ext2fs_get_journal_params(&jparams, fs),
		 EXT2_ET_JOURNAL_TOO_SMALL,
		 "journal_dev < 1024 rejected");

	ext2fs_blocks_count_set(fs->super, 2048);
	CHECK_RC(ext2fs_get_journal_params(&jparams, fs), 0,
		 "journal_dev 2048 params");
	CHECK(jparams.num_journal_blocks == 2048 && jparams.num_fc_blocks == 0,
	      "journal_dev without fc uses all blocks");

	ext2fs_set_feature_fast_commit(fs->super);
	CHECK_RC(ext2fs_get_journal_params(&jparams, fs), 0,
		 "journal_dev 2048 fc params");
	CHECK(jparams.num_journal_blocks + jparams.num_fc_blocks == 2048 &&
	      jparams.num_fc_blocks > 0,
	      "journal_dev with fc splits 2048 blocks");

	ext2fs_blocks_count_set(fs->super, 1030);
	CHECK_RC(ext2fs_get_journal_params(&jparams, fs), 0,
		 "journal_dev 1030 fc clamps min journal blocks");
	CHECK(jparams.num_journal_blocks == JBD2_MIN_JOURNAL_BLOCKS &&
	      jparams.num_fc_blocks == 6,
	      "journal_dev 1030 fc -> 1024 + 6");

	CHECK_RC(ext2fs_create_journal_superblock2(fs, &jparams, 0, &raw_jsb),
		 0, "create 1K journal_dev superblock");
	jsb = (journal_superblock_t *) raw_jsb;
	CHECK(jsb->s_nr_users == 0 && ext2fs_be32_to_cpu(jsb->s_first) == 3,
	      "1K journal_dev nr_users == 0 and s_first == 3");
	ext2fs_free_mem(&raw_jsb);
	ext2fs_free(fs);

	fs = create_fs(path, 2048, 2, 0, 0, 0, 1); /* 4K journal_dev */
	CHECK_RC(ext2fs_create_journal_superblock(fs, 2048, 0, &raw_jsb), 0,
		 "create 4K journal_dev superblock");
	jsb = (journal_superblock_t *) raw_jsb;
	CHECK(jsb->s_nr_users == 0 && ext2fs_be32_to_cpu(jsb->s_first) == 2,
	      "4K journal_dev nr_users == 0 and s_first == 2");
	ext2fs_free_mem(&raw_jsb);
	ext2fs_free(fs);

	ext2fs_zero_blocks2(NULL, 0, 0, NULL, NULL);
	close(fd);
	unlink(path);
}

static errcode_t fail_write_blk64(io_channel channel EXT2FS_ATTR((unused)),
				  unsigned long long block,
				  int count EXT2FS_ATTR((unused)),
				  const void *data EXT2FS_ATTR((unused)))
{
	if (block >= 60)
		return EIO;
	return 0;
}

static void test_zero_blocks(void)
{
	struct struct_io_manager	nozero_mgr;
	ext2_filsys			fs;
	char				path[64], dirty[4096], verify[4096];
	char				zeros[4096];
	int				fd, err_cnt = 0;
	blk64_t				err_blk64 = 0;
	blk_t				err_blk32 = 0;

	fd = make_temp_file(path, (off_t) 256 * 1024);
	fs = create_fs(path, 256, 0, 1, 0, 0, 0);

	memset(dirty, 0xaa, sizeof(dirty));
	memset(zeros, 0, sizeof(zeros));

	/* 1. Standard zeroout path + num <= 0 no-op */
	CHECK_RC(ext2fs_zero_blocks2(fs, 50, 0, NULL, NULL), 0,
		 "zero_blocks2 num == 0 no-op");
	CHECK(pwrite(fd, dirty, sizeof(dirty), 50 * 1024) ==
	      (ssize_t) sizeof(dirty), "dirty blocks 50..53");
	CHECK_RC(ext2fs_zero_blocks2(fs, 50, 4, &err_blk64, &err_cnt), 0,
		 "zero_blocks2 50..53");
	CHECK(pread(fd, verify, sizeof(verify), 50 * 1024) ==
	      (ssize_t) sizeof(verify) &&
	      memcmp(verify, zeros, sizeof(zeros)) == 0,
	      "blocks 50..53 zeroed on disk");

	/* 2. Fallback stride write loop (zeroout == NULL) + 32-bit wrapper */
	ext2fs_zero_blocks2(NULL, 0, 0, NULL, NULL);
	nozero_mgr = *unix_io_manager;
	nozero_mgr.zeroout = NULL;
	fs->io->manager = &nozero_mgr;

	CHECK(pwrite(fd, dirty, sizeof(dirty), 55 * 1024) ==
	      (ssize_t) sizeof(dirty), "dirty blocks 55..58");
	/* Prime stride_length = 2, then zero unaligned range 55..58 (4 blks) */
	CHECK_RC(ext2fs_zero_blocks(fs, 40, 2, &err_blk32, &err_cnt), 0,
		 "zero_blocks prime stride = 2");
	CHECK_RC(ext2fs_zero_blocks(fs, 55, 4, &err_blk32, &err_cnt), 0,
		 "zero_blocks unaligned stride loop");
	CHECK_RC(io_channel_flush(fs->io), 0, "flush io_channel");
	CHECK(pread(fd, verify, sizeof(verify), 55 * 1024) ==
	      (ssize_t) sizeof(verify) &&
	      memcmp(verify, zeros, sizeof(zeros)) == 0,
	      "blocks 55..58 zeroed via fallback write loop");

	/* 3. Error propagation populates ret_blk and ret_count */
	nozero_mgr.write_blk64 = fail_write_blk64;
	err_blk32 = 0;
	err_cnt = 0;
	CHECK_RC(ext2fs_zero_blocks(fs, 60, 3, &err_blk32, &err_cnt), EIO,
		 "zero_blocks propagates write error");
	CHECK(err_blk32 == 60 && err_cnt > 0,
	      "zero_blocks sets ret_blk and ret_count on error");
	/* ret_blk and ret_count are optional */
	CHECK_RC(ext2fs_zero_blocks(fs, 60, 3, NULL, NULL), EIO,
		 "zero_blocks propagates write error with NULL ret_blk");

	fs->io->manager = unix_io_manager;
	ext2fs_zero_blocks2(NULL, 0, 0, NULL, NULL);
	ext2fs_free(fs);
	close(fd);
	unlink(path);
}

/*
 * The buffer used when the io channel cannot zero out blocks by itself
 * is kept across calls, and thus across filesystems.  Zeroing blocks of
 * a filesystem with a larger block size must not write from a buffer
 * that was sized for a smaller block size.
 */
static void test_zero_blocks_blocksize_change(void)
{
	struct struct_io_manager	nozero_mgr;
	ext2_filsys			fs1k, fs4k;
	char				path1k[64], path4k[64];
	char				*dirty = NULL, *verify = NULL;
	char				*zeros = NULL;
	size_t				len = (size_t) 256 * 4096;
	int				fd1k, fd4k;

	fd1k = make_temp_file(path1k, (off_t) 1024 * 1024);
	fs1k = create_fs(path1k, 1024, 0, 1, 0, 0, 0);
	fd4k = make_temp_file(path4k, (off_t) 1024 * 4096);
	fs4k = create_fs(path4k, 1024, 2, 1, 0, 0, 0);
	nozero_mgr = *unix_io_manager;
	nozero_mgr.zeroout = NULL;
	fs1k->io->manager = &nozero_mgr;
	fs4k->io->manager = &nozero_mgr;

	/* Zeroing 256 1K blocks sizes the buffer for 256 KiB */
	CHECK_RC(ext2fs_zero_blocks2(fs1k, 512, 256, NULL, NULL), 0,
		 "zero 256 blocks on 1K-block fs");

	/* Zeroing 256 4K blocks needs 1 MiB of zeroes */
	CHECK_RC(ext2fs_get_mem(len, &dirty), 0, "alloc dirty buf");
	CHECK_RC(ext2fs_get_mem(len, &verify), 0, "alloc verify buf");
	CHECK_RC(ext2fs_get_memzero(len, &zeros), 0, "alloc zero buf");
	memset(dirty, 0xaa, len);
	CHECK(pwrite(fd4k, dirty, len, (off_t) 512 * 4096) == (ssize_t) len,
	      "dirty 4K blocks 512..767");
	CHECK_RC(ext2fs_zero_blocks2(fs4k, 512, 256, NULL, NULL), 0,
		 "zero 256 blocks on 4K-block fs");
	CHECK_RC(io_channel_flush(fs4k->io), 0, "flush 4K io_channel");
	CHECK(pread(fd4k, verify, len, (off_t) 512 * 4096) == (ssize_t) len &&
	      memcmp(verify, zeros, len) == 0,
	      "4K blocks 512..767 zeroed after zeroing 1K blocks");

	ext2fs_free_mem(&zeros);
	ext2fs_free_mem(&verify);
	ext2fs_free_mem(&dirty);
	fs1k->io->manager = unix_io_manager;
	fs4k->io->manager = unix_io_manager;
	ext2fs_zero_blocks2(NULL, 0, 0, NULL, NULL);
	ext2fs_free(fs4k);
	ext2fs_free(fs1k);
	close(fd4k);
	unlink(path4k);
	close(fd1k);
	unlink(path1k);
}

static void test_add_journal_inode_and_device(void)
{
	ext2_filsys			fs, jdev;
	struct ext2fs_journal_params	jparams;
	struct ext2_inode		inode;
	journal_superblock_t		*jsb;
	char				path[64], jpath[64];
	char				buf[1024], zeros[1024], *saved_name;
	int				fd, jfd;
	blk64_t				blk0, blk1;

	memset(zeros, 0, sizeof(zeros));

	/* 1. Extent + fast_commit + full zeroing journal inode */
	fd = make_temp_file(path, (off_t) 8192 * 1024);
	fs = create_fs(path, 8192, 0, 1, 1, 0, 0);
	ext2fs_set_feature_fast_commit(fs->super);
	CHECK_RC(ext2fs_get_journal_params(&jparams, fs), 0,
		 "get_journal_params for extent journal");

	CHECK_RC(ext2fs_add_journal_inode3(fs, &jparams, ~0ULL,
					   EXT2_MKJOURNAL_NO_MNT_CHECK),
		 0, "add_journal_inode3 extent + fast_commit");
	CHECK(ext2fs_has_feature_journal(fs->super) != 0,
	      "HAS_JOURNAL feature set");
	CHECK(fs->super->s_journal_inum == EXT2_JOURNAL_INO,
	      "s_journal_inum == EXT2_JOURNAL_INO");
	CHECK(fs->super->s_jnl_backup_type == EXT3_JNL_BACKUP_BLOCKS,
	      "s_jnl_backup_type == EXT3_JNL_BACKUP_BLOCKS");

	CHECK_RC(ext2fs_read_inode(fs, EXT2_JOURNAL_INO, &inode), 0,
		 "read EXT2_JOURNAL_INO");
	CHECK(EXT2_I_SIZE(&inode) == (ext2_off64_t) 1040 * 1024,
	      "journal inode size == 1040 KB");
	CHECK(fs->super->s_jnl_blocks[16] == inode.i_size,
	      "s_jnl_blocks[16] backs up i_size");

	blk0 = blk1 = 0;
	CHECK_RC(ext2fs_bmap2(fs, EXT2_JOURNAL_INO, &inode, NULL, 0, 0,
			      NULL, &blk0), 0, "bmap journal lblk 0");
	CHECK_RC(ext2fs_bmap2(fs, EXT2_JOURNAL_INO, &inode, NULL, 0, 1,
			      NULL, &blk1), 0, "bmap journal lblk 1");
	CHECK_RC(io_channel_read_blk64(fs->io, blk0, 1, buf), 0,
		 "read journal block 0");
	jsb = (journal_superblock_t *) buf;
	CHECK(ext2fs_be32_to_cpu(jsb->s_header.h_magic) == JBD2_MAGIC_NUMBER &&
	      ext2fs_be32_to_cpu(jsb->s_maxlen) == 1040 &&
	      ext2fs_be32_to_cpu(jsb->s_num_fc_blks) == 16,
	      "on-disk journal superblock valid");
	CHECK_RC(io_channel_read_blk64(fs->io, blk1, 1, buf), 0,
		 "read journal block 1");
	CHECK(memcmp(buf, zeros, sizeof(zeros)) == 0,
	      "non-lazy journal block 1 zeroed");

	/* Duplicate add_journal_inode fails with EEXIST */
	CHECK_RC(ext2fs_add_journal_inode(fs, 1024,
					  EXT2_MKJOURNAL_NO_MNT_CHECK),
		 EEXIST, "adding journal when already present returns EEXIST");
	CHECK_RC(ext2fs_close_free(&fs), 0, "close extent journal fs");
	close(fd);
	unlink(path);

	/* 2. Indirect + LAZYINIT journal on flex_bg multi-group FS */
	fd = make_temp_file(path, (off_t) 65536 * 1024);
	fs = create_fs(path, 65536, 0, 0, 0, 2, 0); /* 8 groups, flex_bg=4 */
	CHECK_RC(ext2fs_add_journal_inode(fs, 1024,
					  EXT2_MKJOURNAL_LAZYINIT |
					  EXT2_MKJOURNAL_NO_MNT_CHECK),
		 0, "add_journal_inode indirect + LAZYINIT + flex_bg");
	CHECK_RC(ext2fs_read_inode(fs, EXT2_JOURNAL_INO, &inode), 0,
		 "read indirect journal inode");
	CHECK((inode.i_flags & EXT4_EXTENTS_FL) == 0 &&
	      inode.i_block[EXT2_IND_BLOCK] != 0,
	      "indirect journal allocated IND block");

	/* 3. ext2fs_add_journal_device validation on non-existent & regfile */
	jfd = make_temp_file(jpath, (off_t) 2048 * 1024);
	jdev = create_fs(jpath, 2048, 0, 0, 0, 0, 1);
	CHECK_RC(ext2fs_add_journal_device(fs, jdev),
		 EXT2_ET_JOURNAL_NOT_BLOCK,
		 "add_journal_device rejects regular file with NOT_BLOCK");

	saved_name = jdev->device_name;
	jdev->device_name = (char *) "/tmp/tst_mkjournal_nonexistent_dev_path";
	CHECK_RC(ext2fs_add_journal_device(fs, jdev), ENOENT,
		 "add_journal_device returns ENOENT on missing path");
	jdev->device_name = saved_name;

	CHECK_RC(ext2fs_close_free(&jdev), 0, "close jdev");
	CHECK_RC(ext2fs_close_free(&fs), 0, "close flex_bg fs");
	ext2fs_zero_blocks2(NULL, 0, 0, NULL, NULL);
	close(jfd);
	unlink(jpath);
	close(fd);
	unlink(path);
}

/*
 * ext2fs_add_journal_device() only accepts a block device, but merely
 * stat()s its name; all I/O goes through journal_dev->io.  Find any
 * block device node to stand in for the name of a journal image file.
 */
static int find_block_device(char *path_out, size_t len)
{
	DIR		*dir;
	struct dirent	*de;
	struct stat	st;
	int		found = 0;

	dir = opendir("/dev");
	if (!dir)
		return 0;
	while (!found && (de = readdir(dir)) != NULL) {
		snprintf(path_out, len, "/dev/%s", de->d_name);
		if (stat(path_out, &st) == 0 && S_ISBLK(st.st_mode))
			found = 1;
	}
	closedir(dir);
	return found;
}

static void write_journal_users(ext2_filsys jdev, char *jsb_buf,
				__u32 nr_users)
{
	journal_superblock_t	*jsb = (journal_superblock_t *) jsb_buf;
	__u32			i;

	jsb->s_nr_users = ext2fs_cpu_to_be32(nr_users);
	memset(jsb->s_users, 0, sizeof(jsb->s_users));
	for (i = 0; i < nr_users; i++)
		memset(&jsb->s_users[i * 16], i + 1, 16);
	CHECK_RC(io_channel_write_blk64(jdev->io,
				ext2fs_journal_sb_start(jdev->blocksize),
				1, jsb_buf),
		 0, "write journal superblock users");
}

/*
 * The journal superblock has room for JBD2_USERS_MAX filesystem UUIDs.
 * Adding another filesystem to a journal whose user table is full must
 * fail instead of writing past the end of the table.
 */
static void test_add_journal_device_users(void)
{
	ext2_filsys			fs, jdev;
	struct ext2fs_journal_params	jparams;
	journal_superblock_t		*jsb;
	char				path[64], jpath[64], bdev[300];
	char				buf[1024], *saved_name;
	char				*jsb_buf = NULL;
	int				fd, jfd;

	if (!find_block_device(bdev, sizeof(bdev))) {
		printf("tst_mkjournal: no block device in /dev, "
		       "skipping journal user table test\n");
		return;
	}

	fd = make_temp_file(path, (off_t) 2048 * 1024);
	fs = create_fs(path, 2048, 0, 0, 0, 0, 0);
	jfd = make_temp_file(jpath, (off_t) 2048 * 1024);
	jdev = create_fs(jpath, 2048, 0, 0, 0, 0, 1);
	CHECK_RC(ext2fs_get_journal_params(&jparams, jdev), 0,
		 "journal_dev params for user table test");
	CHECK_RC(ext2fs_create_journal_superblock2(jdev, &jparams, 0,
						   &jsb_buf),
		 0, "create journal_dev superblock for user table test");
	saved_name = jdev->device_name;
	jdev->device_name = bdev;

	/* 1. Full user table without our UUID: nothing may be added */
	write_journal_users(jdev, jsb_buf, JBD2_USERS_MAX);
	CHECK_RC(ext2fs_add_journal_device(fs, jdev), EXT2_ET_TOO_MANY_REFS,
		 "add_journal_device rejects a full journal user table");
	CHECK_RC(io_channel_read_blk64(jdev->io,
				       ext2fs_journal_sb_start(jdev->blocksize),
				       1, buf),
		 0, "reread full journal superblock");
	jsb = (journal_superblock_t *) buf;
	CHECK(ext2fs_be32_to_cpu(jsb->s_nr_users) == JBD2_USERS_MAX,
	      "full journal user table left unchanged");
	CHECK(!ext2fs_has_feature_journal(fs->super),
	      "rejected journal device not added to fs");

	/* 2. One free slot left: our UUID goes into the last slot */
	write_journal_users(jdev, jsb_buf, JBD2_USERS_MAX - 1);
	CHECK_RC(ext2fs_add_journal_device(fs, jdev), 0,
		 "add_journal_device fills the last journal user slot");
	CHECK_RC(io_channel_read_blk64(jdev->io,
				       ext2fs_journal_sb_start(jdev->blocksize),
				       1, buf),
		 0, "reread journal superblock");
	jsb = (journal_superblock_t *) buf;
	CHECK(ext2fs_be32_to_cpu(jsb->s_nr_users) == JBD2_USERS_MAX &&
	      memcmp(&jsb->s_users[(JBD2_USERS_MAX - 1) * 16],
		     fs->super->s_uuid, 16) == 0,
	      "fs UUID stored in the last journal user slot");
	CHECK(ext2fs_has_feature_journal(fs->super) &&
	      fs->super->s_journal_inum == 0,
	      "fs now uses the external journal");

	jdev->device_name = saved_name;
	ext2fs_free_mem(&jsb_buf);
	CHECK_RC(ext2fs_close_free(&jdev), 0, "close user table jdev");
	ext2fs_free(fs);
	ext2fs_zero_blocks2(NULL, 0, 0, NULL, NULL);
	close(jfd);
	unlink(jpath);
	close(fd);
	unlink(path);
}

int main(int argc EXT2FS_ATTR((unused)), char **argv EXT2FS_ATTR((unused)))
{
	add_error_table(&et_ext2_error_table);

	test_sizing_and_superblock();
	test_zero_blocks();
	test_zero_blocks_blocksize_change();
	test_add_journal_inode_and_device();
	test_add_journal_device_users();

	remove_error_table(&et_ext2_error_table);

	if (fail_count) {
		fprintf(stderr, "tst_mkjournal: %d/%d checks FAILED\n",
			fail_count, test_count);
		return 1;
	}
	printf("tst_mkjournal: all %d checks passed\n", test_count);
	return 0;
}
