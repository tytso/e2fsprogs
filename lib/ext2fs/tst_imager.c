/*
 * tst_imager.c --- unit tests for imager.c
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

static int make_temp_file(char *tmpl, size_t size)
{
	int fd;

	strcpy(tmpl, "/tmp/tst_imager.XXXXXX");
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

static ext2_filsys create_fs_on_file(const char *path, unsigned int blocksize,
				     blk64_t blocks, ext2_ino_t inodes)
{
	ext2_filsys		fs = NULL;
	struct ext2_super_block	param;
	errcode_t		retval;

	memset(&param, 0, sizeof(param));
	ext2fs_blocks_count_set(&param, blocks);
	param.s_inodes_count = inodes;
	param.s_log_block_size = (blocksize == 4096) ? 2 : 0;
	param.s_log_cluster_size = param.s_log_block_size;
	param.s_rev_level = EXT2_DYNAMIC_REV;
	param.s_inode_size = 128;

	retval = ext2fs_initialize(path, EXT2_FLAG_RW | EXT2_FLAG_64BITS,
				   &param, unix_io_manager, &fs);
	if (retval) {
		com_err("create_fs_on_file", retval, "initializing %s", path);
		exit(1);
	}
	retval = ext2fs_allocate_tables(fs);
	if (retval) {
		com_err("create_fs_on_file", retval, "allocating tables");
		exit(1);
	}
	/* Zero inode tables on disk so unallocated slots start deterministic */
	{
		dgrp_t g;
		char *zbuf = NULL;
		size_t zlen = (size_t) fs->blocksize *
			      fs->inode_blocks_per_group;

		if (ext2fs_get_memzero(zlen, &zbuf))
			exit(1);
		for (g = 0; g < fs->group_desc_count; g++) {
			blk64_t itbl = ext2fs_inode_table_loc(fs, g);

			if (io_channel_write_blk64(fs->io, itbl,
						   fs->inode_blocks_per_group,
						   zbuf))
				exit(1);
		}
		ext2fs_free_mem(&zbuf);
	}
	if (ext2fs_flush(fs))
		exit(1);
	return fs;
}

/*
 * Suite 1: ext2fs_image_super_write() & ext2fs_image_super_read()
 */
static void run_super_roundtrip(unsigned int blocksize, blk64_t blocks,
				ext2_ino_t inodes)
{
	char			src_p[64], dst_p[64], img_p[64];
	int			sfd, dfd, ifd;
	ext2_filsys			src_fs, dst_fs;
	struct opaque_ext2_group_desc	*saved_gd;

	sfd = make_temp_file(src_p, (size_t) blocks * blocksize);
	dfd = make_temp_file(dst_p, (size_t) blocks * blocksize);
	ifd = make_temp_file(img_p, 0);

	src_fs = create_fs_on_file(src_p, blocksize, blocks, inodes);
	dst_fs = create_fs_on_file(dst_p, blocksize, blocks, inodes);

	/* Error path: NULL group_desc rejected with EXT2_ET_NO_GDESC */
	saved_gd = src_fs->group_desc;
	src_fs->group_desc = NULL;
	CHECK_RC(ext2fs_image_super_write(src_fs, ifd, 0), EXT2_ET_NO_GDESC,
		 "super_write NULL group_desc rejected");
	src_fs->group_desc = saved_gd;

	/* Mutate superblock and group descriptors with distinct values */
	memset(src_fs->super->s_volume_name, 0,
	       sizeof(src_fs->super->s_volume_name));
	strcpy((char *) src_fs->super->s_volume_name, "imager_unit_tst");
	src_fs->super->s_mnt_count = 37;
	src_fs->super->s_max_mnt_count = 99;
	ext2fs_bg_free_inodes_count_set(src_fs, 0, 19);
	if (src_fs->group_desc_count > 1)
		ext2fs_bg_free_blocks_count_set(src_fs, 1, 1234);

	CHECK_RC(ext2fs_image_super_write(src_fs, ifd, 0), 0,
		 "ext2fs_image_super_write");

	/* Short read check when positioned at EOF */
	CHECK_RC(ext2fs_image_super_read(dst_fs, ifd, 0), EXT2_ET_SHORT_READ,
		 "super_read at EOF returns EXT2_ET_SHORT_READ");

	/* Rewind and read into dst_fs */
	CHECK(lseek(ifd, 0, SEEK_SET) == 0, "rewind super image");
	strcpy((char *) dst_fs->super->s_volume_name, "corrupted_name");
	dst_fs->super->s_mnt_count = 1;
	ext2fs_bg_free_inodes_count_set(dst_fs, 0, 0);

	CHECK_RC(ext2fs_image_super_read(dst_fs, ifd, 0), 0,
		 "ext2fs_image_super_read");

	CHECK(strcmp((char *) dst_fs->super->s_volume_name,
		     "imager_unit_tst") == 0,
	      "volume name restored");
	CHECK(dst_fs->super->s_mnt_count == 37 &&
	      dst_fs->super->s_max_mnt_count == 99,
	      "mount counts restored");
	CHECK(ext2fs_bg_free_inodes_count(dst_fs, 0) == 19,
	      "group 0 free inodes restored");
	if (dst_fs->group_desc_count > 1)
		CHECK(ext2fs_bg_free_blocks_count(dst_fs, 1) == 1234,
		      "group 1 free blocks restored");

	ext2fs_close_free(&dst_fs);
	ext2fs_close_free(&src_fs);
	close(ifd);
	close(dfd);
	close(sfd);
	unlink(img_p);
	unlink(dst_p);
	unlink(src_p);
}

static void test_image_super(void)
{
	/* Multi-group 1K FS (10000 blocks -> 2 groups) and 4K FS */
	run_super_roundtrip(1024, 10000, 256);
	run_super_roundtrip(4096, 1024, 128);
	printf("  test_image_super: ok\n");
}

/*
 * Suite 2: ext2fs_image_inode_write() & ext2fs_image_inode_read()
 *          including error checks, dense & IMAGER_FLAG_SPARSEWRITE modes,
 *          and icache invalidation.
 */
static void test_image_inode(void)
{
	char			src_p[64], dst_p[64], img_p[64];
	int			sfd, dfd, ifd;
	ext2_filsys		src_fs, dst_fs;
	struct ext2_inode	ino_a, ino_b, cached_ino, out_ino;
	blk64_t			orig_itbl;
	ext2_ino_t		last_ino;

	/* 2 groups of 1K blocks, 128 inodes/group -> 16 itable blks/group */
	sfd = make_temp_file(src_p, 10000 * 1024);
	dfd = make_temp_file(dst_p, 10000 * 1024);
	ifd = make_temp_file(img_p, 0);

	src_fs = create_fs_on_file(src_p, 1024, 10000, 256);
	dst_fs = create_fs_on_file(dst_p, 1024, 10000, 256);
	last_ino = src_fs->super->s_inodes_count;

	/* Error cases on inode_table_loc */
	orig_itbl = ext2fs_inode_table_loc(src_fs, 0);
	ext2fs_inode_table_loc_set(src_fs, 0, 0);
	CHECK_RC(ext2fs_image_inode_write(src_fs, ifd, 0),
		 EXT2_ET_MISSING_INODE_TABLE, "inode_write missing itable");
	CHECK_RC(ext2fs_image_inode_read(src_fs, ifd, 0),
		 EXT2_ET_MISSING_INODE_TABLE, "inode_read missing itable");

	ext2fs_inode_table_loc_set(src_fs, 0, 9999);
	CHECK_RC(ext2fs_image_inode_write(src_fs, ifd, 0),
		 EXT2_ET_GDESC_BAD_INODE_TABLE, "inode_write OOB itable");
	ext2fs_inode_table_loc_set(src_fs, 0, orig_itbl);

	/* Write distinct inodes at start (ino 12) and very end (last_ino) */
	memset(&ino_a, 0, sizeof(ino_a));
	ino_a.i_mode = LINUX_S_IFREG | 0644;
	ino_a.i_uid = 1001;
	ino_a.i_size = 4321;
	ino_a.i_links_count = 2;
	CHECK_RC(ext2fs_write_new_inode(src_fs, 12, &ino_a), 0, "write ino 12");

	memset(&ino_b, 0, sizeof(ino_b));
	ino_b.i_mode = LINUX_S_IFDIR | 0755;
	ino_b.i_uid = 2002;
	ino_b.i_size = 1024;
	ino_b.i_links_count = 3;
	CHECK_RC(ext2fs_write_new_inode(src_fs, last_ino, &ino_b), 0,
		 "write last_ino");
	CHECK_RC(ext2fs_flush(src_fs), 0, "flush src_fs inodes");

	/* Prime dst_fs icache with stale data for inode 12 */
	memset(&cached_ino, 0, sizeof(cached_ino));
	cached_ino.i_mode = LINUX_S_IFREG | 0600;
	cached_ino.i_uid = 9999;
	CHECK_RC(ext2fs_write_new_inode(dst_fs, 12, &cached_ino), 0,
		 "seed stale ino 12 in dst_fs");
	/* Also dirty interior inode 50 in dst_fs to verify zeroing */
	CHECK_RC(ext2fs_write_new_inode(dst_fs, 50, &cached_ino), 0,
		 "seed dirty interior ino 50");
	CHECK_RC(ext2fs_flush(dst_fs), 0, "flush dst_fs");
	CHECK_RC(ext2fs_read_inode(dst_fs, 12, &out_ino), 0,
		 "prime dst_fs icache");
	CHECK(out_ino.i_uid == 9999, "icache primed with 9999");

	/* Test IMAGER_FLAG_SPARSEWRITE round-trip (skips zero middle blocks) */
	CHECK_RC(ext2fs_image_inode_write(src_fs, ifd, IMAGER_FLAG_SPARSEWRITE),
		 0, "inode_write SPARSEWRITE");
	CHECK_RC(ext2fs_image_inode_read(dst_fs, ifd, 0), EXT2_ET_SHORT_READ,
		 "inode_read at EOF returns EXT2_ET_SHORT_READ");

	CHECK(lseek(ifd, 0, SEEK_SET) == 0, "rewind inode image");
	CHECK_RC(ext2fs_image_inode_read(dst_fs, ifd, 0), 0,
		 "inode_read from sparse image");

	CHECK_RC(ext2fs_read_inode(dst_fs, 12, &out_ino), 0,
		 "read restored ino 12 (icache invalidated)");
	CHECK(out_ino.i_uid == 1001 && out_ino.i_size == 4321 &&
	      out_ino.i_links_count == 2,
	      "ino 12 restored & stale icache flushed");

	CHECK_RC(ext2fs_read_inode(dst_fs, 50, &out_ino), 0,
		 "read zeroed interior ino 50");
	CHECK(out_ino.i_mode == 0 && out_ino.i_uid == 0,
	      "interior zero block overwrote dirty ino 50");

	CHECK_RC(ext2fs_read_inode(dst_fs, last_ino, &out_ino), 0,
		 "read restored last_ino");
	CHECK(out_ino.i_uid == 2002 && out_ino.i_links_count == 3,
	      "last_ino restored");

	/* Also verify dense mode (flags == 0) */
	CHECK(ftruncate(ifd, 0) == 0 && lseek(ifd, 0, SEEK_SET) == 0,
	      "reset ifd for dense write");
	CHECK_RC(ext2fs_image_inode_write(src_fs, ifd, 0), 0,
		 "inode_write dense");
	CHECK(lseek(ifd, 0, SEEK_SET) == 0, "rewind dense inode image");
	CHECK_RC(ext2fs_image_inode_read(dst_fs, ifd, 0), 0,
		 "inode_read dense");

	ext2fs_close_free(&dst_fs);
	ext2fs_close_free(&src_fs);
	close(ifd);
	close(dfd);
	close(sfd);
	unlink(img_p);
	unlink(dst_p);
	unlink(src_p);
	printf("  test_image_inode: ok\n");
}

/*
 * Suite 3: ext2fs_image_bitmap_write() & ext2fs_image_bitmap_read()
 *          for both block bitmaps and IMAGER_FLAG_INODEMAP (including
 *          block-alignment tail padding and lazy bitmap loading).
 */
static void run_bitmap_roundtrip(unsigned int blocksize, blk64_t blocks,
				 ext2_ino_t inodes)
{
	char		src_p[64], dst_p[64], bb_p[64], ib_p[64];
	int		sfd, dfd, bbfd, ibfd;
	ext2_filsys	src_fs, dst_fs;
	struct stat	st;

	sfd = make_temp_file(src_p, (size_t) blocks * blocksize);
	dfd = make_temp_file(dst_p, (size_t) blocks * blocksize);
	bbfd = make_temp_file(bb_p, 0);
	ibfd = make_temp_file(ib_p, 0);

	src_fs = create_fs_on_file(src_p, blocksize, blocks, inodes);
	dst_fs = create_fs_on_file(dst_p, blocksize, blocks, inodes);

	/* Mark distinct data blocks and inodes in src_fs */
	ext2fs_mark_block_bitmap2(src_fs->block_map, 55);
	ext2fs_mark_block_bitmap2(src_fs->block_map, 211);
	ext2fs_unmark_block_bitmap2(src_fs->block_map, 60);

	ext2fs_mark_inode_bitmap2(src_fs->inode_map, 17);
	ext2fs_mark_inode_bitmap2(src_fs->inode_map, 63);
	ext2fs_unmark_inode_bitmap2(src_fs->inode_map, 18);
	ext2fs_mark_bb_dirty(src_fs);
	ext2fs_mark_ib_dirty(src_fs);
	CHECK_RC(ext2fs_write_bitmaps(src_fs), 0, "write src_fs bitmaps");

	/* Free in-memory maps on src_fs to test lazy auto-read in write */
	ext2fs_free_block_bitmap(src_fs->block_map);
	src_fs->block_map = NULL;
	ext2fs_free_inode_bitmap(src_fs->inode_map);
	src_fs->inode_map = NULL;

	CHECK_RC(ext2fs_image_bitmap_write(src_fs, bbfd, 0), 0,
		 "image_bitmap_write block_map");
	CHECK_RC(ext2fs_image_bitmap_write(src_fs, ibfd, IMAGER_FLAG_INODEMAP),
		 0, "image_bitmap_write inode_map");

	/* Verify output size is padded to a multiple of fs->blocksize */
	CHECK(fstat(bbfd, &st) == 0 && (st.st_size % blocksize) == 0,
	      "block bitmap image padded to blocksize");
	CHECK(fstat(ibfd, &st) == 0 && (st.st_size % blocksize) == 0,
	      "inode bitmap image padded to blocksize");

	/* Short read at EOF */
	CHECK_RC(ext2fs_image_bitmap_read(dst_fs, bbfd, 0), EXT2_ET_SHORT_READ,
		 "bitmap_read block at EOF returns EXT2_ET_SHORT_READ");
	CHECK_RC(ext2fs_image_bitmap_read(dst_fs, ibfd, IMAGER_FLAG_INODEMAP),
		 EXT2_ET_SHORT_READ,
		 "bitmap_read inode at EOF returns EXT2_ET_SHORT_READ");

	/*
	 * Reset dst_fs bitmaps on disk (keeping bit 1 / s_first_data_block
	 * set so rbtree is non-empty) & free maps to test lazy read.
	 */
	ext2fs_clear_block_bitmap(dst_fs->block_map);
	ext2fs_mark_block_bitmap2(dst_fs->block_map,
				  dst_fs->super->s_first_data_block);
	ext2fs_clear_inode_bitmap(dst_fs->inode_map);
	ext2fs_mark_inode_bitmap2(dst_fs->inode_map, 1);
	ext2fs_mark_bb_dirty(dst_fs);
	ext2fs_mark_ib_dirty(dst_fs);
	CHECK_RC(ext2fs_write_bitmaps(dst_fs), 0, "write cleared dst bitmaps");
	ext2fs_free_block_bitmap(dst_fs->block_map);
	dst_fs->block_map = NULL;
	ext2fs_free_inode_bitmap(dst_fs->inode_map);
	dst_fs->inode_map = NULL;

	CHECK(lseek(bbfd, 0, SEEK_SET) == 0, "rewind block bitmap image");
	CHECK(lseek(ibfd, 0, SEEK_SET) == 0, "rewind inode bitmap image");

	CHECK_RC(ext2fs_image_bitmap_read(dst_fs, bbfd, 0), 0,
		 "image_bitmap_read block_map");
	CHECK_RC(ext2fs_image_bitmap_read(dst_fs, ibfd, IMAGER_FLAG_INODEMAP),
		 0, "image_bitmap_read inode_map");

	CHECK(ext2fs_test_block_bitmap2(dst_fs->block_map, 55) != 0 &&
	      ext2fs_test_block_bitmap2(dst_fs->block_map, 211) != 0 &&
	      ext2fs_test_block_bitmap2(dst_fs->block_map, 60) == 0,
	      "block bitmap bits restored accurately");
	CHECK(ext2fs_test_inode_bitmap2(dst_fs->inode_map, 17) != 0 &&
	      ext2fs_test_inode_bitmap2(dst_fs->inode_map, 63) != 0 &&
	      ext2fs_test_inode_bitmap2(dst_fs->inode_map, 18) == 0,
	      "inode bitmap bits restored accurately");

	ext2fs_close_free(&dst_fs);
	ext2fs_close_free(&src_fs);
	close(ibfd);
	close(bbfd);
	close(dfd);
	close(sfd);
	unlink(ib_p);
	unlink(bb_p);
	unlink(dst_p);
	unlink(src_p);
}

static void test_image_bitmap(void)
{
	/*
	 * 1K blocks (2 groups = 16384 bits = 2048B; 256 inodes = 32B ->
	 * exercises non-zero tail padding to 1024B) and 4K blocks.
	 */
	run_bitmap_roundtrip(1024, 16384, 256);
	run_bitmap_roundtrip(4096, 32768, 256);
	printf("  test_image_bitmap: ok\n");
}

/*
 * Suite 4: block bitmap round trip on bigalloc file systems (with 16
 *          blocks per cluster and two block groups), where the block
 *          bitmap has one bit per cluster, using both the bitarray and
 *          the rbtree bitmap backends.
 */
static void run_bigalloc_bitmap_roundtrip(unsigned int blocksize,
					  __u32 clusters_per_group,
					  int bitmap_type)
{
	char			fs_p[64], bb_p[64];
	int			fsfd, bbfd;
	ext2_filsys		fs = NULL;
	struct ext2_super_block	param;
	/* First block of group 1, and number of blocks in the file system */
	blk64_t			g1_start = (blk64_t) clusters_per_group * 16;
	blk64_t			nblocks = 2 * g1_start;
	errcode_t		retval;

	fsfd = make_temp_file(fs_p, 0);
	bbfd = make_temp_file(bb_p, 0);

	memset(&param, 0, sizeof(param));
	ext2fs_blocks_count_set(&param, nblocks);
	param.s_inodes_count = 256;
	param.s_log_block_size = (blocksize == 4096) ? 2 : 0;
	param.s_log_cluster_size = param.s_log_block_size + 4;
	param.s_clusters_per_group = clusters_per_group;
	param.s_rev_level = EXT2_DYNAMIC_REV;
	param.s_inode_size = 128;
	ext2fs_set_feature_bigalloc(&param);
	retval = ext2fs_initialize(fs_p, EXT2_FLAG_RW | EXT2_FLAG_64BITS,
				   &param, unix_io_manager, &fs);
	if (retval) {
		com_err("run_bigalloc_bitmap_roundtrip", retval,
			"initializing %s", fs_p);
		exit(1);
	}

	/*
	 * ext2fs_initialize() sets up a per-block bitmap for bigalloc;
	 * replace it with a regular per-cluster bitmap of the given type.
	 */
	ext2fs_free_block_bitmap(fs->block_map);
	fs->default_bitmap_type = bitmap_type;
	CHECK_RC(ext2fs_allocate_block_bitmap(fs, "src", &fs->block_map), 0,
		 "allocate source cluster bitmap");

	/* Mark the first and last cluster of each block group */
	ext2fs_mark_block_bitmap2(fs->block_map, 0);
	ext2fs_mark_block_bitmap2(fs->block_map, g1_start - 16);
	ext2fs_mark_block_bitmap2(fs->block_map, g1_start);
	ext2fs_mark_block_bitmap2(fs->block_map, nblocks - 16);
	CHECK_RC(ext2fs_image_bitmap_write(fs, bbfd, 0), 0,
		 "image_bitmap_write bigalloc block_map");

	/* Read the bitmap image back into an empty bitmap */
	ext2fs_free_block_bitmap(fs->block_map);
	CHECK_RC(ext2fs_allocate_block_bitmap(fs, "dst", &fs->block_map), 0,
		 "allocate destination cluster bitmap");
	CHECK(lseek(bbfd, 0, SEEK_SET) == 0, "rewind block bitmap image");
	CHECK_RC(ext2fs_image_bitmap_read(fs, bbfd, 0), 0,
		 "image_bitmap_read bigalloc block_map");

	CHECK(ext2fs_test_block_bitmap2(fs->block_map, 0) &&
	      ext2fs_test_block_bitmap2(fs->block_map, g1_start - 16) &&
	      ext2fs_test_block_bitmap2(fs->block_map, g1_start) &&
	      ext2fs_test_block_bitmap2(fs->block_map, nblocks - 16),
	      "bigalloc block bitmap bits restored");
	CHECK(!ext2fs_test_block_bitmap2(fs->block_map, 16) &&
	      !ext2fs_test_block_bitmap2(fs->block_map, g1_start - 32) &&
	      !ext2fs_test_block_bitmap2(fs->block_map, g1_start + 16) &&
	      !ext2fs_test_block_bitmap2(fs->block_map, nblocks - 32),
	      "bigalloc block bitmap clear bits restored");

	ext2fs_free(fs);
	close(bbfd);
	close(fsfd);
	unlink(bb_p);
	unlink(fs_p);
}

static void test_image_bitmap_bigalloc(void)
{
	run_bigalloc_bitmap_roundtrip(1024, 1024, EXT2FS_BMAP64_BITARRAY);
	run_bigalloc_bitmap_roundtrip(1024, 1024, EXT2FS_BMAP64_RBTREE);
	run_bigalloc_bitmap_roundtrip(4096, 8192, EXT2FS_BMAP64_BITARRAY);
	run_bigalloc_bitmap_roundtrip(4096, 8192, EXT2FS_BMAP64_RBTREE);
	printf("  test_image_bitmap_bigalloc: ok\n");
}

/*
 * Suite 5: a short write of the bitmap data is reported as
 *          EXT2_ET_SHORT_WRITE.
 */
static void test_image_bitmap_short_write(void)
{
#if defined(RLIMIT_FSIZE) && defined(SIGXFSZ)
	char		fs_p[64], bb_p[64];
	int		fsfd, bbfd;
	ext2_filsys	fs;
	struct rlimit	old_rlim, rlim;
	errcode_t	retval;

	fsfd = make_temp_file(fs_p, (size_t) 16384 * 1024);
	bbfd = make_temp_file(bb_p, 0);
	fs = create_fs_on_file(fs_p, 1024, 16384, 256);

	/*
	 * The block bitmap is written in two chunks of 1024 bytes; limit
	 * the file size so that the second write is short.
	 */
	signal(SIGXFSZ, SIG_IGN);
	CHECK(getrlimit(RLIMIT_FSIZE, &old_rlim) == 0, "getrlimit");
	rlim = old_rlim;
	rlim.rlim_cur = 1536;
	CHECK(setrlimit(RLIMIT_FSIZE, &rlim) == 0, "lower RLIMIT_FSIZE");
	retval = ext2fs_image_bitmap_write(fs, bbfd, 0);
	CHECK(setrlimit(RLIMIT_FSIZE, &old_rlim) == 0, "restore RLIMIT_FSIZE");
	signal(SIGXFSZ, SIG_DFL);
	CHECK_RC(retval, EXT2_ET_SHORT_WRITE,
		 "short bitmap write returns EXT2_ET_SHORT_WRITE");

	ext2fs_close_free(&fs);
	close(bbfd);
	close(fsfd);
	unlink(bb_p);
	unlink(fs_p);
	printf("  test_image_bitmap_short_write: ok\n");
#else
	printf("  test_image_bitmap_short_write: skipped\n");
#endif
}

int main(int argc EXT2FS_ATTR((unused)), char *argv[] EXT2FS_ATTR((unused)))
{
	add_error_table(&et_ext2_error_table);
	printf("Running tst_imager unit tests...\n");

	test_image_super();
	test_image_inode();
	test_image_bitmap();
	test_image_bitmap_bigalloc();
	test_image_bitmap_short_write();

	remove_error_table(&et_ext2_error_table);
	printf("tst_imager: %d checks run, %d failed\n",
	       test_count, fail_count);
	return fail_count ? 1 : 0;
}
