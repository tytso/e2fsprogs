/*
 * tst_swapfs.c --- unit tests for swapfs.c
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
#include "ext2fsP.h"
#include <ext2fs/ext2_ext_attr.h>
#include <ext2fs/ext3_extents.h>

static int test_fail = 0;

static void check_err(const char *name, errcode_t actual, errcode_t expected)
{
	if (actual != expected) {
		printf("FAIL [%s]: returned %ld (%s), expected %ld (%s)\n",
		       name, (long) actual, error_message(actual),
		       (long) expected, error_message(expected));
		test_fail++;
	}
}

static void check_u16(const char *name, __u16 actual, __u16 expected)
{
	if (actual != expected) {
		printf("FAIL [%s]: got 0x%04x, expected 0x%04x\n",
		       name, actual, expected);
		test_fail++;
	}
}

static void check_u32(const char *name, __u32 actual, __u32 expected)
{
	if (actual != expected) {
		printf("FAIL [%s]: got 0x%08x, expected 0x%08x\n",
		       name, actual, expected);
		test_fail++;
	}
}

static void check_u64(const char *name, __u64 actual, __u64 expected)
{
	if (actual != expected) {
		printf("FAIL [%s]: got 0x%016llx, expected 0x%016llx\n",
		       name, (unsigned long long) actual,
		       (unsigned long long) expected);
		test_fail++;
	}
}

static void check_mem(const char *name, const void *actual,
		      const void *expected, size_t len)
{
	const unsigned char	*a = (const unsigned char *) actual;
	const unsigned char	*e = (const unsigned char *) expected;
	size_t			i;

	for (i = 0; i < len; i++) {
		if (a[i] != e[i]) {
			printf("FAIL [%s]: byte %zu got 0x%02x, "
			       "expected 0x%02x\n",
			       name, i, a[i], e[i]);
			test_fail++;
			return;
		}
	}
}

/*
 * Helper to create a minimal in-memory ext2_filsys handle backed by
 * test_io_manager for group descriptor, inode, and dirent swap tests.
 */
static void setup_test_fs(ext2_filsys *ret_fs, unsigned int blocksize,
			  unsigned int inode_size, unsigned int desc_size,
			  __u32 incompat_flags, __u32 creator_os)
{
	struct ext2_super_block	param;
	errcode_t		retval;
	int			log_blk = 0;
	unsigned int		bs = blocksize;

	while (bs > EXT2_MIN_BLOCK_SIZE) {
		bs >>= 1;
		log_blk++;
	}

	memset(&param, 0, sizeof(param));
	param.s_rev_level = EXT2_DYNAMIC_REV;
	param.s_log_block_size = log_blk;
	param.s_log_cluster_size = log_blk;
	ext2fs_blocks_count_set(&param, 64);
	param.s_inodes_count = 32;
	param.s_inode_size = inode_size;
	param.s_creator_os = creator_os;
	param.s_feature_incompat = incompat_flags;
	param.s_desc_size = desc_size;

	retval = ext2fs_initialize("test_swapfs", EXT2_FLAG_64BITS, &param,
				   test_io_manager, ret_fs);
	if (retval) {
		com_err("setup_test_fs", retval, "initializing filesystem");
		exit(1);
	}
	(*ret_fs)->super->s_creator_os = creator_os;
	(*ret_fs)->super->s_feature_incompat = incompat_flags;
	(*ret_fs)->super->s_desc_size = desc_size;
	(*ret_fs)->super->s_inode_size = inode_size;
}

/*
 * 1. Superblock byte-swapping via ext2fs_swap_super()
 *    - Direct/indirect journal backup (all 17 s_jnl_blocks swapped)
 *    - Extent-based journal backup (s_jnl_blocks[0..14] unswapped,
 *      s_jnl_blocks[15..16] swapped)
 *    - Preservation of all __u8 / char / UUID / s_reserved arrays
 */
static void populate_test_super(struct ext2_super_block *sb)
{
	int	i;

	memset(sb, 0, sizeof(*sb));

	sb->s_inodes_count = 0x01020304U;
	sb->s_blocks_count = 0x05060708U;
	sb->s_r_blocks_count = 0x090a0b0cU;
	sb->s_free_blocks_count = 0x0d0e0f10U;
	sb->s_free_inodes_count = 0x11121314U;
	sb->s_first_data_block = 0x15161718U;
	sb->s_log_block_size = 0x191a1b1cU;
	sb->s_log_cluster_size = 0x1d1e1f20U;
	sb->s_blocks_per_group = 0x21222324U;
	sb->s_clusters_per_group = 0x25262728U;
	sb->s_inodes_per_group = 0x292a2b2cU;
	sb->s_mtime = 0x2d2e2f30U;
	sb->s_wtime = 0x31323334U;
	sb->s_mnt_count = 0x3536U;
	sb->s_max_mnt_count = (short) 0x3738U;
	sb->s_magic = 0xef53U;
	sb->s_state = 0x393aU;
	sb->s_errors = 0x3b3cU;
	sb->s_minor_rev_level = 0x3d3eU;
	sb->s_lastcheck = 0x3f404142U;
	sb->s_checkinterval = 0x43444546U;
	sb->s_creator_os = 0x4748494aU;
	sb->s_rev_level = 0x4b4c4d4eU;
	sb->s_def_resuid = 0x4f50U;
	sb->s_def_resgid = 0x5152U;
	sb->s_first_ino = 0x53545556U;
	sb->s_inode_size = 0x5758U;
	sb->s_block_group_nr = 0x595aU;
	sb->s_feature_compat = 0x5b5c5d5eU;
	sb->s_feature_incompat = 0x5f606162U;
	sb->s_feature_ro_compat = 0x63646566U;

	for (i = 0; i < 16; i++)
		sb->s_uuid[i] = (__u8) (0xa0 + i);
	memcpy(sb->s_volume_name, "TestVolumeLabel!", 16);
	memset(sb->s_last_mounted, 'M', sizeof(sb->s_last_mounted));
	sb->s_last_mounted[0] = '/';

	sb->s_algorithm_usage_bitmap = 0x6768696aU;
	sb->s_prealloc_blocks = 0xab;
	sb->s_prealloc_dir_blocks = 0xcd;
	sb->s_reserved_gdt_blocks = 0x6b6cU;

	for (i = 0; i < 16; i++)
		sb->s_journal_uuid[i] = (__u8) (0xb0 + i);
	sb->s_journal_inum = 0x6d6e6f70U;
	sb->s_journal_dev = 0x71727374U;
	sb->s_last_orphan = 0x75767778U;
	sb->s_hash_seed[0] = 0x10203040U;
	sb->s_hash_seed[1] = 0x50607080U;
	sb->s_hash_seed[2] = 0x90a0b0c0U;
	sb->s_hash_seed[3] = 0xd0e0f001U;
	sb->s_def_hash_version = 0x12;
	sb->s_jnl_backup_type = 0x34;
	sb->s_desc_size = 0x797aU;
	sb->s_default_mount_opts = 0x7b7c7d7eU;
	sb->s_first_meta_bg = 0x7f808182U;
	sb->s_mkfs_time = 0x83848586U;

	for (i = 0; i < 17; i++)
		sb->s_jnl_blocks[i] = 0x11223300U + (__u32) i;

	sb->s_blocks_count_hi = 0x8788898aU;
	sb->s_r_blocks_count_hi = 0x8b8c8d8eU;
	sb->s_free_blocks_hi = 0x8f909192U;
	sb->s_min_extra_isize = 0x9394U;
	sb->s_want_extra_isize = 0x9596U;
	sb->s_flags = 0x9798999aU;
	sb->s_raid_stride = 0x9b9cU;
	sb->s_mmp_update_interval = 0x9d9eU;
	sb->s_mmp_block = 0x0102030405060708ULL;
	sb->s_raid_stripe_width = 0x9fa0a1a2U;
	sb->s_log_groups_per_flex = 0x55;
	sb->s_checksum_type = 0x66;
	sb->s_encryption_level = 0x77;
	sb->s_reserved_pad = 0x88;
	sb->s_kbytes_written = 0x1122334455667788ULL;
	sb->s_snapshot_inum = 0xa3a4a5a6U;
	sb->s_snapshot_id = 0xa7a8a9aaU;
	sb->s_snapshot_r_blocks_count = 0x99aabbccddeeff00ULL;
	sb->s_snapshot_list = 0xabacadaeU;
	sb->s_error_count = 0xafb0b1b2U;
	sb->s_first_error_time = 0xb3b4b5b6U;
	sb->s_first_error_ino = 0xb7b8b9baU;
	sb->s_first_error_block = 0x0a0b0c0d0e0f1011ULL;
	memset(sb->s_first_error_func, 'F', sizeof(sb->s_first_error_func));
	sb->s_first_error_line = 0xdeadbeefU;
	sb->s_last_error_time = 0xbbbcbdbeU;
	sb->s_last_error_ino = 0xbfc0c1c2U;
	sb->s_last_error_line = 0xcafebabeU;
	sb->s_last_error_block = 0x1213141516171819ULL;
	memset(sb->s_last_error_func, 'L', sizeof(sb->s_last_error_func));
	memset(sb->s_mount_opts, 'O', sizeof(sb->s_mount_opts));
	sb->s_usr_quota_inum = 0xc3c4c5c6U;
	sb->s_grp_quota_inum = 0xc7c8c9caU;
	sb->s_overhead_clusters = 0xcbcccdceU;
	sb->s_backup_bgs[0] = 0xcfd0d1d2U;
	sb->s_backup_bgs[1] = 0xd3d4d5d6U;
	for (i = 0; i < 4; i++)
		sb->s_encrypt_algos[i] = (__u8) (0x41 + i);
	for (i = 0; i < 16; i++)
		sb->s_encrypt_pw_salt[i] = (__u8) (0xc0 + i);
	sb->s_lpf_ino = 0xd7d8d9daU;
	sb->s_prj_quota_inum = 0xdbdcdddeU;
	sb->s_checksum_seed = 0xdfe0e1e2U;
	sb->s_wtime_hi = 0x11;
	sb->s_mtime_hi = 0x22;
	sb->s_mkfs_time_hi = 0x33;
	sb->s_lastcheck_hi = 0x44;
	sb->s_first_error_time_hi = 0x55;
	sb->s_last_error_time_hi = 0x66;
	sb->s_first_error_errcode = 0x77;
	sb->s_last_error_errcode = 0x88;
	sb->s_encoding = 0xe3e4U;
	sb->s_encoding_flags = 0xe5e6U;
	sb->s_orphan_file_inum = 0xe7e8e9eaU;
	for (i = 0; i < 94; i++)
		sb->s_reserved[i] = 0xf0000000U + (__u32) i;
	sb->s_checksum = 0xebecedeeU;
}

static void test_swap_super(void)
{
	struct ext2_super_block		sb, orig_sb;
	struct ext3_extent_header	*eh;
	struct ext3_extent		*ex;
	int				i;
	char				name[32];

	/* 1a. Direct/indirect journal backup: all 17 s_jnl_blocks swapped */
	populate_test_super(&orig_sb);
	memcpy(&sb, &orig_sb, sizeof(sb));

	ext2fs_swap_super(&sb);

	check_u32("sb.s_inodes_count", sb.s_inodes_count, 0x04030201U);
	check_u32("sb.s_blocks_count", sb.s_blocks_count, 0x08070605U);
	check_u32("sb.s_r_blocks_count", sb.s_r_blocks_count, 0x0c0b0a09U);
	check_u32("sb.s_free_blocks_count", sb.s_free_blocks_count, 0x100f0e0dU);
	check_u32("sb.s_free_inodes_count", sb.s_free_inodes_count, 0x14131211U);
	check_u32("sb.s_first_data_block", sb.s_first_data_block, 0x18171615U);
	check_u32("sb.s_log_block_size", sb.s_log_block_size, 0x1c1b1a19U);
	check_u32("sb.s_log_cluster_size", sb.s_log_cluster_size, 0x201f1e1dU);
	check_u32("sb.s_blocks_per_group", sb.s_blocks_per_group, 0x24232221U);
	check_u32("sb.s_clusters_per_group",
		  sb.s_clusters_per_group, 0x28272625U);
	check_u32("sb.s_inodes_per_group", sb.s_inodes_per_group, 0x2c2b2a29U);
	check_u32("sb.s_mtime", sb.s_mtime, 0x302f2e2dU);
	check_u32("sb.s_wtime", sb.s_wtime, 0x34333231U);
	check_u16("sb.s_mnt_count", sb.s_mnt_count, 0x3635U);
	check_u16("sb.s_max_mnt_count", (__u16) sb.s_max_mnt_count, 0x3837U);
	check_u16("sb.s_magic", sb.s_magic, 0x53efU);
	check_u16("sb.s_state", sb.s_state, 0x3a39U);
	check_u16("sb.s_errors", sb.s_errors, 0x3c3bU);
	check_u16("sb.s_minor_rev_level", sb.s_minor_rev_level, 0x3e3dU);
	check_u32("sb.s_lastcheck", sb.s_lastcheck, 0x4241403fU);
	check_u32("sb.s_checkinterval", sb.s_checkinterval, 0x46454443U);
	check_u32("sb.s_creator_os", sb.s_creator_os, 0x4a494847U);
	check_u32("sb.s_rev_level", sb.s_rev_level, 0x4e4d4c4bU);
	check_u16("sb.s_def_resuid", sb.s_def_resuid, 0x504fU);
	check_u16("sb.s_def_resgid", sb.s_def_resgid, 0x5251U);
	check_u32("sb.s_first_ino", sb.s_first_ino, 0x56555453U);
	check_u16("sb.s_inode_size", sb.s_inode_size, 0x5857U);
	check_u16("sb.s_block_group_nr", sb.s_block_group_nr, 0x5a59U);
	check_u32("sb.s_feature_compat", sb.s_feature_compat, 0x5e5d5c5bU);
	check_u32("sb.s_feature_incompat", sb.s_feature_incompat, 0x6261605fU);
	check_u32("sb.s_feature_ro_compat",
		  sb.s_feature_ro_compat, 0x66656463U);
	check_u32("sb.s_algorithm_usage_bitmap",
		  sb.s_algorithm_usage_bitmap, 0x6a696867U);
	check_u16("sb.s_reserved_gdt_blocks",
		  sb.s_reserved_gdt_blocks, 0x6c6bU);
	check_u32("sb.s_journal_inum", sb.s_journal_inum, 0x706f6e6dU);
	check_u32("sb.s_journal_dev", sb.s_journal_dev, 0x74737271U);
	check_u32("sb.s_last_orphan", sb.s_last_orphan, 0x78777675U);
	check_u32("sb.s_hash_seed[0]", sb.s_hash_seed[0], 0x40302010U);
	check_u32("sb.s_hash_seed[1]", sb.s_hash_seed[1], 0x80706050U);
	check_u32("sb.s_hash_seed[2]", sb.s_hash_seed[2], 0xc0b0a090U);
	check_u32("sb.s_hash_seed[3]", sb.s_hash_seed[3], 0x01f0e0d0U);
	check_u16("sb.s_desc_size", sb.s_desc_size, 0x7a79U);
	check_u32("sb.s_default_mount_opts",
		  sb.s_default_mount_opts, 0x7e7d7c7bU);
	check_u32("sb.s_first_meta_bg", sb.s_first_meta_bg, 0x8281807fU);
	check_u32("sb.s_mkfs_time", sb.s_mkfs_time, 0x86858483U);

	for (i = 0; i < 17; i++) {
		snprintf(name, sizeof(name), "sb.s_jnl_blocks[%d]", i);
		check_u32(name, sb.s_jnl_blocks[i],
			  ((__u32) i << 24) | 0x00332211U);
	}

	check_u32("sb.s_blocks_count_hi", sb.s_blocks_count_hi, 0x8a898887U);
	check_u32("sb.s_r_blocks_count_hi",
		  sb.s_r_blocks_count_hi, 0x8e8d8c8bU);
	check_u32("sb.s_free_blocks_hi", sb.s_free_blocks_hi, 0x9291908fU);
	check_u16("sb.s_min_extra_isize", sb.s_min_extra_isize, 0x9493U);
	check_u16("sb.s_want_extra_isize", sb.s_want_extra_isize, 0x9695U);
	check_u32("sb.s_flags", sb.s_flags, 0x9a999897U);
	check_u16("sb.s_raid_stride", sb.s_raid_stride, 0x9c9bU);
	check_u16("sb.s_mmp_update_interval",
		  sb.s_mmp_update_interval, 0x9e9dU);
	check_u64("sb.s_mmp_block", sb.s_mmp_block, 0x0807060504030201ULL);
	check_u32("sb.s_raid_stripe_width",
		  sb.s_raid_stripe_width, 0xa2a1a09fU);
	check_u64("sb.s_kbytes_written",
		  sb.s_kbytes_written, 0x8877665544332211ULL);
	check_u32("sb.s_snapshot_inum", sb.s_snapshot_inum, 0xa6a5a4a3U);
	check_u32("sb.s_snapshot_id", sb.s_snapshot_id, 0xaaa9a8a7U);
	check_u64("sb.s_snapshot_r_blocks_count",
		  sb.s_snapshot_r_blocks_count, 0x00ffeeddccbbaa99ULL);
	check_u32("sb.s_snapshot_list", sb.s_snapshot_list, 0xaeadacabU);
	check_u32("sb.s_error_count", sb.s_error_count, 0xb2b1b0afU);
	check_u32("sb.s_first_error_time", sb.s_first_error_time, 0xb6b5b4b3U);
	check_u32("sb.s_first_error_ino", sb.s_first_error_ino, 0xbab9b8b7U);
	check_u64("sb.s_first_error_block",
		  sb.s_first_error_block, 0x11100f0e0d0c0b0aULL);
	check_u32("sb.s_last_error_time", sb.s_last_error_time, 0xbebdbcbbU);
	check_u32("sb.s_last_error_ino", sb.s_last_error_ino, 0xc2c1c0bfU);
	check_u64("sb.s_last_error_block",
		  sb.s_last_error_block, 0x1918171615141312ULL);
	check_u32("sb.s_usr_quota_inum", sb.s_usr_quota_inum, 0xc6c5c4c3U);
	check_u32("sb.s_grp_quota_inum", sb.s_grp_quota_inum, 0xcac9c8c7U);
	check_u32("sb.s_overhead_clusters",
		  sb.s_overhead_clusters, 0xcecdcccbU);
	check_u32("sb.s_backup_bgs[0]", sb.s_backup_bgs[0], 0xd2d1d0cfU);
	check_u32("sb.s_backup_bgs[1]", sb.s_backup_bgs[1], 0xd6d5d4d3U);
	check_u32("sb.s_lpf_ino", sb.s_lpf_ino, 0xdad9d8d7U);
	check_u32("sb.s_prj_quota_inum", sb.s_prj_quota_inum, 0xdedddcdbU);
	check_u32("sb.s_checksum_seed", sb.s_checksum_seed, 0xe2e1e0dfU);
	check_u16("sb.s_encoding", sb.s_encoding, 0xe4e3U);
	check_u16("sb.s_encoding_flags", sb.s_encoding_flags, 0xe6e5U);
	check_u32("sb.s_orphan_file_inum", sb.s_orphan_file_inum, 0xeae9e8e7U);
	check_u32("sb.s_checksum", sb.s_checksum, 0xeeedecebU);

	/* Verify byte arrays and reserved fields are untouched */
	check_mem("sb.s_uuid", sb.s_uuid, orig_sb.s_uuid, sizeof(sb.s_uuid));
	check_mem("sb.s_volume_name", sb.s_volume_name,
		  orig_sb.s_volume_name, sizeof(sb.s_volume_name));
	check_mem("sb.s_last_mounted", sb.s_last_mounted,
		  orig_sb.s_last_mounted, sizeof(sb.s_last_mounted));
	check_u16("sb.s_prealloc_bytes",
		  ((__u16) sb.s_prealloc_blocks << 8) | sb.s_prealloc_dir_blocks,
		  0xabcdU);
	check_mem("sb.s_journal_uuid", sb.s_journal_uuid,
		  orig_sb.s_journal_uuid, sizeof(sb.s_journal_uuid));
	check_u16("sb.s_hash_jnl_bytes",
		  ((__u16) sb.s_def_hash_version << 8) | sb.s_jnl_backup_type,
		  0x1234U);
	check_u32("sb.s_flex_csum_enc_pad",
		  ((__u32) sb.s_log_groups_per_flex << 24) |
		  ((__u32) sb.s_checksum_type << 16) |
		  ((__u32) sb.s_encryption_level << 8) |
		  sb.s_reserved_pad,
		  0x55667788U);
	check_mem("sb.s_first_error_func", sb.s_first_error_func,
		  orig_sb.s_first_error_func, sizeof(sb.s_first_error_func));
	check_mem("sb.s_last_error_func", sb.s_last_error_func,
		  orig_sb.s_last_error_func, sizeof(sb.s_last_error_func));
	check_mem("sb.s_mount_opts", sb.s_mount_opts,
		  orig_sb.s_mount_opts, sizeof(sb.s_mount_opts));
	check_mem("sb.s_encrypt_algos", sb.s_encrypt_algos,
		  orig_sb.s_encrypt_algos, sizeof(sb.s_encrypt_algos));
	check_mem("sb.s_encrypt_pw_salt", sb.s_encrypt_pw_salt,
		  orig_sb.s_encrypt_pw_salt, sizeof(sb.s_encrypt_pw_salt));
	check_mem("sb.s_reserved", sb.s_reserved,
		  orig_sb.s_reserved, sizeof(sb.s_reserved));

	/* Second swap restores the exact original superblock */
	ext2fs_swap_super(&sb);
	check_mem("sb.roundtrip_direct_jnl", &sb, &orig_sb, sizeof(sb));

	/*
	 * 1b. Extent-based journal backup: when s_jnl_blocks contains a
	 *     valid ext3_extent_header, s_jnl_blocks[0..14] are NOT swapped
	 *     and only s_jnl_blocks[15..16] (i_size_high, i_size) are swapped.
	 */
	populate_test_super(&orig_sb);
	memset(orig_sb.s_jnl_blocks, 0, sizeof(orig_sb.s_jnl_blocks));
	eh = (struct ext3_extent_header *) orig_sb.s_jnl_blocks;
	eh->eh_magic = ext2fs_cpu_to_le16(EXT3_EXT_MAGIC);
	eh->eh_entries = ext2fs_cpu_to_le16(1);
	eh->eh_max = ext2fs_cpu_to_le16(4);
	eh->eh_depth = ext2fs_cpu_to_le16(0);
	eh->eh_generation = ext2fs_cpu_to_le32(0x11223344U);
	ex = EXT_FIRST_EXTENT(eh);
	ex->ee_block = ext2fs_cpu_to_le32(0);
	ex->ee_len = ext2fs_cpu_to_le16(1024);
	ex->ee_start_hi = ext2fs_cpu_to_le16(0x0012);
	ex->ee_start = ext2fs_cpu_to_le32(0x3456789aU);
	orig_sb.s_jnl_blocks[15] = 0x01020304U;
	orig_sb.s_jnl_blocks[16] = 0x05060708U;

	memcpy(&sb, &orig_sb, sizeof(sb));
	ext2fs_swap_super(&sb);

	check_mem("sb.extent_jnl_blocks_0_14_unswapped",
		  sb.s_jnl_blocks, orig_sb.s_jnl_blocks, 15 * sizeof(__u32));
	check_u32("sb.extent_jnl_blocks_15_swapped",
		  sb.s_jnl_blocks[15], 0x04030201U);
	check_u32("sb.extent_jnl_blocks_16_swapped",
		  sb.s_jnl_blocks[16], 0x08070605U);

	ext2fs_swap_super(&sb);
	check_mem("sb.roundtrip_extent_jnl", &sb, &orig_sb, sizeof(sb));

	printf("test_swap_super: completed\n");
}

/*
 * 2. Group descriptor byte-swapping via ext2fs_swap_group_desc() and
 *    ext2fs_swap_group_desc2()
 */
static void populate_test_group_desc(struct ext4_group_desc *gdp4)
{
	memset(gdp4, 0, sizeof(*gdp4));
	gdp4->bg_block_bitmap = 0x01020304U;
	gdp4->bg_inode_bitmap = 0x05060708U;
	gdp4->bg_inode_table = 0x090a0b0cU;
	gdp4->bg_free_blocks_count = 0x1122U;
	gdp4->bg_free_inodes_count = 0x3344U;
	gdp4->bg_used_dirs_count = 0x5566U;
	gdp4->bg_flags = 0x7788U;
	gdp4->bg_exclude_bitmap_lo = 0x0d0e0f10U;
	gdp4->bg_block_bitmap_csum_lo = 0x99aaU;
	gdp4->bg_inode_bitmap_csum_lo = 0xbbccU;
	gdp4->bg_itable_unused = 0xddeeU;
	gdp4->bg_checksum = 0xff01U;

	gdp4->bg_block_bitmap_hi = 0x21222324U;
	gdp4->bg_inode_bitmap_hi = 0x25262728U;
	gdp4->bg_inode_table_hi = 0x292a2b2cU;
	gdp4->bg_free_blocks_count_hi = 0x1234U;
	gdp4->bg_free_inodes_count_hi = 0x5678U;
	gdp4->bg_used_dirs_count_hi = 0x9abcU;
	gdp4->bg_itable_unused_hi = 0xdef0U;
	gdp4->bg_exclude_bitmap_hi = 0x1357U;
	gdp4->bg_block_bitmap_csum_hi = 0x2468U;
	gdp4->bg_inode_bitmap_csum_hi = 0xace0U;
	gdp4->bg_reserved = 0xa55a5aa5U;
}

static void check_swapped_group_desc_lo(const char *prefix,
					const struct ext4_group_desc *gdp4)
{
	char	name[64];

	snprintf(name, sizeof(name), "%s.bg_block_bitmap", prefix);
	check_u32(name, gdp4->bg_block_bitmap, 0x04030201U);
	snprintf(name, sizeof(name), "%s.bg_inode_bitmap", prefix);
	check_u32(name, gdp4->bg_inode_bitmap, 0x08070605U);
	snprintf(name, sizeof(name), "%s.bg_inode_table", prefix);
	check_u32(name, gdp4->bg_inode_table, 0x0c0b0a09U);
	snprintf(name, sizeof(name), "%s.bg_free_blocks_count", prefix);
	check_u16(name, gdp4->bg_free_blocks_count, 0x2211U);
	snprintf(name, sizeof(name), "%s.bg_free_inodes_count", prefix);
	check_u16(name, gdp4->bg_free_inodes_count, 0x4433U);
	snprintf(name, sizeof(name), "%s.bg_used_dirs_count", prefix);
	check_u16(name, gdp4->bg_used_dirs_count, 0x6655U);
	snprintf(name, sizeof(name), "%s.bg_flags", prefix);
	check_u16(name, gdp4->bg_flags, 0x8877U);
	snprintf(name, sizeof(name), "%s.bg_exclude_bitmap_lo", prefix);
	check_u32(name, gdp4->bg_exclude_bitmap_lo, 0x100f0e0dU);
	snprintf(name, sizeof(name), "%s.bg_block_bitmap_csum_lo", prefix);
	check_u16(name, gdp4->bg_block_bitmap_csum_lo, 0xaa99U);
	snprintf(name, sizeof(name), "%s.bg_inode_bitmap_csum_lo", prefix);
	check_u16(name, gdp4->bg_inode_bitmap_csum_lo, 0xccbbU);
	snprintf(name, sizeof(name), "%s.bg_itable_unused", prefix);
	check_u16(name, gdp4->bg_itable_unused, 0xeeddU);
	snprintf(name, sizeof(name), "%s.bg_checksum", prefix);
	check_u16(name, gdp4->bg_checksum, 0x01ffU);
}

static void test_swap_group_desc(void)
{
	ext2_filsys		fs = NULL;
	struct ext4_group_desc	gdp4, orig_gdp4;

	populate_test_group_desc(&orig_gdp4);

	/* 2a. ext2fs_swap_group_desc() swaps only the 32-bit half */
	memcpy(&gdp4, &orig_gdp4, sizeof(gdp4));
	ext2fs_swap_group_desc((struct ext2_group_desc *) &gdp4);
	check_swapped_group_desc_lo("gdesc_32", &gdp4);
	check_mem("gdesc_32_hi_untouched",
		  (const char *) &gdp4 + EXT2_MIN_DESC_SIZE,
		  (const char *) &orig_gdp4 + EXT2_MIN_DESC_SIZE,
		  sizeof(gdp4) - EXT2_MIN_DESC_SIZE);
	ext2fs_swap_group_desc((struct ext2_group_desc *) &gdp4);
	check_mem("gdesc_32_roundtrip", &gdp4, &orig_gdp4, sizeof(gdp4));

	/* 2b. ext2fs_swap_group_desc2(NULL, gdp) behaves identically */
	memcpy(&gdp4, &orig_gdp4, sizeof(gdp4));
	ext2fs_swap_group_desc2(NULL, (struct ext2_group_desc *) &gdp4);
	check_swapped_group_desc_lo("gdesc2_null_fs", &gdp4);
	check_mem("gdesc2_null_fs_hi_untouched",
		  (const char *) &gdp4 + EXT2_MIN_DESC_SIZE,
		  (const char *) &orig_gdp4 + EXT2_MIN_DESC_SIZE,
		  sizeof(gdp4) - EXT2_MIN_DESC_SIZE);

	/*
	 * 2c. ext2fs_swap_group_desc2(fs, gdp) when INCOMPAT_64BIT is NOT set
	 *     (even if s_desc_size == 64): EXT2_DESC_SIZE is 32 < 64.
	 */
	setup_test_fs(&fs, 1024, 256, EXT2_MIN_DESC_SIZE_64BIT, 0,
		      EXT2_OS_LINUX);
	memcpy(&gdp4, &orig_gdp4, sizeof(gdp4));
	ext2fs_swap_group_desc2(fs, (struct ext2_group_desc *) &gdp4);
	check_swapped_group_desc_lo("gdesc2_no64bit", &gdp4);
	check_mem("gdesc2_no64bit_hi_untouched",
		  (const char *) &gdp4 + EXT2_MIN_DESC_SIZE,
		  (const char *) &orig_gdp4 + EXT2_MIN_DESC_SIZE,
		  sizeof(gdp4) - EXT2_MIN_DESC_SIZE);
	ext2fs_free(fs);
	fs = NULL;

	/*
	 * 2d. ext2fs_swap_group_desc2(fs, gdp) when INCOMPAT_64BIT IS set
	 *     but s_desc_size == 32 (< EXT2_MIN_DESC_SIZE_64BIT).
	 */
	setup_test_fs(&fs, 1024, 256, EXT2_MIN_DESC_SIZE,
		      EXT4_FEATURE_INCOMPAT_64BIT, EXT2_OS_LINUX);
	memcpy(&gdp4, &orig_gdp4, sizeof(gdp4));
	ext2fs_swap_group_desc2(fs, (struct ext2_group_desc *) &gdp4);
	check_swapped_group_desc_lo("gdesc2_desc32", &gdp4);
	check_mem("gdesc2_desc32_hi_untouched",
		  (const char *) &gdp4 + EXT2_MIN_DESC_SIZE,
		  (const char *) &orig_gdp4 + EXT2_MIN_DESC_SIZE,
		  sizeof(gdp4) - EXT2_MIN_DESC_SIZE);

	/*
	 * 2e. ext2fs_swap_group_desc2(fs, gdp) when INCOMPAT_64BIT IS set
	 *     and s_desc_size == 64: swaps both 32-bit and 64-bit halves.
	 */
	fs->super->s_desc_size = EXT2_MIN_DESC_SIZE_64BIT;
	memcpy(&gdp4, &orig_gdp4, sizeof(gdp4));
	ext2fs_swap_group_desc2(fs, (struct ext2_group_desc *) &gdp4);
	check_swapped_group_desc_lo("gdesc2_64bit", &gdp4);
	check_u32("gdesc2_64bit.bg_block_bitmap_hi",
		  gdp4.bg_block_bitmap_hi, 0x24232221U);
	check_u32("gdesc2_64bit.bg_inode_bitmap_hi",
		  gdp4.bg_inode_bitmap_hi, 0x28272625U);
	check_u32("gdesc2_64bit.bg_inode_table_hi",
		  gdp4.bg_inode_table_hi, 0x2c2b2a29U);
	check_u16("gdesc2_64bit.bg_free_blocks_count_hi",
		  gdp4.bg_free_blocks_count_hi, 0x3412U);
	check_u16("gdesc2_64bit.bg_free_inodes_count_hi",
		  gdp4.bg_free_inodes_count_hi, 0x7856U);
	check_u16("gdesc2_64bit.bg_used_dirs_count_hi",
		  gdp4.bg_used_dirs_count_hi, 0xbc9aU);
	check_u16("gdesc2_64bit.bg_itable_unused_hi",
		  gdp4.bg_itable_unused_hi, 0xf0deU);
	check_u32("gdesc2_64bit.bg_exclude_bitmap_hi",
		  gdp4.bg_exclude_bitmap_hi, 0x5713U);
	check_u16("gdesc2_64bit.bg_block_bitmap_csum_hi",
		  gdp4.bg_block_bitmap_csum_hi, 0x6824U);
	check_u16("gdesc2_64bit.bg_inode_bitmap_csum_hi",
		  gdp4.bg_inode_bitmap_csum_hi, 0xe0acU);
	check_u32("gdesc2_64bit.bg_reserved_untouched",
		  gdp4.bg_reserved, 0xa55a5aa5U);

	ext2fs_swap_group_desc2(fs, (struct ext2_group_desc *) &gdp4);
	check_mem("gdesc2_64bit_roundtrip", &gdp4, &orig_gdp4, sizeof(gdp4));

	ext2fs_free(fs);
	printf("test_swap_group_desc: completed\n");
}

/*
 * 3. Extended attribute header, entry, and buffer swapping via
 *    ext2fs_swap_ext_attr_header(), ext2fs_swap_ext_attr_entry(),
 *    and ext2fs_swap_ext_attr()
 */
static void test_swap_ext_attr(void)
{
	struct ext2_ext_attr_header	from_hdr, to_hdr;
	struct ext2_ext_attr_entry	from_ent, to_ent;
	unsigned char			from_buf[128], to_buf[128];
	struct ext2_ext_attr_header	*bhdr;
	struct ext2_ext_attr_entry	*e1, *e2;

	/* 3a. ext2fs_swap_ext_attr_header (out-of-place and in-place) */
	memset(&from_hdr, 0, sizeof(from_hdr));
	from_hdr.h_magic = 0xea020000U;
	from_hdr.h_refcount = 0x01020304U;
	from_hdr.h_blocks = 0x05060708U;
	from_hdr.h_hash = 0x11223344U;
	from_hdr.h_checksum = 0x55667788U;
	from_hdr.h_reserved[0] = 0xa1a2a3a4U;
	from_hdr.h_reserved[1] = 0xb1b2b3b4U;
	from_hdr.h_reserved[2] = 0xc1c2c3c4U;

	memset(&to_hdr, 0, sizeof(to_hdr));
	ext2fs_swap_ext_attr_header(&to_hdr, &from_hdr);
	check_u32("ea_hdr.h_magic", to_hdr.h_magic, 0x000002eaU);
	check_u32("ea_hdr.h_refcount", to_hdr.h_refcount, 0x04030201U);
	check_u32("ea_hdr.h_blocks", to_hdr.h_blocks, 0x08070605U);
	check_u32("ea_hdr.h_hash", to_hdr.h_hash, 0x44332211U);
	check_u32("ea_hdr.h_checksum", to_hdr.h_checksum, 0x88776655U);
	check_u32("ea_hdr.h_reserved[0]", to_hdr.h_reserved[0], 0xa4a3a2a1U);
	check_u32("ea_hdr.h_reserved[1]", to_hdr.h_reserved[1], 0xb4b3b2b1U);
	check_u32("ea_hdr.h_reserved[2]", to_hdr.h_reserved[2], 0xc4c3c2c1U);

	/* In-place round-trip */
	ext2fs_swap_ext_attr_header(&to_hdr, &to_hdr);
	check_mem("ea_hdr.roundtrip", &to_hdr, &from_hdr, sizeof(from_hdr));

	/* 3b. ext2fs_swap_ext_attr_entry (out-of-place and in-place) */
	memset(&from_ent, 0, sizeof(from_ent));
	from_ent.e_name_len = 7;
	from_ent.e_name_index = 6;
	from_ent.e_value_offs = 0x1234U;
	from_ent.e_value_inum = 0x01020304U;
	from_ent.e_value_size = 0x05060708U;
	from_ent.e_hash = 0x9abcdef0U;

	memset(&to_ent, 0, sizeof(to_ent));
	ext2fs_swap_ext_attr_entry(&to_ent, &from_ent);
	check_u16("ea_ent.e_value_offs", to_ent.e_value_offs, 0x3412U);
	check_u32("ea_ent.e_value_inum", to_ent.e_value_inum, 0x04030201U);
	check_u32("ea_ent.e_value_size", to_ent.e_value_size, 0x08070605U);
	check_u32("ea_ent.e_hash", to_ent.e_hash, 0xf0debc9aU);

	/* In-place round-trip */
	to_ent.e_name_len = from_ent.e_name_len;
	to_ent.e_name_index = from_ent.e_name_index;
	ext2fs_swap_ext_attr_entry(&to_ent, &to_ent);
	check_mem("ea_ent.roundtrip", &to_ent, &from_ent, sizeof(from_ent));

	/*
	 * 3c. ext2fs_swap_ext_attr with has_header = 1 (external EA block):
	 *     Header (32B) + Entry 1 ("selinux", 7B -> 24B) +
	 *     Entry 2 ("acl", 3B -> 20B) + 4B zero sentinel + value payload
	 */
	memset(from_buf, 0, sizeof(from_buf));
	bhdr = (struct ext2_ext_attr_header *) from_buf;
	*bhdr = from_hdr;

	e1 = (struct ext2_ext_attr_entry *) (bhdr + 1);
	e1->e_name_len = 7;
	e1->e_name_index = 6;
	e1->e_value_offs = 0x0060U;
	e1->e_value_inum = 0x11112222U;
	e1->e_value_size = 0x0000000cU;
	e1->e_hash = 0x33334444U;
	memcpy(EXT2_EXT_ATTR_NAME(e1), "selinux", 7);

	e2 = EXT2_EXT_ATTR_NEXT(e1);
	e2->e_name_len = 3;
	e2->e_name_index = 2;
	e2->e_value_offs = 0x0070U;
	e2->e_value_inum = 0x55556666U;
	e2->e_value_size = 0x00000008U;
	e2->e_hash = 0x77778888U;
	memcpy(EXT2_EXT_ATTR_NAME(e2), "acl", 3);

	/* Value payload at offset 0x60..0x7f must not be byte-swapped */
	memcpy(from_buf + 0x60, "unconfined_u:object_r:default_t", 31);

	memset(to_buf, 0xa5, sizeof(to_buf));
	ext2fs_swap_ext_attr((char *) to_buf, (char *) from_buf,
			     sizeof(from_buf), 1);

	bhdr = (struct ext2_ext_attr_header *) to_buf;
	check_u32("ea_blk.h_magic", bhdr->h_magic, 0x000002eaU);
	check_u32("ea_blk.h_refcount", bhdr->h_refcount, 0x04030201U);

	e1 = (struct ext2_ext_attr_entry *) (bhdr + 1);
	check_u16("ea_blk.e1.name_len_idx",
		  ((__u16) e1->e_name_len << 8) | e1->e_name_index, 0x0706U);
	check_u16("ea_blk.e1.e_value_offs", e1->e_value_offs, 0x6000U);
	check_u32("ea_blk.e1.e_value_inum", e1->e_value_inum, 0x22221111U);
	check_u32("ea_blk.e1.e_value_size", e1->e_value_size, 0x0c000000U);
	check_u32("ea_blk.e1.e_hash", e1->e_hash, 0x44443333U);
	check_mem("ea_blk.e1.name", EXT2_EXT_ATTR_NAME(e1), "selinux", 7);

	e2 = EXT2_EXT_ATTR_NEXT(e1);
	check_u16("ea_blk.e2.name_len_idx",
		  ((__u16) e2->e_name_len << 8) | e2->e_name_index, 0x0302U);
	check_u16("ea_blk.e2.e_value_offs", e2->e_value_offs, 0x7000U);
	check_u32("ea_blk.e2.e_value_inum", e2->e_value_inum, 0x66665555U);
	check_u32("ea_blk.e2.e_value_size", e2->e_value_size, 0x08000000U);
	check_u32("ea_blk.e2.e_hash", e2->e_hash, 0x88887777U);
	check_mem("ea_blk.e2.name", EXT2_EXT_ATTR_NAME(e2), "acl", 3);

	/* Value payload at 0x60..0x7f must be copied untouched */
	check_mem("ea_blk.values_untouched",
		  to_buf + 0x60, from_buf + 0x60, 32);

	/* In-place swap restores original buffer */
	ext2fs_swap_ext_attr((char *) to_buf, (char *) to_buf,
			     sizeof(to_buf), 1);
	check_mem("ea_blk.roundtrip", to_buf, from_buf, sizeof(from_buf));

	/*
	 * 3d. ext2fs_swap_ext_attr with has_header = 0 (ibody EA area)
	 *     and boundary checks:
	 *     - Entry 1 (20 bytes) followed by Entry 2 whose e_name_len (50)
	 *       causes EXT2_EXT_ATTR_NEXT(e2) > from_end: e1 is swapped,
	 *       e2 is NOT swapped, and bytes beyond bufsize are untouched.
	 */
	memset(from_buf, 0, sizeof(from_buf));
	e1 = (struct ext2_ext_attr_entry *) from_buf;
	e1->e_name_len = 4;
	e1->e_name_index = 1;
	e1->e_value_offs = 0x0040U;
	e1->e_value_inum = 0x01020304U;
	e1->e_value_size = 0x00000005U;
	e1->e_hash = 0x12345678U;
	memcpy(EXT2_EXT_ATTR_NAME(e1), "test", 4);

	/* e2 starts at offset 20; inside a 40-byte buffer, name_len=50 overflows */
	e2 = EXT2_EXT_ATTR_NEXT(e1);
	e2->e_name_len = 50;
	e2->e_name_index = 1;
	e2->e_value_offs = 0x1122U;
	e2->e_value_inum = 0x33445566U;
	e2->e_value_size = 0x778899aaU;
	e2->e_hash = 0xbbccddeeU;

	memset(to_buf, 0x5a, sizeof(to_buf));
	ext2fs_swap_ext_attr((char *) to_buf, (char *) from_buf, 40, 0);

	e1 = (struct ext2_ext_attr_entry *) to_buf;
	check_u16("ea_nohdr.e1.e_value_offs", e1->e_value_offs, 0x4000U);
	check_u32("ea_nohdr.e1.e_value_inum", e1->e_value_inum, 0x04030201U);
	check_u32("ea_nohdr.e1.e_value_size", e1->e_value_size, 0x05000000U);
	check_u32("ea_nohdr.e1.e_hash", e1->e_hash, 0x78563412U);

	/* e2 must not be swapped because EXT2_EXT_ATTR_NEXT(e2) > from_end */
	e2 = EXT2_EXT_ATTR_NEXT(e1);
	check_u16("ea_nohdr.e2_overflow_unswapped_offs",
		  e2->e_value_offs, 0x1122U);
	check_u32("ea_nohdr.e2_overflow_unswapped_inum",
		  e2->e_value_inum, 0x33445566U);
	/* Guard byte at offset 40 must remain 0x5a */
	check_u16("ea_nohdr.guard_byte", to_buf[40], 0x5aU);

	/*
	 * 3e. Buffer filled to exact end (bufsize = 20, 1 entry of 20 bytes
	 *     with no trailing 4-byte zero sentinel): swaps e1 cleanly.
	 */
	memset(to_buf, 0x5a, sizeof(to_buf));
	ext2fs_swap_ext_attr((char *) to_buf, (char *) from_buf, 20, 0);
	e1 = (struct ext2_ext_attr_entry *) to_buf;
	check_u16("ea_exact_end.e1.e_value_offs", e1->e_value_offs, 0x4000U);
	check_u32("ea_exact_end.e1.e_hash", e1->e_hash, 0x78563412U);
	check_u16("ea_exact_end.guard_byte", to_buf[20], 0x5aU);

	printf("test_swap_ext_attr: completed\n");
}

/*
 * 4. Inode swapping via ext2fs_swap_inode() and ext2fs_swap_inode_full()
 */
struct test_full_inode {
	struct ext2_inode_large	inode;
	unsigned char		ibody[128];
};

static void populate_base_inode(struct ext2_inode_large *ino, __u16 mode,
				__u32 flags, __u32 size)
{
	int	i;

	memset(ino, 0, sizeof(*ino));
	ino->i_mode = mode;
	ino->i_uid = 0x1122U;
	ino->i_size = size;
	ino->i_atime = 0x01020304U;
	ino->i_ctime = 0x05060708U;
	ino->i_mtime = 0x090a0b0cU;
	ino->i_dtime = 0x0d0e0f10U;
	ino->i_gid = 0x3344U;
	ino->i_links_count = 0x5566U;
	ino->i_blocks = 0x11121314U;
	ino->i_flags = flags;
	ino->osd1.linux1.l_i_version = 0x15161718U;
	for (i = 0; i < EXT2_N_BLOCKS; i++)
		ino->i_block[i] = 0x20000100U + (__u32) i;
	ino->i_generation = 0x191a1b1cU;
	ino->i_file_acl = 0x1d1e1f20U;
	ino->i_size_high = 0x21222324U;
	ino->i_faddr = 0x25262728U;
	ino->osd2.linux2.l_i_blocks_hi = 0x7788U;
	ino->osd2.linux2.l_i_file_acl_high = 0x99aaU;
	ino->osd2.linux2.l_i_uid_high = 0xbbccU;
	ino->osd2.linux2.l_i_gid_high = 0xddeeU;
	ino->osd2.linux2.l_i_checksum_lo = 0xff01U;
}

static void populate_extra_inode_fields(struct ext2_inode_large *ino,
					__u16 extra_isize)
{
	ino->i_extra_isize = extra_isize;
	ino->i_checksum_hi = 0x1357U;
	ino->i_ctime_extra = 0x31323334U;
	ino->i_mtime_extra = 0x35363738U;
	ino->i_atime_extra = 0x393a3b3cU;
	ino->i_crtime = 0x3d3e3f40U;
	ino->i_crtime_extra = 0x41424344U;
	ino->i_version_hi = 0x45464748U;
	ino->i_projid = 0x494a4b4cU;
}

static void test_swap_inode_base_and_os(void)
{
	ext2_filsys		fs = NULL;
	struct test_full_inode	src, dst, in_place;
	int			i;
	char			name[48];

	/* 4a. EXT2_OS_LINUX 128-byte ext2fs_swap_inode (hostorder 1 and 0) */
	setup_test_fs(&fs, 1024, 256, 64, 0, EXT2_OS_LINUX);

	memset(&src, 0, sizeof(src));
	populate_base_inode(&src.inode, LINUX_S_IFREG | 0644, 0, 0x10203040U);
	populate_extra_inode_fields(&src.inode, 32);

	/* Fill bytes beyond 128 in dst with 0xa5 to verify swap_inode stops at 128 */
	memset(&dst, 0xa5, sizeof(dst));
	ext2fs_swap_inode(fs, EXT2_INODE(&dst.inode),
			  EXT2_INODE(&src.inode), 1);

	check_u16("ino_linux.i_mode",
		  dst.inode.i_mode, ext2fs_swab16(LINUX_S_IFREG | 0644));
	check_u16("ino_linux.i_uid", dst.inode.i_uid, 0x2211U);
	check_u32("ino_linux.i_size", dst.inode.i_size, 0x40302010U);
	check_u32("ino_linux.i_atime", dst.inode.i_atime, 0x04030201U);
	check_u32("ino_linux.i_ctime", dst.inode.i_ctime, 0x08070605U);
	check_u32("ino_linux.i_mtime", dst.inode.i_mtime, 0x0c0b0a09U);
	check_u32("ino_linux.i_dtime", dst.inode.i_dtime, 0x100f0e0dU);
	check_u16("ino_linux.i_gid", dst.inode.i_gid, 0x4433U);
	check_u16("ino_linux.i_links_count", dst.inode.i_links_count, 0x6655U);
	check_u32("ino_linux.i_blocks", dst.inode.i_blocks, 0x14131211U);
	check_u32("ino_linux.i_flags", dst.inode.i_flags, 0);
	check_u32("ino_linux.l_i_version",
		  dst.inode.osd1.linux1.l_i_version, 0x18171615U);
	for (i = 0; i < EXT2_N_BLOCKS; i++) {
		snprintf(name, sizeof(name), "ino_linux.i_block[%d]", i);
		check_u32(name, dst.inode.i_block[i],
			  ((__u32) i << 24) | 0x00010020U);
	}
	check_u32("ino_linux.i_generation", dst.inode.i_generation, 0x1c1b1a19U);
	check_u32("ino_linux.i_file_acl", dst.inode.i_file_acl, 0x201f1e1dU);
	check_u32("ino_linux.i_size_high", dst.inode.i_size_high, 0x24232221U);
	check_u32("ino_linux.i_faddr", dst.inode.i_faddr, 0x28272625U);
	check_u16("ino_linux.l_i_blocks_hi",
		  dst.inode.osd2.linux2.l_i_blocks_hi, 0x8877U);
	check_u16("ino_linux.l_i_file_acl_high",
		  dst.inode.osd2.linux2.l_i_file_acl_high, 0xaa99U);
	check_u16("ino_linux.l_i_uid_high",
		  dst.inode.osd2.linux2.l_i_uid_high, 0xccbbU);
	check_u16("ino_linux.l_i_gid_high",
		  dst.inode.osd2.linux2.l_i_gid_high, 0xeeddU);
	check_u16("ino_linux.l_i_checksum_lo",
		  dst.inode.osd2.linux2.l_i_checksum_lo, 0x01ffU);
	/* l_i_reserved (at offset 126) and i_extra_isize (at offset 128) remain 0xa5a5 */
	check_u16("ino_linux.l_i_reserved_untouched",
		  dst.inode.osd2.linux2.l_i_reserved, 0xa5a5U);
	check_u16("ino_linux.extra_untouched", dst.inode.i_extra_isize, 0xa5a5U);

	/* Swap back (hostorder = 0) in-place */
	dst.inode.osd2.linux2.l_i_reserved = src.inode.osd2.linux2.l_i_reserved;
	ext2fs_swap_inode(fs, EXT2_INODE(&dst.inode),
			  EXT2_INODE(&dst.inode), 0);
	check_mem("ino_linux.roundtrip_128",
		  &dst.inode, &src.inode, sizeof(struct ext2_inode));

	/* 4b. EXT2_OS_HURD osd1 / osd2 swapping */
	fs->super->s_creator_os = EXT2_OS_HURD;
	memset(&src, 0, sizeof(src));
	populate_base_inode(&src.inode, LINUX_S_IFREG | 0644, 0, 100);
	src.inode.osd1.hurd1.h_i_translator = 0x11223344U;
	src.inode.osd2.hurd2.h_i_frag = 0xab;
	src.inode.osd2.hurd2.h_i_fsize = 0xcd;
	src.inode.osd2.hurd2.h_i_mode_high = 0x1234U;
	src.inode.osd2.hurd2.h_i_uid_high = 0x5678U;
	src.inode.osd2.hurd2.h_i_gid_high = 0x9abcU;
	src.inode.osd2.hurd2.h_i_author = 0xdef01234U;

	memset(&dst, 0, sizeof(dst));
	ext2fs_swap_inode(fs, EXT2_INODE(&dst.inode),
			  EXT2_INODE(&src.inode), 1);
	check_u32("ino_hurd.h_i_translator",
		  dst.inode.osd1.hurd1.h_i_translator, 0x44332211U);
	check_u16("ino_hurd.frag_fsize",
		  ((__u16) dst.inode.osd2.hurd2.h_i_frag << 8) |
		  dst.inode.osd2.hurd2.h_i_fsize,
		  0xabcdU);
	check_u16("ino_hurd.h_i_mode_high",
		  dst.inode.osd2.hurd2.h_i_mode_high, 0x3412U);
	check_u16("ino_hurd.h_i_uid_high",
		  dst.inode.osd2.hurd2.h_i_uid_high, 0x7856U);
	check_u16("ino_hurd.h_i_gid_high",
		  dst.inode.osd2.hurd2.h_i_gid_high, 0xbc9aU);
	check_u32("ino_hurd.h_i_author",
		  dst.inode.osd2.hurd2.h_i_author, 0x3412f0deU);

	memcpy(&in_place, &dst, sizeof(dst));
	ext2fs_swap_inode(fs, EXT2_INODE(&in_place.inode),
			  EXT2_INODE(&in_place.inode), 0);
	check_mem("ino_hurd.roundtrip",
		  &in_place.inode, &src.inode, sizeof(struct ext2_inode));

	/* 4c. Other creator OS (EXT2_OS_FREEBSD): osd1 and osd2 left untouched */
	fs->super->s_creator_os = EXT2_OS_FREEBSD;
	memcpy(&in_place, &src, sizeof(src));
	ext2fs_swap_inode(fs, EXT2_INODE(&in_place.inode),
			  EXT2_INODE(&in_place.inode), 1);
	check_u32("ino_freebsd.osd1_untouched",
		  in_place.inode.osd1.linux1.l_i_version,
		  src.inode.osd1.linux1.l_i_version);
	check_mem("ino_freebsd.osd2_untouched",
		  &in_place.inode.osd2, &src.inode.osd2,
		  sizeof(src.inode.osd2));

	ext2fs_free(fs);
}

static void test_swap_inode_iblock_variants(void)
{
	ext2_filsys		fs = NULL;
	struct ext2_inode_large	src, dst, in_place;
	int			i;

	setup_test_fs(&fs, 1024, 256, 64, 0, EXT2_OS_LINUX);

	/*
	 * 1) Fast symlink: LINUX_S_ISLNK(i_mode), 0 < EXT2_I_SIZE < 60,
	 *    no extents or inline_data -> i_block is NOT swapped!
	 */
	populate_base_inode(&src, LINUX_S_IFLNK | 0777, 0, 19);
	src.i_size_high = 0;
	memset(src.i_block, 0, sizeof(src.i_block));
	memcpy(src.i_block, "/usr/bin/python3.12", 19);

	/* Out-of-place hostorder=1 -> hostorder=0 */
	memset(&dst, 0, sizeof(dst));
	ext2fs_swap_inode(fs, EXT2_INODE(&dst), EXT2_INODE(&src), 1);
	check_u16("fast_symlink.i_mode",
		  dst.i_mode, ext2fs_swab16(LINUX_S_IFLNK | 0777));
	check_u32("fast_symlink.i_size", dst.i_size, ext2fs_swab32(19));
	check_mem("fast_symlink.i_block_copied_unswapped",
		  dst.i_block, src.i_block, sizeof(src.i_block));

	memset(&in_place, 0, sizeof(in_place));
	ext2fs_swap_inode(fs, EXT2_INODE(&in_place), EXT2_INODE(&dst), 0);
	check_mem("fast_symlink.out_of_place_roundtrip",
		  &in_place, &src, sizeof(struct ext2_inode));

	/* In-place hostorder=1 and hostorder=0 */
	memcpy(&in_place, &src, sizeof(src));
	ext2fs_swap_inode(fs, EXT2_INODE(&in_place), EXT2_INODE(&in_place), 1);
	check_mem("fast_symlink.in_place_i_block_unswapped",
		  in_place.i_block, src.i_block, sizeof(src.i_block));
	ext2fs_swap_inode(fs, EXT2_INODE(&in_place), EXT2_INODE(&in_place), 0);
	check_mem("fast_symlink.in_place_roundtrip",
		  &in_place, &src, sizeof(struct ext2_inode));

	/*
	 * 2) Slow symlink (i_size == 60 == sizeof(i_block)):
	 *    ext2fs_is_fast_symlink() is false -> i_block IS swapped!
	 */
	populate_base_inode(&src, LINUX_S_IFLNK | 0777, 0,
			    sizeof(src.i_block));
	src.i_size_high = 0;
	memset(&dst, 0, sizeof(dst));
	ext2fs_swap_inode(fs, EXT2_INODE(&dst), EXT2_INODE(&src), 1);
	for (i = 0; i < EXT2_N_BLOCKS; i++)
		check_u32("slow_symlink.i_block_swapped",
			  dst.i_block[i], ext2fs_swab32(src.i_block[i]));
	ext2fs_swap_inode(fs, EXT2_INODE(&dst), EXT2_INODE(&dst), 0);
	check_mem("slow_symlink.roundtrip",
		  &dst, &src, sizeof(struct ext2_inode));

	/*
	 * 3) Zero-length symlink (i_size == 0, i_size_high == 0):
	 *    ext2fs_is_fast_symlink() is false -> i_block IS swapped!
	 */
	populate_base_inode(&src, LINUX_S_IFLNK | 0777, 0, 0);
	src.i_size_high = 0;
	memset(&dst, 0, sizeof(dst));
	ext2fs_swap_inode(fs, EXT2_INODE(&dst), EXT2_INODE(&src), 1);
	check_u32("zero_symlink.i_block0_swapped",
		  dst.i_block[0], ext2fs_swab32(src.i_block[0]));

	/*
	 * 4) Extent-mapped inode (EXT4_EXTENTS_FL):
	 *    i_block is NOT swapped (copied verbatim when t != f, preserved when t == f).
	 */
	populate_base_inode(&src, LINUX_S_IFREG | 0644, EXT4_EXTENTS_FL, 4096);
	memset(&dst, 0, sizeof(dst));
	ext2fs_swap_inode(fs, EXT2_INODE(&dst), EXT2_INODE(&src), 1);
	check_u32("extents_ino.i_flags",
		  dst.i_flags, ext2fs_swab32(EXT4_EXTENTS_FL));
	check_mem("extents_ino.i_block_unswapped_to_dst",
		  dst.i_block, src.i_block, sizeof(src.i_block));

	memset(&in_place, 0, sizeof(in_place));
	ext2fs_swap_inode(fs, EXT2_INODE(&in_place), EXT2_INODE(&dst), 0);
	check_mem("extents_ino.out_of_place_roundtrip",
		  &in_place, &src, sizeof(struct ext2_inode));

	/* In-place with EXT4_EXTENTS_FL */
	memcpy(&in_place, &src, sizeof(src));
	ext2fs_swap_inode(fs, EXT2_INODE(&in_place), EXT2_INODE(&in_place), 1);
	check_mem("extents_ino.in_place_unswapped",
		  in_place.i_block, src.i_block, sizeof(src.i_block));
	ext2fs_swap_inode(fs, EXT2_INODE(&in_place), EXT2_INODE(&in_place), 0);
	check_mem("extents_ino.in_place_roundtrip",
		  &in_place, &src, sizeof(struct ext2_inode));

	/*
	 * 5) Inline-data inode (EXT4_INLINE_DATA_FL):
	 *    i_block is NOT swapped (copied verbatim when t != f, preserved when t == f).
	 */
	populate_base_inode(&src, LINUX_S_IFREG | 0644,
			    EXT4_INLINE_DATA_FL, 32);
	memset(&dst, 0, sizeof(dst));
	ext2fs_swap_inode(fs, EXT2_INODE(&dst), EXT2_INODE(&src), 1);
	check_u32("inline_ino.i_flags",
		  dst.i_flags, ext2fs_swab32(EXT4_INLINE_DATA_FL));
	check_mem("inline_ino.i_block_unswapped_to_dst",
		  dst.i_block, src.i_block, sizeof(src.i_block));

	memset(&in_place, 0, sizeof(in_place));
	ext2fs_swap_inode(fs, EXT2_INODE(&in_place), EXT2_INODE(&dst), 0);
	check_mem("inline_ino.out_of_place_roundtrip",
		  &in_place, &src, sizeof(struct ext2_inode));

	ext2fs_free(fs);
}

static void test_swap_inode_full_extra_and_ea(void)
{
	ext2_filsys			fs = NULL;
	struct test_full_inode		src, dst, in_place;
	__u32				*ea_magic;
	struct ext2_ext_attr_entry	*e1, *e2;

	setup_test_fs(&fs, 1024, 256, 64, 0, EXT2_OS_LINUX);

	/*
	 * 1) bufsize = 129 (< sizeof(struct ext2_inode) + sizeof(__u16) = 130):
	 *    returns before swapping i_extra_isize.
	 */
	memset(&src, 0, sizeof(src));
	populate_base_inode(&src.inode, LINUX_S_IFREG | 0644, 0, 1024);
	populate_extra_inode_fields(&src.inode, 32);
	memset(&dst, 0xa5, sizeof(dst));
	ext2fs_swap_inode_full(fs, &dst.inode, &src.inode, 1, 129);
	check_u16("bufsize_129.extra_isize_untouched",
		  dst.inode.i_extra_isize, 0xa5a5U);

	/*
	 * 2) Partial extra_isize = 4 (inode_size = 132):
	 *    includes i_extra_isize and i_checksum_hi, but NOT i_ctime_extra..i_projid.
	 */
	populate_extra_inode_fields(&src.inode, 4);
	memset(&dst, 0xa5, sizeof(dst));
	ext2fs_swap_inode_full(fs, &dst.inode, &src.inode, 1,
			       sizeof(struct ext2_inode_large));
	check_u16("extra_4.i_extra_isize", dst.inode.i_extra_isize, 0x0400U);
	check_u16("extra_4.i_checksum_hi", dst.inode.i_checksum_hi, 0x5713U);
	check_u32("extra_4.i_ctime_extra_untouched",
		  dst.inode.i_ctime_extra, 0xa5a5a5a5U);

	/*
	 * 3) Partial extra_isize = 16 (inode_size = 144):
	 *    includes up to i_atime_extra, NOT i_crtime..i_projid.
	 */
	populate_extra_inode_fields(&src.inode, 16);
	memset(&dst, 0xa5, sizeof(dst));
	ext2fs_swap_inode_full(fs, &dst.inode, &src.inode, 1,
			       sizeof(struct ext2_inode_large));
	check_u16("extra_16.i_extra_isize", dst.inode.i_extra_isize, 0x1000U);
	check_u16("extra_16.i_checksum_hi", dst.inode.i_checksum_hi, 0x5713U);
	check_u32("extra_16.i_ctime_extra",
		  dst.inode.i_ctime_extra, 0x34333231U);
	check_u32("extra_16.i_mtime_extra",
		  dst.inode.i_mtime_extra, 0x38373635U);
	check_u32("extra_16.i_atime_extra",
		  dst.inode.i_atime_extra, 0x3c3b3a39U);
	check_u32("extra_16.i_crtime_untouched",
		  dst.inode.i_crtime, 0xa5a5a5a5U);

	/*
	 * 4) Illegal extra_isize > EXT2_INODE_SIZE(fs->super) - 128 (e.g. 132 > 128)
	 *    and unaligned extra_isize (e.g. 30, where extra_isize & 3 != 0):
	 *    i_extra_isize is swapped, then returns early without touching i_checksum_hi.
	 */
	populate_extra_inode_fields(&src.inode, 132);
	memset(&dst, 0xa5, sizeof(dst));
	ext2fs_swap_inode_full(fs, &dst.inode, &src.inode, 1, 256);
	check_u16("extra_too_large_host1.i_extra_isize",
		  dst.inode.i_extra_isize, ext2fs_swab16(132));
	check_u16("extra_too_large_host1.csum_hi_untouched",
		  dst.inode.i_checksum_hi, 0xa5a5U);

	/* Same check when hostorder = 0 */
	src.inode.i_extra_isize = ext2fs_swab16(132);
	memset(&dst, 0xa5, sizeof(dst));
	ext2fs_swap_inode_full(fs, &dst.inode, &src.inode, 0, 256);
	check_u16("extra_too_large_host0.i_extra_isize",
		  dst.inode.i_extra_isize, 132);
	check_u16("extra_too_large_host0.csum_hi_untouched",
		  dst.inode.i_checksum_hi, 0xa5a5U);

	/* Unaligned extra_isize = 30 (30 & 3 == 2 != 0) */
	populate_extra_inode_fields(&src.inode, 30);
	memset(&dst, 0xa5, sizeof(dst));
	ext2fs_swap_inode_full(fs, &dst.inode, &src.inode, 1, 256);
	check_u16("extra_unaligned_host1.i_extra_isize",
		  dst.inode.i_extra_isize, ext2fs_swab16(30));
	check_u16("extra_unaligned_host1.csum_hi_untouched",
		  dst.inode.i_checksum_hi, 0xa5a5U);

	src.inode.i_extra_isize = ext2fs_swab16(30);
	memset(&dst, 0xa5, sizeof(dst));
	ext2fs_swap_inode_full(fs, &dst.inode, &src.inode, 0, 256);
	check_u16("extra_unaligned_host0.i_extra_isize",
		  dst.inode.i_extra_isize, 30);
	check_u16("extra_unaligned_host0.csum_hi_untouched",
		  dst.inode.i_checksum_hi, 0xa5a5U);

	/*
	 * 5) Full extra_isize = 32 with bufsize = 160 (no space for EA magic):
	 *    swaps all extra inode fields up to i_projid, leaves ibody[0..3] untouched.
	 */
	memset(&src, 0, sizeof(src));
	populate_base_inode(&src.inode, LINUX_S_IFREG | 0644, 0, 4096);
	populate_extra_inode_fields(&src.inode, 32);
	ea_magic = (__u32 *) src.ibody;
	*ea_magic = EXT2_EXT_ATTR_MAGIC;

	memset(&dst, 0xa5, sizeof(dst));
	ext2fs_swap_inode_full(fs, &dst.inode, &src.inode, 1, 160);
	check_u16("extra_32_160.i_extra_isize",
		  dst.inode.i_extra_isize, 0x2000U);
	check_u16("extra_32_160.i_checksum_hi",
		  dst.inode.i_checksum_hi, 0x5713U);
	check_u32("extra_32_160.i_ctime_extra",
		  dst.inode.i_ctime_extra, 0x34333231U);
	check_u32("extra_32_160.i_mtime_extra",
		  dst.inode.i_mtime_extra, 0x38373635U);
	check_u32("extra_32_160.i_atime_extra",
		  dst.inode.i_atime_extra, 0x3c3b3a39U);
	check_u32("extra_32_160.i_crtime", dst.inode.i_crtime, 0x403f3e3dU);
	check_u32("extra_32_160.i_crtime_extra",
		  dst.inode.i_crtime_extra, 0x44434241U);
	check_u32("extra_32_160.i_version_hi",
		  dst.inode.i_version_hi, 0x48474645U);
	check_u32("extra_32_160.i_projid", dst.inode.i_projid, 0x4c4b4a49U);
	check_u32("extra_32_160.ea_magic_untouched",
		  *(__u32 *) dst.ibody, 0xa5a5a5a5U);

	/*
	 * 6) bufsize = 256, extra_isize = 32, but attr_magic != EXT2_EXT_ATTR_MAGIC:
	 *    returns without swapping attr_magic or ibody EA entries.
	 */
	*ea_magic = 0x11223344U;
	memset(&dst, 0xa5, sizeof(dst));
	ext2fs_swap_inode_full(fs, &dst.inode, &src.inode, 1, 256);
	check_u32("no_ea_magic.ibody_untouched",
		  *(__u32 *) dst.ibody, 0xa5a5a5a5U);

	/*
	 * 7) Full 256-byte inode with extra_isize = 32, EXT2_EXT_ATTR_MAGIC,
	 *    and two ibody extended attributes + values:
	 *    test out-of-place and in-place for both hostorder = 1 and hostorder = 0.
	 */
	memset(&src, 0, sizeof(src));
	populate_base_inode(&src.inode, LINUX_S_IFREG | 0644, 0, 8192);
	populate_extra_inode_fields(&src.inode, 32);

	ea_magic = (__u32 *) src.ibody;
	*ea_magic = EXT2_EXT_ATTR_MAGIC;
	e1 = (struct ext2_ext_attr_entry *) (ea_magic + 1);
	e1->e_name_len = 4;
	e1->e_name_index = 1;
	e1->e_value_offs = 0x0040U;
	e1->e_value_inum = 0x01020304U;
	e1->e_value_size = 0x00000006U;
	e1->e_hash = 0x11223344U;
	memcpy(EXT2_EXT_ATTR_NAME(e1), "mime", 4);

	e2 = EXT2_EXT_ATTR_NEXT(e1);
	e2->e_name_len = 2;
	e2->e_name_index = 7;
	e2->e_value_offs = 0x0048U;
	e2->e_value_inum = 0;
	e2->e_value_size = 0x00000004U;
	e2->e_hash = 0x55667788U;
	memcpy(EXT2_EXT_ATTR_NAME(e2), "id", 2);

	memcpy((char *) (ea_magic + 1) + 0x40, "binary", 6);
	memcpy((char *) (ea_magic + 1) + 0x48, "1234", 4);

	/* Out-of-place hostorder = 1 (host -> disk) */
	memset(&dst, 0, sizeof(dst));
	ext2fs_swap_inode_full(fs, &dst.inode, &src.inode, 1, 256);

	ea_magic = (__u32 *) dst.ibody;
	check_u32("full_ea.magic_swapped",
		  *ea_magic, ext2fs_swab32(EXT2_EXT_ATTR_MAGIC));
	e1 = (struct ext2_ext_attr_entry *) (ea_magic + 1);
	check_u16("full_ea.e1.e_value_offs", e1->e_value_offs, 0x4000U);
	check_u32("full_ea.e1.e_value_inum", e1->e_value_inum, 0x04030201U);
	check_u32("full_ea.e1.e_value_size", e1->e_value_size, 0x06000000U);
	check_u32("full_ea.e1.e_hash", e1->e_hash, 0x44332211U);
	check_mem("full_ea.e1.name", EXT2_EXT_ATTR_NAME(e1), "mime", 4);

	e2 = EXT2_EXT_ATTR_NEXT(e1);
	check_u16("full_ea.e2.e_value_offs", e2->e_value_offs, 0x4800U);
	check_u32("full_ea.e2.e_value_size", e2->e_value_size, 0x04000000U);
	check_u32("full_ea.e2.e_hash", e2->e_hash, 0x88776655U);
	check_mem("full_ea.e2.name", EXT2_EXT_ATTR_NAME(e2), "id", 2);
	check_mem("full_ea.val1", (char *) (ea_magic + 1) + 0x40, "binary", 6);
	check_mem("full_ea.val2", (char *) (ea_magic + 1) + 0x48, "1234", 4);

	/* Out-of-place hostorder = 0 (disk -> host) */
	memset(&in_place, 0, sizeof(in_place));
	in_place.inode.osd2.linux2.l_i_reserved =
		src.inode.osd2.linux2.l_i_reserved;
	ext2fs_swap_inode_full(fs, &in_place.inode, &dst.inode, 0, 256);
	check_mem("full_ea.out_of_place_roundtrip", &in_place, &src, 256);

	/* In-place hostorder = 1 followed by in-place hostorder = 0 */
	memcpy(&in_place, &src, sizeof(src));
	ext2fs_swap_inode_full(fs, &in_place.inode, &in_place.inode, 1, 256);
	check_mem("full_ea.in_place_matches_dst_ibody",
		  in_place.ibody, dst.ibody, 256 - 160);
	ext2fs_swap_inode_full(fs, &in_place.inode, &in_place.inode, 0, 256);
	check_mem("full_ea.in_place_roundtrip", &in_place, &src, 256);

	ext2fs_free(fs);
	printf("test_swap_inode: completed\n");
}

/*
 * 5. Multiple Mount Protection byte-swapping via ext2fs_swap_mmp()
 */
static void test_swap_mmp(void)
{
	struct mmp_struct	mmp, orig_mmp;
	int			i;

	memset(&orig_mmp, 0, sizeof(orig_mmp));
	orig_mmp.mmp_magic = EXT4_MMP_MAGIC;
	orig_mmp.mmp_seq = 0x12345678U;
	orig_mmp.mmp_time = 0x0102030405060708ULL;
	memset(orig_mmp.mmp_nodename, 'N', sizeof(orig_mmp.mmp_nodename));
	memset(orig_mmp.mmp_bdevname, 'B', sizeof(orig_mmp.mmp_bdevname));
	orig_mmp.mmp_check_interval = 0x9abcU;
	orig_mmp.mmp_pad1 = 0xdef0U;
	for (i = 0; i < 226; i++)
		orig_mmp.mmp_pad2[i] = 0xa0000000U + (__u32) i;
	orig_mmp.mmp_checksum = 0x89abcdefU;

	memcpy(&mmp, &orig_mmp, sizeof(mmp));
	ext2fs_swap_mmp(&mmp);

	check_u32("mmp.mmp_magic", mmp.mmp_magic, 0x504d4d00U);
	check_u32("mmp.mmp_seq", mmp.mmp_seq, 0x78563412U);
	check_u64("mmp.mmp_time", mmp.mmp_time, 0x0807060504030201ULL);
	check_u16("mmp.mmp_check_interval", mmp.mmp_check_interval, 0xbc9aU);
	check_u32("mmp.mmp_checksum", mmp.mmp_checksum, 0xefcdab89U);

	/* Verify nodename, bdevname, pad1, and pad2 are untouched */
	check_mem("mmp.mmp_nodename", mmp.mmp_nodename,
		  orig_mmp.mmp_nodename, sizeof(mmp.mmp_nodename));
	check_mem("mmp.mmp_bdevname", mmp.mmp_bdevname,
		  orig_mmp.mmp_bdevname, sizeof(mmp.mmp_bdevname));
	check_u16("mmp.mmp_pad1", mmp.mmp_pad1, 0xdef0U);
	check_mem("mmp.mmp_pad2", mmp.mmp_pad2,
		  orig_mmp.mmp_pad2, sizeof(mmp.mmp_pad2));

	ext2fs_swap_mmp(&mmp);
	check_mem("mmp.roundtrip", &mmp, &orig_mmp, sizeof(mmp));

	printf("test_swap_mmp: completed\n");
}

/*
 * 6. Directory entry byte-swapping via ext2fs_dirent_swab_in(),
 *    ext2fs_dirent_swab_in2(), ext2fs_dirent_swab_out(), and
 *    ext2fs_dirent_swab_out2()
 */
static void write_dirent_host(char *p, __u32 ino, __u16 rec_len,
			      __u16 name_len, const char *name)
{
	struct ext2_dir_entry	*de = (struct ext2_dir_entry *) p;
	size_t			nlen = name ? strlen(name) : 0;

	de->inode = ino;
	de->rec_len = rec_len;
	de->name_len = name_len;
	if (nlen > 0)
		memcpy(de->name, name, nlen);
}

static void test_dirent_swab(void)
{
	ext2_filsys		fs = NULL;
	ext2_filsys		fs64k = NULL;
	char			*orig_buf = NULL;
	char			*buf = NULL;
	char			*err_buf = NULL;
	struct ext2_dir_entry	*de;
	errcode_t		retval;

	setup_test_fs(&fs, 1024, 256, 64, 0, EXT2_OS_LINUX);

	retval = ext2fs_get_memzero(fs->blocksize, &orig_buf);
	check_err("alloc_orig_buf", retval, 0);
	retval = ext2fs_get_memzero(fs->blocksize, &buf);
	check_err("alloc_buf", retval, 0);

	/*
	 * 6a. Full 1024-byte directory block round-trip with flags = 0
	 *     Entry 0 (offset 0, rec_len 12): "."
	 *     Entry 1 (offset 12, rec_len 12): ".."
	 *     Entry 2 (offset 24, rec_len 20): "hello.txt" (len 9, ft 1)
	 *     Entry 3 (offset 44, rec_len 980): "last" (len 4, ft 1)
	 */
	write_dirent_host(orig_buf + 0, 2, 12,
			  (EXT2_FT_DIR << 8) | 1, ".");
	write_dirent_host(orig_buf + 12, 2, 12,
			  (EXT2_FT_DIR << 8) | 2, "..");
	write_dirent_host(orig_buf + 24, 0x01020304U, 20,
			  (EXT2_FT_REG_FILE << 8) | 9, "hello.txt");
	write_dirent_host(orig_buf + 44, 0x05060708U, 980,
			  (EXT2_FT_REG_FILE << 8) | 4, "last");

	memcpy(buf, orig_buf, fs->blocksize);
	retval = ext2fs_dirent_swab_out(fs, buf, 0);
	check_err("dirent_swab_out_v1", retval, 0);

	/* Verify each entry's header was byte-swapped and name preserved */
	de = (struct ext2_dir_entry *) (buf + 0);
	check_u32("swab_out_v1.e0.inode", de->inode, ext2fs_swab32(2));
	check_u16("swab_out_v1.e0.rec_len", de->rec_len, ext2fs_swab16(12));
	check_u16("swab_out_v1.e0.name_len",
		  de->name_len, ext2fs_swab16((EXT2_FT_DIR << 8) | 1));
	check_mem("swab_out_v1.e0.name", de->name, ".", 1);

	de = (struct ext2_dir_entry *) (buf + 24);
	check_u32("swab_out_v1.e2.inode", de->inode, 0x04030201U);
	check_u16("swab_out_v1.e2.rec_len", de->rec_len, ext2fs_swab16(20));
	check_u16("swab_out_v1.e2.name_len",
		  de->name_len, ext2fs_swab16((EXT2_FT_REG_FILE << 8) | 9));
	check_mem("swab_out_v1.e2.name", de->name, "hello.txt", 9);

	/* Swab back in with ext2fs_dirent_swab_in(fs, buf, 0) */
	retval = ext2fs_dirent_swab_in(fs, buf, 0);
	check_err("dirent_swab_in_v1", retval, 0);
	check_mem("dirent_swab_v1_roundtrip", buf, orig_buf, fs->blocksize);

	/*
	 * 6b. EXT2_DIRBLOCK_V2_STRUCT flag:
	 *     inode and rec_len are byte-swapped, while name_len is swabbed
	 *     twice so the two __u8 bytes (name_len, file_type) remain in
	 *     their original byte positions.
	 */
	memcpy(buf, orig_buf, fs->blocksize);
	retval = ext2fs_dirent_swab_out(fs, buf, EXT2_DIRBLOCK_V2_STRUCT);
	check_err("dirent_swab_out_v2", retval, 0);

	de = (struct ext2_dir_entry *) (buf + 24);
	check_u32("swab_out_v2.e2.inode", de->inode, 0x04030201U);
	check_u16("swab_out_v2.e2.rec_len", de->rec_len, ext2fs_swab16(20));
	check_u16("swab_out_v2.e2.name_len_unswapped",
		  de->name_len, (EXT2_FT_REG_FILE << 8) | 9);

	retval = ext2fs_dirent_swab_in(fs, buf, EXT2_DIRBLOCK_V2_STRUCT);
	check_err("dirent_swab_in_v2", retval, 0);
	check_mem("dirent_swab_v2_roundtrip", buf, orig_buf, fs->blocksize);

	/*
	 * 6c. Sub-block custom size via ext2fs_dirent_swab_out2 / swab_in2
	 *     (e.g. 56-byte inline directory data buffer) and 64KB blocksize
	 *     encoding (rec_len = EXT4_MAX_REC_LEN = 0xFFFF).
	 */
	memset(orig_buf, 0, 64);
	write_dirent_host(orig_buf + 0, 12, 16,
			  (EXT2_FT_REG_FILE << 8) | 4, "file");
	write_dirent_host(orig_buf + 16, 13, 40,
			  (EXT2_FT_DIR << 8) | 3, "sub");
	orig_buf[56] = 0x7f; /* Guard byte outside 56-byte range */

	memcpy(buf, orig_buf, 64);
	retval = ext2fs_dirent_swab_out2(fs, buf, 56, 0);
	check_err("dirent_swab_out2_56", retval, 0);
	check_u16("dirent_swab_out2_56_guard", (__u8) buf[56], 0x7fU);

	retval = ext2fs_dirent_swab_in2(fs, buf, 56, 0);
	check_err("dirent_swab_in2_56", retval, 0);
	check_mem("dirent_swab2_56_roundtrip", buf, orig_buf, 64);

	/* 64KB blocksize where rec_len = 0xFFFF represents 65536 */
	setup_test_fs(&fs64k, 65536, 256, 64, 0, EXT2_OS_LINUX);
	memset(buf, 0, 64);
	write_dirent_host(buf, 0x11223344U, 0xffffU, 0x0104U, "root");
	/* With size = 65536, a single entry of rec_len = 0xffff spans the block */
	ext2fs_free_mem(&buf);
	retval = ext2fs_get_memzero(65536, &buf);
	check_err("alloc_buf_64k", retval, 0);
	write_dirent_host(buf, 0x11223344U, 0xffffU, 0x0104U, "root");

	retval = ext2fs_dirent_swab_out(fs64k, buf, 0);
	check_err("dirent_swab_out_64k", retval, 0);
	de = (struct ext2_dir_entry *) buf;
	check_u32("dirent_swab_out_64k.inode", de->inode, 0x44332211U);
	check_u16("dirent_swab_out_64k.rec_len", de->rec_len, 0xffffU);

	retval = ext2fs_dirent_swab_in(fs64k, buf, 0);
	check_err("dirent_swab_in_64k", retval, 0);
	check_u32("dirent_swab_in_64k.inode", de->inode, 0x11223344U);
	ext2fs_free(fs64k);

	/*
	 * 6d. Corruption detection and EXT2_FLAG_IGNORE_SWAP_DIRENT in
	 *     ext2fs_dirent_swab_in2():
	 *     - rec_len < 8 (rec_len = 4)
	 *     - rec_len % 4 != 0 (rec_len = 10)
	 *     - (name_len & 0xFF) + 8 > rec_len (rec_len = 12, name_len = 5)
	 *     - rec_len > left (rec_len = 36 in a 32-byte buffer)
	 */
	/* Case 1: rec_len < 8 */
	memset(buf, 0, 32);
	write_dirent_host(buf, ext2fs_swab32(12), ext2fs_swab16(4),
			  ext2fs_swab16(1), "a");
	retval = ext2fs_dirent_swab_in2(fs, buf, 32, 0);
	check_err("swab_in2_reclen_lt_8", retval, EXT2_ET_DIR_CORRUPTED);

	memset(buf, 0, 32);
	write_dirent_host(buf, ext2fs_swab32(12), ext2fs_swab16(4),
			  ext2fs_swab16(1), "a");
	write_dirent_host(buf + 8, ext2fs_swab32(13), ext2fs_swab16(24),
			  ext2fs_swab16(1), "b");
	fs->flags |= EXT2_FLAG_IGNORE_SWAP_DIRENT;
	retval = ext2fs_dirent_swab_in2(fs, buf, 32, 0);
	check_err("swab_in2_reclen_lt_8_ignored", retval, 0);
	/* Verify it advanced by rec_len = 8 and swapped the entry at offset 8 */
	de = (struct ext2_dir_entry *) (buf + 8);
	check_u32("swab_in2_reclen_lt_8_next_ino", de->inode, 13);
	fs->flags &= ~EXT2_FLAG_IGNORE_SWAP_DIRENT;

	/* Case 2: rec_len % 4 != 0 */
	memset(buf, 0, 32);
	write_dirent_host(buf, ext2fs_swab32(12), ext2fs_swab16(10),
			  ext2fs_swab16(1), "a");
	retval = ext2fs_dirent_swab_in2(fs, buf, 32, 0);
	check_err("swab_in2_reclen_unaligned", retval, EXT2_ET_DIR_CORRUPTED);

	memset(buf, 0, 32);
	write_dirent_host(buf, ext2fs_swab32(12), ext2fs_swab16(10),
			  ext2fs_swab16(1), "a");
	write_dirent_host(buf + 8, ext2fs_swab32(14), ext2fs_swab16(24),
			  ext2fs_swab16(1), "b");
	fs->flags |= EXT2_FLAG_IGNORE_SWAP_DIRENT;
	retval = ext2fs_dirent_swab_in2(fs, buf, 32, 0);
	check_err("swab_in2_reclen_unaligned_ignored", retval, 0);
	fs->flags &= ~EXT2_FLAG_IGNORE_SWAP_DIRENT;

	/* Case 3: ((name_len & 0xFF) + 8) > rec_len (5 + 8 = 13 > 12) */
	memset(buf, 0, 32);
	write_dirent_host(buf, ext2fs_swab32(12), ext2fs_swab16(12),
			  ext2fs_swab16(5), "hello");
	retval = ext2fs_dirent_swab_in2(fs, buf, 32, 0);
	check_err("swab_in2_namelen_overflow", retval, EXT2_ET_DIR_CORRUPTED);

	memset(buf, 0, 32);
	write_dirent_host(buf, ext2fs_swab32(12), ext2fs_swab16(12),
			  ext2fs_swab16(5), "hello");
	write_dirent_host(buf + 12, ext2fs_swab32(15), ext2fs_swab16(20),
			  ext2fs_swab16(1), "c");
	fs->flags |= EXT2_FLAG_IGNORE_SWAP_DIRENT;
	retval = ext2fs_dirent_swab_in2(fs, buf, 32, 0);
	check_err("swab_in2_namelen_overflow_ignored", retval, 0);
	fs->flags &= ~EXT2_FLAG_IGNORE_SWAP_DIRENT;

	/* Case 4: rec_len > left (entry 1 has rec_len 12, entry 2 has rec_len 24 > 20) */
	memset(buf, 0, 32);
	write_dirent_host(buf, ext2fs_swab32(12), ext2fs_swab16(12),
			  ext2fs_swab16(1), "a");
	write_dirent_host(buf + 12, ext2fs_swab32(13), ext2fs_swab16(24),
			  ext2fs_swab16(1), "b");
	retval = ext2fs_dirent_swab_in2(fs, buf, 32, 0);
	check_err("swab_in2_reclen_gt_left", retval, EXT2_ET_DIR_CORRUPTED);

	memset(buf, 0, 32);
	write_dirent_host(buf, ext2fs_swab32(12), ext2fs_swab16(12),
			  ext2fs_swab16(1), "a");
	write_dirent_host(buf + 12, ext2fs_swab32(13), ext2fs_swab16(24),
			  ext2fs_swab16(1), "b");
	fs->flags |= EXT2_FLAG_IGNORE_SWAP_DIRENT;
	retval = ext2fs_dirent_swab_in2(fs, buf, 32, 0);
	check_err("swab_in2_reclen_gt_left_ignored", retval, 0);
	fs->flags &= ~EXT2_FLAG_IGNORE_SWAP_DIRENT;

	/*
	 * 6e. Corruption detection in ext2fs_dirent_swab_out2():
	 *     Note: when rec_len < 8 or rec_len % 4 != 0, ext2fs_dirent_swab_out2
	 *     calls ext2fs_free_mem(&buf), so pass a fresh heap-allocated buffer.
	 */
	/* Case 1: rec_len < 8 (rec_len = 4) */
	retval = ext2fs_get_memzero(fs->blocksize, &err_buf);
	check_err("alloc_err_buf_lt8", retval, 0);
	write_dirent_host(err_buf, 12, 4, 1, "a");
	retval = ext2fs_dirent_swab_out2(fs, err_buf, 32, 0);
	check_err("swab_out2_reclen_lt_8", retval, EXT2_ET_DIR_CORRUPTED);
	err_buf = NULL; /* freed inside ext2fs_dirent_swab_out2 */

	/* Case 2: rec_len % 4 != 0 (rec_len = 10) */
	retval = ext2fs_get_memzero(fs->blocksize, &err_buf);
	check_err("alloc_err_buf_unaligned", retval, 0);
	write_dirent_host(err_buf, 12, 10, 1, "a");
	retval = ext2fs_dirent_swab_out2(fs, err_buf, 32, 0);
	check_err("swab_out2_reclen_unaligned", retval, EXT2_ET_DIR_CORRUPTED);
	err_buf = NULL; /* freed inside ext2fs_dirent_swab_out2 */

	/* Case 3: rec_len > size (entry 1 rec_len = 12, entry 2 rec_len = 24 > 20) */
	memset(buf, 0, 32);
	write_dirent_host(buf, 12, 12, 1, "a");
	write_dirent_host(buf + 12, 13, 24, 1, "b");
	retval = ext2fs_dirent_swab_out2(fs, buf, 32, 0);
	check_err("swab_out2_reclen_gt_size", retval, EXT2_ET_DIR_CORRUPTED);

	/* Case 4: ext2fs_get_rec_len failure when fs->blocksize > (1 << 18) is not
	 * triggered by get_rec_len, so all swab_out2 paths are now covered. */

	ext2fs_free_mem(&orig_buf);
	ext2fs_free_mem(&buf);
	ext2fs_free(fs);
	printf("test_dirent_swab: completed\n");
}

int main(int argc EXT2FS_ATTR((unused)), char *argv[] EXT2FS_ATTR((unused)))
{
	initialize_ext2_error_table();

	test_swap_super();
	test_swap_group_desc();
	test_swap_ext_attr();
	test_swap_inode_base_and_os();
	test_swap_inode_iblock_variants();
	test_swap_inode_full_extra_and_ea();
	test_swap_mmp();
	test_dirent_swab();

	if (test_fail) {
		printf("tst_swapfs: %d failure(s) detected\n", test_fail);
		return 1;
	}
	printf("tst_swapfs: all tests passed\n");
	return 0;
}
