/*
 * tst_punch.c --- unit tests for punch.c (ext2fs_punch)
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
	fail_read_blk = 0;
	fail_write_blk = 0;
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
 * Suite 1: Argument validation and inline-data punching
 */
static void test_preconditions_and_inline_punch(void)
{
	ext2_filsys		fs = NULL;
	struct ext2_inode	inode;
	unsigned char		payload[100];
	unsigned char		got_buf[100];
	size_t			got_size = 0;
	errcode_t		retval;
	int			i;

	setup_fs(&fs, 512, 64);
	init_reg_inode(fs, 12, 0, &inode);

	/* start > end returns EINVAL */
	retval = ext2fs_punch(fs, 12, &inode, NULL, 10, 5);
	check_err("punch_start_gt_end", retval, EINVAL);

	/* Invalid ino when inode == NULL */
	retval = ext2fs_punch(fs, 0, NULL, NULL, 0, 10);
	check_err("punch_bad_ino", retval, EXT2_ET_BAD_INODE_NUM);

	/* Inline-data inode with > 60 bytes (spilling into system.data EA) */
	init_reg_inode(fs, 13, EXT4_INLINE_DATA_FL, &inode);
	retval = ext2fs_inline_data_init(fs, 13);
	check_err("inline_data_init", retval, 0);
	for (i = 0; i < (int) sizeof(payload); i++)
		payload[i] = (unsigned char) (0x30 + (i % 60));
	retval = ext2fs_inline_data_set(fs, 13, NULL, payload,
					sizeof(payload));
	check_err("inline_data_set_100", retval, 0);

	/* Punching at start > 0 is a no-op for inline-data inodes */
	retval = ext2fs_punch(fs, 13, NULL, NULL, 1, ~0ULL);
	check_err("inline_punch_start_gt_0", retval, 0);
	retval = ext2fs_inline_data_size(fs, 13, &got_size);
	check_err("inline_size_preserved_rc", retval, 0);
	check_u64("inline_size_preserved", got_size, sizeof(payload));
	memset(got_buf, 0, sizeof(got_buf));
	retval = ext2fs_inline_data_get(fs, 13, NULL, got_buf, &got_size);
	check_err("inline_get_preserved", retval, 0);
	check_u64("inline_data_preserved",
		  memcmp(payload, got_buf, sizeof(payload)) == 0, 1);

	/* Punching from start == 0 clears i_block, i_size, and system.data EA */
	retval = ext2fs_punch(fs, 13, NULL, NULL, 0, ~0ULL);
	check_err("inline_punch_all", retval, 0);
	retval = ext2fs_read_inode(fs, 13, &inode);
	check_err("inline_read_after_punch", retval, 0);
	check_u64("inline_isize_zero", EXT2_I_SIZE(&inode), 0);
	check_u64("inline_flag_kept",
		  (inode.i_flags & EXT4_INLINE_DATA_FL) != 0, 1);
	check_u64("inline_iblock_zeroed",
		  ext2fs_mem_is_zero((char *) inode.i_block,
				     EXT4_MIN_INLINE_DATA_SIZE), 1);
	retval = ext2fs_inline_data_size(fs, 13, &got_size);
	check_err("inline_size_after_punch_rc", retval, 0);
	check_u64("inline_ea_removed_min_size", got_size,
		  EXT4_MIN_INLINE_DATA_SIZE);

	teardown_fs(&fs);
	printf("test_preconditions_and_inline_punch: completed\n");
}

/*
 * Suite 2: Indirect block map punching across direct, IND, DIND, and TIND
 */
static void test_indirect_punch(void)
{
	ext2_filsys		fs = NULL;
	struct ext2_inode	inode;
	char			*block_buf = NULL;
	blk64_t			tind_base = 12 + 256 + (256 * 256);
	blk64_t			lblks[12];
	blk64_t			pblks[12];
	blk64_t			pblk = 0;
	blk64_t			initial_free;
	int			i;
	errcode_t		retval;

	setup_fs(&fs, 1024, 64);
	init_reg_inode(fs, 12, 0, &inode);
	initial_free = ext2fs_free_blocks_count(fs->super);

	retval = ext2fs_get_array(3, fs->blocksize, &block_buf);
	check_err("alloc_punch_buf", retval, 0);

	lblks[0] = 0;
	lblks[1] = 1;
	lblks[2] = 5;
	lblks[3] = 11;
	lblks[4] = 12;
	lblks[5] = 13;
	lblks[6] = 200;
	lblks[7] = 268;
	lblks[8] = 269;
	lblks[9] = 600;
	lblks[10] = tind_base + 4;
	lblks[11] = tind_base + 5;

	for (i = 0; i < 12; i++) {
		retval = ext2fs_bmap2(fs, 12, &inode, NULL, BMAP_ALLOC,
				      lblks[i], NULL, &pblks[i]);
		check_err("ind_alloc_lblk", retval, 0);
	}

	/* Punching above 2^32-1 on indirect inode is a safe no-op */
	retval = ext2fs_punch(fs, 12, &inode, block_buf,
			      (1ULL << 32), (1ULL << 32) + 10);
	check_err("ind_punch_above_32bit", retval, 0);
	check_u64("dir0_still_set", inode.i_block[0], pblks[0]);

	/* Punching sparse hole 2..4 frees nothing */
	retval = ext2fs_punch(fs, 12, &inode, block_buf, 2, 4);
	check_err("ind_punch_hole", retval, 0);
	check_u64("dir1_still_set", inode.i_block[1], pblks[1]);
	check_u64("dir5_still_set", inode.i_block[5], pblks[2]);

	/* Punch single direct block 1 */
	retval = ext2fs_punch(fs, 12, &inode, block_buf, 1, 1);
	check_err("ind_punch_dir1", retval, 0);
	check_u64("dir1_cleared", inode.i_block[1], 0);
	check_u64("dir1_bitmap_freed",
		  ext2fs_test_block_bitmap2(fs->block_map, pblks[1]), 0);

	/* Partial IND punch (12..12): frees data block, retains IND block */
	retval = ext2fs_punch(fs, 12, &inode, NULL, 12, 12);
	check_err("ind_punch_12", retval, 0);
	check_u64("ind_block_retained", inode.i_block[EXT2_IND_BLOCK] != 0, 1);
	check_u64("ind12_bitmap_freed",
		  ext2fs_test_block_bitmap2(fs->block_map, pblks[4]), 0);

	/* Punch rest of IND range (13..267): frees 13, 200 AND empty IND */
	retval = ext2fs_punch(fs, 12, &inode, block_buf, 13, 267);
	check_err("ind_punch_rest_of_ind", retval, 0);
	check_u64("ind_block_freed", inode.i_block[EXT2_IND_BLOCK], 0);
	check_u64("ind13_bitmap_freed",
		  ext2fs_test_block_bitmap2(fs->block_map, pblks[5]), 0);
	check_u64("ind200_bitmap_freed",
		  ext2fs_test_block_bitmap2(fs->block_map, pblks[6]), 0);

	/* Partial DIND punch (268..269): frees first L1 IND, retains DIND */
	retval = ext2fs_punch(fs, 12, &inode, block_buf, 268, 269);
	check_err("ind_punch_dind_l1", retval, 0);
	check_u64("dind_block_retained",
		  inode.i_block[EXT2_DIND_BLOCK] != 0, 1);
	retval = ext2fs_bmap2(fs, 12, &inode, NULL, 0, 600, NULL, &pblk);
	check_err("dind_600_lookup", retval, 0);
	check_u64("dind_600_intact", pblk, pblks[9]);

	/* Truncate all remaining blocks (0..~0ULL) via inode == NULL */
	retval = ext2fs_punch(fs, 12, NULL, NULL, 0, ~0ULL);
	check_err("ind_truncate_all", retval, 0);
	retval = ext2fs_read_inode(fs, 12, &inode);
	check_err("ind_read_after_truncate", retval, 0);
	check_u64("ind_iblocks_zero", ext2fs_inode_i_blocks(fs, &inode), 0);
	for (i = 0; i < EXT2_N_BLOCKS; i++)
		check_u64("ind_iblock_slot_zero", inode.i_block[i], 0);
	check_u64("ind_all_blocks_returned",
		  ext2fs_free_blocks_count(fs->super), initial_free);

	ext2fs_free_mem(&block_buf);
	teardown_fs(&fs);
	printf("test_indirect_punch: completed\n");
}

/*
 * Helper to insert a contiguous extent and allocate its physical blocks
 */
static void alloc_extent_run(ext2_filsys fs, ext2_ino_t ino,
			     struct ext2_inode *inode, blk64_t lblk,
			     unsigned int len, int uninit, blk64_t *ret_pblk)
{
	ext2_extent_handle_t	handle = NULL;
	struct ext2fs_extent	ext;
	blk64_t			pblk = 0, plen = 0;
	errcode_t		retval;

	retval = ext2fs_new_range(fs, EXT2_NEWRANGE_MIN_LENGTH, 0,
				  len, NULL, &pblk, &plen);
	check_err("new_range_run", retval, 0);
	ext2fs_block_alloc_stats_range(fs, pblk, len, +1);
	ext2fs_iblk_add_blocks(fs, inode, len / EXT2FS_CLUSTER_RATIO(fs));

	retval = ext2fs_extent_open2(fs, ino, inode, &handle);
	check_err("extent_open2_run", retval, 0);
	ext2fs_extent_goto(handle, lblk);
	memset(&ext, 0, sizeof(ext));
	ext.e_lblk = lblk;
	ext.e_pblk = pblk;
	ext.e_len = len;
	ext.e_flags = EXT2_EXTENT_FLAGS_LEAF |
		      (uninit ? EXT2_EXTENT_FLAGS_UNINIT : 0);
	retval = ext2fs_extent_insert(handle, EXT2_EXTENT_INSERT_AFTER, &ext);
	check_err("extent_insert_run", retval, 0);
	retval = ext2fs_extent_fix_parents(handle);
	if (retval && retval != EXT2_ET_NO_CURRENT_NODE)
		check_err("extent_fix_parents_run", retval, 0);
	ext2fs_extent_free(handle);
	retval = ext2fs_write_inode(fs, ino, inode);
	check_err("write_inode_run", retval, 0);
	if (ret_pblk)
		*ret_pblk = pblk;
}

/*
 * Suite 3: Extent punching (empty, holes, split middle, trim head/tail,
 * UNINIT flag preservation, multi-extent deletion, and multi-level tree collapse)
 */
static void test_extent_punch(void)
{
	ext2_filsys		fs = NULL;
	struct ext2_inode	inode;
	blk64_t			p_first = 0, p_second = 0, pblk = 0;
	blk64_t			initial_free;
	int			ret_flags = 0;
	int			i;
	errcode_t		retval;

	setup_fs(&fs, 1024, 64);
	init_reg_inode(fs, 12, EXT4_EXTENTS_FL, &inode);
	initial_free = ext2fs_free_blocks_count(fs->super);

	/* Empty extent tree punch is a no-op */
	retval = ext2fs_punch(fs, 12, &inode, NULL, 0, 100);
	check_err("ext_punch_empty", retval, 0);

	/* Insert initialized [10..19] and uninitialized [30..39] */
	alloc_extent_run(fs, 12, &inode, 10, 10, 0, &p_first);
	alloc_extent_run(fs, 12, &inode, 30, 10, 1, &p_second);

	/* Punch before first extent (0..4), in hole (22..27), and past end (50..60) */
	retval = ext2fs_punch(fs, 12, &inode, NULL, 0, 4);
	check_err("ext_punch_before", retval, 0);
	retval = ext2fs_punch(fs, 12, &inode, NULL, 22, 27);
	check_err("ext_punch_between", retval, 0);
	retval = ext2fs_punch(fs, 12, &inode, NULL, 50, 60);
	check_err("ext_punch_after", retval, 0);
	check_u64("ext_iblocks_unchanged",
		  ext2fs_inode_i_blocks(fs, &inode), 40);

	/* Case 3: Split middle of uninitialized [30..39] by punching 33..36 */
	retval = ext2fs_punch(fs, 12, &inode, NULL, 33, 36);
	check_err("ext_split_uninit", retval, 0);
	check_u64("ext_iblocks_after_split",
		  ext2fs_inode_i_blocks(fs, &inode), 32);

	/* Verify [30..32] and [37..39] remain UNINIT, while 33..36 are holes */
	retval = ext2fs_bmap2(fs, 12, &inode, NULL, 0, 32, &ret_flags, &pblk);
	check_err("bmap_32", retval, 0);
	check_u64("bmap_32_pblk", pblk, p_second + 2);
	check_u64("bmap_32_uninit", ret_flags, BMAP_RET_UNINIT);

	retval = ext2fs_bmap2(fs, 12, &inode, NULL, 0, 34, &ret_flags, &pblk);
	check_err("bmap_34_hole", retval, 0);
	check_u64("bmap_34_zero", pblk, 0);
	check_u64("pblk_34_freed",
		  ext2fs_test_block_bitmap2(fs->block_map, p_second + 4), 0);

	retval = ext2fs_bmap2(fs, 12, &inode, NULL, 0, 37, &ret_flags, &pblk);
	check_err("bmap_37", retval, 0);
	check_u64("bmap_37_pblk", pblk, p_second + 7);
	check_u64("bmap_37_uninit", ret_flags, BMAP_RET_UNINIT);

	/* Case 1: Trim head of [10..19] by punching 8..12 -> leaves [13..19] */
	retval = ext2fs_punch(fs, 12, &inode, NULL, 8, 12);
	check_err("ext_trim_head", retval, 0);
	retval = ext2fs_bmap2(fs, 12, &inode, NULL, 0, 12, NULL, &pblk);
	check_err("bmap_12_trimmed", retval, 0);
	check_u64("bmap_12_zero", pblk, 0);
	retval = ext2fs_bmap2(fs, 12, &inode, NULL, 0, 13, NULL, &pblk);
	check_err("bmap_13_kept", retval, 0);
	check_u64("bmap_13_pblk", pblk, p_first + 3);

	/* Case 2: Trim tail of [13..19] by punching 17..21 -> leaves [13..16] */
	retval = ext2fs_punch(fs, 12, &inode, NULL, 17, 21);
	check_err("ext_trim_tail", retval, 0);
	retval = ext2fs_bmap2(fs, 12, &inode, NULL, 0, 16, NULL, &pblk);
	check_err("bmap_16_kept", retval, 0);
	check_u64("bmap_16_pblk", pblk, p_first + 6);
	retval = ext2fs_bmap2(fs, 12, &inode, NULL, 0, 17, NULL, &pblk);
	check_err("bmap_17_trimmed", retval, 0);
	check_u64("bmap_17_zero", pblk, 0);

	/* Truncate entire inode and verify all blocks returned */
	retval = ext2fs_punch(fs, 12, &inode, NULL, 0, ~0ULL);
	check_err("ext_truncate_12", retval, 0);
	check_u64("ext_iblocks_zero_12", ext2fs_inode_i_blocks(fs, &inode), 0);
	check_u64("ext_free_restored_12",
		  ext2fs_free_blocks_count(fs->super), initial_free);

	/* Multi-level extent tree (12 separated extents -> depth >= 1) */
	init_reg_inode(fs, 13, EXT4_EXTENTS_FL, &inode);
	for (i = 0; i < 12; i++)
		alloc_extent_run(fs, 13, &inode, (blk64_t) i * 20, 4, 0, NULL);

	/* Punch middle span across multiple leaves (38..165), then rest */
	retval = ext2fs_punch(fs, 13, &inode, NULL, 38, 165);
	check_err("ext_multilevel_mid_punch", retval, 0);
	retval = ext2fs_bmap2(fs, 13, &inode, NULL, 0, 0, NULL, &pblk);
	check_err("ext_multilevel_0_intact", retval, 0);
	check_u64("ext_multilevel_0_nonzero", pblk != 0, 1);
	retval = ext2fs_bmap2(fs, 13, &inode, NULL, 0, 80, NULL, &pblk);
	check_err("ext_multilevel_80_hole", retval, 0);
	check_u64("ext_multilevel_80_zero", pblk, 0);
	retval = ext2fs_bmap2(fs, 13, &inode, NULL, 0, 220, NULL, &pblk);
	check_err("ext_multilevel_220_intact", retval, 0);
	check_u64("ext_multilevel_220_nonzero", pblk != 0, 1);

	retval = ext2fs_punch(fs, 13, NULL, NULL, 0, ~0ULL);
	check_err("ext_multilevel_truncate", retval, 0);
	retval = ext2fs_read_inode(fs, 13, &inode);
	check_err("ext_multilevel_reread", retval, 0);
	check_u64("ext_multilevel_iblocks_zero",
		  ext2fs_inode_i_blocks(fs, &inode), 0);
	check_u64("ext_multilevel_free_restored",
		  ext2fs_free_blocks_count(fs->super), initial_free);

	teardown_fs(&fs);
	printf("test_extent_punch: completed\n");
}

/*
 * Suite 4: Bigalloc cluster-aware extent punching
 */
static void test_bigalloc_punch(void)
{
	ext2_filsys		fs = NULL;
	struct ext2_inode	inode;
	blk64_t			p_base = 0;
	blk64_t			initial_free;
	errcode_t		retval;

	/* 1 KiB blocks, 4 KiB clusters (cluster_ratio = 4) */
	setup_fs_opts(&fs, 1024, 64, 0, 2,
		      EXT3_FEATURE_INCOMPAT_EXTENTS |
		      EXT4_FEATURE_INCOMPAT_64BIT,
		      EXT4_FEATURE_RO_COMPAT_BIGALLOC |
		      EXT4_FEATURE_RO_COMPAT_HUGE_FILE);
	init_reg_inode(fs, 12, EXT4_EXTENTS_FL, &inode);
	initial_free = ext2fs_free_blocks_count(fs->super);

	/* Allocate 3 clusters = 12 blocks [0..11] -> 24 512B sectors */
	alloc_extent_run(fs, 12, &inode, 0, 12, 0, &p_base);
	check_u64("bigalloc_init_iblocks",
		  ext2fs_inode_i_blocks(fs, &inode), 24);

	/* Partial cluster punch 0..1: cluster 0 still has 2..3 mapped */
	retval = ext2fs_punch(fs, 12, &inode, NULL, 0, 1);
	check_err("bigalloc_partial_head", retval, 0);
	check_u64("cluster0_still_marked",
		  ext2fs_test_block_bitmap2(fs->block_map, p_base), 1);
	check_u64("bigalloc_iblocks_still_24",
		  ext2fs_inode_i_blocks(fs, &inode), 24);

	/* Punch remaining 2..3 of cluster 0: now cluster 0 is freed! */
	retval = ext2fs_punch(fs, 12, &inode, NULL, 2, 3);
	check_err("bigalloc_finish_cluster0", retval, 0);
	check_u64("cluster0_freed",
		  ext2fs_test_block_bitmap2(fs->block_map, p_base), 0);
	check_u64("bigalloc_iblocks_16",
		  ext2fs_inode_i_blocks(fs, &inode), 16);

	/* Punch 6..11 (tail of cluster 1 + whole cluster 2): frees cluster 2,
	 * keeps cluster 1 because 4..5 remain mapped */
	retval = ext2fs_punch(fs, 12, &inode, NULL, 6, 11);
	check_err("bigalloc_tail_and_cluster2", retval, 0);
	check_u64("cluster1_still_marked",
		  ext2fs_test_block_bitmap2(fs->block_map, p_base + 4), 1);
	check_u64("cluster2_freed",
		  ext2fs_test_block_bitmap2(fs->block_map, p_base + 8), 0);
	check_u64("bigalloc_iblocks_8",
		  ext2fs_inode_i_blocks(fs, &inode), 8);

	/* Punch remaining 4..5 -> cluster 1 freed, all clusters restored */
	retval = ext2fs_punch(fs, 12, &inode, NULL, 4, 5);
	check_err("bigalloc_finish_cluster1", retval, 0);
	check_u64("cluster1_freed",
		  ext2fs_test_block_bitmap2(fs->block_map, p_base + 4), 0);
	check_u64("bigalloc_iblocks_0",
		  ext2fs_inode_i_blocks(fs, &inode), 0);
	check_u64("bigalloc_free_restored",
		  ext2fs_free_blocks_count(fs->super), initial_free);

	teardown_fs(&fs);
	printf("test_bigalloc_punch: completed\n");
}

/*
 * Suite 5: Corrupt extent block bounds and I/O fault propagation
 */
static void test_punch_error_paths(void)
{
	ext2_filsys		fs = NULL;
	struct ext2_inode	ext_ino, ind_ino;
	ext2_extent_handle_t	handle = NULL;
	struct ext2fs_extent	ext;
	blk64_t			pblk = 0;
	errcode_t		retval;

	setup_fs(&fs, 512, 64);

	/* Corrupt extent pointing past s_blocks_count -> EXT2_ET_BAD_BLOCK_NUM */
	init_reg_inode(fs, 12, EXT4_EXTENTS_FL, &ext_ino);
	retval = ext2fs_extent_open2(fs, 12, &ext_ino, &handle);
	check_err("extent_open_corrupt", retval, 0);
	memset(&ext, 0, sizeof(ext));
	ext.e_lblk = 0;
	ext.e_pblk = 9999;
	ext.e_len = 2;
	ext.e_flags = EXT2_EXTENT_FLAGS_LEAF;
	retval = ext2fs_extent_insert(handle, EXT2_EXTENT_INSERT_AFTER, &ext);
	check_err("extent_insert_corrupt", retval, 0);
	ext2fs_extent_free(handle);

	retval = ext2fs_punch(fs, 12, &ext_ino, NULL, 0, 1);
	check_err("punch_oob_extent", retval, EXT2_ET_BAD_BLOCK_NUM);

	/* Indirect read EIO propagation */
	init_reg_inode(fs, 13, 0, &ind_ino);
	retval = ext2fs_bmap2(fs, 13, &ind_ino, NULL, BMAP_ALLOC, 12,
			      NULL, &pblk);
	check_err("alloc_ind_for_eio", retval, 0);
	fail_read_blk = ind_ino.i_block[EXT2_IND_BLOCK];
	retval = ext2fs_punch(fs, 13, &ind_ino, NULL, 12, 12);
	check_err("punch_ind_read_eio", retval, EIO);
	fail_read_blk = 0;

	teardown_fs(&fs);
	printf("test_punch_error_paths: completed\n");
}

int main(int argc EXT2FS_ATTR((unused)), char *argv[] EXT2FS_ATTR((unused)))
{
	add_error_table(&et_ext2_error_table);

	test_preconditions_and_inline_punch();
	test_indirect_punch();
	test_extent_punch();
	test_bigalloc_punch();
	test_punch_error_paths();

	if (test_fail == 0)
		printf("libext2fs punch unit tests passed!\n");
	else
		printf("libext2fs punch unit tests FAILED (%d errors)\n",
		       test_fail);

	return test_fail ? 1 : 0;
}
