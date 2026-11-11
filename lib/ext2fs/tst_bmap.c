/*
 * tst_bmap.c --- unit tests for bmap.c and ind_block.c
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

static void setup_fs_opts(ext2_filsys *ret_fs, unsigned int blocks,
			  unsigned int inodes, int log_blksize,
			  int log_clustersize, __u32 incompat,
			  __u32 ro_compat)
{
	struct ext2_super_block	param;
	errcode_t		retval;
	unsigned int		blksize = 1024U << log_blksize;

	fail_read_blk = 0;
	fail_write_blk = 0;
	mem_disk_size = (unsigned long long) blocks * blksize;
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
	param.s_log_block_size = log_blksize;
	param.s_log_cluster_size = log_clustersize;
	param.s_feature_incompat = incompat;
	param.s_feature_ro_compat = ro_compat;
	if (incompat & EXT4_FEATURE_INCOMPAT_64BIT)
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

static void setup_fs(ext2_filsys *ret_fs, unsigned int blocks,
		     unsigned int inodes)
{
	setup_fs_opts(ret_fs, blocks, inodes, 0, 0,
		      EXT3_FEATURE_INCOMPAT_EXTENTS |
		      EXT4_FEATURE_INCOMPAT_INLINE_DATA |
		      EXT4_FEATURE_INCOMPAT_64BIT,
		      EXT4_FEATURE_RO_COMPAT_HUGE_FILE);
}

static void teardown_fs(ext2_filsys *fs)
{
	fail_read_blk = 0;
	fail_write_blk = 0;
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
 * Suite 1: Preconditions, inline-data rejection, and maximum block boundaries
 */
static void test_preconditions_and_bounds(void)
{
	ext2_filsys		fs = NULL;
	struct ext2_inode	ind_ino, ext_ino, inline_ino;
	blk64_t			pblk = 999;
	blk64_t			max_ind;
	int			ret_flags = -1;
	errcode_t		retval;

	setup_fs(&fs, 512, 64);

	/* On 1K blocksize: addr_per_block = 256; max = 12 + 256 + 65536 + 256^3 */
	max_ind = 12ULL + 256ULL + (256ULL * 256ULL) +
		  (256ULL * 256ULL * 256ULL);

	init_reg_inode(fs, 12, 0, &ind_ino);
	init_reg_inode(fs, 13, EXT4_EXTENTS_FL, &ext_ino);
	init_reg_inode(fs, 14, EXT4_INLINE_DATA_FL, &inline_ino);

	/* ext2fs_file_block_offset_too_big() checks */
	check_u64("ind_max_minus_1_ok",
		  ext2fs_file_block_offset_too_big(fs, &ind_ino, max_ind - 1),
		  0);
	check_u64("ind_max_too_big",
		  ext2fs_file_block_offset_too_big(fs, &ind_ino, max_ind), 1);

	check_u64("ext_past_ind_max_ok",
		  ext2fs_file_block_offset_too_big(fs, &ext_ino, max_ind), 0);
	check_u64("ext_u32_max_minus_2_ok",
		  ext2fs_file_block_offset_too_big(fs, &ext_ino,
						   (1ULL << 32) - 2), 0);
	check_u64("ext_u32_max_minus_1_too_big",
		  ext2fs_file_block_offset_too_big(fs, &ext_ino,
						   (1ULL << 32) - 1), 1);

	/* ext2fs_bmap2() returns EXT2_ET_FILE_TOO_BIG at exact cutoffs */
	retval = ext2fs_bmap2(fs, 12, &ind_ino, NULL, 0, max_ind - 1,
			      &ret_flags, &pblk);
	check_err("bmap2_ind_last_valid", retval, 0);
	check_u64("bmap2_ind_last_valid_pblk", pblk, 0);
	check_u64("bmap2_ind_last_valid_flags", ret_flags, 0);

	retval = ext2fs_bmap2(fs, 12, &ind_ino, NULL, 0, max_ind,
			      &ret_flags, &pblk);
	check_err("bmap2_ind_too_big", retval, EXT2_ET_FILE_TOO_BIG);

	retval = ext2fs_bmap2(fs, 13, &ext_ino, NULL, 0, (1ULL << 32) - 1,
			      &ret_flags, &pblk);
	check_err("bmap2_ext_too_big", retval, EXT2_ET_FILE_TOO_BIG);

	/* Inline-data inode rejection */
	pblk = 777;
	retval = ext2fs_bmap2(fs, 14, &inline_ino, NULL, 0, 0,
			      &ret_flags, &pblk);
	check_err("bmap2_inline_data", retval, EXT2_ET_INLINE_DATA_NO_BLOCK);
	check_u64("bmap2_inline_zeroed_pblk", pblk, 0);

	/* Invalid inode number when inode == NULL */
	retval = ext2fs_bmap2(fs, 0, NULL, NULL, 0, 0, NULL, &pblk);
	check_err("bmap2_bad_ino_0", retval, EXT2_ET_BAD_INODE_NUM);
	retval = ext2fs_bmap2(fs, 9999, NULL, NULL, 0, 0, NULL, &pblk);
	check_err("bmap2_bad_ino_high", retval, EXT2_ET_BAD_INODE_NUM);

	teardown_fs(&fs);
	printf("test_preconditions_and_bounds: completed\n");
}

/*
 * Suite 2: Indirect block mapping (direct, IND, DIND, TIND; BMAP_ALLOC,
 * BMAP_SET, BMAP_ZERO, caller block_buf vs NULL, inode == NULL)
 */
static void test_indirect_bmap(void)
{
	ext2_filsys		fs = NULL;
	struct ext2_inode	inode, disk_inode;
	char			*block_buf = NULL;
	unsigned char		pattern[1024];
	unsigned char		readback[1024];
	blk64_t			b_dir0 = 0, b_dir11 = 0;
	blk64_t			b_ind12 = 0, b_ind200 = 0;
	blk64_t			b_dind268 = 0, b_dind600 = 0;
	blk64_t			b_tind = 0, custom_blk = 0, pblk = 0;
	blk64_t			tind_lblk = 12 + 256 + (256 * 256) + 5;
	int			ret_flags = 0;
	errcode_t		retval;

	setup_fs(&fs, 1024, 64);
	init_reg_inode(fs, 12, 0, &inode);

	retval = ext2fs_get_array(2, fs->blocksize, &block_buf);
	check_err("alloc_block_buf", retval, 0);

	/* Sparse lookups across all 4 levels return 0 */
	retval = ext2fs_bmap2(fs, 12, &inode, block_buf, 0, 0,
			      &ret_flags, &pblk);
	check_err("sparse_dir", retval, 0);
	check_u64("sparse_dir_pblk", pblk, 0);

	retval = ext2fs_bmap2(fs, 12, &inode, NULL, 0, 12, &ret_flags, &pblk);
	check_err("sparse_ind", retval, 0);
	check_u64("sparse_ind_pblk", pblk, 0);

	retval = ext2fs_bmap2(fs, 12, &inode, NULL, 0, 268, &ret_flags, &pblk);
	check_err("sparse_dind", retval, 0);
	check_u64("sparse_dind_pblk", pblk, 0);

	retval = ext2fs_bmap2(fs, 12, &inode, NULL, 0, tind_lblk,
			      &ret_flags, &pblk);
	check_err("sparse_tind", retval, 0);
	check_u64("sparse_tind_pblk", pblk, 0);

	/* BMAP_SET without BMAP_ALLOC fails when IND/DIND/TIND is missing */
	pblk = 100;
	retval = ext2fs_bmap2(fs, 12, &inode, block_buf, BMAP_SET, 12,
			      NULL, &pblk);
	check_err("set_no_ind", retval, EXT2_ET_SET_BMAP_NO_IND);

	retval = ext2fs_bmap2(fs, 12, &inode, block_buf, BMAP_SET, 268,
			      NULL, &pblk);
	check_err("set_no_dind", retval, EXT2_ET_SET_BMAP_NO_IND);

	retval = ext2fs_bmap2(fs, 12, &inode, block_buf, BMAP_SET, tind_lblk,
			      NULL, &pblk);
	check_err("set_no_tind", retval, EXT2_ET_SET_BMAP_NO_IND);

	/* Direct BMAP_ALLOC: +1 fs block (2 512-byte sectors) each */
	retval = ext2fs_bmap2(fs, 12, &inode, block_buf, BMAP_ALLOC, 0,
			      &ret_flags, &b_dir0);
	check_err("alloc_dir0", retval, 0);
	check_u64("dir0_nonzero", b_dir0 != 0, 1);
	check_u64("dir0_bitmap", ext2fs_test_block_bitmap2(fs->block_map,
							   b_dir0), 1);
	check_u64("iblocks_after_dir0", ext2fs_inode_i_blocks(fs, &inode), 2);

	retval = ext2fs_bmap2(fs, 12, &inode, NULL, BMAP_ALLOC, 11,
			      NULL, &b_dir11);
	check_err("alloc_dir11", retval, 0);
	check_u64("iblocks_after_dir11", ext2fs_inode_i_blocks(fs, &inode), 4);

	/* Single-indirect BMAP_ALLOC: first adds IND + data (+4 sectors),
	 * second reuses IND (+2 sectors) */
	retval = ext2fs_bmap2(fs, 12, &inode, block_buf, BMAP_ALLOC, 12,
			      NULL, &b_ind12);
	check_err("alloc_ind12", retval, 0);
	check_u64("iblocks_after_ind12", ext2fs_inode_i_blocks(fs, &inode), 8);

	retval = ext2fs_bmap2(fs, 12, &inode, block_buf, BMAP_ALLOC, 200,
			      NULL, &b_ind200);
	check_err("alloc_ind200", retval, 0);
	check_u64("iblocks_after_ind200",
		  ext2fs_inode_i_blocks(fs, &inode), 10);

	/* Double-indirect BMAP_ALLOC: first adds DIND + L1 + data (+6 sectors),
	 * second in another L1 slot adds L1 + data (+4 sectors) */
	retval = ext2fs_bmap2(fs, 12, &inode, block_buf, BMAP_ALLOC, 268,
			      NULL, &b_dind268);
	check_err("alloc_dind268", retval, 0);
	check_u64("iblocks_after_dind268",
		  ext2fs_inode_i_blocks(fs, &inode), 16);

	retval = ext2fs_bmap2(fs, 12, &inode, block_buf, BMAP_ALLOC, 600,
			      NULL, &b_dind600);
	check_err("alloc_dind600", retval, 0);
	check_u64("iblocks_after_dind600",
		  ext2fs_inode_i_blocks(fs, &inode), 20);

	/* Triple-indirect BMAP_ALLOC: adds TIND + L2 + L1 + data (+8 sectors) */
	retval = ext2fs_bmap2(fs, 12, &inode, block_buf, BMAP_ALLOC, tind_lblk,
			      NULL, &b_tind);
	check_err("alloc_tind", retval, 0);
	check_u64("iblocks_after_tind", ext2fs_inode_i_blocks(fs, &inode), 28);

	/* Verify lookups via inode == NULL read back from on-disk inode */
	retval = ext2fs_bmap2(fs, 12, NULL, NULL, 0, 0, NULL, &pblk);
	check_err("lookup_dir0", retval, 0);
	check_u64("lookup_dir0_val", pblk, b_dir0);

	retval = ext2fs_bmap2(fs, 12, NULL, NULL, 0, 200, NULL, &pblk);
	check_err("lookup_ind200", retval, 0);
	check_u64("lookup_ind200_val", pblk, b_ind200);

	retval = ext2fs_bmap2(fs, 12, NULL, NULL, 0, 600, NULL, &pblk);
	check_err("lookup_dind600", retval, 0);
	check_u64("lookup_dind600_val", pblk, b_dind600);

	retval = ext2fs_bmap2(fs, 12, NULL, NULL, 0, tind_lblk, NULL, &pblk);
	check_err("lookup_tind", retval, 0);
	check_u64("lookup_tind_val", pblk, b_tind);

	/* Unallocated slot inside allocated IND still returns 0 */
	retval = ext2fs_bmap2(fs, 12, NULL, NULL, 0, 15, NULL, &pblk);
	check_err("lookup_ind_hole", retval, 0);
	check_u64("lookup_ind_hole_val", pblk, 0);

	/* BMAP_SET + BMAP_ZERO on direct and indirect slots */
	retval = ext2fs_new_block2(fs, 0, fs->block_map, &custom_blk);
	check_err("new_block2_custom", retval, 0);
	ext2fs_block_alloc_stats2(fs, custom_blk, +1);
	memset(pattern, 0xa5, sizeof(pattern));
	retval = io_channel_write_blk64(fs->io, custom_blk, 1, pattern);
	check_err("write_pattern", retval, 0);

	pblk = custom_blk;
	retval = ext2fs_bmap2(fs, 12, NULL, block_buf, BMAP_SET | BMAP_ZERO,
			      5, NULL, &pblk);
	check_err("bmap_set_zero_dir5", retval, 0);

	retval = ext2fs_bmap2(fs, 12, NULL, NULL, 0, 5, NULL, &pblk);
	check_err("lookup_dir5_after_set", retval, 0);
	check_u64("lookup_dir5_val", pblk, custom_blk);

	retval = io_channel_read_blk64(fs->io, custom_blk, 1, readback);
	check_err("read_zeroed_custom", retval, 0);
	check_u64("custom_is_zeroed",
		  ext2fs_mem_is_zero((char *) readback, sizeof(readback)), 1);

	/* BMAP_SET on IND slot 15 and clear back to 0 */
	pblk = custom_blk;
	retval = ext2fs_bmap2(fs, 12, NULL, block_buf, BMAP_SET, 15,
			      NULL, &pblk);
	check_err("bmap_set_ind15", retval, 0);
	retval = ext2fs_bmap2(fs, 12, NULL, NULL, 0, 15, NULL, &pblk);
	check_err("lookup_ind15_set", retval, 0);
	check_u64("lookup_ind15_val", pblk, custom_blk);

	pblk = 0;
	retval = ext2fs_bmap2(fs, 12, NULL, block_buf, BMAP_SET, 15,
			      NULL, &pblk);
	check_err("bmap_clear_ind15", retval, 0);
	retval = ext2fs_bmap2(fs, 12, NULL, NULL, 0, 15, NULL, &pblk);
	check_err("lookup_ind15_cleared", retval, 0);
	check_u64("lookup_ind15_zero", pblk, 0);

	/* Verify inode == NULL BMAP_ALLOC persists to disk */
	init_reg_inode(fs, 13, 0, &disk_inode);
	retval = ext2fs_bmap2(fs, 13, NULL, NULL, BMAP_ALLOC, 2, NULL, &pblk);
	check_err("alloc_null_inode_ptr", retval, 0);
	retval = ext2fs_read_inode(fs, 13, &disk_inode);
	check_err("read_ino13", retval, 0);
	check_u64("ino13_iblock2", disk_inode.i_block[2], pblk);
	check_u64("ino13_iblocks", ext2fs_inode_i_blocks(fs, &disk_inode), 2);

	ext2fs_free_mem(&block_buf);
	teardown_fs(&fs);
	printf("test_indirect_bmap: completed\n");
}

/*
 * Suite 3: Extent-mapped inodes (initialized, BMAP_UNINIT / BMAP_RET_UNINIT,
 * BMAP_SET, sparse holes, and multi-level extent trees)
 */
static void test_extent_bmap(void)
{
	ext2_filsys		fs = NULL;
	struct ext2_inode	inode;
	blk64_t			b_init = 0, b_uninit = 0, pblk = 0;
	blk64_t			mapped[10];
	int			ret_flags = 0;
	int			i;
	errcode_t		retval;

	setup_fs(&fs, 1024, 64);
	init_reg_inode(fs, 12, EXT4_EXTENTS_FL, &inode);

	/* Empty extent tree lookup returns 0 */
	retval = ext2fs_bmap2(fs, 12, &inode, NULL, 0, 10, &ret_flags, &pblk);
	check_err("ext_empty_lookup", retval, 0);
	check_u64("ext_empty_pblk", pblk, 0);
	check_u64("ext_empty_flags", ret_flags, 0);

	/* Initialized extent allocation */
	retval = ext2fs_bmap2(fs, 12, &inode, NULL, BMAP_ALLOC, 10,
			      &ret_flags, &b_init);
	check_err("ext_alloc_init", retval, 0);
	check_u64("ext_alloc_init_nonzero", b_init != 0, 1);

	retval = ext2fs_bmap2(fs, 12, &inode, NULL, 0, 10, &ret_flags, &pblk);
	check_err("ext_lookup_init", retval, 0);
	check_u64("ext_lookup_init_pblk", pblk, b_init);
	check_u64("ext_lookup_init_flags", ret_flags, 0);

	/* Uninitialized extent allocation (BMAP_UNINIT -> BMAP_RET_UNINIT) */
	retval = ext2fs_bmap2(fs, 12, &inode, NULL, BMAP_ALLOC | BMAP_UNINIT,
			      20, &ret_flags, &b_uninit);
	check_err("ext_alloc_uninit", retval, 0);
	check_u64("ext_alloc_uninit_nonzero", b_uninit != 0, 1);

	retval = ext2fs_bmap2(fs, 12, &inode, NULL, 0, 20, &ret_flags, &pblk);
	check_err("ext_lookup_uninit", retval, 0);
	check_u64("ext_lookup_uninit_pblk", pblk, b_uninit);
	check_u64("ext_lookup_uninit_flags", ret_flags, BMAP_RET_UNINIT);

	/* Lookup with ret_flags == NULL also succeeds on uninitialized extent */
	retval = ext2fs_bmap2(fs, 12, &inode, NULL, 0, 20, NULL, &pblk);
	check_err("ext_lookup_uninit_null_flags", retval, 0);
	check_u64("ext_lookup_uninit_null_pblk", pblk, b_uninit);

	/* Sparse holes before, between, and after extents */
	retval = ext2fs_bmap2(fs, 12, &inode, NULL, 0, 5, &ret_flags, &pblk);
	check_err("ext_hole_before", retval, 0);
	check_u64("ext_hole_before_pblk", pblk, 0);

	retval = ext2fs_bmap2(fs, 12, &inode, NULL, 0, 15, &ret_flags, &pblk);
	check_err("ext_hole_between", retval, 0);
	check_u64("ext_hole_between_pblk", pblk, 0);

	retval = ext2fs_bmap2(fs, 12, &inode, NULL, 0, 99, &ret_flags, &pblk);
	check_err("ext_hole_after", retval, 0);
	check_u64("ext_hole_after_pblk", pblk, 0);

	/* BMAP_SET converts uninitialized block 20 to initialized, then unmaps */
	pblk = b_uninit;
	retval = ext2fs_bmap2(fs, 12, &inode, NULL, BMAP_SET, 20, NULL, &pblk);
	check_err("ext_set_init", retval, 0);
	retval = ext2fs_read_inode(fs, 12, &inode);
	check_err("ext_reread_ino", retval, 0);
	retval = ext2fs_bmap2(fs, 12, &inode, NULL, 0, 20, &ret_flags, &pblk);
	check_err("ext_lookup_converted", retval, 0);
	check_u64("ext_converted_pblk", pblk, b_uninit);
	check_u64("ext_converted_flags", ret_flags, 0);

	pblk = 0;
	retval = ext2fs_bmap2(fs, 12, &inode, NULL, BMAP_SET, 20, NULL, &pblk);
	check_err("ext_unmap_20", retval, 0);
	retval = ext2fs_read_inode(fs, 12, &inode);
	check_err("ext_reread_after_unmap", retval, 0);
	retval = ext2fs_bmap2(fs, 12, &inode, NULL, 0, 20, &ret_flags, &pblk);
	check_err("ext_lookup_unmapped", retval, 0);
	check_u64("ext_unmapped_pblk", pblk, 0);

	/* Grow a multi-level extent tree (> 4 non-contiguous extents) */
	init_reg_inode(fs, 13, EXT4_EXTENTS_FL, &inode);
	for (i = 0; i < 10; i++) {
		blk64_t lblk = (blk64_t) i * 10;

		retval = ext2fs_bmap2(fs, 13, &inode, NULL, BMAP_ALLOC,
				      lblk, NULL, &mapped[i]);
		check_err("ext_multilevel_alloc", retval, 0);
	}
	for (i = 0; i < 10; i++) {
		blk64_t lblk = (blk64_t) i * 10;

		retval = ext2fs_bmap2(fs, 13, &inode, NULL, 0, lblk,
				      NULL, &pblk);
		check_err("ext_multilevel_lookup", retval, 0);
		check_u64("ext_multilevel_pblk", pblk, mapped[i]);
	}

	teardown_fs(&fs);
	printf("test_extent_bmap: completed\n");
}

/*
 * Suite 4: bigalloc implied cluster allocation and ext2fs_map_cluster_block()
 */
static void test_bigalloc_cluster_bmap(void)
{
	ext2_filsys		fs = NULL;
	struct ext2_inode	ext_ino, ind_ino;
	blk64_t			pblk = 0, base_cluster = 0;
	blk64_t			b0 = 0, b2 = 0, b3 = 0;
	unsigned long long	iblocks_after_first;
	errcode_t		retval;

	/* 1 KiB blocks, 4 KiB clusters (cluster_ratio = 4) */
	setup_fs_opts(&fs, 1024, 64, 0, 2,
		      EXT3_FEATURE_INCOMPAT_EXTENTS |
		      EXT4_FEATURE_INCOMPAT_64BIT,
		      EXT4_FEATURE_RO_COMPAT_BIGALLOC |
		      EXT4_FEATURE_RO_COMPAT_HUGE_FILE);

	init_reg_inode(fs, 12, EXT4_EXTENTS_FL, &ext_ino);
	init_reg_inode(fs, 13, 0, &ind_ino);

	/* Non-extent inode returns 0 / pblk == 0 */
	pblk = 99;
	retval = ext2fs_map_cluster_block(fs, 13, &ind_ino, 0, &pblk);
	check_err("map_cluster_non_extent", retval, 0);
	check_u64("map_cluster_non_extent_pblk", pblk, 0);

	/* Unmapped cluster on extent inode returns pblk == 0 */
	pblk = 99;
	retval = ext2fs_map_cluster_block(fs, 12, &ext_ino, 2, &pblk);
	check_err("map_cluster_unmapped", retval, 0);
	check_u64("map_cluster_unmapped_pblk", pblk, 0);

	/* Allocate logical block 2 first (mid-cluster / out-of-order) */
	retval = ext2fs_bmap2(fs, 12, &ext_ino, NULL, BMAP_ALLOC, 2,
			      NULL, &b2);
	check_err("bigalloc_bmap_lblk2", retval, 0);
	base_cluster = b2 & ~EXT2FS_CLUSTER_MASK(fs);
	check_u64("bigalloc_lblk2_offset", b2 - base_cluster, 2);
	iblocks_after_first = ext2fs_inode_i_blocks(fs, &ext_ino);
	check_u64("bigalloc_first_cluster_iblocks", iblocks_after_first, 8);

	/* ext2fs_map_cluster_block resolves sibling blocks 0, 1, and 3 */
	retval = ext2fs_map_cluster_block(fs, 12, &ext_ino, 0, &pblk);
	check_err("map_cluster_sibling0", retval, 0);
	check_u64("map_cluster_sibling0_pblk", pblk, base_cluster + 0);

	retval = ext2fs_map_cluster_block(fs, 12, &ext_ino, 1, &pblk);
	check_err("map_cluster_sibling1", retval, 0);
	check_u64("map_cluster_sibling1_pblk", pblk, base_cluster + 1);

	retval = ext2fs_map_cluster_block(fs, 12, &ext_ino, 3, &pblk);
	check_err("map_cluster_sibling3", retval, 0);
	check_u64("map_cluster_sibling3_pblk", pblk, base_cluster + 3);

	/* Allocating sibling blocks 0 and 3 reuses implied physical cluster
	 * without charging extra clusters to i_blocks */
	retval = ext2fs_bmap2(fs, 12, &ext_ino, NULL, BMAP_ALLOC, 0,
			      NULL, &b0);
	check_err("bigalloc_bmap_lblk0", retval, 0);
	check_u64("bigalloc_lblk0_pblk", b0, base_cluster + 0);

	retval = ext2fs_bmap2(fs, 12, &ext_ino, NULL, BMAP_ALLOC, 3,
			      NULL, &b3);
	check_err("bigalloc_bmap_lblk3", retval, 0);
	check_u64("bigalloc_lblk3_pblk", b3, base_cluster + 3);
	check_u64("bigalloc_same_cluster_iblocks",
		  ext2fs_inode_i_blocks(fs, &ext_ino), iblocks_after_first);

	teardown_fs(&fs);
	printf("test_bigalloc_cluster_bmap: completed\n");
}

/*
 * Suite 5: Legacy 32-bit ext2fs_bmap() wrapper and >= 2^32 EOVERFLOW guard
 */
static void test_legacy_bmap32(void)
{
	ext2_filsys		fs = NULL;
	struct ext2_inode	inode;
	ext2_extent_handle_t	handle = NULL;
	struct ext2fs_extent	ext;
	blk_t			blk32 = 0, got32 = 0;
	blk64_t			got64 = 0;
	blk64_t			high_pblk = (1ULL << 32) + 7;
	errcode_t		retval;

	setup_fs(&fs, 512, 64);

	/* Non-bigalloc ext2fs_map_cluster_block returns 0 */
	init_reg_inode(fs, 12, EXT4_EXTENTS_FL, &inode);
	got64 = 55;
	retval = ext2fs_map_cluster_block(fs, 12, &inode, 0, &got64);
	check_err("non_bigalloc_map_cluster", retval, 0);
	check_u64("non_bigalloc_map_cluster_zero", got64, 0);

	/* Normal 32-bit allocation and lookup */
	retval = ext2fs_bmap(fs, 12, &inode, NULL, BMAP_ALLOC, 0, &blk32);
	check_err("bmap32_alloc", retval, 0);
	check_u64("bmap32_nonzero", blk32 != 0, 1);

	retval = ext2fs_bmap(fs, 12, &inode, NULL, 0, 0, &got32);
	check_err("bmap32_lookup", retval, 0);
	check_u64("bmap32_match", got32, blk32);

	/* Insert an extent pointing above 2^32 and verify EOVERFLOW */
	retval = ext2fs_extent_open2(fs, 12, &inode, &handle);
	check_err("extent_open2_high", retval, 0);
	memset(&ext, 0, sizeof(ext));
	ext.e_lblk = 5;
	ext.e_pblk = high_pblk;
	ext.e_len = 1;
	ext.e_flags = EXT2_EXTENT_FLAGS_LEAF;
	retval = ext2fs_extent_insert(handle, EXT2_EXTENT_INSERT_AFTER, &ext);
	check_err("extent_insert_high", retval, 0);
	ext2fs_extent_free(handle);

	retval = ext2fs_bmap2(fs, 12, &inode, NULL, 0, 5, NULL, &got64);
	check_err("bmap2_high_ok", retval, 0);
	check_u64("bmap2_high_val", got64, high_pblk);

	got32 = 123;
	retval = ext2fs_bmap(fs, 12, &inode, NULL, 0, 5, &got32);
	check_err("bmap32_high_overflow", retval, EOVERFLOW);
	check_u64("bmap32_unchanged_on_overflow", got32, 123);

	teardown_fs(&fs);
	printf("test_legacy_bmap32: completed\n");
}

/*
 * Suite 6: ext2fs_read_ind_block(), ext2fs_write_ind_block(),
 * EXT2_FLAG_IMAGE_FILE behavior, and I/O fault injection
 */
static void test_ind_block_io_and_errors(void)
{
	ext2_filsys		fs = NULL;
	struct ext2_inode	inode;
	blk_t			*wbuf = NULL, *rbuf = NULL;
	blk64_t			ind_blk = 0, pblk = 0;
	io_channel		saved_image_io;
	unsigned char		*disk;
	__u32			le;
	int			i, nentries;
	errcode_t		retval;

	setup_fs(&fs, 512, 64);
	nentries = fs->blocksize >> 2;

	retval = ext2fs_get_memzero(fs->blocksize, &wbuf);
	check_err("alloc_wbuf", retval, 0);
	retval = ext2fs_get_memzero(fs->blocksize, &rbuf);
	check_err("alloc_rbuf", retval, 0);

	retval = ext2fs_new_block2(fs, 0, fs->block_map, &ind_blk);
	check_err("alloc_ind_blk", retval, 0);
	ext2fs_block_alloc_stats2(fs, ind_blk, +1);

	for (i = 0; i < nentries; i++)
		wbuf[i] = (blk_t) (1000 + i * 3);

	retval = ext2fs_write_ind_block(fs, (blk_t) ind_blk, wbuf);
	check_err("write_ind_block", retval, 0);

	/*
	 * The block must be little-endian on disk, and the caller's buffer
	 * must be left in CPU byte order (big-endian hosts byte-swap it).
	 */
	for (i = 0; i < nentries; i++) {
		if (wbuf[i] != (blk_t) (1000 + i * 3)) {
			printf("FAIL [ind_buf_kept]: idx %d got %u != %u\n",
			       i, wbuf[i], 1000 + i * 3);
			test_fail++;
			break;
		}
	}
	disk = mem_disk + ind_blk * fs->blocksize;
	for (i = 0; i < nentries; i++) {
		memcpy(&le, disk + i * 4, sizeof(le));
		if (ext2fs_le32_to_cpu(le) != (__u32) (1000 + i * 3)) {
			printf("FAIL [ind_disk_le]: idx %d got %u != %u\n",
			       i, ext2fs_le32_to_cpu(le), 1000 + i * 3);
			test_fail++;
			break;
		}
	}

	retval = ext2fs_read_ind_block(fs, (blk_t) ind_blk, rbuf);
	check_err("read_ind_block", retval, 0);
	for (i = 0; i < nentries; i++) {
		if (rbuf[i] != (blk_t) (1000 + i * 3)) {
			printf("FAIL [ind_roundtrip]: idx %d got %u != %u\n",
			       i, rbuf[i], 1000 + i * 3);
			test_fail++;
			break;
		}
	}

	/* EXT2_FLAG_IMAGE_FILE write is a no-op; read zeroes when io != image_io */
	fs->flags |= EXT2_FLAG_IMAGE_FILE;
	for (i = 0; i < nentries; i++)
		wbuf[i] = 0xdeadbeefU;
	retval = ext2fs_write_ind_block(fs, (blk_t) ind_blk, wbuf);
	check_err("write_ind_image_noop", retval, 0);

	/* When fs->io == fs->image_io, ext2fs_read_ind_block still reads */
	memset(rbuf, 0, fs->blocksize);
	retval = ext2fs_read_ind_block(fs, (blk_t) ind_blk, rbuf);
	check_err("read_ind_image_same_io", retval, 0);
	check_u64("read_ind_image_same_io_val0", rbuf[0], 1000);

	/* When fs->io != fs->image_io, ext2fs_read_ind_block zeroes buf */
	saved_image_io = fs->image_io;
	fs->image_io = NULL;
	memset(rbuf, 0xff, fs->blocksize);
	retval = ext2fs_read_ind_block(fs, (blk_t) ind_blk, rbuf);
	check_err("read_ind_image_diff_io", retval, 0);
	check_u64("read_ind_image_zeroed",
		  ext2fs_mem_is_zero((char *) rbuf, fs->blocksize), 1);
	fs->image_io = saved_image_io;
	fs->flags &= ~EXT2_FLAG_IMAGE_FILE;

	/* I/O fault injection on ind_block and ext2fs_bmap2 */
	fail_read_blk = ind_blk;
	retval = ext2fs_read_ind_block(fs, (blk_t) ind_blk, rbuf);
	check_err("read_ind_eio", retval, EIO);
	fail_read_blk = 0;

	fail_write_blk = ind_blk;
	for (i = 0; i < nentries; i++)
		wbuf[i] = (blk_t) (1000 + i * 3);
	retval = ext2fs_write_ind_block(fs, (blk_t) ind_blk, wbuf);
	check_err("write_ind_eio", retval, EIO);
	fail_write_blk = 0;

	init_reg_inode(fs, 12, 0, &inode);
	inode.i_block[EXT2_IND_BLOCK] = (blk_t) ind_blk;
	retval = ext2fs_write_inode(fs, 12, &inode);
	check_err("write_ino_with_ind", retval, 0);

	fail_read_blk = ind_blk;
	retval = ext2fs_bmap2(fs, 12, &inode, NULL, 0, 12, NULL, &pblk);
	check_err("bmap2_ind_read_eio", retval, EIO);
	fail_read_blk = 0;

	fail_write_blk = ind_blk;
	pblk = 200;
	retval = ext2fs_bmap2(fs, 12, &inode, NULL, BMAP_SET, 12, NULL, &pblk);
	check_err("bmap2_ind_write_eio", retval, EIO);
	fail_write_blk = 0;

	ext2fs_free_mem(&wbuf);
	ext2fs_free_mem(&rbuf);
	teardown_fs(&fs);
	printf("test_ind_block_io_and_errors: completed\n");
}

/*
 * Suite 7: the allocation context that ext2fs_bmap2() hands to the
 * fs->get_alloc_block2 callback must describe the inode, even when the
 * caller passes inode == NULL and lets ext2fs_bmap2() read it.
 */
static int		alloc_cb_ctx_calls;
static int		alloc_cb_ctx_no_inode;
static ext2_ino_t	alloc_cb_ino;
static __u16		alloc_cb_mode;

static void reset_alloc_cb(void)
{
	alloc_cb_ctx_calls = 0;
	alloc_cb_ctx_no_inode = 0;
	alloc_cb_ino = 0;
	alloc_cb_mode = 0;
}

static errcode_t alloc_cb(ext2_filsys fs, blk64_t goal, blk64_t *ret,
			  struct blk_alloc_ctx *ctx)
{
	/* Blocks allocated by block_ind_bmap() and friends have no ctx */
	if (ctx) {
		alloc_cb_ctx_calls++;
		alloc_cb_ino = ctx->ino;
		if (ctx->inode)
			alloc_cb_mode = ctx->inode->i_mode;
		else
			alloc_cb_ctx_no_inode++;
	}
	return ext2fs_new_block2(fs, goal, fs->block_map, ret);
}

static void test_alloc_ctx_inode(void)
{
	ext2_filsys		fs = NULL;
	struct ext2_inode	inode;
	blk64_t			pblk = 0;
	blk64_t			tind_lblk = 12 + 256 + (256 * 256) + 5;
	errcode_t		retval;

	setup_fs(&fs, 1024, 64);
	init_reg_inode(fs, 12, 0, &inode);
	fs->get_alloc_block2 = alloc_cb;

	/* Direct block: the data block is allocated with a context */
	reset_alloc_cb();
	retval = ext2fs_bmap2(fs, 12, NULL, NULL, BMAP_ALLOC, 0, NULL, &pblk);
	check_err("ctx_dir_alloc", retval, 0);
	check_u64("ctx_dir_calls", alloc_cb_ctx_calls, 1);
	check_u64("ctx_dir_no_inode", alloc_cb_ctx_no_inode, 0);
	check_u64("ctx_dir_ino", alloc_cb_ino, 12);
	check_u64("ctx_dir_mode", alloc_cb_mode, LINUX_S_IFREG | 0644);

	/* Block 12: the IND block is allocated with a context */
	reset_alloc_cb();
	retval = ext2fs_bmap2(fs, 12, NULL, NULL, BMAP_ALLOC, 12, NULL, &pblk);
	check_err("ctx_ind_alloc", retval, 0);
	check_u64("ctx_ind_calls", alloc_cb_ctx_calls, 1);
	check_u64("ctx_ind_no_inode", alloc_cb_ctx_no_inode, 0);
	check_u64("ctx_ind_ino", alloc_cb_ino, 12);
	check_u64("ctx_ind_mode", alloc_cb_mode, LINUX_S_IFREG | 0644);

	/* Block 268: the DIND block is allocated with a context */
	reset_alloc_cb();
	retval = ext2fs_bmap2(fs, 12, NULL, NULL, BMAP_ALLOC, 268, NULL,
			      &pblk);
	check_err("ctx_dind_alloc", retval, 0);
	check_u64("ctx_dind_calls", alloc_cb_ctx_calls, 1);
	check_u64("ctx_dind_no_inode", alloc_cb_ctx_no_inode, 0);
	check_u64("ctx_dind_ino", alloc_cb_ino, 12);
	check_u64("ctx_dind_mode", alloc_cb_mode, LINUX_S_IFREG | 0644);

	/* TIND-mapped block: the TIND block is allocated with a context */
	reset_alloc_cb();
	retval = ext2fs_bmap2(fs, 12, NULL, NULL, BMAP_ALLOC, tind_lblk, NULL,
			      &pblk);
	check_err("ctx_tind_alloc", retval, 0);
	check_u64("ctx_tind_calls", alloc_cb_ctx_calls, 1);
	check_u64("ctx_tind_no_inode", alloc_cb_ctx_no_inode, 0);
	check_u64("ctx_tind_ino", alloc_cb_ino, 12);
	check_u64("ctx_tind_mode", alloc_cb_mode, LINUX_S_IFREG | 0644);

	fs->get_alloc_block2 = NULL;
	teardown_fs(&fs);
	printf("test_alloc_ctx_inode: completed\n");
}

int main(int argc EXT2FS_ATTR((unused)), char *argv[] EXT2FS_ATTR((unused)))
{
	add_error_table(&et_ext2_error_table);

	test_preconditions_and_bounds();
	test_indirect_bmap();
	test_extent_bmap();
	test_bigalloc_cluster_bmap();
	test_legacy_bmap32();
	test_ind_block_io_and_errors();
	test_alloc_ctx_inode();

	if (test_fail == 0)
		printf("libext2fs bmap and ind_block unit tests passed!\n");
	else
		printf("libext2fs bmap and ind_block unit tests FAILED (%d errors)\n",
		       test_fail);

	return test_fail ? 1 : 0;
}
