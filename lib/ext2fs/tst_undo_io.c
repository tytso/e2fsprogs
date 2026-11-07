/*
 * tst_undo_io.c --- unit tests for undo_io.c
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
#ifdef HAVE_SYS_RESOURCE_H
#include <sys/resource.h>
#endif
#ifdef HAVE_SIGNAL_H
#include <signal.h>
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

/* On-disk structures matching undo_io.c */
#define E2UNDO_MAGIC			"E2UNDO02"
#define KEYBLOCK_MAGIC			0xCADECADEU
#define E2UNDO_STATE_FINISHED		0x1U
#define E2UNDO_FEATURE_COMPAT_FS_OFFSET	0x1U

struct tst_undo_header {
	char	magic[8];
	__le64	num_keys;
	__le64	super_offset;
	__le64	key_offset;
	__le32	block_size;
	__le32	fs_block_size;
	__le32	sb_crc;
	__le32	state;
	__le32	f_compat;
	__le32	f_incompat;
	__le32	f_rocompat;
	__le32	pad32;
	__le64	fs_offset;
	__u8	padding[436];
	__le32	header_crc;
};

struct tst_undo_key {
	__le64	fsblk;
	__le32	blk_crc;
	__le32	size;
};

struct tst_undo_key_block {
	__le32			magic;
	__le32			crc;
	__le64			reserved;
	struct tst_undo_key	keys[];
};

static int make_temp_file(char *tmpl, size_t size)
{
	int fd;

	strcpy(tmpl, "/tmp/tst_undo_io.XXXXXX");
	fd = mkstemp(tmpl);
	if (fd < 0) {
		perror("mkstemp");
		exit(1);
	}
	if (size > 0 && ftruncate(fd, (off_t) size) < 0) {
		perror("ftruncate");
		close(fd);
		unlink(tmpl);
		exit(1);
	}
	return fd;
}

static void init_ext2_image(const char *path, unsigned int blocks,
			    unsigned long long fs_offset)
{
	ext2_filsys		fs = NULL;
	struct ext2_super_block	param;
	char			opt[64];
	errcode_t		retval;

	memset(&param, 0, sizeof(param));
	ext2fs_blocks_count_set(&param, blocks);
	param.s_inodes_count = 64;
	param.s_rev_level = EXT2_DYNAMIC_REV;
	param.s_inode_size = 128;
	param.s_uuid[0] = (unsigned char) (blocks & 0xff);

	opt[0] = '\0';
	if (fs_offset)
		snprintf(opt, sizeof(opt), "offset=%llu", fs_offset);

	retval = ext2fs_initialize(path, EXT2_FLAG_RW, &param,
				   unix_io_manager, &fs);
	if (retval) {
		com_err("init_ext2_image", retval, "initializing %s", path);
		exit(1);
	}
	if (fs_offset) {
		retval = io_channel_set_options(fs->io, opt);
		if (retval) {
			com_err("init_ext2_image", retval, "setting offset");
			exit(1);
		}
	}
	retval = ext2fs_allocate_tables(fs);
	if (retval) {
		com_err("init_ext2_image", retval, "allocating tables");
		exit(1);
	}
	retval = ext2fs_close_free(&fs);
	if (retval) {
		com_err("init_ext2_image", retval, "closing fs");
		exit(1);
	}
}

/*
 * Verify undo file integrity and replay all undo records onto img_fd.
 */
static int verify_and_replay_undo(int undo_fd, int img_fd,
				  size_t expected_keys, int *keyblocks_seen)
{
	struct tst_undo_header		hdr;
	struct tst_undo_key_block	*kb = NULL;
	unsigned char			*data_buf = NULL;
	unsigned int			ublk_sz, fs_blk_sz;
	size_t				num_keys, keys_per_blk, i, j, max_j;
	unsigned long long		lblk, fs_off;
	__u32				crc, exp_crc;
	int				kb_count = 0, ok = 1;

	if (pread(undo_fd, &hdr, sizeof(hdr), 0) != (ssize_t) sizeof(hdr))
		return 0;
	if (memcmp(hdr.magic, E2UNDO_MAGIC, 8) != 0)
		return 0;
	crc = ext2fs_crc32c_le(~0, (unsigned char *) &hdr,
			       sizeof(hdr) - sizeof(__u32));
	if (ext2fs_le32_to_cpu(hdr.header_crc) != crc)
		return 0;
	if (!(ext2fs_le32_to_cpu(hdr.state) & E2UNDO_STATE_FINISHED))
		return 0;

	num_keys = (size_t) ext2fs_le64_to_cpu(hdr.num_keys);
	if (num_keys != expected_keys)
		return 0;

	ublk_sz = ext2fs_le32_to_cpu(hdr.block_size);
	fs_blk_sz = ext2fs_le32_to_cpu(hdr.fs_block_size);
	fs_off = ext2fs_le64_to_cpu(hdr.fs_offset);
	keys_per_blk = (ublk_sz / sizeof(struct tst_undo_key)) - 1;

	if (ext2fs_get_mem(ublk_sz, &kb))
		return 0;

	lblk = ext2fs_le64_to_cpu(hdr.key_offset);
	for (i = 0; i < num_keys; i += keys_per_blk) {
		if (pread(undo_fd, kb, ublk_sz,
			  (off_t) (lblk * ublk_sz)) != (ssize_t) ublk_sz) {
			ok = 0;
			break;
		}
		if (ext2fs_le32_to_cpu(kb->magic) != KEYBLOCK_MAGIC) {
			ok = 0;
			break;
		}
		exp_crc = ext2fs_le32_to_cpu(kb->crc);
		kb->crc = 0;
		crc = ext2fs_crc32c_le(~0, (unsigned char *) kb, ublk_sz);
		if (crc != exp_crc) {
			ok = 0;
			break;
		}
		kb_count++;
		lblk++;

		max_j = num_keys - i;
		if (max_j > keys_per_blk)
			max_j = keys_per_blk;
		for (j = 0; j < max_j; j++) {
			unsigned long long fsblk =
				ext2fs_le64_to_cpu(kb->keys[j].fsblk);
			__u32 ksz = ext2fs_le32_to_cpu(kb->keys[j].size);
			__u32 kcrc = ext2fs_le32_to_cpu(kb->keys[j].blk_crc);

			if (ext2fs_get_mem(ksz, &data_buf)) {
				ok = 0;
				break;
			}
			if (pread(undo_fd, data_buf, ksz,
				  (off_t) (lblk * ublk_sz)) != (ssize_t) ksz) {
				ext2fs_free_mem(&data_buf);
				ok = 0;
				break;
			}
			if (ext2fs_crc32c_le(~0, data_buf, ksz) != kcrc) {
				ext2fs_free_mem(&data_buf);
				ok = 0;
				break;
			}
			if (pwrite(img_fd, data_buf, ksz,
				   (off_t) (fs_off + fsblk * fs_blk_sz)) !=
			    (ssize_t) ksz) {
				ext2fs_free_mem(&data_buf);
				ok = 0;
				break;
			}
			ext2fs_free_mem(&data_buf);
			lblk += (ksz + ublk_sz - 1) / ublk_sz;
		}
		if (!ok)
			break;
	}

	ext2fs_free_mem(&kb);
	if (keyblocks_seen)
		*keyblocks_seen = kb_count;
	return ok;
}

/*
 * Suite 1 & 2: Header/CRC format, contiguous key coalescing, duplicate
 *              write suppression, write_byte/zeroout/discard capture,
 *              reopening an existing undo file, rollback replay, and
 *              wrong-FS / corrupt-undo rejection.
 */
static void test_undo_coalesce_reopen_and_replay(void)
{
	char		img_path[64], undo_path[64], other_path[64];
	unsigned char	*orig_img = NULL, *cur_img = NULL;
	unsigned char	blk[1024];
	int		img_fd, undo_fd, other_fd, i, kb_seen = 0;
	io_channel	io = NULL;
	io_stats	stats = NULL;
	size_t		img_size = 256 * 1024;
	errcode_t	rc;

	img_fd = make_temp_file(img_path, img_size);
	undo_fd = make_temp_file(undo_path, 0);
	other_fd = make_temp_file(other_path, img_size);

	init_ext2_image(img_path, 256, 0);
	init_ext2_image(other_path, 200, 0);

	/* Seed data blocks 20..60 with distinct byte patterns */
	for (i = 20; i <= 60; i++) {
		memset(blk, 0x10 + i, sizeof(blk));
		CHECK(pwrite(img_fd, blk, sizeof(blk), i * 1024) == 1024,
		      "seed block");
	}
	fsync(img_fd);

	CHECK_RC(ext2fs_get_mem(img_size, &orig_img), 0, "alloc orig_img");
	CHECK_RC(ext2fs_get_mem(img_size, &cur_img), 0, "alloc cur_img");
	CHECK(pread(img_fd, orig_img, img_size, 0) == (ssize_t) img_size,
	      "snapshot orig_img");

	CHECK_RC(set_undo_io_backing_manager(unix_io_manager), 0,
		 "set_undo_io_backing_manager");
	CHECK_RC(set_undo_io_backup_file(undo_path), 0,
		 "set_undo_io_backup_file");

	CHECK_RC(undo_io_manager->open(NULL, IO_FLAG_RW, &io),
		 EXT2_ET_BAD_DEVICE_NAME, "undo_open NULL rejected");
	CHECK_RC(undo_io_manager->open(img_path, IO_FLAG_RW, &io), 0,
		 "undo_open RW");

	/* Blocksize and option validation */
	CHECK_RC(io_channel_set_blksize(io, 512), EXT2_ET_INVALID_ARGUMENT,
		 "blksize < 1024 rejected");
	CHECK_RC(io_channel_set_blksize(io, 2 * 1024 * 1024),
		 EXT2_ET_INVALID_ARGUMENT, "blksize > 1M rejected");
	CHECK_RC(io_channel_set_options(io, "tdb_data_size=512"),
		 EXT2_ET_INVALID_ARGUMENT, "tdb_data_size < 1024 rejected");
	CHECK_RC(io_channel_set_options(io, "tdb_data_size=bad"),
		 EXT2_ET_INVALID_ARGUMENT, "non-numeric tdb_data_size");
	CHECK_RC(io_channel_set_blksize(io, 1024), 0, "set_blksize 1024");

	/* Overwrite contiguous blocks 20, 21, 22 -> coalesces into 1 key! */
	for (i = 20; i <= 22; i++) {
		memset(blk, 0xaa, sizeof(blk));
		CHECK_RC(io_channel_write_blk64(io, i, 1, blk), 0,
			 "overwrite contiguous blk");
	}
	/* Overwrite block 21 a second time -> suppressed by written bitmap */
	memset(blk, 0xbb, sizeof(blk));
	CHECK_RC(io_channel_write_blk64(io, 21, 1, blk), 0,
		 "second overwrite of blk 21");

	/* write_byte on non-contiguous block 30 -> key #2 */
	CHECK_RC(io_channel_write_byte(io, 30 * 1024 + 64, 16,
				       "0123456789ABCDEF"), 0,
		 "undo_write_byte on blk 30");

	/* zeroout on block 35 -> key #3, discard on block 40 -> key #4 */
	CHECK_RC(io_channel_flush(io), 0, "undo_flush");
	rc = io_channel_zeroout(io, 35, 1);
	CHECK(rc == 0 || rc == EXT2_ET_UNIMPLEMENTED, "undo_zeroout");
	rc = io_channel_discard(io, 40, 1);
	CHECK(rc == 0 || rc == EXT2_ET_UNIMPLEMENTED, "undo_discard");
	CHECK_RC(io->manager->get_stats(io, &stats), 0, "undo_get_stats");
	CHECK(stats != NULL, "stats returned from backing manager");

	CHECK_RC(io_channel_close(io), 0, "close undo session 1");

	/* Reopen SAME undo file, re-write blk 20 (deduped) + new blk 50 (#5) */
	CHECK_RC(undo_io_manager->open(img_path, IO_FLAG_RW, &io), 0,
		 "reopen existing undo file");
	CHECK_RC(io_channel_set_blksize(io, 1024), 0, "set_blksize reopen");
	memset(blk, 0xcc, sizeof(blk));
	CHECK_RC(io_channel_write_blk64(io, 20, 1, blk), 0,
		 "overwrite blk 20 after reopen (deduped)");
	CHECK_RC(io_channel_write_blk64(io, 50, 1, blk), 0,
		 "overwrite new blk 50 after reopen");
	CHECK_RC(io_channel_close(io), 0, "close undo session 2");

	/* Attempting to pair undo_path with a different FS fails cleanly */
	CHECK_RC(undo_io_manager->open(other_path, IO_FLAG_RW, &io),
		 EXT2_ET_UNDO_FILE_WRONG,
		 "mismatched filesystem superblock rejected");

	/* Verify image was modified, then replay undo and check exact match */
	CHECK(pread(img_fd, cur_img, img_size, 0) == (ssize_t) img_size,
	      "read modified image");
	CHECK(memcmp(orig_img, cur_img, img_size) != 0,
	      "image differs before undo replay");

	CHECK(verify_and_replay_undo(undo_fd, img_fd, 5, &kb_seen) == 1,
	      "verify CRCs and replay 5 coalesced undo keys");
	CHECK(kb_seen == 1, "5 keys fit in 1 keyblock");

	CHECK(pread(img_fd, cur_img, img_size, 0) == (ssize_t) img_size,
	      "read restored image");
	CHECK(memcmp(orig_img, cur_img, img_size) == 0,
	      "image restored byte-for-byte to original state");

	/* Corrupt undo magic and verify EXT2_ET_UNDO_FILE_CORRUPT */
	CHECK(pwrite(undo_fd, "BADMAGIC", 8, 0) == 8, "corrupt undo magic");
	CHECK_RC(undo_io_manager->open(img_path, IO_FLAG_RW, &io),
		 EXT2_ET_UNDO_FILE_CORRUPT, "corrupt undo header rejected");

	ext2fs_free_mem(&cur_img);
	ext2fs_free_mem(&orig_img);
	close(other_fd);
	close(undo_fd);
	close(img_fd);
	unlink(other_path);
	unlink(undo_path);
	unlink(img_path);
	printf("  test_undo_coalesce_reopen_and_replay: ok\n");
}

/*
 * Suite 3: Multi-keyblock overflow (> 63 non-contiguous keys for 1K blocks)
 *          and offset= option (E2UNDO_FEATURE_COMPAT_FS_OFFSET).
 */
static void test_undo_multi_keyblock_and_offset(void)
{
	char			img_path[64], undo_path[64];
	unsigned char		*orig_img = NULL, *cur_img = NULL;
	unsigned char		blk[1024];
	struct tst_undo_header	hdr;
	int			img_fd, undo_fd, i, kb_seen = 0;
	io_channel		io = NULL;
	size_t			img_size = 256 * 1024;

	img_fd = make_temp_file(img_path, img_size);
	undo_fd = make_temp_file(undo_path, 0);
	init_ext2_image(img_path, 256, 0);

	for (i = 20; i < 180; i++) {
		memset(blk, (unsigned char) (i ^ 0x55), sizeof(blk));
		CHECK(pwrite(img_fd, blk, sizeof(blk), i * 1024) == 1024,
		      "seed multi-kb block");
	}
	fsync(img_fd);

	CHECK_RC(ext2fs_get_mem(img_size, &orig_img), 0, "alloc orig_img");
	CHECK_RC(ext2fs_get_mem(img_size, &cur_img), 0, "alloc cur_img");
	CHECK(pread(img_fd, orig_img, img_size, 0) == (ssize_t) img_size,
	      "snapshot orig_img");

	CHECK_RC(set_undo_io_backing_manager(unix_io_manager), 0,
		 "set backing");
	CHECK_RC(set_undo_io_backup_file(undo_path), 0, "set backup");
	CHECK_RC(undo_io_manager->open(img_path, IO_FLAG_RW, &io), 0,
		 "open undo RW");
	CHECK_RC(io_channel_set_blksize(io, 1024), 0, "set_blksize 1024");

	/*
	 * Write 70 non-contiguous blocks (20, 22, ..., 158) so no two
	 * adjacent writes coalesce; overflows keyblock 1 (63 keys) into
	 * keyblock 2 (7 keys).
	 */
	memset(blk, 0xff, sizeof(blk));
	for (i = 0; i < 70; i++)
		CHECK_RC(io_channel_write_blk64(io, 20 + i * 2, 1, blk), 0,
			 "write non-contiguous block");
	CHECK_RC(io_channel_close(io), 0, "close 70-key undo file");

	CHECK(verify_and_replay_undo(undo_fd, img_fd, 70, &kb_seen) == 1,
	      "verify and replay 70 keys across 2 keyblocks");
	CHECK(kb_seen == 2, "spanned 2 keyblocks (63 + 7 keys)");
	CHECK(pread(img_fd, cur_img, img_size, 0) == (ssize_t) img_size &&
	      memcmp(orig_img, cur_img, img_size) == 0,
	      "all 70 non-contiguous blocks restored identically");

	ext2fs_free_mem(&cur_img);
	ext2fs_free_mem(&orig_img);
	close(undo_fd);
	close(img_fd);
	unlink(undo_path);
	unlink(img_path);

	/* Test offset=4096 option recording E2UNDO_FEATURE_COMPAT_FS_OFFSET */
	img_fd = make_temp_file(img_path, img_size + 4096);
	undo_fd = make_temp_file(undo_path, 0);
	init_ext2_image(img_path, 256, 4096);

	CHECK_RC(set_undo_io_backup_file(undo_path), 0, "set backup offset");
	CHECK_RC(undo_io_manager->open(img_path, IO_FLAG_RW, &io), 0,
		 "open undo for offset test");
	CHECK_RC(io_channel_set_blksize(io, 1024), 0, "set_blksize 1024");
	CHECK_RC(io_channel_set_options(io, "offset=4096"), 0,
		 "set offset=4096");
	memset(blk, 0x33, sizeof(blk));
	CHECK_RC(io_channel_write_blk64(io, 20, 1, blk), 0,
		 "write blk 20 with offset=4096");
	CHECK_RC(io_channel_close(io), 0, "close offset undo");

	CHECK(pread(undo_fd, &hdr, sizeof(hdr), 0) == (ssize_t) sizeof(hdr),
	      "read offset undo header");
	CHECK((ext2fs_le32_to_cpu(hdr.f_compat) &
	       E2UNDO_FEATURE_COMPAT_FS_OFFSET) != 0 &&
	      ext2fs_le64_to_cpu(hdr.fs_offset) == 4096ULL,
	      "E2UNDO_FEATURE_COMPAT_FS_OFFSET and fs_offset=4096 recorded");

	close(undo_fd);
	close(img_fd);
	unlink(undo_path);
	unlink(img_path);
	printf("  test_undo_multi_keyblock_and_offset: ok\n");
}

/*
 * Suite 4: reopening an undo file must fail cleanly, without leaking
 *          the in-memory undo state, if a key block is corrupt or if
 *          the undo file can't be written.
 */
static void test_undo_reopen_errors(void)
{
	char			img_path[64], undo_path[64];
	unsigned char		blk[1024];
	struct tst_undo_header	hdr;
	int			img_fd, undo_fd;
	io_channel		io = NULL;
	size_t			img_size = 256 * 1024;
#if defined(RLIMIT_FSIZE) && defined(SIGXFSZ)
	struct rlimit		old_rlim, rlim;
	errcode_t		rc;
#endif

	img_fd = make_temp_file(img_path, img_size);
	undo_fd = make_temp_file(undo_path, 0);
	init_ext2_image(img_path, 256, 0);

	CHECK_RC(set_undo_io_backing_manager(unix_io_manager), 0,
		 "set backing");
	CHECK_RC(set_undo_io_backup_file(undo_path), 0, "set backup");
	CHECK_RC(undo_io_manager->open(img_path, IO_FLAG_RW, &io), 0,
		 "open undo RW");
	CHECK_RC(io_channel_set_blksize(io, 1024), 0, "set_blksize 1024");
	memset(blk, 0x5a, sizeof(blk));
	CHECK_RC(io_channel_write_blk64(io, 20, 1, blk), 0, "write blk 20");
	CHECK_RC(io_channel_close(io), 0, "close undo file");

	/* Corrupt the magic of the first key block */
	CHECK(pread(undo_fd, &hdr, sizeof(hdr), 0) == (ssize_t) sizeof(hdr),
	      "read undo header");
	CHECK(pwrite(undo_fd, "BADMAGIC", 8,
		     (off_t) (ext2fs_le64_to_cpu(hdr.key_offset) *
			      ext2fs_le32_to_cpu(hdr.block_size))) == 8,
	      "corrupt key block magic");
	io = NULL;
	CHECK_RC(undo_io_manager->open(img_path, IO_FLAG_RW, &io),
		 EXT2_ET_UNDO_FILE_CORRUPT, "corrupt key block rejected");
	CHECK(io == NULL, "no channel returned for a corrupt key block");

#if defined(RLIMIT_FSIZE) && defined(SIGXFSZ)
	/*
	 * Cut the undo file down to its header and superblock blocks, and
	 * limit the file size so that writing back the key block fails
	 * when the undo file is reopened.
	 */
	CHECK(ftruncate(undo_fd, 2 * 1024) == 0, "truncate undo file");
	signal(SIGXFSZ, SIG_IGN);
	CHECK(getrlimit(RLIMIT_FSIZE, &old_rlim) == 0, "getrlimit");
	rlim = old_rlim;
	rlim.rlim_cur = 2 * 1024;
	CHECK(setrlimit(RLIMIT_FSIZE, &rlim) == 0, "lower RLIMIT_FSIZE");
	rc = undo_io_manager->open(img_path, IO_FLAG_RW, &io);
	CHECK(setrlimit(RLIMIT_FSIZE, &old_rlim) == 0, "restore RLIMIT_FSIZE");
	signal(SIGXFSZ, SIG_DFL);
	CHECK_RC(rc, EXT2_ET_UNDO_FILE_CORRUPT,
		 "unwritable undo file rejected");
	CHECK(io == NULL, "no channel returned for an unwritable undo file");
#endif

	close(undo_fd);
	close(img_fd);
	unlink(undo_path);
	unlink(img_path);
	printf("  test_undo_reopen_errors: ok\n");
}

int main(int argc EXT2FS_ATTR((unused)), char *argv[] EXT2FS_ATTR((unused)))
{
	add_error_table(&et_ext2_error_table);
	printf("Running tst_undo_io unit tests...\n");

	test_undo_coalesce_reopen_and_replay();
	test_undo_multi_keyblock_and_offset();
	test_undo_reopen_errors();

	remove_error_table(&et_ext2_error_table);
	printf("tst_undo_io: %d checks run, %d failed\n",
	       test_count, fail_count);
	return fail_count ? 1 : 0;
}
