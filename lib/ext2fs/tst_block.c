/*
 * tst_block.c --- unit tests for block.c (ext2fs_block_iterate*)
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Library
 * General Public License, version 2.
 * %End-Header%
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif

#include "ext2_fs.h"
#include "ext2fs.h"

static int test_fail = 0;

/*
 * Lightweight in-memory io_manager with fault injection hooks
 */
static unsigned char *mem_disk;
static unsigned long long mem_disk_size;
static blk64_t fail_read_blk;
static blk64_t fail_write_blk;
static io_manager mem_io_manager;

static errcode_t mem_open(const char *name, int flags EXT2FS_ATTR((unused)),
			  io_channel *channel)
{
	io_channel	io = NULL;
	errcode_t	retval;

	if (!name)
		return EXT2_ET_BAD_DEVICE_NAME;
	retval = ext2fs_get_memzero(sizeof(struct struct_io_channel), &io);
	if (retval)
		return retval;
	io->magic = EXT2_ET_MAGIC_IO_CHANNEL;
	io->manager = mem_io_manager;
	retval = ext2fs_get_mem(strlen(name) + 1, &io->name);
	if (retval) {
		ext2fs_free_mem(&io);
		return retval;
	}
	strcpy(io->name, name);
	io->block_size = 1024;
	io->refcount = 1;
	*channel = io;
	return 0;
}

static errcode_t mem_close(io_channel channel)
{
	EXT2_CHECK_MAGIC(channel, EXT2_ET_MAGIC_IO_CHANNEL);
	if (--channel->refcount > 0)
		return 0;
	if (channel->name)
		ext2fs_free_mem(&channel->name);
	ext2fs_free_mem(&channel);
	return 0;
}

static errcode_t mem_set_blksize(io_channel channel, int blksize)
{
	EXT2_CHECK_MAGIC(channel, EXT2_ET_MAGIC_IO_CHANNEL);
	channel->block_size = blksize;
	return 0;
}

static errcode_t mem_read_blk64(io_channel channel, unsigned long long block,
				int count, void *buf)
{
	size_t			size;
	unsigned long long	offset;

	EXT2_CHECK_MAGIC(channel, EXT2_ET_MAGIC_IO_CHANNEL);
	if (fail_read_blk && block == fail_read_blk)
		return EIO;
	size = (count < 0) ? (size_t) (-count) :
		(size_t) count * channel->block_size;
	offset = block * channel->block_size;
	if (!mem_disk || offset >= mem_disk_size) {
		memset(buf, 0, size);
		return 0;
	}
	if (offset + size > mem_disk_size) {
		size_t avail = (size_t) (mem_disk_size - offset);

		memcpy(buf, mem_disk + offset, avail);
		memset((char *) buf + avail, 0, size - avail);
		return 0;
	}
	memcpy(buf, mem_disk + offset, size);
	return 0;
}

static errcode_t mem_write_blk64(io_channel channel, unsigned long long block,
				 int count, const void *buf)
{
	size_t			size;
	unsigned long long	offset;

	EXT2_CHECK_MAGIC(channel, EXT2_ET_MAGIC_IO_CHANNEL);
	if (fail_write_blk && block == fail_write_blk)
		return EIO;
	size = (count < 0) ? (size_t) (-count) :
		(size_t) count * channel->block_size;
	offset = block * channel->block_size;
	if (!mem_disk || offset + size > mem_disk_size)
		return EXT2_ET_LLSEEK_FAILED;
	memcpy(mem_disk + offset, buf, size);
	return 0;
}

static errcode_t mem_read_blk(io_channel channel, unsigned long block,
			      int count, void *buf)
{
	return mem_read_blk64(channel, block, count, buf);
}

static errcode_t mem_write_blk(io_channel channel, unsigned long block,
			       int count, const void *buf)
{
	return mem_write_blk64(channel, block, count, buf);
}

static errcode_t mem_flush(io_channel channel)
{
	EXT2_CHECK_MAGIC(channel, EXT2_ET_MAGIC_IO_CHANNEL);
	return 0;
}

static struct struct_io_manager struct_mem_manager = {
	.magic		= EXT2_ET_MAGIC_IO_MANAGER,
	.name		= "In-memory test I/O Manager",
	.open		= mem_open,
	.close		= mem_close,
	.set_blksize	= mem_set_blksize,
	.read_blk	= mem_read_blk,
	.write_blk	= mem_write_blk,
	.flush		= mem_flush,
	.read_blk64	= mem_read_blk64,
	.write_blk64	= mem_write_blk64,
};

static io_manager mem_io_manager = &struct_mem_manager;

static void setup_fs(ext2_filsys *ret_fs, unsigned int blocks,
		     unsigned int inodes, __u32 creator_os)
{
	struct ext2_super_block	param;
	errcode_t		retval;

	fail_read_blk = 0;
	fail_write_blk = 0;
	mem_disk_size = (unsigned long long) blocks * 1024;
	retval = ext2fs_get_memzero((size_t) mem_disk_size, &mem_disk);
	if (retval) {
		com_err("setup_fs", retval, "allocating mem_disk");
		exit(1);
	}

	memset(&param, 0, sizeof(param));
	ext2fs_blocks_count_set(&param, blocks);
	param.s_inodes_count = inodes;
	param.s_inode_size = 256;
	param.s_creator_os = creator_os;
	param.s_feature_incompat = EXT3_FEATURE_INCOMPAT_EXTENTS |
				   EXT4_FEATURE_INCOMPAT_INLINE_DATA |
				   EXT4_FEATURE_INCOMPAT_64BIT;
	param.s_desc_size = EXT2_MIN_DESC_SIZE_64BIT;

	retval = ext2fs_initialize("test_fs",
				   EXT2_FLAG_RW | EXT2_FLAG_64BITS |
				   EXT2_FLAG_EXCLUSIVE,
				   &param, mem_io_manager, ret_fs);
	if (retval) {
		com_err("setup_fs", retval, "initializing filesystem");
		exit(1);
	}
	(*ret_fs)->super->s_creator_os = creator_os;
	retval = ext2fs_allocate_tables(*ret_fs);
	if (retval) {
		com_err("setup_fs", retval, "allocating tables");
		exit(1);
	}
}

static void teardown_fs(ext2_filsys *fs)
{
	fail_read_blk = 0;
	fail_write_blk = 0;
	if (*fs)
		ext2fs_close_free(fs);
	if (mem_disk)
		ext2fs_free_mem(&mem_disk);
	mem_disk_size = 0;
}

/*
 * Callback trace recording & verification helpers
 */
#define MAX_ITER_ENTRIES 64

struct iter_entry {
	blk64_t		blk;
	e2_blkcnt_t	bcount;
	blk64_t		ref_blk;
	int		ref_offset;
};

struct iter_log {
	struct iter_entry	entries[MAX_ITER_ENTRIES];
	int			count;
	e2_blkcnt_t		abort_at_bcount;
	int			has_abort_at;
	blk64_t			abort_at_blk;
	int			has_abort_blk;
	e2_blkcnt_t		change_at_bcount;
	int			has_change_at;
	blk64_t			new_blk_val;
};

static void reset_log(struct iter_log *log)
{
	memset(log, 0, sizeof(*log));
}

static int record_iter_cb(ext2_filsys fs EXT2FS_ATTR((unused)),
			  blk64_t *blocknr, e2_blkcnt_t blockcnt,
			  blk64_t ref_blk, int ref_offset, void *priv_data)
{
	struct iter_log	*log = (struct iter_log *) priv_data;
	int		ret = 0;

	if (log->count < MAX_ITER_ENTRIES) {
		log->entries[log->count].blk = *blocknr;
		log->entries[log->count].bcount = blockcnt;
		log->entries[log->count].ref_blk = ref_blk;
		log->entries[log->count].ref_offset = ref_offset;
	}
	log->count++;

	if (log->has_change_at && blockcnt == log->change_at_bcount) {
		*blocknr = log->new_blk_val;
		ret |= BLOCK_CHANGED;
	}
	if (log->has_abort_at && blockcnt == log->abort_at_bcount)
		ret |= BLOCK_ABORT;
	if (log->has_abort_blk && *blocknr == log->abort_at_blk)
		ret |= BLOCK_ABORT;

	return ret;
}

static void check_iter_log(const char *test_name, const struct iter_log *log,
			   const struct iter_entry *exp, int exp_count)
{
	int	i;

	if (log->count != exp_count) {
		printf("FAIL [%s]: callback count %d, expected %d\n",
		       test_name, log->count, exp_count);
		test_fail++;
		return;
	}
	for (i = 0; i < exp_count; i++) {
		if (log->entries[i].blk != exp[i].blk ||
		    log->entries[i].bcount != exp[i].bcount ||
		    log->entries[i].ref_blk != exp[i].ref_blk ||
		    log->entries[i].ref_offset != exp[i].ref_offset) {
			printf("FAIL [%s] entry %d: got (blk=%llu, bcount=%lld, "
			       "ref_blk=%llu, ref_off=%d), expected (blk=%llu, "
			       "bcount=%lld, ref_blk=%llu, ref_off=%d)\n",
			       test_name, i,
			       (unsigned long long) log->entries[i].blk,
			       (long long) log->entries[i].bcount,
			       (unsigned long long) log->entries[i].ref_blk,
			       log->entries[i].ref_offset,
			       (unsigned long long) exp[i].blk,
			       (long long) exp[i].bcount,
			       (unsigned long long) exp[i].ref_blk,
			       exp[i].ref_offset);
			test_fail++;
			return;
		}
	}
}

static void check_err(const char *test_name, errcode_t actual,
		      errcode_t expected)
{
	if (actual != expected) {
		printf("FAIL [%s]: returned %ld (%s), expected %ld (%s)\n",
		       test_name, (long) actual, error_message(actual),
		       (long) expected, error_message(expected));
		test_fail++;
	}
}

/*
 * 1. Preconditions, magic checks, inline data, BLOCK_FLAG_NO_LARGE, block_buf
 */
static void test_preconditions(void)
{
	ext2_filsys			fs = NULL;
	struct struct_ext2_filsys	bad_fs;
	struct ext2_inode		inode;
	struct iter_log			log;
	char				*block_buf = NULL;
	char				*ind_buf = NULL;
	errcode_t			retval;
	static const struct iter_entry	exp_single[] = {
		{ 50, 0, 0, 0 },
	};
	static const struct iter_entry	exp_dir[] = {
		{ 51, 0, 0, 0 },
	};
	static const struct iter_entry	exp_buf[] = {
		{ 50, 0, 0, 0 },
		{ 61, 12, 60, 0 },
	};

	setup_fs(&fs, 256, 64, EXT2_OS_LINUX);

	/* Invalid ext2_filsys magic */
	memset(&bad_fs, 0, sizeof(bad_fs));
	reset_log(&log);
	retval = ext2fs_block_iterate3(&bad_fs, EXT2_ROOT_INO, 0, NULL,
				       record_iter_cb, &log);
	check_err("bad_fs_magic", retval, EXT2_ET_MAGIC_EXT2FS_FILSYS);

	/* Invalid inode numbers */
	retval = ext2fs_block_iterate3(fs, 0, 0, NULL, record_iter_cb, &log);
	check_err("invalid_ino_0", retval, EXT2_ET_BAD_INODE_NUM);

	retval = ext2fs_block_iterate3(fs, 9999, 0, NULL, record_iter_cb, &log);
	check_err("invalid_ino_9999", retval, EXT2_ET_BAD_INODE_NUM);

	/* Inode with EXT4_INLINE_DATA_FL */
	memset(&inode, 0, sizeof(inode));
	inode.i_mode = LINUX_S_IFREG | 0644;
	inode.i_flags = EXT4_INLINE_DATA_FL;
	inode.i_block[0] = 50;
	retval = ext2fs_write_inode(fs, 12, &inode);
	check_err("write_inline_inode", retval, 0);

	reset_log(&log);
	retval = ext2fs_block_iterate3(fs, 12, 0, NULL, record_iter_cb, &log);
	check_err("inline_data_cant_iterate", retval,
		  EXT2_ET_INLINE_DATA_CANT_ITERATE);
	check_iter_log("inline_data_no_cb", &log, NULL, 0);

	/* BLOCK_FLAG_NO_LARGE on regular file with i_size_high != 0 */
	memset(&inode, 0, sizeof(inode));
	inode.i_mode = LINUX_S_IFREG | 0644;
	inode.i_size_high = 1;
	inode.i_block[0] = 50;
	retval = ext2fs_write_inode(fs, 13, &inode);
	check_err("write_large_reg_inode", retval, 0);

	reset_log(&log);
	retval = ext2fs_block_iterate3(fs, 13, BLOCK_FLAG_NO_LARGE, NULL,
				       record_iter_cb, &log);
	check_err("no_large_reg_file_rejected", retval, EXT2_ET_FILE_TOO_BIG);
	check_iter_log("no_large_reg_file_no_cb", &log, NULL, 0);

	/* Regular file with i_size_high != 0 succeeds without BLOCK_FLAG_NO_LARGE */
	reset_log(&log);
	retval = ext2fs_block_iterate3(fs, 13, 0, NULL, record_iter_cb, &log);
	check_err("large_reg_file_allowed", retval, 0);
	check_iter_log("large_reg_file_cb", &log, exp_single, 1);

	/* Directory with i_size_high != 0 is allowed even with BLOCK_FLAG_NO_LARGE */
	memset(&inode, 0, sizeof(inode));
	inode.i_mode = LINUX_S_IFDIR | 0755;
	inode.i_size_high = 1;
	inode.i_block[0] = 51;
	retval = ext2fs_write_inode(fs, 14, &inode);
	check_err("write_large_dir_inode", retval, 0);

	reset_log(&log);
	retval = ext2fs_block_iterate3(fs, 14, BLOCK_FLAG_NO_LARGE, NULL,
				       record_iter_cb, &log);
	check_err("no_large_dir_allowed", retval, 0);
	check_iter_log("no_large_dir_cb", &log, exp_dir, 1);

	/* Caller-supplied block_buf (3 * blocksize) */
	retval = ext2fs_get_memzero(fs->blocksize, &ind_buf);
	check_err("alloc_ind_buf", retval, 0);
	((blk_t *) ind_buf)[0] = 61;
	retval = ext2fs_write_ind_block(fs, 60, ind_buf);
	check_err("write_ind_60", retval, 0);

	memset(&inode, 0, sizeof(inode));
	inode.i_mode = LINUX_S_IFREG | 0644;
	inode.i_block[0] = 50;
	inode.i_block[EXT2_IND_BLOCK] = 60;
	retval = ext2fs_write_inode(fs, 15, &inode);
	check_err("write_ino_15", retval, 0);

	retval = ext2fs_get_array(3, fs->blocksize, &block_buf);
	check_err("alloc_block_buf", retval, 0);
	memset(block_buf, 0xa5, 3 * fs->blocksize);

	reset_log(&log);
	retval = ext2fs_block_iterate3(fs, 15, BLOCK_FLAG_DATA_ONLY,
				       block_buf, record_iter_cb, &log);
	check_err("caller_block_buf_iter", retval, 0);
	check_iter_log("caller_block_buf_cb", &log, exp_buf, 2);
	if (((blk_t *) block_buf)[0] != 61) {
		printf("FAIL [caller_block_buf]: block_buf[0]=%u, expected 61\n",
		       ((blk_t *) block_buf)[0]);
		test_fail++;
	}

	ext2fs_free_mem(&block_buf);
	ext2fs_free_mem(&ind_buf);
	teardown_fs(&fs);
	printf("test_preconditions: completed\n");
}

/*
 * 2. HURD translator block iteration, mutation, abort, and RO violation
 */
static void test_hurd_translator(void)
{
	ext2_filsys		fs = NULL;
	struct ext2_inode	inode;
	struct iter_log		log;
	errcode_t		retval;
	static const struct iter_entry exp_data_only[] = {
		{ 46, 0, 0, 0 },
	};
	static const struct iter_entry exp_hurd_both[] = {
		{ 45, BLOCK_COUNT_TRANSLATOR, 0, 0 },
		{ 46, 0, 0, 0 },
	};
	static const struct iter_entry exp_hurd_abort[] = {
		{ 45, BLOCK_COUNT_TRANSLATOR, 0, 0 },
	};
	static const struct iter_entry exp_hurd_modified[] = {
		{ 47, BLOCK_COUNT_TRANSLATOR, 0, 0 },
		{ 46, 0, 0, 0 },
	};

	/* On EXT2_OS_LINUX, h_i_translator is ignored */
	setup_fs(&fs, 256, 64, EXT2_OS_LINUX);
	memset(&inode, 0, sizeof(inode));
	inode.i_mode = LINUX_S_IFREG | 0644;
	inode.osd1.hurd1.h_i_translator = 45;
	inode.i_block[0] = 46;
	retval = ext2fs_write_inode(fs, 12, &inode);
	check_err("write_linux_translator_ino", retval, 0);

	reset_log(&log);
	retval = ext2fs_block_iterate3(fs, 12, 0, NULL, record_iter_cb, &log);
	check_err("linux_ignores_translator", retval, 0);
	check_iter_log("linux_ignores_translator_cb", &log, exp_data_only, 1);
	teardown_fs(&fs);

	/* On EXT2_OS_HURD, test h_i_translator == 0 and h_i_translator != 0 */
	setup_fs(&fs, 256, 64, EXT2_OS_HURD);
	memset(&inode, 0, sizeof(inode));
	inode.i_mode = LINUX_S_IFREG | 0644;
	inode.osd1.hurd1.h_i_translator = 0;
	inode.i_block[0] = 46;
	retval = ext2fs_write_inode(fs, 12, &inode);
	check_err("write_hurd_zero_translator", retval, 0);

	reset_log(&log);
	retval = ext2fs_block_iterate3(fs, 12, 0, NULL, record_iter_cb, &log);
	check_err("hurd_zero_translator", retval, 0);
	check_iter_log("hurd_zero_translator_cb", &log, exp_data_only, 1);

	/* Set h_i_translator = 45 */
	inode.osd1.hurd1.h_i_translator = 45;
	retval = ext2fs_write_inode(fs, 12, &inode);
	check_err("write_hurd_translator_45", retval, 0);

	reset_log(&log);
	retval = ext2fs_block_iterate3(fs, 12, 0, NULL, record_iter_cb, &log);
	check_err("hurd_translator_visited", retval, 0);
	check_iter_log("hurd_translator_visited_cb", &log, exp_hurd_both, 2);

	/* BLOCK_FLAG_DATA_ONLY skips HURD translator block */
	reset_log(&log);
	retval = ext2fs_block_iterate3(fs, 12, BLOCK_FLAG_DATA_ONLY, NULL,
				       record_iter_cb, &log);
	check_err("hurd_translator_data_only", retval, 0);
	check_iter_log("hurd_translator_data_only_cb", &log, exp_data_only, 1);

	/* BLOCK_ABORT on HURD translator stops before i_block[0] */
	reset_log(&log);
	log.has_abort_at = 1;
	log.abort_at_bcount = BLOCK_COUNT_TRANSLATOR;
	retval = ext2fs_block_iterate3(fs, 12, 0, NULL, record_iter_cb, &log);
	check_err("hurd_translator_abort", retval, 0);
	check_iter_log("hurd_translator_abort_cb", &log, exp_hurd_abort, 1);

	/* BLOCK_FLAG_READ_ONLY + BLOCK_CHANGED on HURD translator */
	reset_log(&log);
	log.has_change_at = 1;
	log.change_at_bcount = BLOCK_COUNT_TRANSLATOR;
	log.new_blk_val = 47;
	retval = ext2fs_block_iterate3(fs, 12, BLOCK_FLAG_READ_ONLY, NULL,
				       record_iter_cb, &log);
	check_err("hurd_translator_ro_violation", retval,
		  EXT2_ET_RO_BLOCK_ITERATE);

	/* Reset inode translator to 45 and test valid RW mutation -> 47 */
	inode.osd1.hurd1.h_i_translator = 45;
	retval = ext2fs_write_inode(fs, 12, &inode);
	check_err("reset_hurd_translator_45", retval, 0);

	reset_log(&log);
	log.has_change_at = 1;
	log.change_at_bcount = BLOCK_COUNT_TRANSLATOR;
	log.new_blk_val = 47;
	retval = ext2fs_block_iterate3(fs, 12, 0, NULL, record_iter_cb, &log);
	check_err("hurd_translator_modify", retval, 0);

	reset_log(&log);
	retval = ext2fs_block_iterate3(fs, 12, 0, NULL, record_iter_cb, &log);
	check_err("hurd_translator_verify_modified", retval, 0);
	check_iter_log("hurd_translator_verify_cb", &log, exp_hurd_modified, 2);

	teardown_fs(&fs);
	printf("test_hurd_translator: completed\n");
}

/*
 * 3. Indirect block map traversal order (pre-order, post-order, data-only,
 *    and sparse IND/DIND/TIND bcount advancement)
 */
static void test_indirect_traversal(void)
{
	ext2_filsys		fs = NULL;
	struct ext2_inode	inode;
	struct iter_log		log;
	blk_t			*buf = NULL;
	errcode_t		retval;

	/*
	 * With blocksize = 1024, limit = 256:
	 * - Direct blocks: 0..11
	 * - IND (100): bcount 12..267
	 *   - ind[0] = 101 (bcount = 12, ref_blk = 100, ref_off = 0)
	 *   - ind[3] = 104 (bcount = 15, ref_blk = 100, ref_off = 12)
	 * - DIND (110): bcount 268..65803
	 *   - dind[0] = 111 (IND block, ref_blk = 110, ref_off = 0)
	 *     - dind_ind[2] = 112 (bcount = 268 + 2 = 270, ref_blk = 111, ref_off = 8)
	 *   - dind[1..255] = 0 (skips remaining 255 * 256 blocks)
	 * - TIND (120): bcount 65804..
	 *   - tind[0] = 121 (DIND block, ref_blk = 120, ref_off = 0)
	 *     - tind_dind[0] = 122 (IND block, ref_blk = 121, ref_off = 0)
	 *       - tind_ind[4] = 123 (bcount = 65804 + 4 = 65808, ref_blk = 122, ref_off = 16)
	 */
	static const struct iter_entry exp_preorder[] = {
		{ 90, 0, 0, 0 },
		{ 92, 2, 0, 2 },
		{ 100, BLOCK_COUNT_IND, 0, EXT2_IND_BLOCK },
		{ 101, 12, 100, 0 },
		{ 104, 15, 100, 12 },
		{ 110, BLOCK_COUNT_DIND, 0, EXT2_DIND_BLOCK },
		{ 111, BLOCK_COUNT_IND, 110, 0 },
		{ 112, 270, 111, 8 },
		{ 120, BLOCK_COUNT_TIND, 0, EXT2_TIND_BLOCK },
		{ 121, BLOCK_COUNT_DIND, 120, 0 },
		{ 122, BLOCK_COUNT_IND, 121, 0 },
		{ 123, 65808, 122, 16 },
	};
	static const struct iter_entry exp_postorder[] = {
		{ 90, 0, 0, 0 },
		{ 92, 2, 0, 2 },
		{ 101, 12, 100, 0 },
		{ 104, 15, 100, 12 },
		{ 100, BLOCK_COUNT_IND, 0, EXT2_IND_BLOCK },
		{ 112, 270, 111, 8 },
		{ 111, BLOCK_COUNT_IND, 110, 0 },
		{ 110, BLOCK_COUNT_DIND, 0, EXT2_DIND_BLOCK },
		{ 123, 65808, 122, 16 },
		{ 122, BLOCK_COUNT_IND, 121, 0 },
		{ 121, BLOCK_COUNT_DIND, 120, 0 },
		{ 120, BLOCK_COUNT_TIND, 0, EXT2_TIND_BLOCK },
	};
	static const struct iter_entry exp_data_only[] = {
		{ 90, 0, 0, 0 },
		{ 92, 2, 0, 2 },
		{ 101, 12, 100, 0 },
		{ 104, 15, 100, 12 },
		{ 112, 270, 111, 8 },
		{ 123, 65808, 122, 16 },
	};
	static const struct iter_entry exp_tind_only_data[] = {
		{ 123, 65808, 122, 16 },
	};

	setup_fs(&fs, 256, 64, EXT2_OS_LINUX);
	retval = ext2fs_get_memzero(fs->blocksize, &buf);
	check_err("alloc_ind_buf", retval, 0);

	/* Populate IND block 100 */
	memset(buf, 0, fs->blocksize);
	buf[0] = 101;
	buf[3] = 104;
	retval = ext2fs_write_ind_block(fs, 100, (char *) buf);
	check_err("write_blk_100", retval, 0);

	/* Populate DIND block 110 and child IND block 111 */
	memset(buf, 0, fs->blocksize);
	buf[0] = 111;
	retval = ext2fs_write_ind_block(fs, 110, (char *) buf);
	check_err("write_blk_110", retval, 0);

	memset(buf, 0, fs->blocksize);
	buf[2] = 112;
	retval = ext2fs_write_ind_block(fs, 111, (char *) buf);
	check_err("write_blk_111", retval, 0);

	/* Populate TIND block 120, child DIND 121, grandchild IND 122 */
	memset(buf, 0, fs->blocksize);
	buf[0] = 121;
	retval = ext2fs_write_ind_block(fs, 120, (char *) buf);
	check_err("write_blk_120", retval, 0);

	memset(buf, 0, fs->blocksize);
	buf[0] = 122;
	retval = ext2fs_write_ind_block(fs, 121, (char *) buf);
	check_err("write_blk_121", retval, 0);

	memset(buf, 0, fs->blocksize);
	buf[4] = 123;
	retval = ext2fs_write_ind_block(fs, 122, (char *) buf);
	check_err("write_blk_122", retval, 0);

	memset(&inode, 0, sizeof(inode));
	inode.i_mode = LINUX_S_IFREG | 0644;
	inode.i_block[0] = 90;
	inode.i_block[2] = 92;
	inode.i_block[EXT2_IND_BLOCK] = 100;
	inode.i_block[EXT2_DIND_BLOCK] = 110;
	inode.i_block[EXT2_TIND_BLOCK] = 120;
	retval = ext2fs_write_inode(fs, 12, &inode);
	check_err("write_indirect_ino_12", retval, 0);

	/* 3a: Pre-order traversal (flags = 0) */
	reset_log(&log);
	retval = ext2fs_block_iterate3(fs, 12, 0, NULL, record_iter_cb, &log);
	check_err("indirect_preorder", retval, 0);
	check_iter_log("indirect_preorder_cb", &log, exp_preorder, 12);

	/* 3b: Depth-traverse / post-order (flags = BLOCK_FLAG_DEPTH_TRAVERSE) */
	reset_log(&log);
	retval = ext2fs_block_iterate3(fs, 12, BLOCK_FLAG_DEPTH_TRAVERSE,
				       NULL, record_iter_cb, &log);
	check_err("indirect_postorder", retval, 0);
	check_iter_log("indirect_postorder_cb", &log, exp_postorder, 12);

	/* 3c: Data-only traversal (flags = BLOCK_FLAG_DATA_ONLY) */
	reset_log(&log);
	retval = ext2fs_block_iterate3(fs, 12, BLOCK_FLAG_DATA_ONLY,
				       NULL, record_iter_cb, &log);
	check_err("indirect_data_only", retval, 0);
	check_iter_log("indirect_data_only_cb", &log, exp_data_only, 6);

	/* 3d: Sparse i_block[EXT2_IND_BLOCK]=0 and i_block[EXT2_DIND_BLOCK]=0 */
	memset(&inode, 0, sizeof(inode));
	inode.i_mode = LINUX_S_IFREG | 0644;
	inode.i_block[EXT2_TIND_BLOCK] = 120;
	retval = ext2fs_write_inode(fs, 13, &inode);
	check_err("write_tind_only_ino_13", retval, 0);

	reset_log(&log);
	retval = ext2fs_block_iterate3(fs, 13, BLOCK_FLAG_DATA_ONLY,
				       NULL, record_iter_cb, &log);
	check_err("indirect_tind_only", retval, 0);
	check_iter_log("indirect_tind_only_cb", &log, exp_tind_only_data, 1);

	ext2fs_free_mem(&buf);
	teardown_fs(&fs);
	printf("test_indirect_traversal: completed\n");
}

/*
 * Callback for test_indirect_mutation_and_append
 */
static int remap_plus_10_cb(ext2_filsys fs EXT2FS_ATTR((unused)),
			    blk64_t *blocknr, e2_blkcnt_t blockcnt,
			    blk64_t ref_blk EXT2FS_ATTR((unused)),
			    int ref_offset EXT2FS_ATTR((unused)),
			    void *priv_data EXT2FS_ATTR((unused)))
{
	if (blockcnt >= 0) {
		*blocknr += 10;
		return BLOCK_CHANGED;
	}
	return 0;
}

static int zero_all_blocks_cb(ext2_filsys fs EXT2FS_ATTR((unused)),
			      blk64_t *blocknr,
			      e2_blkcnt_t blockcnt EXT2FS_ATTR((unused)),
			      blk64_t ref_blk EXT2FS_ATTR((unused)),
			      int ref_offset EXT2FS_ATTR((unused)),
			      void *priv_data EXT2FS_ATTR((unused)))
{
	*blocknr = 0;
	return BLOCK_CHANGED;
}

struct append_ind_plan {
	int	step;
};

/*
 * Allocates direct block 0 (70), leaves direct 1..11 as 0, allocates IND (71),
 * allocates IND[0] at bcount=12 (72), and aborts at bcount=13.
 */
static int append_ind_cb(ext2_filsys fs EXT2FS_ATTR((unused)),
			 blk64_t *blocknr, e2_blkcnt_t blockcnt,
			 blk64_t ref_blk, int ref_offset,
			 void *priv_data EXT2FS_ATTR((unused)))
{
	if (blockcnt == 0) {
		*blocknr = 70;
		return BLOCK_CHANGED;
	}
	if (blockcnt == BLOCK_COUNT_IND && ref_blk == 0 &&
	    ref_offset == EXT2_IND_BLOCK) {
		*blocknr = 71;
		return BLOCK_CHANGED;
	}
	if (blockcnt == 12) {
		*blocknr = 72;
		return BLOCK_CHANGED;
	}
	if (blockcnt == 13)
		return BLOCK_ABORT;
	return 0;
}

/*
 * Leaves direct 0..11, IND, and DIND unallocated (verifying bcount advances
 * by 256 for unallocated IND and 65536 for unallocated DIND), then allocates
 * TIND=80, DIND=81, IND=82, and data block=83 at bcount=65804 (12+256+65536)
 * before aborting.
 */
static int append_tind_cb(ext2_filsys fs EXT2FS_ATTR((unused)),
			  blk64_t *blocknr, e2_blkcnt_t blockcnt,
			  blk64_t ref_blk, int ref_offset,
			  void *priv_data EXT2FS_ATTR((unused)))
{
	if (blockcnt == BLOCK_COUNT_TIND && ref_blk == 0 &&
	    ref_offset == EXT2_TIND_BLOCK) {
		*blocknr = 80;
		return BLOCK_CHANGED;
	}
	if (blockcnt == BLOCK_COUNT_DIND && ref_blk == 80 && ref_offset == 0) {
		*blocknr = 81;
		return BLOCK_CHANGED;
	}
	if (blockcnt == BLOCK_COUNT_IND && ref_blk == 81 && ref_offset == 0) {
		*blocknr = 82;
		return BLOCK_CHANGED;
	}
	if (blockcnt == 65804 && ref_blk == 82 && ref_offset == 0) {
		*blocknr = 83;
		return BLOCK_CHANGED | BLOCK_ABORT;
	}
	return 0;
}

/*
 * 4. Indirect block map mutation, BLOCK_FLAG_APPEND, and BLOCK_ABORT
 */
static void test_indirect_mutation_and_append(void)
{
	ext2_filsys		fs = NULL;
	struct ext2_inode	inode;
	struct iter_log		log;
	blk_t			*buf = NULL;
	errcode_t		retval;
	static const struct iter_entry exp_remapped[] = {
		{ 100, 0, 0, 0 },
		{ 111, 12, 100, 0 },
		{ 122, 268, 111, 0 },
		{ 133, 65804, 122, 0 },
	};
	static const struct iter_entry exp_appended_ind[] = {
		{ 70, 0, 0, 0 },
		{ 71, BLOCK_COUNT_IND, 0, EXT2_IND_BLOCK },
		{ 72, 12, 71, 0 },
	};
	static const struct iter_entry exp_appended_tind[] = {
		{ 80, BLOCK_COUNT_TIND, 0, EXT2_TIND_BLOCK },
		{ 81, BLOCK_COUNT_DIND, 80, 0 },
		{ 82, BLOCK_COUNT_IND, 81, 0 },
		{ 83, 65804, 82, 0 },
	};
	static const struct iter_entry exp_abort_dind_data[] = {
		{ 100, 0, 0, 0 },
		{ 111, 12, 100, 0 },
		{ 100, BLOCK_COUNT_IND, 0, EXT2_IND_BLOCK },
		{ 122, 268, 111, 0 },
	};

	setup_fs(&fs, 256, 64, EXT2_OS_LINUX);
	retval = ext2fs_get_memzero(fs->blocksize, &buf);
	check_err("alloc_buf", retval, 0);

	/* Setup inode 12 with 1 direct, 1 IND, 1 DIND, 1 TIND data block */
	memset(buf, 0, fs->blocksize);
	buf[0] = 101;
	ext2fs_write_ind_block(fs, 100, (char *) buf);

	buf[0] = 111;
	ext2fs_write_ind_block(fs, 110, (char *) buf);
	buf[0] = 112;
	ext2fs_write_ind_block(fs, 111, (char *) buf);

	buf[0] = 121;
	ext2fs_write_ind_block(fs, 120, (char *) buf);
	buf[0] = 122;
	ext2fs_write_ind_block(fs, 121, (char *) buf);
	buf[0] = 123;
	ext2fs_write_ind_block(fs, 122, (char *) buf);

	memset(&inode, 0, sizeof(inode));
	inode.i_mode = LINUX_S_IFREG | 0644;
	inode.i_block[0] = 90;
	inode.i_block[EXT2_IND_BLOCK] = 100;
	inode.i_block[EXT2_DIND_BLOCK] = 110;
	inode.i_block[EXT2_TIND_BLOCK] = 120;
	retval = ext2fs_write_inode(fs, 12, &inode);
	check_err("write_ino_12", retval, 0);

	/* 4a: Remap all data blocks (+10) and verify on-disk persistence */
	retval = ext2fs_block_iterate3(fs, 12, 0, NULL, remap_plus_10_cb, NULL);
	check_err("remap_plus_10", retval, 0);

	reset_log(&log);
	retval = ext2fs_block_iterate3(fs, 12, BLOCK_FLAG_DATA_ONLY, NULL,
				       record_iter_cb, &log);
	check_err("verify_remap_plus_10", retval, 0);
	check_iter_log("verify_remap_plus_10_cb", &log, exp_remapped, 4);

	/* 4b: Abort inside DIND data block with BLOCK_FLAG_DEPTH_TRAVERSE */
	reset_log(&log);
	log.has_abort_at = 1;
	log.abort_at_bcount = 268;
	retval = ext2fs_block_iterate3(fs, 12, BLOCK_FLAG_DEPTH_TRAVERSE, NULL,
				       record_iter_cb, &log);
	check_err("abort_in_dind_depth_traverse", retval, 0);
	/* Post-order IND(111) and DIND(110) must NOT be visited after abort! */
	check_iter_log("abort_in_dind_depth_cb", &log, exp_abort_dind_data, 4);

	/* 4c: Zero all blocks in post-order (BLOCK_FLAG_DEPTH_TRAVERSE) */
	retval = ext2fs_block_iterate3(fs, 12, BLOCK_FLAG_DEPTH_TRAVERSE, NULL,
				       zero_all_blocks_cb, NULL);
	check_err("zero_all_blocks_postorder", retval, 0);

	reset_log(&log);
	retval = ext2fs_block_iterate3(fs, 12, 0, NULL, record_iter_cb, &log);
	check_err("verify_zero_all_blocks", retval, 0);
	check_iter_log("verify_zero_all_blocks_cb", &log, NULL, 0);

	/* 4d: Allocate direct + IND blocks on empty inode via BLOCK_FLAG_APPEND */
	memset(&inode, 0, sizeof(inode));
	inode.i_mode = LINUX_S_IFREG | 0644;
	retval = ext2fs_write_inode(fs, 13, &inode);
	check_err("write_empty_ino_13", retval, 0);

	retval = ext2fs_block_iterate3(fs, 13, BLOCK_FLAG_APPEND, NULL,
				       append_ind_cb, NULL);
	check_err("append_ind_ino_13", retval, 0);

	reset_log(&log);
	retval = ext2fs_block_iterate3(fs, 13, 0, NULL, record_iter_cb, &log);
	check_err("verify_append_ind", retval, 0);
	check_iter_log("verify_append_ind_cb", &log, exp_appended_ind, 3);

	/* 4e: Allocate TIND hierarchy via BLOCK_FLAG_APPEND skipping IND & DIND */
	memset(&inode, 0, sizeof(inode));
	inode.i_mode = LINUX_S_IFREG | 0644;
	retval = ext2fs_write_inode(fs, 14, &inode);
	check_err("write_empty_ino_14", retval, 0);

	retval = ext2fs_block_iterate3(fs, 14, BLOCK_FLAG_APPEND, NULL,
				       append_tind_cb, NULL);
	check_err("append_tind_ino_14", retval, 0);

	reset_log(&log);
	retval = ext2fs_block_iterate3(fs, 14, 0, NULL, record_iter_cb, &log);
	check_err("verify_append_tind", retval, 0);
	check_iter_log("verify_append_tind_cb", &log, exp_appended_tind, 4);

	ext2fs_free_mem(&buf);
	teardown_fs(&fs);
	printf("test_indirect_mutation_and_append: completed\n");
}

/*
 * 5. Indirect block map error paths: BAD_IND/DIND/TIND_BLOCK,
 *    RO_BLOCK_ITERATE, and I/O read/write errors
 */
static void test_indirect_errors(void)
{
	ext2_filsys		fs = NULL;
	struct ext2_inode	inode;
	struct iter_log		log;
	blk_t			*buf = NULL;
	errcode_t		retval;

	setup_fs(&fs, 256, 64, EXT2_OS_LINUX);
	retval = ext2fs_get_memzero(fs->blocksize, &buf);
	check_err("alloc_buf", retval, 0);

	/* 5a: Out-of-bounds IND, DIND, and TIND (>= blocks_count and < s_first_data_block) */
	fs->super->s_first_data_block = 2;

	memset(&inode, 0, sizeof(inode));
	inode.i_mode = LINUX_S_IFREG | 0644;
	inode.i_block[EXT2_IND_BLOCK] = 500;
	ext2fs_write_inode(fs, 12, &inode);
	reset_log(&log);
	retval = ext2fs_block_iterate3(fs, 12, 0, NULL, record_iter_cb, &log);
	check_err("bad_ind_too_high", retval, EXT2_ET_BAD_IND_BLOCK);

	inode.i_block[EXT2_IND_BLOCK] = 1;
	ext2fs_write_inode(fs, 12, &inode);
	retval = ext2fs_block_iterate3(fs, 12, 0, NULL, record_iter_cb, &log);
	check_err("bad_ind_below_first_data", retval, EXT2_ET_BAD_IND_BLOCK);

	memset(&inode, 0, sizeof(inode));
	inode.i_mode = LINUX_S_IFREG | 0644;
	inode.i_block[EXT2_DIND_BLOCK] = 500;
	ext2fs_write_inode(fs, 12, &inode);
	retval = ext2fs_block_iterate3(fs, 12, 0, NULL, record_iter_cb, &log);
	check_err("bad_dind_too_high", retval, EXT2_ET_BAD_DIND_BLOCK);

	inode.i_block[EXT2_DIND_BLOCK] = 1;
	ext2fs_write_inode(fs, 12, &inode);
	retval = ext2fs_block_iterate3(fs, 12, 0, NULL, record_iter_cb, &log);
	check_err("bad_dind_below_first_data", retval, EXT2_ET_BAD_DIND_BLOCK);

	memset(&inode, 0, sizeof(inode));
	inode.i_mode = LINUX_S_IFREG | 0644;
	inode.i_block[EXT2_TIND_BLOCK] = 500;
	ext2fs_write_inode(fs, 12, &inode);
	retval = ext2fs_block_iterate3(fs, 12, 0, NULL, record_iter_cb, &log);
	check_err("bad_tind_too_high", retval, EXT2_ET_BAD_TIND_BLOCK);

	inode.i_block[EXT2_TIND_BLOCK] = 1;
	ext2fs_write_inode(fs, 12, &inode);
	retval = ext2fs_block_iterate3(fs, 12, 0, NULL, record_iter_cb, &log);
	check_err("bad_tind_below_first_data", retval, EXT2_ET_BAD_TIND_BLOCK);

	fs->super->s_first_data_block = 1;

	/* 5b: Read-only violations (BLOCK_FLAG_READ_ONLY + BLOCK_CHANGED) */
	memset(buf, 0, fs->blocksize);
	buf[0] = 101;
	ext2fs_write_ind_block(fs, 100, (char *) buf);
	buf[0] = 111;
	ext2fs_write_ind_block(fs, 110, (char *) buf);
	buf[0] = 112;
	ext2fs_write_ind_block(fs, 111, (char *) buf);
	buf[0] = 121;
	ext2fs_write_ind_block(fs, 120, (char *) buf);
	buf[0] = 122;
	ext2fs_write_ind_block(fs, 121, (char *) buf);
	buf[0] = 123;
	ext2fs_write_ind_block(fs, 122, (char *) buf);

	memset(&inode, 0, sizeof(inode));
	inode.i_mode = LINUX_S_IFREG | 0644;
	inode.i_block[0] = 90;
	inode.i_block[EXT2_IND_BLOCK] = 100;
	inode.i_block[EXT2_DIND_BLOCK] = 110;
	inode.i_block[EXT2_TIND_BLOCK] = 120;
	ext2fs_write_inode(fs, 12, &inode);

	/* RO violation on direct block */
	reset_log(&log);
	log.has_change_at = 1;
	log.change_at_bcount = 0;
	log.new_blk_val = 99;
	retval = ext2fs_block_iterate3(fs, 12, BLOCK_FLAG_READ_ONLY, NULL,
				       record_iter_cb, &log);
	check_err("ro_violation_direct", retval, EXT2_ET_RO_BLOCK_ITERATE);

	/* RO violation on pre-order IND */
	ext2fs_write_inode(fs, 12, &inode);
	reset_log(&log);
	log.has_change_at = 1;
	log.change_at_bcount = BLOCK_COUNT_IND;
	log.new_blk_val = 105;
	retval = ext2fs_block_iterate3(fs, 12, BLOCK_FLAG_READ_ONLY, NULL,
				       record_iter_cb, &log);
	check_err("ro_violation_preorder_ind", retval,
		  EXT2_ET_RO_BLOCK_ITERATE);

	/* RO violation on IND data block */
	ext2fs_write_inode(fs, 12, &inode);
	reset_log(&log);
	log.has_change_at = 1;
	log.change_at_bcount = 12;
	log.new_blk_val = 105;
	retval = ext2fs_block_iterate3(fs, 12, BLOCK_FLAG_READ_ONLY, NULL,
				       record_iter_cb, &log);
	check_err("ro_violation_ind_data", retval, EXT2_ET_RO_BLOCK_ITERATE);

	/* RO violation on post-order IND */
	ext2fs_write_inode(fs, 12, &inode);
	reset_log(&log);
	log.has_change_at = 1;
	log.change_at_bcount = BLOCK_COUNT_IND;
	log.new_blk_val = 105;
	retval = ext2fs_block_iterate3(fs, 12,
				       BLOCK_FLAG_READ_ONLY |
				       BLOCK_FLAG_DEPTH_TRAVERSE,
				       NULL, record_iter_cb, &log);
	check_err("ro_violation_postorder_ind", retval,
		  EXT2_ET_RO_BLOCK_ITERATE);

	/* RO violation on pre-order DIND and post-order DIND */
	ext2fs_write_inode(fs, 12, &inode);
	reset_log(&log);
	log.has_change_at = 1;
	log.change_at_bcount = BLOCK_COUNT_DIND;
	log.new_blk_val = 115;
	retval = ext2fs_block_iterate3(fs, 12, BLOCK_FLAG_READ_ONLY, NULL,
				       record_iter_cb, &log);
	check_err("ro_violation_preorder_dind", retval,
		  EXT2_ET_RO_BLOCK_ITERATE);

	ext2fs_write_inode(fs, 12, &inode);
	reset_log(&log);
	log.has_change_at = 1;
	log.change_at_bcount = BLOCK_COUNT_DIND;
	log.new_blk_val = 115;
	retval = ext2fs_block_iterate3(fs, 12,
				       BLOCK_FLAG_READ_ONLY |
				       BLOCK_FLAG_DEPTH_TRAVERSE,
				       NULL, record_iter_cb, &log);
	check_err("ro_violation_postorder_dind", retval,
		  EXT2_ET_RO_BLOCK_ITERATE);

	/* RO violation on pre-order TIND and post-order TIND */
	ext2fs_write_inode(fs, 12, &inode);
	reset_log(&log);
	log.has_change_at = 1;
	log.change_at_bcount = BLOCK_COUNT_TIND;
	log.new_blk_val = 125;
	retval = ext2fs_block_iterate3(fs, 12, BLOCK_FLAG_READ_ONLY, NULL,
				       record_iter_cb, &log);
	check_err("ro_violation_preorder_tind", retval,
		  EXT2_ET_RO_BLOCK_ITERATE);

	ext2fs_write_inode(fs, 12, &inode);
	reset_log(&log);
	log.has_change_at = 1;
	log.change_at_bcount = BLOCK_COUNT_TIND;
	log.new_blk_val = 125;
	retval = ext2fs_block_iterate3(fs, 12,
				       BLOCK_FLAG_READ_ONLY |
				       BLOCK_FLAG_DEPTH_TRAVERSE,
				       NULL, record_iter_cb, &log);
	check_err("ro_violation_postorder_tind", retval,
		  EXT2_ET_RO_BLOCK_ITERATE);

	/* 5c: Simulated I/O read and write failures on IND, DIND, TIND */
	memset(&inode, 0, sizeof(inode));
	inode.i_mode = LINUX_S_IFREG | 0644;
	inode.i_block[EXT2_IND_BLOCK] = 100;
	ext2fs_write_inode(fs, 12, &inode);

	fail_read_blk = 100;
	reset_log(&log);
	retval = ext2fs_block_iterate3(fs, 12, 0, NULL, record_iter_cb, &log);
	check_err("io_read_err_ind", retval, EIO);
	fail_read_blk = 0;

	/* Write error on IND block 100 */
	fail_write_blk = 100;
	reset_log(&log);
	log.has_change_at = 1;
	log.change_at_bcount = 12;
	log.new_blk_val = 199;
	retval = ext2fs_block_iterate3(fs, 12, 0, NULL, record_iter_cb, &log);
	check_err("io_write_err_ind", retval, EIO);
	fail_write_blk = 0;

	memset(&inode, 0, sizeof(inode));
	inode.i_mode = LINUX_S_IFREG | 0644;
	inode.i_block[EXT2_DIND_BLOCK] = 110;
	ext2fs_write_inode(fs, 12, &inode);

	fail_read_blk = 110;
	retval = ext2fs_block_iterate3(fs, 12, 0, NULL, record_iter_cb, &log);
	check_err("io_read_err_dind", retval, EIO);
	fail_read_blk = 0;

	/* Write error on DIND block 110 when child IND pointer is modified */
	fail_write_blk = 110;
	reset_log(&log);
	log.has_change_at = 1;
	log.change_at_bcount = BLOCK_COUNT_IND;
	log.new_blk_val = 111;
	retval = ext2fs_block_iterate3(fs, 12, 0, NULL, record_iter_cb, &log);
	check_err("io_write_err_dind", retval, EIO);
	fail_write_blk = 0;

	memset(&inode, 0, sizeof(inode));
	inode.i_mode = LINUX_S_IFREG | 0644;
	inode.i_block[EXT2_TIND_BLOCK] = 120;
	ext2fs_write_inode(fs, 12, &inode);

	fail_read_blk = 120;
	retval = ext2fs_block_iterate3(fs, 12, 0, NULL, record_iter_cb, &log);
	check_err("io_read_err_tind", retval, EIO);
	fail_read_blk = 0;

	/* Write error on TIND block 120 when child DIND pointer is modified */
	fail_write_blk = 120;
	reset_log(&log);
	log.has_change_at = 1;
	log.change_at_bcount = BLOCK_COUNT_DIND;
	log.new_blk_val = 121;
	retval = ext2fs_block_iterate3(fs, 12, 0, NULL, record_iter_cb, &log);
	check_err("io_write_err_tind", retval, EIO);
	fail_write_blk = 0;

	ext2fs_free_mem(&buf);
	teardown_fs(&fs);
	printf("test_indirect_errors: completed\n");
}

struct extent_append_ctx {
	blk64_t	next_pblk;
	int	remaining;
	int	abort_on_last;
};

static int extent_append_cb(ext2_filsys fs EXT2FS_ATTR((unused)),
			    blk64_t *blocknr,
			    e2_blkcnt_t blockcnt EXT2FS_ATTR((unused)),
			    blk64_t ref_blk EXT2FS_ATTR((unused)),
			    int ref_offset EXT2FS_ATTR((unused)),
			    void *priv_data)
{
	struct extent_append_ctx *ctx = (struct extent_append_ctx *) priv_data;

	if (*blocknr != 0)
		return 0;
	if (ctx->remaining <= 0)
		return 0;
	*blocknr = ctx->next_pblk++;
	ctx->remaining--;
	if (ctx->remaining == 0 && ctx->abort_on_last)
		return BLOCK_CHANGED | BLOCK_ABORT;
	return BLOCK_CHANGED;
}

static int modify_extent_blocks_cb(ext2_filsys fs EXT2FS_ATTR((unused)),
				   blk64_t *blocknr, e2_blkcnt_t blockcnt,
				   blk64_t ref_blk EXT2FS_ATTR((unused)),
				   int ref_offset EXT2FS_ATTR((unused)),
				   void *priv_data EXT2FS_ATTR((unused)))
{
	if (blockcnt == 1) {
		*blocknr = 211;
		return BLOCK_CHANGED;
	}
	if (blockcnt == 4) {
		*blocknr = 214;
		return BLOCK_CHANGED;
	}
	return 0;
}

/*
 * 6. Extent-mapped inode iteration (multi-level tree, pre-order vs post-order
 *    vs data-only, initialized & uninitialized extent mutation, interior block
 *    replacement, BLOCK_FLAG_APPEND, BLOCK_ABORT, RO violation, corrupt header)
 */
static void test_extent_iteration(void)
{
	ext2_filsys		fs = NULL;
	struct ext2_inode	inode;
	ext2_extent_handle_t	handle = NULL;
	struct ext2fs_extent	extent;
	struct iter_log		log;
	struct extent_append_ctx app_ctx;
	blk64_t			interior_blk, pblk;
	int			ret_flags;
	char			*buf = NULL;
	errcode_t		retval;

	setup_fs(&fs, 512, 64, EXT2_OS_LINUX);

	/*
	 * 6a: Build a 2-level (depth = 1) extent tree on inode 13:
	 * - [0..1] -> [200..201] (initialized, len 2)
	 * - [4..5] -> [204..205] (uninitialized, len 2)
	 * - [8..8] -> [208]      (initialized, len 1)
	 * - [10..10] -> [210]    (initialized, len 1)
	 * - [12..12] -> [212]    (initialized, len 1, splits root into depth 1)
	 */
	memset(&inode, 0, sizeof(inode));
	inode.i_mode = LINUX_S_IFREG | 0644;
	retval = ext2fs_write_inode(fs, 13, &inode);
	check_err("write_ino_13", retval, 0);

	retval = ext2fs_extent_open(fs, 13, &handle);
	check_err("extent_open_13", retval, 0);

	memset(&extent, 0, sizeof(extent));
	extent.e_lblk = 0;
	extent.e_len = 2;
	extent.e_pblk = 200;
	extent.e_flags = EXT2_EXTENT_FLAGS_LEAF;
	retval = ext2fs_extent_insert(handle, EXT2_EXTENT_INSERT_AFTER, &extent);
	check_err("insert_ext_0", retval, 0);

	extent.e_lblk = 4;
	extent.e_len = 2;
	extent.e_pblk = 204;
	extent.e_flags = EXT2_EXTENT_FLAGS_LEAF | EXT2_EXTENT_FLAGS_UNINIT;
	retval = ext2fs_extent_insert(handle, EXT2_EXTENT_INSERT_AFTER, &extent);
	check_err("insert_ext_4_uninit", retval, 0);

	extent.e_lblk = 8;
	extent.e_len = 1;
	extent.e_pblk = 208;
	extent.e_flags = EXT2_EXTENT_FLAGS_LEAF;
	retval = ext2fs_extent_insert(handle, EXT2_EXTENT_INSERT_AFTER, &extent);
	check_err("insert_ext_8", retval, 0);

	extent.e_lblk = 10;
	extent.e_len = 1;
	extent.e_pblk = 210;
	extent.e_flags = EXT2_EXTENT_FLAGS_LEAF;
	retval = ext2fs_extent_insert(handle, EXT2_EXTENT_INSERT_AFTER, &extent);
	check_err("insert_ext_10", retval, 0);

	extent.e_lblk = 12;
	extent.e_len = 1;
	extent.e_pblk = 212;
	extent.e_flags = EXT2_EXTENT_FLAGS_LEAF;
	retval = ext2fs_extent_insert(handle, EXT2_EXTENT_INSERT_AFTER, &extent);
	check_err("insert_ext_12_split", retval, 0);

	retval = ext2fs_extent_get(handle, EXT2_EXTENT_ROOT, &extent);
	check_err("get_extent_root", retval, 0);
	interior_blk = extent.e_pblk;
	ext2fs_extent_free(handle);

	{
		struct iter_entry exp_ext_preorder[] = {
			{ interior_blk, BLOCK_COUNT_IND, 0, 0 },
			{ 200, 0, 0, 0 },
			{ 201, 1, 0, 0 },
			{ 204, 4, 0, 0 },
			{ 205, 5, 0, 0 },
			{ 208, 8, 0, 0 },
			{ 210, 10, 0, 0 },
			{ 212, 12, 0, 0 },
		};
		struct iter_entry exp_ext_postorder[] = {
			{ 200, 0, 0, 0 },
			{ 201, 1, 0, 0 },
			{ 204, 4, 0, 0 },
			{ 205, 5, 0, 0 },
			{ 208, 8, 0, 0 },
			{ 210, 10, 0, 0 },
			{ 212, 12, 0, 0 },
			{ interior_blk, BLOCK_COUNT_IND, 0, 0 },
		};
		static const struct iter_entry exp_ext_data_only[] = {
			{ 200, 0, 0, 0 },
			{ 201, 1, 0, 0 },
			{ 204, 4, 0, 0 },
			{ 205, 5, 0, 0 },
			{ 208, 8, 0, 0 },
			{ 210, 10, 0, 0 },
			{ 212, 12, 0, 0 },
		};
		struct iter_entry exp_ext_abort_interior[] = {
			{ interior_blk, BLOCK_COUNT_IND, 0, 0 },
		};
		struct iter_entry exp_ext_abort_leaf[] = {
			{ interior_blk, BLOCK_COUNT_IND, 0, 0 },
			{ 200, 0, 0, 0 },
			{ 201, 1, 0, 0 },
		};

		/* Pre-order visits interior block first, then leaves */
		reset_log(&log);
		retval = ext2fs_block_iterate3(fs, 13, 0, NULL,
					       record_iter_cb, &log);
		check_err("extent_preorder", retval, 0);
		check_iter_log("extent_preorder_cb", &log, exp_ext_preorder, 8);

		/* Post-order (BLOCK_FLAG_DEPTH_TRAVERSE) visits leaves first */
		reset_log(&log);
		retval = ext2fs_block_iterate3(fs, 13,
					       BLOCK_FLAG_DEPTH_TRAVERSE,
					       NULL, record_iter_cb, &log);
		check_err("extent_postorder", retval, 0);
		check_iter_log("extent_postorder_cb", &log,
			       exp_ext_postorder, 8);

		/* BLOCK_FLAG_DATA_ONLY skips interior node */
		reset_log(&log);
		retval = ext2fs_block_iterate3(fs, 13, BLOCK_FLAG_DATA_ONLY,
					       NULL, record_iter_cb, &log);
		check_err("extent_data_only", retval, 0);
		check_iter_log("extent_data_only_cb", &log,
			       exp_ext_data_only, 7);

		/* BLOCK_ABORT on interior node */
		reset_log(&log);
		log.has_abort_at = 1;
		log.abort_at_bcount = BLOCK_COUNT_IND;
		retval = ext2fs_block_iterate3(fs, 13, 0, NULL,
					       record_iter_cb, &log);
		check_err("extent_abort_interior", retval, 0);
		check_iter_log("extent_abort_interior_cb", &log,
			       exp_ext_abort_interior, 1);

		/* BLOCK_ABORT on leaf block (bcount == 1) */
		reset_log(&log);
		log.has_abort_at = 1;
		log.abort_at_bcount = 1;
		retval = ext2fs_block_iterate3(fs, 13, 0, NULL,
					       record_iter_cb, &log);
		check_err("extent_abort_leaf", retval, 0);
		check_iter_log("extent_abort_leaf_cb", &log,
			       exp_ext_abort_leaf, 3);
	}

	/* 6c: RO violation on leaf extent block */
	reset_log(&log);
	log.has_change_at = 1;
	log.change_at_bcount = 0;
	log.new_blk_val = 250;
	retval = ext2fs_block_iterate3(fs, 13, BLOCK_FLAG_READ_ONLY, NULL,
				       record_iter_cb, &log);
	check_err("extent_ro_violation_leaf", retval,
		  EXT2_ET_RO_BLOCK_ITERATE);

	/* 6d: Modify interior node physical block via BLOCK_CHANGED */
	retval = ext2fs_get_mem(fs->blocksize, &buf);
	check_err("alloc_extent_copy_buf", retval, 0);
	retval = io_channel_read_blk64(fs->io, interior_blk, 1, buf);
	check_err("read_interior_blk", retval, 0);
	retval = io_channel_write_blk64(fs->io, 190, 1, buf);
	check_err("write_new_interior_blk_190", retval, 0);
	ext2fs_free_mem(&buf);

	reset_log(&log);
	log.has_change_at = 1;
	log.change_at_bcount = BLOCK_COUNT_IND;
	log.new_blk_val = 190;
	retval = ext2fs_block_iterate3(fs, 13, 0, NULL, record_iter_cb, &log);
	check_err("extent_replace_interior_blk", retval, 0);

	/* 6e: Modify initialized (bcount=1) and uninitialized (bcount=4) leaf blocks */
	retval = ext2fs_block_iterate3(fs, 13, BLOCK_FLAG_DATA_ONLY, NULL,
				       modify_extent_blocks_cb, NULL);
	check_err("extent_modify_init_and_uninit", retval, 0);

	/* Verify bmap2 preserves initialized vs uninitialized status */
	pblk = 0;
	ret_flags = 0;
	retval = ext2fs_bmap2(fs, 13, NULL, NULL, 0, 1, &ret_flags, &pblk);
	check_err("bmap2_lblk_1", retval, 0);
	if (pblk != 211 || (ret_flags & BMAP_RET_UNINIT) != 0) {
		printf("FAIL [extent_modify_init]: lblk 1 -> pblk=%llu "
		       "flags=0x%x, expected pblk=211 flags=0\n",
		       (unsigned long long) pblk, ret_flags);
		test_fail++;
	}

	pblk = 0;
	ret_flags = 0;
	retval = ext2fs_bmap2(fs, 13, NULL, NULL, 0, 4, &ret_flags, &pblk);
	check_err("bmap2_lblk_4", retval, 0);
	if (pblk != 214 || !(ret_flags & BMAP_RET_UNINIT)) {
		printf("FAIL [extent_modify_uninit]: lblk 4 -> pblk=%llu "
		       "flags=0x%x, expected pblk=214 with BMAP_RET_UNINIT\n",
		       (unsigned long long) pblk, ret_flags);
		test_fail++;
	}

	/* 6f: BLOCK_FLAG_APPEND on an extent-mapped inode */
	memset(&inode, 0, sizeof(inode));
	inode.i_mode = LINUX_S_IFREG | 0644;
	inode.i_flags = EXT4_EXTENTS_FL;
	retval = ext2fs_write_inode(fs, 14, &inode);
	check_err("write_extent_ino_14", retval, 0);

	retval = ext2fs_extent_open(fs, 14, &handle);
	check_err("extent_open_14", retval, 0);
	ext2fs_extent_free(handle);

	/* Append 3 blocks (300, 301, 302) to empty extent tree, stopping normally */
	app_ctx.next_pblk = 300;
	app_ctx.remaining = 3;
	app_ctx.abort_on_last = 0;
	retval = ext2fs_block_iterate3(fs, 14, BLOCK_FLAG_APPEND, NULL,
				       extent_append_cb, &app_ctx);
	check_err("extent_append_3_blks", retval, 0);

	/* Append 2 more blocks (303, 304) stopping via BLOCK_ABORT */
	app_ctx.next_pblk = 303;
	app_ctx.remaining = 2;
	app_ctx.abort_on_last = 1;
	retval = ext2fs_block_iterate3(fs, 14, BLOCK_FLAG_APPEND, NULL,
				       extent_append_cb, &app_ctx);
	check_err("extent_append_2_blks_abort", retval, 0);

	{
		static const struct iter_entry exp_ext_appended[] = {
			{ 300, 0, 0, 0 },
			{ 301, 1, 0, 0 },
			{ 302, 2, 0, 0 },
			{ 303, 3, 0, 0 },
			{ 304, 4, 0, 0 },
		};

		reset_log(&log);
		retval = ext2fs_block_iterate3(fs, 14, BLOCK_FLAG_DATA_ONLY,
					       NULL, record_iter_cb, &log);
		check_err("verify_extent_appended", retval, 0);
		check_iter_log("verify_extent_appended_cb", &log,
			       exp_ext_appended, 5);
	}

	/* RO violation on extent BLOCK_FLAG_APPEND */
	app_ctx.next_pblk = 305;
	app_ctx.remaining = 1;
	app_ctx.abort_on_last = 0;
	retval = ext2fs_block_iterate3(fs, 14,
				       BLOCK_FLAG_APPEND | BLOCK_FLAG_READ_ONLY,
				       NULL, extent_append_cb, &app_ctx);
	check_err("extent_append_ro_violation", retval,
		  EXT2_ET_RO_BLOCK_ITERATE);

	teardown_fs(&fs);
	printf("test_extent_iteration: completed\n");
}

/*
 * 7. Legacy wrappers: ext2fs_block_iterate2 and ext2fs_block_iterate
 */
static int iter2_cb(ext2_filsys fs EXT2FS_ATTR((unused)),
		    blk_t *blocknr, e2_blkcnt_t blockcnt,
		    blk_t ref_blk, int ref_offset, void *priv_data)
{
	struct iter_log *log = (struct iter_log *) priv_data;

	if (log->count < MAX_ITER_ENTRIES) {
		log->entries[log->count].blk = *blocknr;
		log->entries[log->count].bcount = blockcnt;
		log->entries[log->count].ref_blk = ref_blk;
		log->entries[log->count].ref_offset = ref_offset;
	}
	log->count++;
	if (log->has_change_at && blockcnt == log->change_at_bcount) {
		*blocknr = (blk_t) log->new_blk_val;
		return BLOCK_CHANGED;
	}
	return 0;
}

static int iter1_cb(ext2_filsys fs EXT2FS_ATTR((unused)),
		    blk_t *blocknr, int bcount, void *priv_data)
{
	struct iter_log *log = (struct iter_log *) priv_data;

	if (log->count < MAX_ITER_ENTRIES) {
		log->entries[log->count].blk = *blocknr;
		log->entries[log->count].bcount = bcount;
		log->entries[log->count].ref_blk = 0;
		log->entries[log->count].ref_offset = 0;
	}
	log->count++;
	if (log->has_change_at && bcount == (int) log->change_at_bcount) {
		*blocknr = (blk_t) log->new_blk_val;
		return BLOCK_CHANGED;
	}
	return 0;
}

static void test_legacy_wrappers(void)
{
	ext2_filsys		fs = NULL;
	struct ext2_inode	inode;
	struct iter_log		log;
	blk_t			*buf = NULL;
	errcode_t		retval;
	static const struct iter_entry exp_iter2[] = {
		{ 50, 0, 0, 0 },
		{ 60, BLOCK_COUNT_IND, 0, EXT2_IND_BLOCK },
		{ 61, 12, 60, 0 },
	};
	static const struct iter_entry exp_iter1_after_iter2[] = {
		{ 50, 0, 0, 0 },
		{ 60, BLOCK_COUNT_IND, 0, 0 },
		{ 65, 12, 0, 0 },
	};
	static const struct iter_entry exp_after_iter1[] = {
		{ 55, 0, 0, 0 },
		{ 60, BLOCK_COUNT_IND, 0, EXT2_IND_BLOCK },
		{ 65, 12, 60, 0 },
	};

	setup_fs(&fs, 256, 64, EXT2_OS_LINUX);
	retval = ext2fs_get_memzero(fs->blocksize, &buf);
	check_err("alloc_buf", retval, 0);

	buf[0] = 61;
	retval = ext2fs_write_ind_block(fs, 60, (char *) buf);
	check_err("write_ind_60", retval, 0);

	memset(&inode, 0, sizeof(inode));
	inode.i_mode = LINUX_S_IFREG | 0644;
	inode.i_block[0] = 50;
	inode.i_block[EXT2_IND_BLOCK] = 60;
	retval = ext2fs_write_inode(fs, 12, &inode);
	check_err("write_ino_12", retval, 0);

	/* Test ext2fs_block_iterate2 traversal and mutation (61 -> 65) */
	reset_log(&log);
	log.has_change_at = 1;
	log.change_at_bcount = 12;
	log.new_blk_val = 65;
	retval = ext2fs_block_iterate2(fs, 12, 0, NULL, iter2_cb, &log);
	check_err("block_iterate2", retval, 0);
	check_iter_log("block_iterate2_cb", &log, exp_iter2, 3);

	/* Test ext2fs_block_iterate traversal and mutation (50 -> 55) */
	reset_log(&log);
	log.has_change_at = 1;
	log.change_at_bcount = 0;
	log.new_blk_val = 55;
	retval = ext2fs_block_iterate(fs, 12, 0, NULL, iter1_cb, &log);
	check_err("block_iterate", retval, 0);
	check_iter_log("block_iterate_cb", &log, exp_iter1_after_iter2, 3);

	/* Verify both mutations persisted via ext2fs_block_iterate3 */
	reset_log(&log);
	retval = ext2fs_block_iterate3(fs, 12, 0, NULL, record_iter_cb, &log);
	check_err("verify_legacy_mutations", retval, 0);
	check_iter_log("verify_legacy_mutations_cb", &log, exp_after_iter1, 3);

	/*
	 * Verify ext2fs_block_iterate implicitly sets BLOCK_FLAG_NO_LARGE,
	 * rejecting a regular file with i_size_high != 0 while
	 * ext2fs_block_iterate2 allows it when flags == 0.
	 */
	inode.i_size_high = 1;
	retval = ext2fs_write_inode(fs, 12, &inode);
	check_err("write_large_ino_12", retval, 0);

	reset_log(&log);
	retval = ext2fs_block_iterate(fs, 12, 0, NULL, iter1_cb, &log);
	check_err("block_iterate_rejects_large", retval, EXT2_ET_FILE_TOO_BIG);

	reset_log(&log);
	retval = ext2fs_block_iterate2(fs, 12, 0, NULL, iter2_cb, &log);
	check_err("block_iterate2_allows_large", retval, 0);

	ext2fs_free_mem(&buf);
	teardown_fs(&fs);
	printf("test_legacy_wrappers: completed\n");
}

int main(int argc EXT2FS_ATTR((unused)), char *argv[] EXT2FS_ATTR((unused)))
{
	add_error_table(&et_ext2_error_table);

	test_preconditions();
	test_hurd_translator();
	test_indirect_traversal();
	test_indirect_mutation_and_append();
	test_indirect_errors();
	test_extent_iteration();
	test_legacy_wrappers();

	if (test_fail == 0)
		printf("libext2fs block_iterate unit tests passed!\n");
	else
		printf("libext2fs block_iterate unit tests FAILED (%d errors)\n",
		       test_fail);

	return test_fail ? 1 : 0;
}
