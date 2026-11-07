/*
 * tst_fileio.c --- unit tests for fileio.c (ext2fs_file_* APIs)
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
#include "ext2fsP.h"

static int test_fail = 0;

static unsigned char *mem_disk;
static unsigned long long mem_disk_size;
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

static void check_err(const char *label, errcode_t got, errcode_t expected)
{
	if (got != expected) {
		printf("FAIL [%s]: got errcode %ld (%s), expected %ld (%s)\n",
		       label, (long) got, error_message(got),
		       (long) expected, error_message(expected));
		test_fail++;
	}
}

static void check_u64(const char *label, unsigned long long got,
		      unsigned long long expected)
{
	if (got != expected) {
		printf("FAIL [%s]: got %llu, expected %llu\n",
		       label, got, expected);
		test_fail++;
	}
}

static void setup_fs(ext2_filsys *ret_fs, unsigned int blocks,
		     unsigned int inodes)
{
	struct ext2_super_block	param;
	errcode_t		retval;

	mem_disk_size = (unsigned long long) blocks * 1024ULL;
	retval = ext2fs_get_memzero((size_t) mem_disk_size, &mem_disk);
	if (retval) {
		com_err("setup_fs", retval, "allocating mem_disk");
		exit(1);
	}

	memset(&param, 0, sizeof(param));
	ext2fs_blocks_count_set(&param, blocks);
	param.s_inodes_count = inodes;
	param.s_rev_level = EXT2_DYNAMIC_REV;
	param.s_inode_size = 256;
	param.s_feature_compat = EXT2_FEATURE_COMPAT_EXT_ATTR;
	param.s_feature_incompat = EXT3_FEATURE_INCOMPAT_EXTENTS |
				   EXT4_FEATURE_INCOMPAT_INLINE_DATA |
				   EXT4_FEATURE_INCOMPAT_64BIT;
	param.s_feature_ro_compat = EXT4_FEATURE_RO_COMPAT_HUGE_FILE |
				    EXT2_FEATURE_RO_COMPAT_LARGE_FILE;
	param.s_desc_size = EXT2_MIN_DESC_SIZE_64BIT;

	retval = ext2fs_initialize("test_fs",
				   EXT2_FLAG_RW | EXT2_FLAG_64BITS |
				   EXT2_FLAG_EXCLUSIVE,
				   &param, mem_io_manager, ret_fs);
	if (retval) {
		com_err("setup_fs", retval, "initializing filesystem");
		exit(1);
	}
	retval = ext2fs_allocate_tables(*ret_fs);
	if (retval) {
		com_err("setup_fs", retval, "allocating tables");
		exit(1);
	}
}

static void teardown_fs(ext2_filsys *fs)
{
	ext2fs_zero_blocks2(NULL, 0, 0, NULL, NULL);
	if (*fs)
		ext2fs_close_free(fs);
	if (mem_disk)
		ext2fs_free_mem(&mem_disk);
	mem_disk_size = 0;
}

static void init_reg_inode(ext2_filsys fs, ext2_ino_t ino, __u32 flags,
			   struct ext2_inode *inode)
{
	errcode_t	retval;

	memset(inode, 0, sizeof(*inode));
	inode->i_mode = LINUX_S_IFREG | 0644;
	inode->i_links_count = 1;
	inode->i_flags = flags;
	ext2fs_inode_alloc_stats2(fs, ino, +1, 0);
	retval = ext2fs_write_new_inode(fs, ino, inode);
	check_err("write_new_inode", retval, 0);
	if (flags & EXT4_EXTENTS_FL) {
		ext2_extent_handle_t handle;

		retval = ext2fs_extent_open2(fs, ino, inode, &handle);
		check_err("extent_open2_init", retval, 0);
		ext2fs_extent_free(handle);
		retval = ext2fs_write_inode(fs, ino, inode);
		check_err("write_extent_inode", retval, 0);
	}
}

/*
 * Suite 1: Open flags, accessors, and read-only enforcement
 */
static void test_open_accessors_and_ro(void)
{
	ext2_filsys		fs = NULL;
	struct ext2_inode	inode, *inop;
	ext2_file_t		file = NULL;
	unsigned int		written = 0;
	errcode_t		retval;

	setup_fs(&fs, 512, 64);
	init_reg_inode(fs, 12, EXT4_EXTENTS_FL, &inode);

	/* Invalid inode number when inode == NULL */
	retval = ext2fs_file_open(fs, 0, 0, &file);
	check_err("open_bad_ino", retval, EXT2_ET_BAD_INODE_NUM);

	/* Read-only filesystem rejects EXT2_FILE_WRITE and EXT2_FILE_CREATE */
	fs->flags &= ~EXT2_FLAG_RW;
	retval = ext2fs_file_open(fs, 12, EXT2_FILE_WRITE, &file);
	check_err("open_write_on_ro_fs", retval, EXT2_ET_RO_FILSYS);
	retval = ext2fs_file_open(fs, 12, EXT2_FILE_CREATE, &file);
	check_err("open_create_on_ro_fs", retval, EXT2_ET_RO_FILSYS);

	/* Read-only open on RO fs succeeds */
	retval = ext2fs_file_open(fs, 12, 0, &file);
	check_err("open_ro_on_ro_fs", retval, 0);
	check_u64("get_fs_matches", ext2fs_file_get_fs(file) == fs, 1);
	check_u64("get_ino_matches", ext2fs_file_get_inode_num(file), 12);
	inop = ext2fs_file_get_inode(file);
	check_u64("get_inode_nonnull", inop != NULL, 1);
	check_u64("get_inode_mode", inop->i_mode, LINUX_S_IFREG | 0644);

	/* Writing to a read-only file handle returns EXT2_ET_FILE_RO */
	fs->flags |= EXT2_FLAG_RW;
	retval = ext2fs_file_write(file, "abc", 3, &written);
	check_err("write_on_ro_handle", retval, EXT2_ET_FILE_RO);
	retval = ext2fs_file_close(file);
	check_err("close_ro_handle", retval, 0);

	/* ext2fs_file_open2 with caller-supplied struct ext2_inode */
	retval = ext2fs_file_open2(fs, 12, &inode, EXT2_FILE_WRITE, &file);
	check_err("open2_with_inode", retval, 0);
	retval = ext2fs_file_close(file);
	check_err("close_open2", retval, 0);

	teardown_fs(&fs);
	printf("test_open_accessors_and_ro: completed\n");
}

/*
 * Suite 2: Multi-block read/write, seeking (llseek/lseek), and sparse holes
 */
static void test_rw_seek_and_sparse_holes(void)
{
	ext2_filsys		fs = NULL;
	struct ext2_inode	inode;
	ext2_file_t		file = NULL;
	unsigned char		*wbuf = NULL, *rbuf = NULL;
	unsigned char		tail[300];
	unsigned int		written = 0, got = 0;
	__u64			pos64 = 0, lsize = 0;
	ext2_off_t		pos32 = 0;
	int			i;
	errcode_t		retval;

	setup_fs(&fs, 512, 64);
	init_reg_inode(fs, 12, EXT4_EXTENTS_FL, &inode);

	retval = ext2fs_get_mem(2500, &wbuf);
	check_err("alloc_wbuf", retval, 0);
	retval = ext2fs_get_memzero(6000, &rbuf);
	check_err("alloc_rbuf", retval, 0);

	for (i = 0; i < 2500; i++)
		wbuf[i] = (unsigned char) ((i * 7 + 3) & 0xff);
	for (i = 0; i < (int) sizeof(tail); i++)
		tail[i] = (unsigned char) (0xA0 + (i % 63));

	retval = ext2fs_file_open(fs, 12, EXT2_FILE_WRITE, &file);
	check_err("open_rw_12", retval, 0);

	/* Write 2500 bytes across 3 blocks (full + full + partial) */
	retval = ext2fs_file_write(file, wbuf, 2500, &written);
	check_err("write_2500", retval, 0);
	check_u64("written_2500", written, 2500);
	check_u64("get_size_2500", ext2fs_file_get_size(file), 2500);
	retval = ext2fs_file_get_lsize(file, &lsize);
	check_err("get_lsize_rc", retval, 0);
	check_u64("get_lsize_2500", lsize, 2500);

	/* Exercise llseek and lseek modes + invalid whence */
	retval = ext2fs_file_llseek(file, 100, 99, &pos64);
	check_err("llseek_bad_whence", retval, EXT2_ET_INVALID_ARGUMENT);

	retval = ext2fs_file_lseek(file, 500, EXT2_SEEK_SET, &pos32);
	check_err("lseek_set_500", retval, 0);
	check_u64("lseek_pos_500", pos32, 500);

	retval = ext2fs_file_llseek(file, 250, EXT2_SEEK_CUR, &pos64);
	check_err("llseek_cur_750", retval, 0);
	check_u64("llseek_pos_750", pos64, 750);

	/* Seek 2620 bytes past EOF (to 5120 -> block 5) leaving sparse hole */
	retval = ext2fs_file_llseek(file, 2620, EXT2_SEEK_END, &pos64);
	check_err("llseek_end_5120", retval, 0);
	check_u64("llseek_pos_5120", pos64, 5120);

	retval = ext2fs_file_write(file, tail, sizeof(tail), &written);
	check_err("write_tail_300", retval, 0);
	check_u64("written_tail_300", written, sizeof(tail));
	retval = ext2fs_file_close(file);
	check_err("close_after_sparse_write", retval, 0);

	/* Reopen read-only and read entire 5420-byte file */
	retval = ext2fs_file_open(fs, 12, 0, &file);
	check_err("reopen_ro_12", retval, 0);
	check_u64("reopen_size_5420", ext2fs_file_get_size(file), 5420);

	memset(rbuf, 0xEE, 6000);
	retval = ext2fs_file_read(file, rbuf, 6000, &got);
	check_err("read_all_5420", retval, 0);
	check_u64("read_got_5420", got, 5420);
	check_u64("read_prefix_match", memcmp(rbuf, wbuf, 2500) == 0, 1);
	check_u64("read_sparse_hole_zero",
		  ext2fs_mem_is_zero((char *) rbuf + 2500, 5120 - 2500), 1);
	check_u64("read_tail_match",
		  memcmp(rbuf + 5120, tail, sizeof(tail)) == 0, 1);

	/* Reading at EOF returns 0 bytes */
	retval = ext2fs_file_read(file, rbuf, 128, &got);
	check_err("read_at_eof", retval, 0);
	check_u64("read_at_eof_zero", got, 0);

	retval = ext2fs_file_close(file);
	check_err("close_ro_12", retval, 0);

	ext2fs_free_mem(&wbuf);
	ext2fs_free_mem(&rbuf);
	teardown_fs(&fs);
	printf("test_rw_seek_and_sparse_holes: completed\n");
}

/*
 * Suite 3: Uninitialized extent read/write conversion and truncation
 * (ext2fs_file_set_size2 / ext2fs_file_set_size)
 */
static void test_uninit_extent_and_truncate(void)
{
	ext2_filsys		fs = NULL;
	struct ext2_inode	inode;
	ext2_file_t		file = NULL;
	unsigned char		buf[4096];
	unsigned int		got = 0, written = 0;
	__u64			lsize = 0;
	blk64_t			pblk = 0, pblk1 = 0;
	int			ret_flags = 0;
	int			i;
	errcode_t		retval;

	setup_fs(&fs, 512, 64);
	init_reg_inode(fs, 12, EXT4_EXTENTS_FL, &inode);

	/* Preallocate 4 UNINIT blocks [0..3], poison disk with 0xAB */
	retval = ext2fs_fallocate(fs, EXT2_FALLOCATE_FORCE_UNINIT,
				  12, &inode, ~0ULL, 0, 4);
	check_err("falloc_uninit_4", retval, 0);
	for (i = 0; i < 4; i++) {
		retval = ext2fs_bmap2(fs, 12, &inode, NULL, 0, i, NULL, &pblk);
		check_err("bmap_poison_uninit", retval, 0);
		memset(mem_disk + pblk * fs->blocksize, 0xAB, fs->blocksize);
	}
	retval = ext2fs_inode_size_set(fs, &inode, 4096);
	check_err("set_isize_4096", retval, 0);
	retval = ext2fs_write_inode(fs, 12, &inode);
	check_err("write_inode_4096", retval, 0);

	/* Reading UNINIT extents must return zeroes, not 0xAB */
	retval = ext2fs_file_open(fs, 12, EXT2_FILE_WRITE, &file);
	check_err("open_uninit_file", retval, 0);
	memset(buf, 0xFF, sizeof(buf));
	retval = ext2fs_file_read(file, buf, sizeof(buf), &got);
	check_err("read_uninit_4096", retval, 0);
	check_u64("read_uninit_got", got, 4096);
	check_u64("read_uninit_all_zero",
		  ext2fs_mem_is_zero((char *) buf, sizeof(buf)), 1);

	/* Partial write into block 1 converts block 1 to initialized */
	retval = ext2fs_file_llseek(file, 1024, EXT2_SEEK_SET, NULL);
	check_err("seek_blk1", retval, 0);
	memset(buf, 0x5A, 500);
	retval = ext2fs_file_write(file, buf, 500, &written);
	check_err("write_uninit_blk1", retval, 0);
	retval = ext2fs_file_flush(file);
	check_err("flush_uninit_blk1", retval, 0);
	retval = ext2fs_file_close(file);
	check_err("close_uninit_file", retval, 0);

	retval = ext2fs_read_inode(fs, 12, &inode);
	check_err("reread_after_uninit_write", retval, 0);
	retval = ext2fs_bmap2(fs, 12, &inode, NULL, 0, 0, &ret_flags, &pblk);
	check_err("bmap_blk0_still_uninit", retval, 0);
	check_u64("blk0_flag_uninit", ret_flags, BMAP_RET_UNINIT);
	retval = ext2fs_bmap2(fs, 12, &inode, NULL, 0, 1, &ret_flags, &pblk1);
	check_err("bmap_blk1_now_init", retval, 0);
	check_u64("blk1_flag_init", ret_flags, 0);
	check_u64("blk1_tail_zeroed",
		  ext2fs_mem_is_zero((char *) mem_disk +
				     pblk1 * fs->blocksize + 500,
				     fs->blocksize - 500), 1);

	/* Fill all 4096 bytes with 0x77, flush, then test set_size2 shrink */
	retval = ext2fs_file_open(fs, 12, EXT2_FILE_WRITE, &file);
	check_err("open_for_truncate", retval, 0);
	memset(buf, 0x77, sizeof(buf));
	retval = ext2fs_file_write(file, buf, sizeof(buf), &written);
	check_err("write_full_4096", retval, 0);
	retval = ext2fs_file_flush(file);
	check_err("flush_full_4096", retval, 0);

	/* Shrink to 1500 bytes: blocks 2..3 freed, block 1 tail [476..1023] zeroed */
	retval = ext2fs_file_set_size2(file, 1500);
	check_err("set_size2_1500", retval, 0);
	check_u64("get_size_1500", ext2fs_file_get_size(file), 1500);

	/* Grow to > 4 GiB sparse size: get_size returns 0, get_lsize returns 5 GiB */
	retval = ext2fs_file_set_size2(file, 5ULL << 30);
	check_err("set_size2_5gib", retval, 0);
	check_u64("get_size_32bit_overflow_zero",
		  ext2fs_file_get_size(file), 0);
	retval = ext2fs_file_get_lsize(file, &lsize);
	check_err("get_lsize_5gib_rc", retval, 0);
	check_u64("get_lsize_5gib", lsize, 5ULL << 30);

	/* Shrink back to 1500, verify block 1 tail zeroed and block 2 unmapped */
	retval = ext2fs_file_set_size(file, 1500);
	check_err("set_size_1500", retval, 0);
	retval = ext2fs_file_close(file);
	check_err("close_after_truncate", retval, 0);

	retval = ext2fs_read_inode(fs, 12, &inode);
	check_err("reread_truncated", retval, 0);
	retval = ext2fs_bmap2(fs, 12, &inode, NULL, 0, 1, NULL, &pblk1);
	check_err("bmap_trunc_blk1", retval, 0);
	check_u64("trunc_blk1_head_kept",
		  mem_disk[pblk1 * fs->blocksize + 475], 0x77);
	check_u64("trunc_blk1_tail_zero",
		  ext2fs_mem_is_zero((char *) mem_disk +
				     pblk1 * fs->blocksize + 476,
				     fs->blocksize - 476), 1);
	retval = ext2fs_bmap2(fs, 12, &inode, NULL, 0, 2, NULL, &pblk);
	check_err("bmap_trunc_blk2", retval, 0);
	check_u64("trunc_blk2_freed", pblk, 0);

	/* Indirect-mapped inode rejects size beyond max indirect block offset */
	init_reg_inode(fs, 13, 0, &inode);
	retval = ext2fs_file_open(fs, 13, EXT2_FILE_WRITE, &file);
	check_err("open_ind_13", retval, 0);
	retval = ext2fs_file_set_size2(file, (1ULL << 42));
	check_err("ind_set_size2_too_big", retval, EXT2_ET_FILE_TOO_BIG);
	retval = ext2fs_file_close(file);
	check_err("close_ind_13", retval, 0);

	teardown_fs(&fs);
	printf("test_uninit_extent_and_truncate: completed\n");
}

/*
 * Suite 4: Inline-data file read/write and automatic expansion to extents
 */
static void test_inline_data_fileio(void)
{
	ext2_filsys		fs = NULL;
	struct ext2_inode	inode;
	ext2_file_t		file = NULL;
	unsigned char		wbuf[1600], rbuf[1600];
	unsigned int		written = 0, got = 0;
	int			i;
	errcode_t		retval;

	setup_fs(&fs, 512, 64);
	init_reg_inode(fs, 12, EXT4_INLINE_DATA_FL, &inode);
	retval = ext2fs_inline_data_init(fs, 12);
	check_err("inline_init_12", retval, 0);

	for (i = 0; i < (int) sizeof(wbuf); i++)
		wbuf[i] = (unsigned char) ((i * 13 + 9) & 0xff);

	/* Write 40 bytes (fits in i_block[0..14]) */
	retval = ext2fs_file_open(fs, 12, EXT2_FILE_WRITE, &file);
	check_err("inline_open_rw", retval, 0);
	retval = ext2fs_file_write(file, wbuf, 40, &written);
	check_err("inline_write_40", retval, 0);
	check_u64("inline_written_40", written, 40);

	/* Append 60 more bytes (100 bytes total -> spills into system.data EA) */
	retval = ext2fs_file_write(file, wbuf + 40, 60, &written);
	check_err("inline_write_60_ea", retval, 0);
	check_u64("inline_still_flagged",
		  (ext2fs_file_get_inode(file)->i_flags &
		   EXT4_INLINE_DATA_FL) != 0, 1);

	/* Read back 100 bytes while still inline */
	retval = ext2fs_file_llseek(file, 0, EXT2_SEEK_SET, NULL);
	check_err("inline_seek_0", retval, 0);
	memset(rbuf, 0, sizeof(rbuf));
	retval = ext2fs_file_read(file, rbuf, 100, &got);
	check_err("inline_read_100", retval, 0);
	check_u64("inline_read_got_100", got, 100);
	check_u64("inline_read_match_100", memcmp(rbuf, wbuf, 100) == 0, 1);

	/* Append 1500 more bytes (total 1600 > blocksize) -> expands to blocks */
	retval = ext2fs_file_llseek(file, 100, EXT2_SEEK_SET, NULL);
	check_err("inline_seek_100", retval, 0);
	retval = ext2fs_file_write(file, wbuf + 100, 1500, &written);
	check_err("inline_expand_write_1500", retval, 0);
	check_u64("inline_expand_written_1500", written, 1500);
	check_u64("inline_flag_cleared_after_expand",
		  (ext2fs_file_get_inode(file)->i_flags &
		   EXT4_INLINE_DATA_FL) == 0, 1);
	retval = ext2fs_file_close(file);
	check_err("inline_close_expanded", retval, 0);

	/* Reopen and verify all 1600 bytes survived inline -> block expansion */
	retval = ext2fs_file_open(fs, 12, 0, &file);
	check_err("inline_reopen_expanded", retval, 0);
	memset(rbuf, 0, sizeof(rbuf));
	retval = ext2fs_file_read(file, rbuf, sizeof(rbuf), &got);
	check_err("inline_read_expanded_1600", retval, 0);
	check_u64("inline_got_1600", got, 1600);
	check_u64("inline_match_1600",
		  memcmp(rbuf, wbuf, sizeof(wbuf)) == 0, 1);
	retval = ext2fs_file_close(file);
	check_err("inline_close_ro", retval, 0);

	teardown_fs(&fs);
	printf("test_inline_data_fileio: completed\n");
}

/*
 * Suite 5: EXT2_FLAG_SHARE_DUP block deduplication across files
 */
static void test_share_dup_dedup(void)
{
	ext2_filsys		fs = NULL;
	struct ext2_inode	ino12, ino13;
	ext2_file_t		file = NULL;
	unsigned char		payload[2048], rbuf[2048];
	unsigned int		written = 0, got = 0;
	blk64_t			free_before, p12_0 = 0, p12_1 = 0;
	blk64_t			p13_0 = 0, p13_1 = 0;
	int			i;
	errcode_t		retval;

	setup_fs(&fs, 512, 64);
	init_reg_inode(fs, 12, EXT4_EXTENTS_FL, &ino12);
	init_reg_inode(fs, 13, EXT4_EXTENTS_FL, &ino13);

	/* Flush and reopen with EXT2_FLAG_SHARE_DUP */
	retval = ext2fs_close_free(&fs);
	check_err("close_before_share_dup", retval, 0);

	retval = ext2fs_open2("test_fs", NULL,
			      EXT2_FLAG_RW | EXT2_FLAG_64BITS |
			      EXT2_FLAG_SHARE_DUP,
			      0, 0, mem_io_manager, &fs);
	check_err("reopen_share_dup", retval, 0);
	retval = ext2fs_read_bitmaps(fs);
	check_err("read_bitmaps_share_dup", retval, 0);
	check_u64("shared_blocks_feature_set",
		  ext2fs_has_feature_shared_blocks(fs->super) != 0, 1);

	for (i = 0; i < (int) sizeof(payload); i++)
		payload[i] = (unsigned char) ((i < 1024) ? 0xB1 : 0xC2);

	free_before = ext2fs_free_blocks_count(fs->super);

	retval = ext2fs_file_open(fs, 12, EXT2_FILE_WRITE, &file);
	check_err("open_dup_12", retval, 0);
	retval = ext2fs_file_write(file, payload, sizeof(payload), &written);
	check_err("write_dup_12", retval, 0);
	retval = ext2fs_file_close(file);
	check_err("close_dup_12", retval, 0);

	retval = ext2fs_file_open(fs, 13, EXT2_FILE_WRITE, &file);
	check_err("open_dup_13", retval, 0);
	retval = ext2fs_file_write(file, payload, sizeof(payload), &written);
	check_err("write_dup_13", retval, 0);
	retval = ext2fs_file_close(file);
	check_err("close_dup_13", retval, 0);

	/* Only 2 physical blocks should be consumed across both 2-KiB files */
	check_u64("share_dup_consumed_2_blocks",
		  free_before - ext2fs_free_blocks_count(fs->super), 2);

	retval = ext2fs_bmap2(fs, 12, NULL, NULL, 0, 0, NULL, &p12_0);
	check_err("bmap_12_0", retval, 0);
	retval = ext2fs_bmap2(fs, 12, NULL, NULL, 0, 1, NULL, &p12_1);
	check_err("bmap_12_1", retval, 0);
	retval = ext2fs_bmap2(fs, 13, NULL, NULL, 0, 0, NULL, &p13_0);
	check_err("bmap_13_0", retval, 0);
	retval = ext2fs_bmap2(fs, 13, NULL, NULL, 0, 1, NULL, &p13_1);
	check_err("bmap_13_1", retval, 0);
	check_u64("dedup_blk0_shared", p12_0, p13_0);
	check_u64("dedup_blk1_shared", p12_1, p13_1);

	retval = ext2fs_file_open(fs, 13, 0, &file);
	check_err("open_ro_dup_13", retval, 0);
	retval = ext2fs_file_read(file, rbuf, sizeof(rbuf), &got);
	check_err("read_dup_13", retval, 0);
	check_u64("read_dup_13_match",
		  memcmp(rbuf, payload, sizeof(payload)) == 0, 1);
	retval = ext2fs_file_close(file);
	check_err("close_ro_dup_13", retval, 0);

	teardown_fs(&fs);
	printf("test_share_dup_dedup: completed\n");
}

int main(int argc EXT2FS_ATTR((unused)), char *argv[] EXT2FS_ATTR((unused)))
{
	add_error_table(&et_ext2_error_table);

	test_open_accessors_and_ro();
	test_rw_seek_and_sparse_holes();
	test_uninit_extent_and_truncate();
	test_inline_data_fileio();
	test_share_dup_dedup();

	if (test_fail == 0)
		printf("libext2fs fileio unit tests passed!\n");
	else
		printf("libext2fs fileio unit tests FAILED (%d errors)\n",
		       test_fail);

	return test_fail ? 1 : 0;
}
