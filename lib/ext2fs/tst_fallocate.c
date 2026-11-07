/*
 * tst_fallocate.c --- unit tests for fallocate.c (ext2fs_fallocate)
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

static void setup_fs_opts(ext2_filsys *ret_fs, unsigned int blocks,
			  unsigned int inodes, int log_blksize,
			  int log_clustersize, __u32 incompat,
			  __u32 ro_compat)
{
	struct ext2_super_block	param;
	errcode_t		retval;
	unsigned int		blksize = 1024U << log_blksize;

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
	param.s_feature_compat = EXT2_FEATURE_COMPAT_EXT_ATTR;
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
	if (ext2fs_has_feature_bigalloc((*ret_fs)->super)) {
		retval = ext2fs_convert_subcluster_bitmap(*ret_fs,
						&(*ret_fs)->block_map);
		if (retval) {
			com_err("setup_fs", retval,
				"converting subcluster bitmap");
			exit(1);
		}
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

static void poison_free_blocks(ext2_filsys fs, unsigned char byte)
{
	blk64_t	b, end = ext2fs_blocks_count(fs->super);

	for (b = fs->super->s_first_data_block; b < end; b++) {
		if (!ext2fs_test_block_bitmap2(fs->block_map, b))
			memset(mem_disk + b * fs->blocksize, byte,
			       fs->blocksize);
	}
}

static unsigned int count_leaf_extents(ext2_filsys fs, ext2_ino_t ino,
				       struct ext2_inode *inode)
{
	ext2_extent_handle_t	handle = NULL;
	struct ext2fs_extent	ext;
	unsigned int		cnt = 0;
	errcode_t		retval;

	retval = ext2fs_extent_open2(fs, ino, inode, &handle);
	if (retval)
		return 0;
	retval = ext2fs_extent_get(handle, EXT2_EXTENT_FIRST_SIB, &ext);
	while (!retval) {
		if (ext.e_flags & EXT2_EXTENT_FLAGS_LEAF) {
			cnt++;
			retval = ext2fs_extent_get(handle,
						   EXT2_EXTENT_NEXT_LEAF,
						   &ext);
		} else {
			retval = ext2fs_extent_get(handle, EXT2_EXTENT_DOWN,
						   &ext);
		}
	}
	ext2fs_extent_free(handle);
	return cnt;
}

/*
 * Suite 1: Flag validation, bounds checks, len==0 no-op, and inline-data error
 */
static void test_preconditions_and_flags(void)
{
	ext2_filsys		fs = NULL;
	struct ext2_inode	inode;
	blk64_t			initial_free;
	errcode_t		retval;

	setup_fs(&fs, 512, 64);
	init_reg_inode(fs, 12, EXT4_EXTENTS_FL, &inode);
	initial_free = ext2fs_free_blocks_count(fs->super);

	/* Mutually exclusive FORCE_INIT | FORCE_UNINIT */
	retval = ext2fs_fallocate(fs,
				  EXT2_FALLOCATE_FORCE_INIT |
				  EXT2_FALLOCATE_FORCE_UNINIT,
				  12, &inode, ~0ULL, 0, 4);
	check_err("flags_init_and_uninit", retval, EXT2_ET_INVALID_ARGUMENT);

	/* Unknown flag bit */
	retval = ext2fs_fallocate(fs, 0x4000, 12, &inode, ~0ULL, 0, 4);
	check_err("flags_unknown", retval, EXT2_ET_INVALID_ARGUMENT);

	/* len > s_blocks_count */
	retval = ext2fs_fallocate(fs, 0, 12, &inode, ~0ULL, 0, 513);
	check_err("len_gt_blocks_count", retval, EXT2_ET_BLOCK_ALLOC_FAIL);

	/* len == 0 is a safe no-op */
	retval = ext2fs_fallocate(fs, 0, 12, &inode, ~0ULL, 5, 0);
	check_err("len_zero_noop", retval, 0);
	check_u64("len_zero_free_unchanged",
		  ext2fs_free_blocks_count(fs->super), initial_free);

	/* Invalid ino when inode == NULL */
	retval = ext2fs_fallocate(fs, 0, 0, NULL, ~0ULL, 0, 4);
	check_err("inode_null_bad_ino", retval, EXT2_ET_BAD_INODE_NUM);

	/* Inline-data inode cannot be fallocated via block map */
	init_reg_inode(fs, 13, EXT4_INLINE_DATA_FL, &inode);
	retval = ext2fs_inline_data_init(fs, 13);
	check_err("inline_init", retval, 0);
	retval = ext2fs_fallocate(fs, 0, 13, NULL, ~0ULL, 0, 1);
	check_err("inline_fallocate_err", retval,
		  EXT2_ET_INLINE_DATA_NO_BLOCK);

	teardown_fs(&fs);
	printf("test_preconditions_and_flags: completed\n");
}

/*
 * Suite 2: Extent preallocation modes (default UNINIT, FORCE_INIT + ZERO_BLOCKS,
 * FORCE_UNINIT, inode == NULL writeback, explicit goal)
 */
static void test_extent_modes_and_zeroing(void)
{
	ext2_filsys		fs = NULL;
	struct ext2_inode	inode;
	blk64_t			pblk = 0;
	int			ret_flags = 0;
	int			i;
	errcode_t		retval;

	setup_fs(&fs, 512, 64);
	poison_free_blocks(fs, 0xAA);

	/* Default flags (0) with inode == NULL creates UNINIT extent */
	init_reg_inode(fs, 12, EXT4_EXTENTS_FL, &inode);
	retval = ext2fs_fallocate(fs, 0, 12, NULL, ~0ULL, 4, 6);
	check_err("default_uninit_falloc", retval, 0);
	retval = ext2fs_read_inode(fs, 12, &inode);
	check_err("default_reread_inode", retval, 0);
	check_u64("default_iblocks", ext2fs_inode_i_blocks(fs, &inode), 12);
	check_u64("default_isize_unchanged", EXT2_I_SIZE(&inode), 0);
	for (i = 4; i < 10; i++) {
		retval = ext2fs_bmap2(fs, 12, &inode, NULL, 0, i,
				      &ret_flags, &pblk);
		check_err("default_bmap", retval, 0);
		check_u64("default_mapped", pblk != 0, 1);
		check_u64("default_flag_uninit", ret_flags, BMAP_RET_UNINIT);
		/* Uninitialized blocks are not zeroed on disk */
		check_u64("default_disk_untouched",
			  mem_disk[pblk * fs->blocksize], 0xAA);
	}

	/* FORCE_INIT | ZERO_BLOCKS creates initialized extent & zeroes disk */
	init_reg_inode(fs, 13, EXT4_EXTENTS_FL, &inode);
	retval = ext2fs_fallocate(fs,
				  EXT2_FALLOCATE_FORCE_INIT |
				  EXT2_FALLOCATE_ZERO_BLOCKS,
				  13, &inode, 100, 0, 5);
	check_err("force_init_zero_falloc", retval, 0);
	check_u64("force_init_goal_100",
		  count_leaf_extents(fs, 13, &inode), 1);
	for (i = 0; i < 5; i++) {
		retval = ext2fs_bmap2(fs, 13, &inode, NULL, 0, i,
				      &ret_flags, &pblk);
		check_err("force_init_bmap", retval, 0);
		check_u64("force_init_goal_honored", pblk >= 100, 1);
		check_u64("force_init_flag_clear", ret_flags, 0);
		check_u64("force_init_zeroed",
			  ext2fs_mem_is_zero((char *) mem_disk +
					     pblk * fs->blocksize,
					     fs->blocksize), 1);
	}

	teardown_fs(&fs);
	printf("test_extent_modes_and_zeroing: completed\n");
}

/*
 * Suite 3: Extent growth (try_left, try_right), EOF capping vs.
 * EXT2_FALLOCATE_INIT_BEYOND_EOF, and multi-hole allocation across extents
 */
static void test_extent_growth_and_holes(void)
{
	ext2_filsys		fs = NULL;
	struct ext2_inode	inode;
	blk64_t			pblk0 = 0, pblk = 0;
	int			ret_flags = 0;
	int			i;
	errcode_t		retval;

	setup_fs(&fs, 1024, 64);

	/*
	 * Test EOF boundary behavior when extending an initialized left extent:
	 * Initialized [0..3], i_size = 6 KiB (eof_blk = 6).
	 * Fallocating [4..9] with ZERO_BLOCKS (without INIT_BEYOND_EOF) must
	 * extend the initialized left extent only through lblk 6, and allocate
	 * [7..9] as a second UNINIT extent.
	 */
	init_reg_inode(fs, 12, EXT4_EXTENTS_FL, &inode);
	retval = ext2fs_inode_size_set(fs, &inode, 6 * fs->blocksize);
	check_err("set_isize_6k", retval, 0);
	retval = ext2fs_fallocate(fs,
				  EXT2_FALLOCATE_FORCE_INIT |
				  EXT2_FALLOCATE_ZERO_BLOCKS,
				  12, &inode, 100, 0, 4);
	check_err("seed_init_left", retval, 0);
	poison_free_blocks(fs, 0x55);

	retval = ext2fs_fallocate(fs, EXT2_FALLOCATE_ZERO_BLOCKS,
				  12, &inode, ~0ULL, 4, 6);
	check_err("extend_left_capped_at_eof", retval, 0);
	check_u64("eof_cap_two_extents",
		  count_leaf_extents(fs, 12, &inode), 2);
	for (i = 0; i <= 6; i++) {
		retval = ext2fs_bmap2(fs, 12, &inode, NULL, 0, i,
				      &ret_flags, &pblk);
		check_err("eof_cap_init_part", retval, 0);
		check_u64("eof_cap_init_flag", ret_flags, 0);
		check_u64("eof_cap_zeroed",
			  ext2fs_mem_is_zero((char *) mem_disk +
					     pblk * fs->blocksize,
					     fs->blocksize), 1);
	}
	for (i = 7; i <= 9; i++) {
		retval = ext2fs_bmap2(fs, 12, &inode, NULL, 0, i,
				      &ret_flags, &pblk);
		check_err("eof_cap_uninit_part", retval, 0);
		check_u64("eof_cap_uninit_flag", ret_flags, BMAP_RET_UNINIT);
	}

	/* With EXT2_FALLOCATE_INIT_BEYOND_EOF, [0..3] extends to [0..9] as 1 extent */
	init_reg_inode(fs, 13, EXT4_EXTENTS_FL, &inode);
	retval = ext2fs_fallocate(fs,
				  EXT2_FALLOCATE_FORCE_INIT |
				  EXT2_FALLOCATE_ZERO_BLOCKS |
				  EXT2_FALLOCATE_INIT_BEYOND_EOF,
				  13, &inode, 200, 0, 4);
	check_err("seed_beyond_eof", retval, 0);
	retval = ext2fs_fallocate(fs,
				  EXT2_FALLOCATE_ZERO_BLOCKS |
				  EXT2_FALLOCATE_INIT_BEYOND_EOF,
				  13, &inode, ~0ULL, 4, 6);
	check_err("extend_beyond_eof", retval, 0);
	check_u64("beyond_eof_single_extent",
		  count_leaf_extents(fs, 13, &inode), 1);

	/*
	 * Test try_right: seed UNINIT extent at [10..13] near goal 300,
	 * then fallocate [6..9] and verify it prepends into 1 extent [6..13].
	 */
	init_reg_inode(fs, 14, EXT4_EXTENTS_FL, &inode);
	retval = ext2fs_fallocate(fs, 0, 14, &inode, 300, 10, 4);
	check_err("seed_right_ext", retval, 0);
	retval = ext2fs_bmap2(fs, 14, &inode, NULL, 0, 10, NULL, &pblk0);
	check_err("bmap_right_10", retval, 0);

	retval = ext2fs_fallocate(fs, 0, 14, &inode, ~0ULL, 6, 4);
	check_err("prepend_right_ext", retval, 0);
	check_u64("prepend_single_extent",
		  count_leaf_extents(fs, 14, &inode), 1);
	retval = ext2fs_bmap2(fs, 14, &inode, NULL, 0, 6, &ret_flags, &pblk);
	check_err("bmap_prepended_6", retval, 0);
	check_u64("prepended_pblk_contiguous", pblk, pblk0 - 4);
	check_u64("prepended_uninit", ret_flags, BMAP_RET_UNINIT);

	/*
	 * Multi-hole span: seed INIT [2..3] and UNINIT [8..9], then fallocate
	 * [0..11] and verify existing mappings are preserved while holes fill.
	 */
	init_reg_inode(fs, 15, EXT4_EXTENTS_FL, &inode);
	retval = ext2fs_fallocate(fs,
				  EXT2_FALLOCATE_FORCE_INIT |
				  EXT2_FALLOCATE_ZERO_BLOCKS |
				  EXT2_FALLOCATE_INIT_BEYOND_EOF,
				  15, &inode, 400, 2, 2);
	check_err("seed_hole_init_2_3", retval, 0);
	retval = ext2fs_bmap2(fs, 15, &inode, NULL, 0, 2, NULL, &pblk0);
	check_err("bmap_seed_2", retval, 0);
	retval = ext2fs_fallocate(fs, EXT2_FALLOCATE_FORCE_UNINIT,
				  15, &inode, 450, 8, 2);
	check_err("seed_hole_uninit_8_9", retval, 0);

	retval = ext2fs_fallocate(fs, 0, 15, &inode, ~0ULL, 0, 12);
	check_err("falloc_across_holes", retval, 0);
	check_u64("falloc_across_iblocks",
		  ext2fs_inode_i_blocks(fs, &inode), 24);
	for (i = 0; i < 12; i++) {
		retval = ext2fs_bmap2(fs, 15, &inode, NULL, 0, i,
				      &ret_flags, &pblk);
		check_err("across_holes_bmap", retval, 0);
		check_u64("across_holes_nonzero", pblk != 0, 1);
		if (i == 2 || i == 3) {
			check_u64("across_holes_preserved_pblk",
				  pblk, pblk0 + (i - 2));
			check_u64("across_holes_preserved_init", ret_flags, 0);
		} else {
			check_u64("across_holes_filled_uninit",
				  ret_flags, BMAP_RET_UNINIT);
		}
	}

	/*
	 * Two-extent merge (try_merge): allocate [0..5] UNINIT, punch middle
	 * [2..3] so physical gap [pblk0+2..pblk0+3] is free, then fallocate
	 * [2..3] and verify both extents merge into a single [0..5] extent.
	 */
	init_reg_inode(fs, 16, EXT4_EXTENTS_FL, &inode);
	retval = ext2fs_fallocate(fs, EXT2_FALLOCATE_FORCE_UNINIT,
				  16, &inode, 500, 0, 6);
	check_err("merge_uninit_seed", retval, 0);
	retval = ext2fs_bmap2(fs, 16, &inode, NULL, 0, 0, NULL, &pblk0);
	check_err("merge_uninit_bmap0", retval, 0);
	retval = ext2fs_punch(fs, 16, &inode, NULL, 2, 3);
	check_err("merge_uninit_punch_hole", retval, 0);
	check_u64("merge_uninit_two_before",
		  count_leaf_extents(fs, 16, &inode), 2);
	retval = ext2fs_fallocate(fs, 0, 16, &inode, ~0ULL, 2, 2);
	check_err("merge_uninit_fill_hole", retval, 0);
	check_u64("merge_uninit_one_after",
		  count_leaf_extents(fs, 16, &inode), 1);

	/*
	 * Two-extent merge with INIT + ZERO_BLOCKS at lblk 1: verify both
	 * extents merge into 1, physical block pblk0+1 is zeroed, and physical
	 * block 1 (primary superblock on 1 KiB fs, == range_start!) is intact.
	 */
	init_reg_inode(fs, 17, EXT4_EXTENTS_FL, &inode);
	retval = ext2fs_inode_size_set(fs, &inode, 3 * fs->blocksize);
	check_err("merge_init_isize", retval, 0);
	retval = ext2fs_fallocate(fs,
				  EXT2_FALLOCATE_FORCE_INIT |
				  EXT2_FALLOCATE_ZERO_BLOCKS,
				  17, &inode, 550, 0, 3);
	check_err("merge_init_seed", retval, 0);
	retval = ext2fs_bmap2(fs, 17, &inode, NULL, 0, 0, NULL, &pblk0);
	check_err("merge_init_bmap0", retval, 0);
	retval = ext2fs_punch(fs, 17, &inode, NULL, 1, 1);
	check_err("merge_init_punch_1", retval, 0);
	check_u64("merge_init_two_before",
		  count_leaf_extents(fs, 17, &inode), 2);
	poison_free_blocks(fs, 0xA5);
	memset(mem_disk + 1 * fs->blocksize, 0x5E, fs->blocksize);

	retval = ext2fs_fallocate(fs,
				  EXT2_FALLOCATE_FORCE_INIT |
				  EXT2_FALLOCATE_ZERO_BLOCKS,
				  17, &inode, ~0ULL, 1, 1);
	check_err("merge_init_fill_1", retval, 0);
	check_u64("merge_init_one_after",
		  count_leaf_extents(fs, 17, &inode), 1);
	retval = ext2fs_bmap2(fs, 17, &inode, NULL, 0, 1, &ret_flags, &pblk);
	check_err("merge_init_bmap1", retval, 0);
	check_u64("merge_init_pblk1_contig", pblk, pblk0 + 1);
	check_u64("merge_init_pblk1_init", ret_flags, 0);
	check_u64("merge_init_pblk1_zeroed",
		  ext2fs_mem_is_zero((char *) mem_disk + pblk * fs->blocksize,
				     fs->blocksize), 1);
	check_u64("merge_init_phys_blk1_untouched",
		  mem_disk[1 * fs->blocksize], 0x5E);

	teardown_fs(&fs);
	printf("test_extent_growth_and_holes: completed\n");
}

/*
 * Suite 4: Splitting allocations larger than EXT_UNINIT_MAX_LEN (32767) and
 * EXT_INIT_MAX_LEN (32768)
 */
static void test_max_extent_len_splitting(void)
{
	ext2_filsys		fs = NULL;
	struct ext2_inode	inode;
	blk64_t			pblk = 0;
	int			ret_flags = 0;
	errcode_t		retval;

	/* 40960 1-KiB blocks (40 MiB in-memory fs) */
	setup_fs(&fs, 40960, 64);

	init_reg_inode(fs, 12, EXT4_EXTENTS_FL, &inode);
	retval = ext2fs_fallocate(fs, EXT2_FALLOCATE_FORCE_UNINIT,
				  12, &inode, ~0ULL, 0, 35000);
	check_err("large_uninit_falloc", retval, 0);
	check_u64("large_uninit_split_ge_2",
		  count_leaf_extents(fs, 12, &inode) >= 2, 1);
	check_u64("large_uninit_iblocks_ge_70000",
		  ext2fs_inode_i_blocks(fs, &inode) >= 70000, 1);
	retval = ext2fs_bmap2(fs, 12, &inode, NULL, 0, 34999,
			      &ret_flags, &pblk);
	check_err("large_uninit_last_lblk", retval, 0);
	check_u64("large_uninit_last_mapped", pblk != 0, 1);
	check_u64("large_uninit_last_flag", ret_flags, BMAP_RET_UNINIT);

	/* Free inode 12's blocks and test > EXT_INIT_MAX_LEN (32768) */
	retval = ext2fs_punch(fs, 12, &inode, NULL, 0, ~0ULL);
	check_err("punch_large_uninit", retval, 0);
	check_u64("punch_large_uninit_iblocks_zero",
		  ext2fs_inode_i_blocks(fs, &inode), 0);

	init_reg_inode(fs, 13, EXT4_EXTENTS_FL, &inode);
	retval = ext2fs_fallocate(fs, EXT2_FALLOCATE_FORCE_INIT,
				  13, &inode, ~0ULL, 0, 35000);
	check_err("large_init_falloc", retval, 0);
	check_u64("large_init_split_ge_2",
		  count_leaf_extents(fs, 13, &inode) >= 2, 1);
	retval = ext2fs_bmap2(fs, 13, &inode, NULL, 0, 34999,
			      &ret_flags, &pblk);
	check_err("large_init_last_lblk", retval, 0);
	check_u64("large_init_last_mapped", pblk != 0, 1);
	check_u64("large_init_last_flag", ret_flags, 0);

	teardown_fs(&fs);
	printf("test_max_extent_len_splitting: completed\n");
}

/*
 * Suite 5: Bigalloc implied cluster allocations (expand_left, expand_right,
 * and try_anywhere non-adjacent implied cluster mapping)
 */
static void test_bigalloc_fallocate(void)
{
	ext2_filsys		fs = NULL;
	struct ext2_inode	inode;
	ext2_extent_handle_t	handle = NULL;
	struct ext2fs_extent	ext;
	blk64_t			p_c0 = 0, p_c2 = 0, plen = 0, pblk = 0;
	blk64_t			free_before;
	int			i;
	errcode_t		retval;

	/* 1 KiB blocks, 4 KiB clusters (cluster_ratio = 4) */
	setup_fs_opts(&fs, 1024, 64, 0, 2,
		      EXT3_FEATURE_INCOMPAT_EXTENTS |
		      EXT4_FEATURE_INCOMPAT_64BIT,
		      EXT4_FEATURE_RO_COMPAT_BIGALLOC |
		      EXT4_FEATURE_RO_COMPAT_HUGE_FILE);

	/*
	 * Part A: Seed [0..1] at p_c0..p_c0+1 and [10..11] at p_c2+2..p_c2+3.
	 * Fallocating [2..9] should use implied blocks for [2..3] (expand_left)
	 * and [8..9] (expand_right), allocating only 1 new cluster for [4..7].
	 */
	init_reg_inode(fs, 12, EXT4_EXTENTS_FL, &inode);
	retval = ext2fs_new_range(fs, EXT2_NEWRANGE_MIN_LENGTH, 100, 4,
				  NULL, &p_c0, &plen);
	check_err("bigalloc_alloc_c0", retval, 0);
	ext2fs_block_alloc_stats_range(fs, p_c0, 4, +1);
	ext2fs_iblk_add_blocks(fs, &inode, 1);

	retval = ext2fs_new_range(fs, EXT2_NEWRANGE_MIN_LENGTH, 200, 4,
				  NULL, &p_c2, &plen);
	check_err("bigalloc_alloc_c2", retval, 0);
	ext2fs_block_alloc_stats_range(fs, p_c2, 4, +1);
	ext2fs_iblk_add_blocks(fs, &inode, 1);

	retval = ext2fs_extent_open2(fs, 12, &inode, &handle);
	check_err("bigalloc_open_ext", retval, 0);
	memset(&ext, 0, sizeof(ext));
	ext.e_lblk = 0;
	ext.e_pblk = p_c0;
	ext.e_len = 2;
	ext.e_flags = EXT2_EXTENT_FLAGS_LEAF | EXT2_EXTENT_FLAGS_UNINIT;
	retval = ext2fs_extent_insert(handle, EXT2_EXTENT_INSERT_AFTER, &ext);
	check_err("bigalloc_insert_left", retval, 0);
	ext.e_lblk = 10;
	ext.e_pblk = p_c2 + 2;
	ext.e_len = 2;
	retval = ext2fs_extent_insert(handle, EXT2_EXTENT_INSERT_AFTER, &ext);
	check_err("bigalloc_insert_right", retval, 0);
	ext2fs_extent_free(handle);

	free_before = ext2fs_free_blocks_count(fs->super);
	retval = ext2fs_fallocate(fs, 0, 12, &inode, ~0ULL, 2, 8);
	check_err("bigalloc_falloc_2_9", retval, 0);

	/* Only 1 cluster (4 blocks) newly claimed for middle [4..7] */
	check_u64("bigalloc_only_1_new_cluster",
		  free_before - ext2fs_free_blocks_count(fs->super), 4);
	check_u64("bigalloc_iblocks_3_clusters",
		  ext2fs_inode_i_blocks(fs, &inode), 24);
	for (i = 0; i < 12; i++) {
		retval = ext2fs_bmap2(fs, 12, &inode, NULL, 0, i, NULL, &pblk);
		check_err("bigalloc_bmap_0_11", retval, 0);
		if (i < 4)
			check_u64("bigalloc_c0_implied", pblk, p_c0 + i);
		else if (i >= 8)
			check_u64("bigalloc_c2_implied", pblk, p_c2 + (i - 8));
		else
			check_u64("bigalloc_mid_mapped", pblk != 0, 1);
	}

	/*
	 * Part B: Seed [0..0] at p_c0, leave hole at lblk 1, and fallocate
	 * [2..3].  Because left_adjacent is NULL across the hole, try_anywhere
	 * resolves the implied cluster allocation with 0 new clusters claimed.
	 */
	init_reg_inode(fs, 13, EXT4_EXTENTS_FL, &inode);
	retval = ext2fs_fallocate(fs, 0, 13, &inode, ~0ULL, 0, 1);
	check_err("bigalloc_seed_0", retval, 0);
	retval = ext2fs_bmap2(fs, 13, &inode, NULL, 0, 0, NULL, &p_c0);
	check_err("bigalloc_bmap_seed_0", retval, 0);
	free_before = ext2fs_free_blocks_count(fs->super);

	retval = ext2fs_fallocate(fs, 0, 13, &inode, ~0ULL, 2, 2);
	check_err("bigalloc_nonadj_implied", retval, 0);
	check_u64("bigalloc_nonadj_zero_new_clusters",
		  ext2fs_free_blocks_count(fs->super), free_before);
	retval = ext2fs_bmap2(fs, 13, &inode, NULL, 0, 2, NULL, &pblk);
	check_err("bigalloc_nonadj_bmap_2", retval, 0);
	check_u64("bigalloc_nonadj_pblk_2", pblk, p_c0 + 2);

	teardown_fs(&fs);
	printf("test_bigalloc_fallocate: completed\n");
}

/*
 * Suite 6: Indirect-mapped inode ext2fs_fallocate() (zeroing newly allocated
 * direct & indirect blocks while preserving already-mapped blocks)
 */
static void test_indirect_fallocate(void)
{
	ext2_filsys		fs = NULL;
	struct ext2_inode	inode;
	blk64_t			pre_pblk = 0, pblk = 0;
	int			i;
	errcode_t		retval;

	setup_fs(&fs, 512, 64);
	init_reg_inode(fs, 12, 0, &inode);

	/* Pre-allocate logical block 2 with sentinel pattern 0x3C */
	retval = ext2fs_bmap2(fs, 12, &inode, NULL, BMAP_ALLOC, 2,
			      NULL, &pre_pblk);
	check_err("ind_prealloc_2", retval, 0);
	memset(mem_disk + pre_pblk * fs->blocksize, 0x3C, fs->blocksize);

	/* Poison all unallocated blocks so we can verify zeroing */
	poison_free_blocks(fs, 0xCC);

	/* Fallocate 0..15 (direct 0..11 + single-indirect 12..15) */
	retval = ext2fs_fallocate(fs, 0, 12, NULL, ~0ULL, 0, 16);
	check_err("ind_fallocate_0_15", retval, 0);
	retval = ext2fs_read_inode(fs, 12, &inode);
	check_err("ind_reread_inode", retval, 0);

	/* 16 data blocks + 1 single-indirect block = 17 KiB = 34 sectors */
	check_u64("ind_iblocks_34", ext2fs_inode_i_blocks(fs, &inode), 34);

	for (i = 0; i < 16; i++) {
		retval = ext2fs_bmap2(fs, 12, &inode, NULL, 0, i, NULL, &pblk);
		check_err("ind_bmap_check", retval, 0);
		check_u64("ind_bmap_nonzero", pblk != 0, 1);
		if (i == 2) {
			check_u64("ind_blk2_preserved_pblk", pblk, pre_pblk);
			check_u64("ind_blk2_preserved_data",
				  mem_disk[pblk * fs->blocksize], 0x3C);
		} else {
			check_u64("ind_new_blk_zeroed",
				  ext2fs_mem_is_zero((char *) mem_disk +
						     pblk * fs->blocksize,
						     fs->blocksize), 1);
		}
	}

	teardown_fs(&fs);
	printf("test_indirect_fallocate: completed\n");
}

int main(int argc EXT2FS_ATTR((unused)), char *argv[] EXT2FS_ATTR((unused)))
{
	add_error_table(&et_ext2_error_table);

	test_preconditions_and_flags();
	test_extent_modes_and_zeroing();
	test_extent_growth_and_holes();
	test_max_extent_len_splitting();
	test_bigalloc_fallocate();
	test_indirect_fallocate();

	if (test_fail == 0)
		printf("libext2fs fallocate unit tests passed!\n");
	else
		printf("libext2fs fallocate unit tests FAILED (%d errors)\n",
		       test_fail);

	return test_fail ? 1 : 0;
}
