/*
 * tst_orphan.c --- unit tests for orphan.c
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

static ext2_filsys create_test_fs(char *path_out, int *fd_out,
				  blk64_t blocks, int log_block_size,
				  int log_cluster_size, int use_extents,
				  int meta_csum)
{
	struct ext2_super_block	param;
	ext2_filsys		fs = NULL;
	char			*zbuf = NULL;
	int			fd;
	unsigned int		blksize = 1024U << log_block_size;

	ext2fs_zero_blocks2(NULL, 0, 0, NULL, NULL);

	strcpy(path_out, "/tmp/tst_orphan.XXXXXX");
	fd = mkstemp(path_out);
	if (fd < 0) {
		perror("mkstemp");
		exit(1);
	}
	if (ftruncate(fd, (off_t) blocks * blksize) < 0) {
		perror("ftruncate");
		close(fd);
		unlink(path_out);
		exit(1);
	}
	*fd_out = fd;

	memset(&param, 0, sizeof(param));
	ext2fs_blocks_count_set(&param, blocks);
	param.s_inodes_count = 128;
	param.s_log_block_size = log_block_size;
	param.s_log_cluster_size = log_cluster_size;
	param.s_rev_level = EXT2_DYNAMIC_REV;
	param.s_inode_size = 256;
	ext2fs_set_feature_sparse_super(&param);
	if (use_extents)
		ext2fs_set_feature_extents(&param);
	if (log_cluster_size > log_block_size)
		ext2fs_set_feature_bigalloc(&param);
	if (meta_csum) {
		param.s_desc_size = EXT2_MIN_DESC_SIZE_64BIT;
		ext2fs_set_feature_64bit(&param);
		ext2fs_set_feature_metadata_csum(&param);
	}

	if (ext2fs_initialize(path_out, EXT2_FLAG_RW | EXT2_FLAG_64BITS,
			      &param, unix_io_manager, &fs))
		exit(1);
	memset(fs->super->s_uuid, 0x67, sizeof(fs->super->s_uuid));
	if (meta_csum) {
		fs->super->s_checksum_type = EXT2_CRC32C_CHKSUM;
		ext2fs_init_csum_seed(fs);
	}
	fs->now = 1700000000;
	if (ext2fs_allocate_tables(fs))
		exit(1);
	if (log_cluster_size > log_block_size &&
	    ext2fs_convert_subcluster_bitmap(fs, &fs->block_map))
		exit(1);

	/* Zero group 0 inode table so reads of newly allocated inodes are clean */
	if (ext2fs_get_memzero((size_t) fs->blocksize *
			       fs->inode_blocks_per_group, &zbuf))
		exit(1);
	if (io_channel_write_blk64(fs->io, ext2fs_inode_table_loc(fs, 0),
				   fs->inode_blocks_per_group, zbuf))
		exit(1);
	ext2fs_free_mem(&zbuf);
	/* Mark reserved inodes 1..10 so ext2fs_new_inode allocates >= 11 */
	{
		ext2_ino_t i;

		for (i = 1; i < EXT2_GOOD_OLD_FIRST_INO; i++)
			ext2fs_mark_inode_bitmap2(fs->inode_map, i);
	}
	if (meta_csum && ext2fs_set_gdt_csum(fs))
		exit(1);

	return fs;
}

static void test_default_orphan_blocks(void)
{
	ext2_filsys	fs;
	char		path[64];
	int		fd;

	fs = create_test_fs(path, &fd, 8192, 0, 0, 1, 0);
	/* < 128K blocks -> 32 */
	ext2fs_blocks_count_set(fs->super, 16384);
	CHECK(ext2fs_default_orphan_file_blocks(fs) == 32,
	      "default orphan blocks for 16K fs == 32");

	/* 128K <= blocks < 2M -> blocks / 4096 */
	ext2fs_blocks_count_set(fs->super, 400ULL * 1024);
	CHECK(ext2fs_default_orphan_file_blocks(fs) == 100,
	      "default orphan blocks for 400K fs == 100");

	/* >= 2M blocks -> 512 */
	ext2fs_blocks_count_set(fs->super, 3ULL * 1024 * 1024);
	CHECK(ext2fs_default_orphan_file_blocks(fs) == 512,
	      "default orphan blocks for 3M fs == 512");

	/* Cluster rounding when cluster_ratio_bits == 3 (8 blocks/cluster) */
	fs->cluster_ratio_bits = 3;
	ext2fs_blocks_count_set(fs->super, 400ULL * 1024); /* 100 -> 104 */
	CHECK(ext2fs_default_orphan_file_blocks(fs) == 104,
	      "default orphan blocks rounds up to cluster boundary");
	fs->cluster_ratio_bits = 0;
	ext2fs_blocks_count_set(fs->super, 8192);

	ext2fs_free(fs);
	close(fd);
	unlink(path);
}

static void test_extent_orphan_and_csums(void)
{
	ext2_filsys			fs;
	struct ext2_inode		inode;
	struct ext4_orphan_block_tail	*tail;
	char				path[64], *buf = NULL;
	int				fd, i;
	ext2_ino_t			ino, ino2;
	blk64_t				pblk, first_blks[8];
	__u32				expected_crc;

	fs = create_test_fs(path, &fd, 8192, 0, 0, 1, 1);
	CHECK_RC(ext2fs_get_mem(fs->blocksize, &buf), 0, "alloc block buf");

	/* 1. Create 8-block extent orphan file with metadata_csum */
	fs->flags |= EXT2_FLAG_SUPER_ONLY;
	CHECK_RC(ext2fs_create_orphan_file(fs, 8), 0,
		 "create 8-block extent orphan file");
	CHECK((fs->flags & EXT2_FLAG_SUPER_ONLY) == 0,
	      "create_orphan_file clears EXT2_FLAG_SUPER_ONLY");
	CHECK(ext2fs_has_feature_orphan_file(fs->super) != 0,
	      "ORPHAN_FILE feature set");
	ino = fs->super->s_orphan_file_inum;
	CHECK(ino >= EXT2_GOOD_OLD_FIRST_INO, "valid orphan ino allocated");

	CHECK_RC(ext2fs_read_inode(fs, ino, &inode), 0, "read orphan inode");
	CHECK((inode.i_flags & EXT4_EXTENTS_FL) != 0, "orphan uses extents");
	CHECK(inode.i_links_count == 1, "orphan i_links_count == 1");
	CHECK(inode.i_mode == (LINUX_S_IFREG | 0600), "orphan mode 0100600");
	CHECK(EXT2_I_SIZE(&inode) == (ext2_off64_t) 8 * fs->blocksize,
	      "orphan size == 8 blocks");
	CHECK(ext2fs_inode_data_blocks2(fs, &inode) ==
	      (blk64_t) 8 * (fs->blocksize / 512),
	      "orphan i_blocks == 8 fs blocks in 512B sectors");

	for (i = 0; i < 8; i++) {
		pblk = 0;
		CHECK_RC(ext2fs_bmap2(fs, ino, &inode, NULL, 0, i, NULL, &pblk),
			 0, "bmap orphan block");
		CHECK(pblk != 0 && ext2fs_test_block_bitmap2(fs->block_map, pblk),
		      "orphan block allocated in block_map");
		first_blks[i] = pblk;
		CHECK_RC(io_channel_read_blk64(fs->io, pblk, 1, buf), 0,
			 "read orphan block");
		tail = ext2fs_orphan_block_tail(fs, buf);
		CHECK(ext2fs_le32_to_cpu(tail->ob_magic) ==
		      EXT4_ORPHAN_BLOCK_MAGIC,
		      "orphan block magic valid");
		CHECK(ext2fs_orphan_file_block_csum_verify(fs, ino, pblk, buf) == 1,
		      "orphan block initial csum valid");
	}

	/* 2. Test do_orphan_file_block_csum, csum_set, and csum_verify */
	pblk = first_blks[0];
	CHECK_RC(io_channel_read_blk64(fs->io, pblk, 1, buf), 0,
		 "read block 0 for csum mutation");
	((__u32 *) buf)[0] = ext2fs_cpu_to_le32(12);
	CHECK(ext2fs_orphan_file_block_csum_verify(fs, ino, pblk, buf) == 0,
	      "csum_verify detects modified orphan slot");

	CHECK_RC(ext2fs_orphan_file_block_csum_set(fs, ino, pblk, buf), 0,
		 "orphan_file_block_csum_set");
	CHECK(ext2fs_orphan_file_block_csum_verify(fs, ino, pblk, buf) == 1,
	      "csum_verify passes after csum_set");

	tail = ext2fs_orphan_block_tail(fs, buf);
	expected_crc = ext2fs_do_orphan_file_block_csum(fs, ino,
							inode.i_generation,
							pblk, buf);
	CHECK(ext2fs_le32_to_cpu(tail->ob_checksum) == expected_crc,
	      "ob_checksum matches ext2fs_do_orphan_file_block_csum");
	CHECK(ext2fs_orphan_file_block_csum_verify(fs, ino, pblk + 1, buf) == 0,
	      "csum_verify detects wrong block number");
	CHECK(ext2fs_orphan_file_block_csum_verify(fs, 999999, pblk, buf) == 0,
	      "csum_verify returns 0 on invalid inode");

	/* 3. Recreate orphan file on existing s_orphan_file_inum */
	CHECK_RC(ext2fs_create_orphan_file(fs, 4), 0,
		 "recreate 4-block orphan file on existing ino");
	ino2 = fs->super->s_orphan_file_inum;
	CHECK(ino2 == ino, "recreate reuses existing s_orphan_file_inum");
	CHECK_RC(ext2fs_read_inode(fs, ino, &inode), 0, "reread orphan inode");
	CHECK(EXT2_I_SIZE(&inode) == (ext2_off64_t) 4 * fs->blocksize,
	      "recreated orphan size == 4 blocks");
	CHECK(!ext2fs_test_block_bitmap2(fs->block_map, first_blks[7]),
	      "old block 7 freed when recreating smaller orphan file");

	/* 4. Clamp num_blocks > EXT4_MAX_ORPHAN_FILE_BLOCKS (512) */
	CHECK_RC(ext2fs_create_orphan_file(fs, 600), 0,
		 "create orphan file with >512 blocks clamps to 512");
	CHECK_RC(ext2fs_read_inode(fs, ino, &inode), 0, "read clamped inode");
	CHECK(EXT2_I_SIZE(&inode) == (ext2_off64_t) 512 * fs->blocksize,
	      "clamped orphan size == 512 blocks");

	/* 5. ext2fs_truncate_orphan_file clears features and frees blocks */
	pblk = 0;
	CHECK_RC(ext2fs_bmap2(fs, ino, &inode, NULL, 0, 0, NULL, &pblk), 0,
		 "bmap before truncate");
	ext2fs_set_feature_orphan_present(fs->super);
	fs->flags |= EXT2_FLAG_SUPER_ONLY;
	CHECK_RC(ext2fs_truncate_orphan_file(fs), 0, "truncate_orphan_file");
	CHECK(!ext2fs_has_feature_orphan_file(fs->super) &&
	      !ext2fs_has_feature_orphan_present(fs->super),
	      "truncate clears ORPHAN_FILE and ORPHAN_PRESENT");
	CHECK((fs->flags & EXT2_FLAG_SUPER_ONLY) == 0,
	      "truncate clears EXT2_FLAG_SUPER_ONLY");
	CHECK(!ext2fs_test_block_bitmap2(fs->block_map, pblk),
	      "truncate freed orphan data blocks");
	CHECK_RC(ext2fs_read_inode(fs, ino, &inode), 0,
		 "read truncated orphan inode");
	CHECK(inode.i_links_count == 0 && EXT2_I_SIZE(&inode) == 0,
	      "truncated orphan inode zeroed");

	ext2fs_free_mem(&buf);
	CHECK_RC(ext2fs_close_free(&fs), 0, "close extent orphan fs");
	close(fd);
	unlink(path);
}

static void test_indirect_and_bigalloc_orphan(void)
{
	ext2_filsys			fs;
	struct ext2_inode		inode;
	struct ext4_orphan_block_tail	*tail;
	char				path[64], *buf = NULL;
	int				fd;
	ext2_ino_t			ino;
	blk64_t				pblk0, pblk1, pblk15;

	/* 1. Indirect-mapped 16-block orphan file (12 direct + 1 ind + 4) */
	fs = create_test_fs(path, &fd, 8192, 0, 0, 0, 0);
	CHECK_RC(ext2fs_get_mem(fs->blocksize, &buf), 0, "alloc ind buf");
	CHECK_RC(ext2fs_create_orphan_file(fs, 16), 0,
		 "create 16-block indirect orphan file");
	ino = fs->super->s_orphan_file_inum;
	CHECK_RC(ext2fs_read_inode(fs, ino, &inode), 0, "read ind orphan ino");
	CHECK((inode.i_flags & EXT4_EXTENTS_FL) == 0, "indirect mapped");
	CHECK(inode.i_block[EXT2_IND_BLOCK] != 0, "IND block allocated");
	CHECK(EXT2_I_SIZE(&inode) == (ext2_off64_t) 16 * fs->blocksize,
	      "indirect orphan size == 16 blocks");
	CHECK(ext2fs_inode_data_blocks2(fs, &inode) ==
	      (blk64_t) 17 * (fs->blocksize / 512),
	      "indirect orphan total blocks == 17 (16 data + 1 ind)");

	pblk15 = 0;
	CHECK_RC(ext2fs_bmap2(fs, ino, &inode, NULL, 0, 15, NULL, &pblk15), 0,
		 "bmap indirect block 15");
	CHECK(pblk15 != 0, "indirect block 15 mapped");
	CHECK_RC(io_channel_read_blk64(fs->io, pblk15, 1, buf), 0,
		 "read indirect block 15");
	tail = ext2fs_orphan_block_tail(fs, buf);
	CHECK(ext2fs_le32_to_cpu(tail->ob_magic) == EXT4_ORPHAN_BLOCK_MAGIC,
	      "indirect block 15 has ORPHAN_BLOCK_MAGIC");
	/* Without metadata_csum, csum_set is a no-op and csum_verify == 1 */
	CHECK_RC(ext2fs_orphan_file_block_csum_set(fs, ino, pblk15, buf), 0,
		 "csum_set no-op without metadata_csum");
	CHECK(ext2fs_orphan_file_block_csum_verify(fs, ino, pblk15, buf) == 1,
	      "csum_verify == 1 without metadata_csum");

	CHECK_RC(ext2fs_truncate_orphan_file(fs), 0, "truncate indirect orphan");
	CHECK(!ext2fs_test_block_bitmap2(fs->block_map, pblk15),
	      "indirect block 15 freed on truncate");
	ext2fs_free_mem(&buf);
	CHECK_RC(ext2fs_close_free(&fs), 0, "close indirect fs");
	close(fd);
	unlink(path);

	/* 2. bigalloc (4K block, 16K = 4-block cluster) 8-block orphan file */
	fs = create_test_fs(path, &fd, 16384, 2, 4, 1, 1);
	CHECK_RC(ext2fs_create_orphan_file(fs, 8), 0,
		 "create 8-block bigalloc orphan file");
	ino = fs->super->s_orphan_file_inum;
	CHECK_RC(ext2fs_read_inode(fs, ino, &inode), 0,
		 "read bigalloc orphan inode");
	CHECK(EXT2_I_SIZE(&inode) == (ext2_off64_t) 8 * fs->blocksize,
	      "bigalloc orphan size == 8 blocks");
	pblk0 = pblk1 = 0;
	CHECK_RC(ext2fs_bmap2(fs, ino, &inode, NULL, 0, 0, NULL, &pblk0), 0,
		 "bmap bigalloc lblk 0");
	CHECK_RC(ext2fs_bmap2(fs, ino, &inode, NULL, 0, 1, NULL, &pblk1), 0,
		 "bmap bigalloc lblk 1");
	CHECK(pblk1 == pblk0 + 1 && EXT2FS_B2C(fs, pblk0) == EXT2FS_B2C(fs, pblk1),
	      "bigalloc orphan shares cluster across consecutive blocks");
	CHECK_RC(ext2fs_truncate_orphan_file(fs), 0, "truncate bigalloc orphan");
	CHECK_RC(ext2fs_close_free(&fs), 0, "close bigalloc fs");
	ext2fs_zero_blocks2(NULL, 0, 0, NULL, NULL);
	close(fd);
	unlink(path);
}

int main(int argc EXT2FS_ATTR((unused)), char **argv EXT2FS_ATTR((unused)))
{
	add_error_table(&et_ext2_error_table);

	test_default_orphan_blocks();
	test_extent_orphan_and_csums();
	test_indirect_and_bigalloc_orphan();

	remove_error_table(&et_ext2_error_table);

	if (fail_count) {
		fprintf(stderr, "tst_orphan: %d/%d checks FAILED\n",
			fail_count, test_count);
		return 1;
	}
	printf("tst_orphan: all %d checks passed\n", test_count);
	return 0;
}
