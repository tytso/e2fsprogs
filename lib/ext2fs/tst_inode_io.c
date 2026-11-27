/*
 * tst_inode_io.c --- unit tests for inode_io.c
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

/* Return 1 if all len bytes at buf are equal to c, 0 otherwise. */
static int mem_is_filled(const unsigned char *buf, unsigned char c,
			 size_t len)
{
	size_t	i;

	for (i = 0; i < len; i++)
		if (buf[i] != c)
			return 0;
	return 1;
}

static ext2_filsys setup_outer_fs(char *path_out, int *fd_out)
{
	ext2_filsys		fs = NULL;
	struct ext2_super_block	param;
	int			fd;

	strcpy(path_out, "/tmp/tst_inode_io.XXXXXX");
	fd = mkstemp(path_out);
	if (fd < 0) {
		perror("mkstemp");
		exit(1);
	}
	if (ftruncate(fd, 2 * 1024 * 1024) < 0) {
		perror("ftruncate");
		close(fd);
		unlink(path_out);
		exit(1);
	}
	*fd_out = fd;

	memset(&param, 0, sizeof(param));
	ext2fs_blocks_count_set(&param, 512);
	param.s_inodes_count = 128;
	param.s_log_block_size = 2; /* 4096-byte outer blocks */
	param.s_log_cluster_size = 2;
	param.s_rev_level = EXT2_DYNAMIC_REV;
	param.s_inode_size = 256;
	ext2fs_set_feature_extents(&param);

	if (ext2fs_initialize(path_out, EXT2_FLAG_RW | EXT2_FLAG_64BITS,
			      &param, unix_io_manager, &fs))
		exit(1);
	if (ext2fs_allocate_tables(fs))
		exit(1);
	return fs;
}

static ext2_ino_t alloc_reg_inode(ext2_filsys fs)
{
	ext2_ino_t		ino = 0;
	struct ext2_inode	inode;

	if (ext2fs_new_inode(fs, EXT2_ROOT_INO, LINUX_S_IFREG | 0644,
			     0, &ino))
		exit(1);
	ext2fs_inode_alloc_stats2(fs, ino, +1, 0);
	memset(&inode, 0, sizeof(inode));
	inode.i_mode = LINUX_S_IFREG | 0644;
	inode.i_links_count = 1;
	inode.i_flags = EXT4_EXTENTS_FL;
	if (ext2fs_write_new_inode(fs, ino, &inode))
		exit(1);
	return ino;
}

/*
 * Suite 1: ext2fs_inode_io_intern / intern2 linked-list management,
 *          open error paths, refcounting, block/sub-block/byte I/O, and flush.
 */
static void test_inode_io_channel_ops(void)
{
	char			outer_path[64];
	char			*n1 = NULL, *n2 = NULL, *n3 = NULL;
	char			saved_n2[32];
	int			outer_fd;
	ext2_filsys		fs;
	ext2_ino_t		ino1, ino2;
	struct ext2_inode	mem_inode;
	io_channel		io1 = NULL, io2 = NULL, io3 = NULL;
	unsigned char		wbuf[4096], rbuf[4096];
	unsigned int		i;

	fs = setup_outer_fs(outer_path, &outer_fd);
	ino1 = alloc_reg_inode(fs);
	ino2 = alloc_reg_inode(fs);

	/* Open validation: NULL and un-interned names */
	CHECK_RC(inode_io_manager->open(NULL, IO_FLAG_RW, &io1),
		 EXT2_ET_BAD_DEVICE_NAME, "open NULL rejected");
	CHECK_RC(inode_io_manager->open("999:999", IO_FLAG_RW, &io1),
		 ENOENT, "open un-interned name returns ENOENT");

	/*
	 * Intern 3 entries onto top_intern (list order: n3 -> n2 -> n1),
	 * then open n2 first to exercise middle-of-list removal (`prev != NULL`).
	 */
	CHECK_RC(ext2fs_inode_io_intern(fs, ino1, &n1), 0, "intern n1");
	CHECK_RC(ext2fs_inode_io_intern(fs, ino2, &n2), 0, "intern n2");

	memset(&mem_inode, 0, sizeof(mem_inode));
	mem_inode.i_mode = LINUX_S_IFREG | 0600;
	mem_inode.i_links_count = 1;
	mem_inode.i_flags = EXT4_EXTENTS_FL;
	/* ino 50 is not written to itable yet; intern2 supplies &mem_inode */
	ext2fs_inode_alloc_stats2(fs, 50, +1, 0);
	CHECK_RC(ext2fs_inode_io_intern2(fs, 50, &mem_inode, &n3), 0,
		 "intern2 n3 with explicit struct ext2_inode");

	snprintf(saved_n2, sizeof(saved_n2), "%s", n2);
	CHECK_RC(inode_io_manager->open(n2, IO_FLAG_RW, &io2), 0,
		 "open middle interned entry n2");
	/* Re-opening consumed entry n2 returns ENOENT */
	CHECK_RC(inode_io_manager->open(saved_n2, IO_FLAG_RW, &io1), ENOENT,
		 "re-open consumed entry returns ENOENT");

	CHECK_RC(inode_io_manager->open(n3, IO_FLAG_RW, &io3), 0,
		 "open head interned entry n3 (CHANNEL_HAS_INODE)");
	/* Open n1 read-only (flags == 0) */
	CHECK_RC(inode_io_manager->open(n1, 0, &io1), 0,
		 "open tail interned entry n1 RO");

	/* Writing to RO channel fails with EXT2_ET_FILE_RO */
	memset(wbuf, 0x42, sizeof(wbuf));
	CHECK_RC(io_channel_write_blk64(io1, 0, 1, wbuf), EXT2_ET_FILE_RO,
		 "write_blk64 on RO inode_io fails with EXT2_ET_FILE_RO");
	CHECK_RC(io_channel_close(io1), 0, "close RO io1");

	/* Exercise refcounting, set_blksize, 32/64-bit R/W, -count, write_byte */
	io_channel_bumpcount(io2);
	CHECK_RC(io_channel_set_blksize(io2, 2048), 0, "set_blksize 2048");
	for (i = 0; i < sizeof(wbuf); i++)
		wbuf[i] = (unsigned char) ((i * 5 + 0x21) & 0xff);

	/* Write 2 x 2048B blocks via 64-bit API at block 0 */
	CHECK_RC(io_channel_write_blk64(io2, 0, 2, wbuf), 0,
		 "write_blk64 2 blocks");
	/* Write 512 bytes via negative count (-512) at block 3 (off 6144) */
	memset(wbuf, 0xee, 512);
	CHECK_RC(io2->manager->write_blk(io2, 3, -512, wbuf), 0,
		 "legacy 32-bit write_blk with -512 count");
	/* Overwrite 8 bytes at offset 16 via write_byte */
	CHECK_RC(io_channel_write_byte(io2, 16, 8, "INODE_IO"), 0,
		 "inode_write_byte at offset 16");
	CHECK_RC(io_channel_flush(io2), 0, "inode_flush");

	/* Read back via 64-bit and legacy 32-bit read_blk */
	memset(rbuf, 0, sizeof(rbuf));
	CHECK_RC(io_channel_read_blk64(io2, 0, 2, rbuf), 0,
		 "read_blk64 2 blocks");
	CHECK(memcmp(rbuf + 16, "INODE_IO", 8) == 0,
	      "write_byte payload verified");
	CHECK(rbuf[0] == 0x21 && rbuf[4095] == ((4095 * 5 + 0x21) & 0xff),
	      "surrounding 2-block data intact");

	/* Sparse hole between 4096..6143 reads back as 0; 6144..6655 is 0xee */
	memset(rbuf, 0x99, 1024);
	CHECK_RC(io2->manager->read_blk(io2, 2, -512, rbuf), 0,
		 "read sparse hole at block 2 (offset 4096)");
	CHECK(rbuf[0] == 0 && rbuf[511] == 0, "sparse hole reads as zeros");

	memset(rbuf, 0, 512);
	CHECK_RC(io2->manager->read_blk(io2, 3, -512, rbuf), 0,
		 "read -512 block at block 3 (offset 6144)");
	CHECK(rbuf[0] == 0xee && rbuf[511] == 0xee,
	      "sub-block data at block 3 verified");

	/* First close only decrements refcount; second close finalizes */
	CHECK_RC(io_channel_close(io2), 0, "close bumped io2 (refcount -> 1)");
	CHECK_RC(io_channel_close(io2), 0, "final close io2");

	/* Also verify CHANNEL_HAS_INODE channel io3 writes and closes cleanly */
	memset(wbuf, 0x77, 1024);
	CHECK_RC(io_channel_write_blk64(io3, 0, 1, wbuf), 0,
		 "write to intern2 channel io3");
	CHECK_RC(io_channel_close(io3), 0, "close io3");

	CHECK_RC(ext2fs_close_free(&fs), 0, "close outer fs");
	close(outer_fd);
	unlink(outer_path);
	printf("  test_inode_io_channel_ops: ok\n");
}

/*
 * Suite 2: End-to-end nested ext2_filsys hosted inside a regular file inode
 *          of an outer ext4 filesystem via inode_io_manager.
 */
static void test_nested_filesystem_in_inode(void)
{
	char			outer_path[64];
	char			*intern_name = NULL;
	int			outer_fd;
	ext2_filsys		outer_fs, inner_fs = NULL;
	ext2_ino_t		host_ino, found_ino = 0;
	struct ext2_super_block	inner_param;

	outer_fs = setup_outer_fs(outer_path, &outer_fd);
	host_ino = alloc_reg_inode(outer_fs);

	/* Format a 128-block 1KiB ext2 filesystem inside host_ino */
	CHECK_RC(ext2fs_inode_io_intern(outer_fs, host_ino, &intern_name), 0,
		 "intern host_ino for mkfs");
	memset(&inner_param, 0, sizeof(inner_param));
	ext2fs_blocks_count_set(&inner_param, 128);
	inner_param.s_inodes_count = 32;
	inner_param.s_rev_level = EXT2_DYNAMIC_REV;
	inner_param.s_inode_size = 128;

	CHECK_RC(ext2fs_initialize(intern_name, EXT2_FLAG_RW, &inner_param,
				   inode_io_manager, &inner_fs), 0,
		 "ext2fs_initialize nested fs");
	strcpy((char *) inner_fs->super->s_volume_name, "nested_ext2");
	CHECK_RC(ext2fs_allocate_tables(inner_fs), 0,
		 "allocate_tables nested fs");
	CHECK_RC(ext2fs_mkdir(inner_fs, EXT2_ROOT_INO, EXT2_ROOT_INO, 0), 0,
		 "create root dir in nested fs");
	CHECK_RC(ext2fs_mkdir(inner_fs, EXT2_ROOT_INO, 0, "subdir"), 0,
		 "create /subdir in nested fs");
	CHECK_RC(ext2fs_close_free(&inner_fs), 0, "close nested fs");

	/* Flush & reopen nested fs read-only via a fresh intern handle */
	CHECK_RC(ext2fs_flush(outer_fs), 0, "flush outer_fs");
	CHECK_RC(ext2fs_inode_io_intern(outer_fs, host_ino, &intern_name), 0,
		 "re-intern host_ino for open");
	CHECK_RC(ext2fs_open(intern_name, 0, 0, 0, inode_io_manager,
			     &inner_fs), 0,
		 "ext2fs_open nested fs RO");
	CHECK_RC(ext2fs_read_bitmaps(inner_fs), 0, "read nested bitmaps");
	CHECK(strcmp((char *) inner_fs->super->s_volume_name,
		     "nested_ext2") == 0,
	      "nested superblock volume name persisted");
	CHECK_RC(ext2fs_namei(inner_fs, EXT2_ROOT_INO, EXT2_ROOT_INO,
			      "/subdir", &found_ino), 0,
		 "lookup /subdir in nested fs");
	CHECK(found_ino >= EXT2_GOOD_OLD_FIRST_INO,
	      "valid /subdir inode found in nested fs");

	CHECK_RC(ext2fs_close_free(&inner_fs), 0, "close reopened nested fs");
	CHECK_RC(ext2fs_close_free(&outer_fs), 0, "close outer_fs");
	close(outer_fd);
	unlink(outer_path);
	printf("  test_nested_filesystem_in_inode: ok\n");
}

/*
 * Suite 3: Reads extending past i_size.  The inode is treated like a
 *          sparse device: bytes beyond EOF read back as zeros, just
 *          like a hole, instead of leaving the caller's buffer as is.
 */
static void test_read_past_eof(void)
{
	char			outer_path[64];
	char			*name = NULL;
	int			outer_fd;
	ext2_filsys		fs;
	ext2_ino_t		ino;
	io_channel		io = NULL;
	struct ext2_inode	inode;
	unsigned char		wbuf[1024], rbuf[1024];

	fs = setup_outer_fs(outer_path, &outer_fd);
	ino = alloc_reg_inode(fs);
	CHECK_RC(ext2fs_inode_io_intern(fs, ino, &name), 0, "intern ino");
	CHECK_RC(inode_io_manager->open(name, IO_FLAG_RW, &io), 0,
		 "open inode channel");

	/* Write 1024 + 476 bytes: i_size is 1500, block 1 straddles EOF */
	memset(wbuf, 0x5a, sizeof(wbuf));
	CHECK_RC(io_channel_write_blk64(io, 0, 1, wbuf), 0, "write block 0");
	CHECK_RC(io_channel_write_blk64(io, 1, -476, wbuf), 0,
		 "write 476 bytes at block 1");

	memset(rbuf, 0x99, sizeof(rbuf));
	CHECK_RC(io_channel_read_blk64(io, 1, 1, rbuf), 0,
		 "read block straddling EOF");
	CHECK(mem_is_filled(rbuf, 0x5a, 476), "data before EOF read back");
	CHECK(mem_is_filled(rbuf + 476, 0, 548),
	      "bytes past EOF read as zeros");

	memset(rbuf, 0x99, sizeof(rbuf));
	CHECK_RC(io_channel_read_blk64(io, 2, 1, rbuf), 0,
		 "read block entirely past EOF");
	CHECK(mem_is_filled(rbuf, 0, sizeof(rbuf)),
	      "block past EOF reads as zeros");

	CHECK_RC(io_channel_close(io), 0, "close inode channel");
	CHECK_RC(ext2fs_read_inode(fs, ino, &inode), 0, "read inode");
	CHECK(EXT2_I_SIZE(&inode) == 1500, "reads past EOF leave i_size alone");

	CHECK_RC(ext2fs_close_free(&fs), 0, "close outer fs");
	close(outer_fd);
	unlink(outer_path);
	printf("  test_read_past_eof: ok\n");
}

/*
 * Suite 4: write_byte at byte offsets which overflow a signed (>= 2 GiB)
 *          or unsigned (>= 4 GiB) 32-bit offset must land exactly there.
 */
static void test_write_byte_large_offset(void)
{
	char			outer_path[64];
	char			*name = NULL;
	int			outer_fd;
	ext2_filsys		fs;
	ext2_ino_t		ino;
	io_channel		io = NULL;
	struct ext2_inode	inode;
	unsigned char		rbuf[1024];
#if SIZEOF_LONG >= 8
	unsigned long long	off_4g = (4ULL << 30) + 3 * 1024 + 16;
#endif

	fs = setup_outer_fs(outer_path, &outer_fd);
	ino = alloc_reg_inode(fs);
	CHECK_RC(ext2fs_inode_io_intern(fs, ino, &name), 0, "intern ino");
	CHECK_RC(inode_io_manager->open(name, IO_FLAG_RW, &io), 0,
		 "open inode channel");

	/* 3 GiB + 16 does not fit in a signed 32-bit offset */
	CHECK_RC(io_channel_write_byte(io, 0xC0000010UL, 8, "OFF_3GIB"), 0,
		 "write_byte at 3 GiB + 16");
	CHECK_RC(ext2fs_read_inode(fs, ino, &inode), 0,
		 "read inode after 3 GiB write_byte");
	CHECK(EXT2_I_SIZE(&inode) == 0xC0000018ULL,
	      "i_size after write_byte at 3 GiB + 16");
	memset(rbuf, 0x99, sizeof(rbuf));
	CHECK_RC(io_channel_read_blk64(io, 0xC0000000ULL / 1024, 1, rbuf), 0,
		 "read block at 3 GiB");
	CHECK(memcmp(rbuf + 16, "OFF_3GIB", 8) == 0,
	      "write_byte payload found at 3 GiB + 16");

#if SIZEOF_LONG >= 8
	/* 4 GiB + 3 KiB + 16 does not fit in an unsigned 32-bit offset */
	CHECK_RC(io_channel_write_byte(io, (unsigned long) off_4g, 8,
				       "OFF_4GIB"), 0,
		 "write_byte at 4 GiB + 3 KiB + 16");
	CHECK_RC(ext2fs_read_inode(fs, ino, &inode), 0,
		 "read inode after 4 GiB write_byte");
	CHECK(EXT2_I_SIZE(&inode) == off_4g + 8,
	      "i_size after write_byte at 4 GiB + 3 KiB + 16");
	memset(rbuf, 0x99, sizeof(rbuf));
	CHECK_RC(io_channel_read_blk64(io, (off_4g - 16) / 1024, 1, rbuf), 0,
		 "read block at 4 GiB + 3 KiB");
	CHECK(memcmp(rbuf + 16, "OFF_4GIB", 8) == 0,
	      "write_byte payload found at 4 GiB + 3 KiB + 16");
	/* Nothing may land at the offset truncated to 32 bits (3 KiB + 16) */
	memset(rbuf, 0x99, sizeof(rbuf));
	CHECK_RC(io_channel_read_blk64(io, 3, 1, rbuf), 0,
		 "read block at 3 KiB");
	CHECK(mem_is_filled(rbuf, 0, sizeof(rbuf)),
	      "no write_byte payload at truncated offset 3 KiB + 16");
#endif

	CHECK_RC(io_channel_close(io), 0, "close inode channel");
	CHECK_RC(ext2fs_close_free(&fs), 0, "close outer fs");
	close(outer_fd);
	unlink(outer_path);
	printf("  test_write_byte_large_offset: ok\n");
}

int main(int argc EXT2FS_ATTR((unused)), char *argv[] EXT2FS_ATTR((unused)))
{
	add_error_table(&et_ext2_error_table);
	printf("Running tst_inode_io unit tests...\n");

	test_inode_io_channel_ops();
	test_nested_filesystem_in_inode();
	test_read_past_eof();
	test_write_byte_large_offset();

	remove_error_table(&et_ext2_error_table);
	printf("tst_inode_io: %d checks run, %d failed\n",
	       test_count, fail_count);
	return fail_count ? 1 : 0;
}
