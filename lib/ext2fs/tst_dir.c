/*
 * tst_dir.c --- unit tests for directory, symlink, and pathname operations
 *               (newdir.c, dirblock.c, dir_iterate.c, mkdir.c, expanddir.c,
 *                link.c, unlink.c, lookup.c, symlink.c, namei.c,
 *                get_pathname.c)
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

/*
 * In-memory I/O manager so directory/symlink blocks persist in RAM.
 */
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

static ext2_filsys setup_fs(unsigned int blocksize, unsigned int blocks,
			    unsigned int inodes, unsigned int inode_size,
			    __u32 feat_compat, __u32 feat_incompat,
			    __u32 feat_ro_compat)
{
	ext2_filsys		fs = NULL;
	struct ext2_super_block	param;
	errcode_t		retval;
	unsigned int		log_bs = 0, tmp = blocksize >> 10;

	while (tmp > 1) {
		log_bs++;
		tmp >>= 1;
	}
	mem_disk_size = (unsigned long long) blocks * blocksize;
	retval = ext2fs_get_memzero((size_t) mem_disk_size, &mem_disk);
	if (retval) {
		com_err("setup_fs", retval, "allocating mem_disk");
		exit(1);
	}

	memset(&param, 0, sizeof(param));
	ext2fs_blocks_count_set(&param, blocks);
	param.s_inodes_count = inodes;
	param.s_log_block_size = log_bs;
	param.s_log_cluster_size = log_bs;
	param.s_rev_level = EXT2_DYNAMIC_REV;
	param.s_inode_size = inode_size;
	param.s_feature_compat = feat_compat;
	param.s_feature_incompat = feat_incompat;
	param.s_feature_ro_compat = feat_ro_compat;
	if (feat_ro_compat & EXT4_FEATURE_RO_COMPAT_METADATA_CSUM)
		param.s_checksum_type = EXT2_CRC32C_CHKSUM;

	retval = ext2fs_initialize("test fs",
				   EXT2_FLAG_RW | EXT2_FLAG_64BITS |
				   EXT2_FLAG_EXCLUSIVE,
				   &param, mem_io_manager, &fs);
	if (retval) {
		com_err("setup_fs", retval, "initializing fs");
		exit(1);
	}
	retval = ext2fs_allocate_tables(fs);
	if (retval) {
		com_err("setup_fs", retval, "allocating tables");
		exit(1);
	}
	retval = ext2fs_mkdir(fs, EXT2_ROOT_INO, EXT2_ROOT_INO, 0);
	if (retval) {
		com_err("setup_fs", retval, "creating root dir");
		exit(1);
	}
	return fs;
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

static ext2_ino_t alloc_reg_inode(ext2_filsys fs)
{
	ext2_ino_t		ino = 0;
	struct ext2_inode	inode;
	errcode_t		retval;

	retval = ext2fs_new_inode(fs, EXT2_ROOT_INO, LINUX_S_IFREG | 0644,
				  0, &ino);
	if (retval)
		return 0;
	ext2fs_inode_alloc_stats2(fs, ino, +1, 0);
	memset(&inode, 0, sizeof(inode));
	inode.i_mode = LINUX_S_IFREG | 0644;
	inode.i_links_count = 1;
	if (ext2fs_write_new_inode(fs, ino, &inode))
		return 0;
	return ino;
}

/*
 * Suite 1: ext2fs_get_rec_len / ext2fs_set_rec_len, ext2fs_new_dir_block,
 *          ext2fs_new_dir_inline_data, and ext2fs_read/write_dir_block*.
 */
static void test_dirblock_and_reclen(void)
{
	ext2_filsys		fs, fs_nocsum;
	struct ext2_dir_entry	de;
	unsigned int		rec_len = 0, saved_bs;
	char			*blkbuf = NULL, *readbuf = NULL;
	__u32			iblock[15];
	blk64_t			blk = 0;

	fs = setup_fs(1024, 512, 128, 256,
		      EXT2_FEATURE_COMPAT_EXT_ATTR,
		      EXT3_FEATURE_INCOMPAT_EXTENTS |
		      EXT2_FEATURE_INCOMPAT_FILETYPE,
		      EXT4_FEATURE_RO_COMPAT_METADATA_CSUM);

	/* Standard rec_len encoding and validation */
	memset(&de, 0, sizeof(de));
	CHECK_RC(ext2fs_set_rec_len(fs, 13, &de), EINVAL,
		 "unaligned rec_len rejected");
	CHECK_RC(ext2fs_set_rec_len(fs, 2048, &de), EINVAL,
		 "rec_len > blocksize rejected");
	CHECK_RC(ext2fs_set_rec_len(fs, 24, &de), 0, "set_rec_len 24");
	CHECK_RC(ext2fs_get_rec_len(fs, &de, &rec_len), 0, "get_rec_len 24");
	CHECK(rec_len == 24 && de.rec_len == 24, "24B rec_len roundtrip");

	/* 64 KiB and 128 KiB blocksize rec_len encoding */
	saved_bs = fs->blocksize;
	fs->blocksize = 65536;
	CHECK_RC(ext2fs_set_rec_len(fs, 65536, &de), 0, "set 64KiB rec_len");
	CHECK(de.rec_len == 0xffff, "64KiB encoded as 0xffff");
	CHECK_RC(ext2fs_get_rec_len(fs, &de, &rec_len), 0, "get 64KiB");
	CHECK(rec_len == 65536, "0xffff decoded to 65536");
	de.rec_len = 0;
	CHECK_RC(ext2fs_get_rec_len(fs, &de, &rec_len), 0, "get 0 on 64KiB");
	CHECK(rec_len == 65536, "0 decoded to 65536");

	fs->blocksize = 131072;
	CHECK_RC(ext2fs_set_rec_len(fs, 131072, &de), 0, "set 128KiB full");
	CHECK(de.rec_len == 0, "128KiB full encoded as 0");
	CHECK_RC(ext2fs_set_rec_len(fs, 65540, &de), 0, "set 65540 on 128KiB");
	CHECK_RC(ext2fs_get_rec_len(fs, &de, &rec_len), 0, "get 65540");
	CHECK(rec_len == 65540, "65540 high-bit encoding roundtrip");

	fs->blocksize = 524288;
	CHECK_RC(ext2fs_set_rec_len(fs, 12, &de), EINVAL,
		 "blocksize > 256KiB rejected");
	fs->blocksize = saved_bs;

	/* ext2fs_new_dir_inline_data */
	memset(iblock, 0, sizeof(iblock));
	CHECK_RC(ext2fs_new_dir_inline_data(fs, 15, EXT2_ROOT_INO, iblock), 0,
		 "ext2fs_new_dir_inline_data");
	CHECK(ext2fs_le32_to_cpu(iblock[0]) == EXT2_ROOT_INO,
	      "inline parent_ino stored in iblock[0]");

	/* ext2fs_new_dir_block (empty vs. dot/dotdot) & dirblock4 csums */
	CHECK_RC(ext2fs_new_dir_block(fs, 0, 0, &blkbuf), 0,
		 "new_dir_block empty");
	CHECK(ext2fs_dirent_has_tail(fs, (struct ext2_dir_entry *) blkbuf),
	      "empty new_dir_block has metadata_csum tail");
	ext2fs_free_mem(&blkbuf);

	CHECK_RC(ext2fs_new_dir_block(fs, EXT2_ROOT_INO, EXT2_ROOT_INO,
				      &blkbuf), 0, "new_dir_block dot/dotdot");
	CHECK_RC(ext2fs_new_block2(fs, 0, NULL, &blk), 0, "alloc dir blk");
	ext2fs_block_alloc_stats2(fs, blk, +1);
	CHECK_RC(ext2fs_write_dir_block4(fs, blk, blkbuf, 0, EXT2_ROOT_INO), 0,
		 "write_dir_block4");

	CHECK_RC(ext2fs_get_mem(fs->blocksize, &readbuf), 0, "alloc readbuf");
	CHECK_RC(ext2fs_read_dir_block4(fs, blk, readbuf, 0, EXT2_ROOT_INO), 0,
		 "read_dir_block4 valid csum");
	CHECK(memcmp(blkbuf, readbuf, fs->blocksize - 4) == 0,
	      "dirblock content preserved");

	/* Flip a byte on disk and verify EXT2_ET_DIR_CSUM_INVALID */
	readbuf[8] ^= 0x01;
	CHECK_RC(io_channel_write_blk64(fs->io, blk, 1, readbuf), 0,
		 "corrupt raw dir block");
	CHECK_RC(ext2fs_read_dir_block4(fs, blk, readbuf, 0, EXT2_ROOT_INO),
		 EXT2_ET_DIR_CSUM_INVALID, "corrupt dir csum detected");
	fs->flags |= EXT2_FLAG_IGNORE_CSUM_ERRORS;
	CHECK_RC(ext2fs_read_dir_block4(fs, blk, readbuf, 0, EXT2_ROOT_INO), 0,
		 "IGNORE_CSUM_ERRORS bypasses csum failure");
	fs->flags &= ~EXT2_FLAG_IGNORE_CSUM_ERRORS;

	ext2fs_free_mem(&readbuf);
	ext2fs_free_mem(&blkbuf);
	teardown_fs(&fs);

	/* Legacy read_dir_block / write_dir_block wrappers on non-csum fs */
	fs_nocsum = setup_fs(1024, 256, 64, 128, 0,
			     EXT2_FEATURE_INCOMPAT_FILETYPE, 0);
	CHECK_RC(ext2fs_new_dir_block(fs_nocsum, 0, 0, &blkbuf), 0,
		 "new_dir_block nocsum");
	CHECK_RC(ext2fs_new_block2(fs_nocsum, 0, NULL, &blk), 0,
		 "alloc blk nocsum");
	CHECK_RC(ext2fs_write_dir_block(fs_nocsum, (blk_t) blk, blkbuf), 0,
		 "ext2fs_write_dir_block");
	CHECK_RC(ext2fs_get_mem(fs_nocsum->blocksize, &readbuf), 0,
		 "alloc readbuf nocsum");
	CHECK_RC(ext2fs_read_dir_block(fs_nocsum, (blk_t) blk, readbuf), 0,
		 "ext2fs_read_dir_block");
	CHECK(memcmp(blkbuf, readbuf, fs_nocsum->blocksize) == 0,
	      "legacy dirblock roundtrip");
	ext2fs_free_mem(&readbuf);
	ext2fs_free_mem(&blkbuf);
	teardown_fs(&fs_nocsum);

	printf("  test_dirblock_and_reclen: ok\n");
}

/*
 * Suite 2: ext2fs_mkdir, ext2fs_mkdir2, and ext2fs_expand_dir.
 */
static void test_mkdir_and_expanddir(void)
{
	ext2_filsys		fs;
	ext2_ino_t		sub1 = 0, sub_dup = 0, reg_ino, inl_sub = 0;
	struct ext2_inode	inode, root_inode;
	ext2fs_block_bitmap	saved_bmap;
	blk64_t			free_before;

	fs = setup_fs(1024, 512, 128, 256,
		      EXT2_FEATURE_COMPAT_EXT_ATTR,
		      EXT3_FEATURE_INCOMPAT_EXTENTS |
		      EXT2_FEATURE_INCOMPAT_FILETYPE,
		      EXT4_FEATURE_RO_COMPAT_METADATA_CSUM);

	CHECK_RC(ext2fs_mkdir2(fs, EXT2_ROOT_INO, 0, EXT2_NODUMP_FL, 0,
			       "alpha", &sub1), 0, "mkdir2 alpha");
	CHECK(sub1 >= EXT2_FIRST_INO(fs->super), "ret_ino populated");
	CHECK_RC(ext2fs_read_inode(fs, sub1, &inode), 0, "read alpha inode");
	CHECK(LINUX_S_ISDIR(inode.i_mode), "alpha is directory");
	CHECK((inode.i_flags & EXT2_NODUMP_FL) != 0, "mkdir2 flags applied");
	CHECK(inode.i_links_count == 2, "alpha links_count == 2");
	CHECK_RC(ext2fs_read_inode(fs, EXT2_ROOT_INO, &root_inode), 0,
		 "read root inode");
	CHECK(root_inode.i_links_count == 3, "parent links_count incremented");

	/* Duplicate mkdir returns EXT2_ET_DIR_EXISTS and rolls back stats */
	free_before = ext2fs_free_blocks_count(fs->super);
	CHECK_RC(ext2fs_mkdir2(fs, EXT2_ROOT_INO, 0, 0, 0, "alpha", &sub_dup),
		 EXT2_ET_DIR_EXISTS, "duplicate mkdir rejected");
	CHECK(ext2fs_free_blocks_count(fs->super) == free_before,
	      "failed duplicate mkdir rolled back allocated block");

	/* ext2fs_expand_dir error paths and multi-block expansion */
	fs->flags &= ~EXT2_FLAG_RW;
	CHECK_RC(ext2fs_expand_dir(fs, sub1), EXT2_ET_RO_FILSYS,
		 "expand_dir on RO fs");
	fs->flags |= EXT2_FLAG_RW;

	saved_bmap = fs->block_map;
	fs->block_map = NULL;
	CHECK_RC(ext2fs_expand_dir(fs, sub1), EXT2_ET_NO_BLOCK_BITMAP,
		 "expand_dir without block_map");
	fs->block_map = saved_bmap;

	reg_ino = alloc_reg_inode(fs);
	CHECK(reg_ino != 0, "alloc reg inode");
	CHECK_RC(ext2fs_expand_dir(fs, reg_ino), EXT2_ET_NO_DIRECTORY,
		 "expand_dir on regular file rejected");

	CHECK_RC(ext2fs_expand_dir(fs, sub1), 0, "expand_dir 1 -> 2 blocks");
	CHECK_RC(ext2fs_expand_dir(fs, sub1), 0, "expand_dir 2 -> 3 blocks");
	CHECK_RC(ext2fs_read_inode(fs, sub1, &inode), 0, "read expanded sub1");
	CHECK(EXT2_I_SIZE(&inode) == 3072, "expanded dir i_size == 3072");

	teardown_fs(&fs);

	/* Inline directory creation via ext2fs_mkdir2 */
	fs = setup_fs(1024, 512, 128, 256,
		      EXT2_FEATURE_COMPAT_EXT_ATTR,
		      EXT4_FEATURE_INCOMPAT_INLINE_DATA |
		      EXT3_FEATURE_INCOMPAT_EXTENTS |
		      EXT2_FEATURE_INCOMPAT_FILETYPE,
		      EXT4_FEATURE_RO_COMPAT_METADATA_CSUM);
	CHECK_RC(ext2fs_mkdir2(fs, EXT2_ROOT_INO, 0, 0, 0, "inldir", &inl_sub),
		 0, "mkdir2 inline dir");
	CHECK_RC(ext2fs_read_inode(fs, inl_sub, &inode), 0, "read inldir");
	CHECK((inode.i_flags & EXT4_INLINE_DATA_FL) != 0,
	      "new dir has INLINE_DATA_FL");
	CHECK(inode.i_size == EXT4_MIN_INLINE_DATA_SIZE,
	      "inline dir i_size == 60");
	teardown_fs(&fs);

	/*
	 * ext2fs_mkdir2() must propagate ext2fs_inline_data_init() failure
	 * (e.g., 128-byte inodes lack ibody EA space for system.data) without
	 * allocating the inode, linking the uninitialized directory, or
	 * incrementing parent i_links_count.
	 */
	fs = setup_fs(1024, 512, 128, EXT2_GOOD_OLD_INODE_SIZE,
		      EXT2_FEATURE_COMPAT_EXT_ATTR,
		      EXT4_FEATURE_INCOMPAT_INLINE_DATA |
		      EXT2_FEATURE_INCOMPAT_FILETYPE, 0);
	inl_sub = 0;
	CHECK_RC(ext2fs_mkdir2(fs, EXT2_ROOT_INO, 0, 0, 0, "bad_inldir",
			       &inl_sub),
		 EXT2_ET_EA_NO_SPACE,
		 "mkdir2 propagates inline_data_init failure on 128B inode");
	CHECK(inl_sub != 0 &&
	      !ext2fs_test_inode_bitmap2(fs->inode_map, inl_sub),
	      "failed inline mkdir2 did not mark child inode in-use");
	CHECK_RC(ext2fs_lookup(fs, EXT2_ROOT_INO, "bad_inldir", 10, NULL,
			       &sub_dup),
		 EXT2_ET_FILE_NOT_FOUND,
		 "failed inline mkdir2 did not link bad_inldir into parent");
	CHECK_RC(ext2fs_read_inode(fs, EXT2_ROOT_INO, &root_inode), 0,
		 "read root after failed inline mkdir2");
	CHECK(root_inode.i_links_count == 2,
	      "failed inline mkdir2 did not increment root i_links_count");
	teardown_fs(&fs);

	printf("  test_mkdir_and_expanddir: ok\n");
}

/*
 * Suite 3: ext2fs_link, ext2fs_unlink, ext2fs_lookup, and
 *          ext2fs_dir_iterate / ext2fs_dir_iterate2.
 */
struct dir_walk_info {
	int	dot_seen;
	int	dotdot_seen;
	int	csum_seen;
	int	deleted_seen;
	int	empty_seen;
	int	other_seen;
	int	abort_at;
	int	total;
	const char *mutate_name;
	ext2_ino_t new_ino;
};

static int dir_walk_cb(ext2_ino_t dir EXT2FS_ATTR((unused)),
		       int entry,
		       struct ext2_dir_entry *dirent,
		       int offset EXT2FS_ATTR((unused)),
		       int blocksize EXT2FS_ATTR((unused)),
		       char *buf EXT2FS_ATTR((unused)),
		       void *priv_data)
{
	struct dir_walk_info *wi = (struct dir_walk_info *) priv_data;
	int nlen = ext2fs_dirent_name_len(dirent);

	wi->total++;
	if (entry == DIRENT_DOT_FILE)
		wi->dot_seen++;
	else if (entry == DIRENT_DOT_DOT_FILE)
		wi->dotdot_seen++;
	else if (entry == DIRENT_CHECKSUM)
		wi->csum_seen++;
	else if (entry == DIRENT_DELETED_FILE)
		wi->deleted_seen++;
	else if (dirent->inode == 0)
		wi->empty_seen++;
	else
		wi->other_seen++;

	if (wi->mutate_name && dirent->inode != 0 &&
	    nlen == (int) strlen(wi->mutate_name) &&
	    strncmp(dirent->name, wi->mutate_name, nlen) == 0) {
		dirent->inode = wi->new_ino;
		return DIRENT_CHANGED;
	}
	if (wi->abort_at && wi->total >= wi->abort_at)
		return DIRENT_ABORT;
	return 0;
}

static void test_link_unlink_and_iterate(void)
{
	ext2_filsys		fs;
	ext2_ino_t		dir = 0, f1, f2, f3, f4, found = 0;
	struct dir_walk_info	wi;
	struct ext2_inode	inode;
	char			name[32], *blkbuf = NULL;
	blk64_t			blk0 = 0, blk1 = 0;
	int			i, filled = 0;
	errcode_t		rc;

	fs = setup_fs(1024, 1024, 256, 256,
		      EXT2_FEATURE_COMPAT_EXT_ATTR,
		      EXT3_FEATURE_INCOMPAT_EXTENTS |
		      EXT2_FEATURE_INCOMPAT_FILETYPE,
		      EXT4_FEATURE_RO_COMPAT_METADATA_CSUM);

	CHECK_RC(ext2fs_mkdir2(fs, EXT2_ROOT_INO, 0, 0, 0, "work", &dir), 0,
		 "mkdir work");
	f1 = alloc_reg_inode(fs);
	f2 = alloc_reg_inode(fs);
	f3 = alloc_reg_inode(fs);
	f4 = alloc_reg_inode(fs);

	/* RO and invalid-argument checks */
	fs->flags &= ~EXT2_FLAG_RW;
	CHECK_RC(ext2fs_link(fs, dir, "ro", f1, EXT2_FT_REG_FILE),
		 EXT2_ET_RO_FILSYS, "link on RO fs");
	CHECK_RC(ext2fs_unlink(fs, dir, "ro", f1, 0),
		 EXT2_ET_RO_FILSYS, "unlink on RO fs");
	fs->flags |= EXT2_FLAG_RW;
	CHECK_RC(ext2fs_unlink(fs, dir, NULL, 0, 0),
		 EXT2_ET_INVALID_ARGUMENT, "unlink with !name && !ino");

	/* Fill block 0 without EXT2FS_LINK_EXPAND until DIR_NO_SPACE */
	for (i = 0; i < 100; i++) {
		snprintf(name, sizeof(name), "item_%03d", i);
		rc = ext2fs_link(fs, dir, name, f1, EXT2_FT_REG_FILE);
		if (rc == EXT2_ET_DIR_NO_SPACE)
			break;
		CHECK_RC(rc, 0, "link item in block 0");
		filled++;
	}
	CHECK(filled > 20 && filled < 100, "block 0 filled cleanly");

	/* EXT2FS_LINK_EXPAND grows directory to 2 blocks automatically */
	CHECK_RC(ext2fs_link(fs, dir, "expanded_first", f2,
			     EXT2_FT_REG_FILE | EXT2FS_LINK_EXPAND), 0,
		 "link with EXT2FS_LINK_EXPAND");
	/* EXT2FS_LINK_APPEND places entry directly in last block */
	CHECK_RC(ext2fs_link(fs, dir, "appended_second", f3,
			     EXT2_FT_REG_FILE | EXT2FS_LINK_APPEND), 0,
		 "link with EXT2FS_LINK_APPEND");

	CHECK_RC(ext2fs_lookup(fs, dir, "expanded_first", 14, NULL, &found), 0,
		 "lookup expanded_first");
	CHECK(found == f2, "expanded_first -> f2");
	CHECK_RC(ext2fs_lookup(fs, dir, "appended_second", 15, NULL, &found), 0,
		 "lookup appended_second");
	CHECK(found == f3, "appended_second -> f3");

	/* Unlink middle entry in block 0 and verify DIRENT_FLAG_INCLUDE_REMOVED */
	CHECK_RC(ext2fs_unlink(fs, dir, "item_005", f1, 0), 0,
		 "unlink item_005");
	CHECK_RC(ext2fs_lookup(fs, dir, "item_005", 8, NULL, &found),
		 EXT2_ET_FILE_NOT_FOUND, "item_005 gone from lookup");

	memset(&wi, 0, sizeof(wi));
	CHECK_RC(ext2fs_dir_iterate2(fs, dir,
				     DIRENT_FLAG_INCLUDE_REMOVED |
				     DIRENT_FLAG_INCLUDE_CSUM,
				     NULL, dir_walk_cb, &wi), 0,
		 "dir_iterate2 with INCLUDE_REMOVED | INCLUDE_CSUM");
	CHECK(wi.dot_seen == 1 && wi.dotdot_seen == 1, "saw . and ..");
	CHECK(wi.csum_seen == 2, "saw 2 block checksum tails");
	CHECK(wi.deleted_seen >= 1, "saw deleted item_005 entry");

	/* Unlink with mismatched ino fails without FORCE, succeeds with FORCE */
	CHECK_RC(ext2fs_unlink(fs, dir, "appended_second", f1, 0),
		 EXT2_ET_DIR_NO_SPACE, "unlink mismatched ino rejected");
	CHECK_RC(ext2fs_unlink(fs, dir, "appended_second", f1,
			       EXT2FS_UNLINK_FORCE), 0,
		 "unlink with EXT2FS_UNLINK_FORCE");
	CHECK_RC(ext2fs_lookup(fs, dir, "appended_second", 15, NULL, &found),
		 EXT2_ET_FILE_NOT_FOUND, "appended_second unlinked");

	/* Unlink first entry of block 1 (offset == 0 -> dirent->inode = 0) */
	CHECK_RC(ext2fs_unlink(fs, dir, NULL, f2, 0), 0,
		 "unlink by inode number only (offset 0 in block 1)");
	memset(&wi, 0, sizeof(wi));
	CHECK_RC(ext2fs_dir_iterate2(fs, dir, DIRENT_FLAG_INCLUDE_EMPTY,
				     NULL, dir_walk_cb, &wi), 0,
		 "dir_iterate2 with INCLUDE_EMPTY");
	CHECK(wi.empty_seen >= 1, "offset-0 unlinked slot seen as empty");

	/* Reuse empty slot & test DIRENT_CHANGED writeback + DIRENT_ABORT */
	CHECK_RC(ext2fs_link(fs, dir, "reused_slot", f2, EXT2_FT_REG_FILE), 0,
		 "reuse empty slot via ext2fs_link");
	memset(&wi, 0, sizeof(wi));
	wi.mutate_name = "reused_slot";
	wi.new_ino = f4;
	CHECK_RC(ext2fs_dir_iterate2(fs, dir, 0, NULL, dir_walk_cb, &wi), 0,
		 "DIRENT_CHANGED writeback");
	CHECK_RC(ext2fs_lookup(fs, dir, "reused_slot", 11, NULL, &found), 0,
		 "lookup after DIRENT_CHANGED");
	CHECK(found == f4, "DIRENT_CHANGED persisted new inode & csum");

	memset(&wi, 0, sizeof(wi));
	wi.abort_at = 3;
	CHECK_RC(ext2fs_dir_iterate2(fs, dir, 0, NULL, dir_walk_cb, &wi), 0,
		 "DIRENT_ABORT early termination");
	CHECK(wi.total == 3, "DIRENT_ABORT stopped after 3 entries");

	/*
	 * Coalescing >= 3 consecutive deleted (inode == 0) entries in a
	 * single link_proc() pass without expanding the directory.
	 */
	{
		ext2_ino_t		cdir = 0, cf = 0;
		struct ext2_inode	cinode;
		blk64_t			cblk = 0;
		char			*cbuf = NULL;
		unsigned int		off = 0, rlen = 0;
		int			cleared = 0;
		const char		*long_name =
			"coalesced_four_deleted_entries_ok"; /* 33B -> 44B */

		CHECK_RC(ext2fs_mkdir2(fs, EXT2_ROOT_INO, 0, 0, 0,
				       "coal_dir", &cdir), 0, "mkdir coal_dir");
		for (i = 0; i < 100; i++) {
			snprintf(name, sizeof(name), "s%02d", i); /* 12B rec */
			rc = ext2fs_link(fs, cdir, name, f1, EXT2_FT_REG_FILE);
			if (rc == EXT2_ET_DIR_NO_SPACE)
				break;
			CHECK_RC(rc, 0, "fill coal_dir");
		}
		CHECK_RC(ext2fs_read_inode(fs, cdir, &cinode), 0,
			 "read coal_dir");
		CHECK_RC(ext2fs_bmap2(fs, cdir, &cinode, NULL, 0, 0, NULL,
				      &cblk), 0, "bmap coal_dir");
		CHECK_RC(ext2fs_get_mem(fs->blocksize, &cbuf), 0,
			 "alloc cbuf");
		CHECK_RC(ext2fs_read_dir_block4(fs, cblk, cbuf, 0, cdir), 0,
			 "read coal_dir blk0");
		for (off = 0; off < fs->blocksize - 12; off += rlen) {
			struct ext2_dir_entry *cde =
				(struct ext2_dir_entry *) (cbuf + off);

			CHECK_RC(ext2fs_get_rec_len(fs, cde, &rlen), 0,
				 "get cde rec_len");
			if (off >= 24 && cleared < 4) {
				cde->inode = 0;
				cleared++;
			}
		}
		CHECK(cleared == 4, "cleared 4 adjacent 12B entries (48B)");
		CHECK_RC(ext2fs_write_dir_block4(fs, cblk, cbuf, 0, cdir), 0,
			 "write coal_dir blk0");
		CHECK_RC(ext2fs_link(fs, cdir, long_name, f3,
				     EXT2_FT_REG_FILE), 0,
			 "link 44B entry into 4x12B deleted run");
		CHECK_RC(ext2fs_lookup(fs, cdir, long_name, strlen(long_name),
				       NULL, &cf), 0,
			 "lookup coalesced entry");
		CHECK(cf == f3, "coalesced entry points to f3");
		ext2fs_free_mem(&cbuf);
	}

	/*
	 * EXT2FS_LINK_APPEND with i_size < blocksize must not underflow
	 * lblk to UINT64_MAX (EXT2_ET_FILE_TOO_BIG), and trailing sparse
	 * hole blocks must not read/overwrite physical block 0.
	 */
	{
		ext2_ino_t		udir = 0, edir = 0, uf = 0;
		struct ext2_inode	uinode;
		unsigned char		blk0_save[64];

		CHECK_RC(ext2fs_mkdir2(fs, EXT2_ROOT_INO, 0, 0, 0,
				       "under_dir", &udir), 0,
			 "mkdir under_dir");
		CHECK_RC(ext2fs_read_inode(fs, udir, &uinode), 0,
			 "read under_dir");
		uinode.i_size = 0;
		CHECK_RC(ext2fs_write_inode(fs, udir, &uinode), 0,
			 "set under_dir i_size = 0");
		CHECK_RC(ext2fs_link(fs, udir, "append_zero_isize", f2,
				     EXT2_FT_REG_FILE | EXT2FS_LINK_APPEND), 0,
			 "EXT2FS_LINK_APPEND with i_size == 0");
		CHECK_RC(ext2fs_lookup(fs, udir, "append_zero_isize", 17,
				       NULL, &uf), 0,
			 "lookup append_zero_isize");
		CHECK(uf == f2, "append_zero_isize linked to f2");

		/* Trailing sparse hole at lblk 1 must not clobber pblk 0 */
		memset(mem_disk, 0x5a, sizeof(blk0_save));
		memcpy(blk0_save, mem_disk, sizeof(blk0_save));
		CHECK_RC(ext2fs_read_inode(fs, udir, &uinode), 0,
			 "re-read under_dir");
		uinode.i_size = 2 * fs->blocksize;
		CHECK_RC(ext2fs_write_inode(fs, udir, &uinode), 0,
			 "set under_dir sparse lblk 1");
		CHECK_RC(ext2fs_link(fs, udir, "append_sparse_tail", f3,
				     EXT2_FT_REG_FILE | EXT2FS_LINK_APPEND), 0,
			 "EXT2FS_LINK_APPEND falls back on sparse tail");
		CHECK(memcmp(mem_disk, blk0_save, sizeof(blk0_save)) == 0,
		      "physical block 0 untouched by sparse EXT2FS_LINK_APPEND");

		/* Unallocated 0-byte dir returns DIR_NO_SPACE, not FILE_TOO_BIG */
		CHECK_RC(ext2fs_new_inode(fs, EXT2_ROOT_INO,
					  LINUX_S_IFDIR | 0755, 0, &edir), 0,
			 "alloc empty dir ino");
		ext2fs_inode_alloc_stats2(fs, edir, +1, 1);
		memset(&uinode, 0, sizeof(uinode));
		uinode.i_mode = LINUX_S_IFDIR | 0755;
		uinode.i_links_count = 2;
		CHECK_RC(ext2fs_write_new_inode(fs, edir, &uinode), 0,
			 "write 0-byte empty dir");
		CHECK_RC(ext2fs_link(fs, edir, "x", f1,
				     EXT2_FT_REG_FILE | EXT2FS_LINK_APPEND),
			 EXT2_ET_DIR_NO_SPACE,
			 "0-byte unallocated dir returns DIR_NO_SPACE");
	}

	/* Corrupt rec_len in block 1 returns EXT2_ET_DIR_CORRUPTED */
	CHECK_RC(ext2fs_read_inode(fs, dir, &inode), 0, "read dir inode");
	CHECK_RC(ext2fs_bmap2(fs, dir, &inode, NULL, 0, 0, NULL, &blk0), 0,
		 "bmap blk0");
	CHECK_RC(ext2fs_bmap2(fs, dir, &inode, NULL, 0, 1, NULL, &blk1), 0,
		 "bmap blk1");
	CHECK_RC(ext2fs_get_mem(fs->blocksize, &blkbuf), 0, "alloc blkbuf");
	CHECK_RC(ext2fs_read_dir_block4(fs, blk1, blkbuf, 0, dir), 0,
		 "read blk1");
	((struct ext2_dir_entry *) blkbuf)->rec_len = 6; /* < 8 and % 4 != 0 */
	fs->flags |= EXT2_FLAG_IGNORE_CSUM_ERRORS;
	CHECK_RC(io_channel_write_blk64(fs->io, blk1, 1, blkbuf), 0,
		 "write corrupt rec_len blk1");
	memset(&wi, 0, sizeof(wi));
	CHECK_RC(ext2fs_dir_iterate2(fs, dir, 0, NULL, dir_walk_cb, &wi),
		 EXT2_ET_DIR_CORRUPTED, "corrupt rec_len detected");
	ext2fs_free_mem(&blkbuf);

	teardown_fs(&fs);

	/*
	 * 64 KiB blocksize unlink: merging the only two entries in a 64 KiB
	 * directory block must encode rec_len via ext2fs_set_rec_len()
	 * (EXT4_MAX_REC_LEN == 0xffff) instead of wrapping 16-bit addition
	 * to 0.
	 */
	fs = setup_fs(65536, 64, 256, 256, 0,
		      EXT3_FEATURE_INCOMPAT_EXTENTS |
		      EXT2_FEATURE_INCOMPAT_FILETYPE, 0);
	dir = 0;
	CHECK_RC(ext2fs_mkdir2(fs, EXT2_ROOT_INO, 0, 0, 0, "d64k", &dir), 0,
		 "mkdir d64k");
	CHECK_RC(ext2fs_expand_dir(fs, dir), 0, "expand d64k to 2x64KiB");
	f1 = alloc_reg_inode(fs);
	f2 = alloc_reg_inode(fs);
	CHECK_RC(ext2fs_link(fs, dir, "first", f1,
			     EXT2_FT_REG_FILE | EXT2FS_LINK_APPEND), 0,
		 "link first in 64KiB blk1");
	CHECK_RC(ext2fs_link(fs, dir, "second", f2,
			     EXT2_FT_REG_FILE | EXT2FS_LINK_APPEND), 0,
		 "link second in 64KiB blk1");
	CHECK_RC(ext2fs_unlink(fs, dir, "second", f2, 0), 0,
		 "unlink second in 64KiB blk1");
	CHECK_RC(ext2fs_read_inode(fs, dir, &inode), 0, "read d64k inode");
	CHECK_RC(ext2fs_bmap2(fs, dir, &inode, NULL, 0, 1, NULL, &blk1), 0,
		 "bmap d64k blk1");
	CHECK_RC(ext2fs_get_mem(fs->blocksize, &blkbuf), 0, "alloc 64k buf");
	CHECK_RC(ext2fs_read_dir_block4(fs, blk1, blkbuf, 0, dir), 0,
		 "read d64k blk1");
	CHECK(((struct ext2_dir_entry *) blkbuf)->rec_len == 0xffff,
	      "64KiB merged rec_len encoded as 0xffff, not wrapped to 0");
	CHECK_RC(ext2fs_lookup(fs, dir, "first", 5, NULL, &found), 0,
		 "lookup first after 64KiB unlink");
	CHECK(found == f1, "first still resolves to f1");
	CHECK_RC(ext2fs_lookup(fs, dir, "second", 6, NULL, &found),
		 EXT2_ET_FILE_NOT_FOUND, "second gone after 64KiB unlink");
	ext2fs_free_mem(&blkbuf);
	teardown_fs(&fs);
	printf("  test_link_unlink_and_iterate: ok\n");
}

/*
 * Suite 4: HTree (EXT2_INDEX_FL) indexed directory leaf splitting and
 *          multi-level tree depth growth via ext2fs_link().
 */
static void test_htree_dx_link(void)
{
	ext2_filsys			fs;
	ext2_ino_t			hdir = 0, target_ino, found = 0;
	struct ext2_inode		inode;
	struct ext2_dir_entry		*de;
	struct ext2_dx_root_info	*root;
	struct ext2_dx_countlimit	*cl;
	struct ext2_dx_entry		*ent;
	blk64_t				b0 = 0, b1 = 0;
	char				*rbuf = NULL, *lbuf = NULL;
	char				name[32];
	int				i;

	fs = setup_fs(1024, 2048, 256, 256,
		      EXT2_FEATURE_COMPAT_DIR_INDEX |
		      EXT2_FEATURE_COMPAT_EXT_ATTR,
		      EXT3_FEATURE_INCOMPAT_EXTENTS |
		      EXT2_FEATURE_INCOMPAT_FILETYPE,
		      EXT4_FEATURE_RO_COMPAT_METADATA_CSUM);
	fs->super->s_def_hash_version = EXT2_HASH_HALF_MD4;

	CHECK_RC(ext2fs_mkdir2(fs, EXT2_ROOT_INO, 0, 0, 0, "htree", &hdir), 0,
		 "mkdir htree");
	CHECK_RC(ext2fs_expand_dir(fs, hdir), 0, "expand htree to 2 blocks");
	CHECK_RC(ext2fs_read_inode(fs, hdir, &inode), 0, "read hdir inode");
	CHECK_RC(ext2fs_bmap2(fs, hdir, &inode, NULL, 0, 0, NULL, &b0), 0,
		 "bmap hdir lblk 0");
	CHECK_RC(ext2fs_bmap2(fs, hdir, &inode, NULL, 0, 1, NULL, &b1), 0,
		 "bmap hdir lblk 1");

	/*
	 * Format lblk 0 as a valid DX root block with limit = 4 entries so
	 * inserting ~220 entries splits leaves AND grows indirect_levels
	 * from 0 -> 1!
	 */
	CHECK_RC(ext2fs_get_memzero(fs->blocksize, &rbuf), 0, "alloc rbuf");
	de = (struct ext2_dir_entry *) rbuf;
	de->inode = hdir;
	ext2fs_dirent_set_name_len(de, 1);
	ext2fs_dirent_set_file_type(de, EXT2_FT_DIR);
	de->rec_len = 12;
	de->name[0] = '.';

	de = (struct ext2_dir_entry *) (rbuf + 12);
	de->inode = EXT2_ROOT_INO;
	ext2fs_dirent_set_name_len(de, 2);
	ext2fs_dirent_set_file_type(de, EXT2_FT_DIR);
	de->rec_len = fs->blocksize - 12;
	de->name[0] = '.';
	de->name[1] = '.';

	root = (struct ext2_dx_root_info *) (rbuf + 24);
	root->hash_version = EXT2_HASH_HALF_MD4;
	root->info_length = sizeof(struct ext2_dx_root_info);
	root->indirect_levels = 0;

	cl = (struct ext2_dx_countlimit *) (rbuf + 32);
	cl->limit = ext2fs_cpu_to_le16(4);
	cl->count = ext2fs_cpu_to_le16(1);
	ent = (struct ext2_dx_entry *) cl;
	ent[0].block = ext2fs_cpu_to_le32(1);

	CHECK_RC(ext2fs_write_dir_block4(fs, b0, rbuf, 0, hdir), 0,
		 "write dx root block");
	CHECK_RC(ext2fs_new_dir_block(fs, 0, 0, &lbuf), 0,
		 "new empty leaf block");
	CHECK_RC(ext2fs_write_dir_block4(fs, b1, lbuf, 0, hdir), 0,
		 "write initial leaf block");

	inode.i_flags |= EXT2_INDEX_FL;
	CHECK_RC(ext2fs_write_inode(fs, hdir, &inode), 0,
		 "set EXT2_INDEX_FL on hdir");

	target_ino = alloc_reg_inode(fs);
	for (i = 0; i < 220; i++) {
		snprintf(name, sizeof(name), "htree_file_%04d", i);
		CHECK_RC(ext2fs_link(fs, hdir, name, target_ino,
				     EXT2_FT_REG_FILE), 0,
			 "dx_link entry");
	}

	/* Verify tree grew to indirect_levels == 1 and every entry resolves */
	CHECK_RC(ext2fs_read_dir_block4(fs, b0, rbuf, 0, hdir), 0,
		 "re-read dx root with valid csum");
	root = (struct ext2_dx_root_info *) (rbuf + 24);
	CHECK(root->indirect_levels == 1,
	      "HTree grew from 0 to 1 indirect level");

	for (i = 0; i < 220; i += 17) {
		snprintf(name, sizeof(name), "htree_file_%04d", i);
		found = 0;
		CHECK_RC(ext2fs_lookup(fs, hdir, name, strlen(name),
				       NULL, &found), 0,
			 "lookup HTree entry");
		CHECK(found == target_ino, "HTree lookup matched target_ino");
	}

	/*
	 * Verify HTree split zeroes slack bytes in repacked leaf blocks (no
	 * leaked heap garbage or leftover dirents from the sibling block).
	 */
	{
		blk64_t		leaf_blk = 0;
		unsigned int	off = 0, rlen = 0, used;
		int		slack_nonzero = 0;

		CHECK_RC(ext2fs_read_inode(fs, hdir, &inode), 0,
			 "re-read hdir inode");
		CHECK_RC(ext2fs_bmap2(fs, hdir, &inode, NULL, 0, 1, NULL,
				      &leaf_blk), 0, "bmap hdir leaf 1");
		CHECK_RC(ext2fs_read_dir_block4(fs, leaf_blk, lbuf, 0, hdir),
			 0, "read split leaf 1");
		for (off = 0; off < fs->blocksize - 12; off += rlen) {
			de = (struct ext2_dir_entry *) (lbuf + off);
			CHECK_RC(ext2fs_get_rec_len(fs, de, &rlen), 0,
				 "get split leaf rec_len");
			if (de->inode != 0)
				used = EXT2_DIR_REC_LEN(
					ext2fs_dirent_name_len(de));
			else
				used = 8;
			for (i = (int) used; i < (int) rlen; i++) {
				if (lbuf[off + i] != 0)
					slack_nonzero++;
			}
		}
		CHECK(slack_nonzero == 0,
		      "split HTree leaf has zeroed slack space");
	}

	/*
	 * Test HTree leaf with only deleted small entries (coalesced cleanly)
	 * and corrupt nameless entries (rejected without map[-1] OOB or
	 * writing uninitialized blocks).
	 */
	{
		ext2_ino_t	hdir2 = 0, found2 = 0;
		blk64_t		hb0 = 0, hb1 = 0;
		unsigned int	off;
		const char	*big60 =
			"sixty_char_htree_entry_into_fragmented_deleted_leaf_12345678";

		CHECK_RC(ext2fs_mkdir2(fs, EXT2_ROOT_INO, 0, 0, 0,
				       "htree2", &hdir2), 0, "mkdir htree2");
		CHECK_RC(ext2fs_expand_dir(fs, hdir2), 0, "expand htree2");
		CHECK_RC(ext2fs_read_inode(fs, hdir2, &inode), 0, "read hdir2");
		CHECK_RC(ext2fs_bmap2(fs, hdir2, &inode, NULL, 0, 0, NULL,
				      &hb0), 0, "bmap hdir2 0");
		CHECK_RC(ext2fs_bmap2(fs, hdir2, &inode, NULL, 0, 1, NULL,
				      &hb1), 0, "bmap hdir2 1");

		memset(rbuf, 0, fs->blocksize);
		de = (struct ext2_dir_entry *) rbuf;
		de->inode = hdir2;
		ext2fs_dirent_set_name_len(de, 1);
		ext2fs_dirent_set_file_type(de, EXT2_FT_DIR);
		de->rec_len = 12;
		de->name[0] = '.';
		de = (struct ext2_dir_entry *) (rbuf + 12);
		de->inode = EXT2_ROOT_INO;
		ext2fs_dirent_set_name_len(de, 2);
		ext2fs_dirent_set_file_type(de, EXT2_FT_DIR);
		de->rec_len = fs->blocksize - 12;
		de->name[0] = '.';
		de->name[1] = '.';
		root = (struct ext2_dx_root_info *) (rbuf + 24);
		root->hash_version = EXT2_HASH_HALF_MD4;
		root->info_length = sizeof(struct ext2_dx_root_info);
		cl = (struct ext2_dx_countlimit *) (rbuf + 32);
		cl->limit = ext2fs_cpu_to_le16(8);
		cl->count = ext2fs_cpu_to_le16(1);
		ent = (struct ext2_dx_entry *) cl;
		ent[0].block = ext2fs_cpu_to_le32(1);
		CHECK_RC(ext2fs_write_dir_block4(fs, hb0, rbuf, 0, hdir2), 0,
			 "write htree2 root");

		/* Fill leaf 1 with fragmented 12B deleted slots (inode = 0) */
		memset(lbuf, 0, fs->blocksize);
		for (off = 0; off + 12 <= fs->blocksize - 12; off += 12) {
			unsigned int rlen = (off + 24 > fs->blocksize - 12) ?
				(fs->blocksize - 12 - off) : 12;

			de = (struct ext2_dir_entry *) (lbuf + off);
			de->rec_len = rlen;
		}
		ext2fs_initialize_dirent_tail(fs,
			EXT2_DIRENT_TAIL(lbuf, fs->blocksize));
		CHECK_RC(ext2fs_write_dir_block4(fs, hb1, lbuf, 0, hdir2), 0,
			 "write fragmented empty htree2 leaf");
		inode.i_flags |= EXT2_INDEX_FL;
		CHECK_RC(ext2fs_write_inode(fs, hdir2, &inode), 0,
			 "mark hdir2 indexed");

		/* Poison freed 1 KiB heap chunk to catch uninit buf2 writes */
		{
			char *poison = NULL;

			if (ext2fs_get_mem(fs->blocksize, &poison) == 0) {
				memset(poison, 0xaa, fs->blocksize);
				ext2fs_free_mem(&poison);
			}
		}

		CHECK_RC(ext2fs_link(fs, hdir2, big60, target_ino,
				     EXT2_FT_REG_FILE), 0,
			 "dx_link 60B into fragmented deleted leaf");
		CHECK_RC(ext2fs_lookup(fs, hdir2, big60, strlen(big60),
				       NULL, &found2), 0,
			 "lookup big60 in htree2");
		CHECK(found2 == target_ino, "big60 points to target_ino");

		/* Corrupt nameless live entries rejected cleanly */
		memset(lbuf, 0, fs->blocksize);
		for (off = 0; off + 12 <= fs->blocksize - 12; off += 12) {
			unsigned int rlen = (off + 24 > fs->blocksize - 12) ?
				(fs->blocksize - 12 - off) : 12;

			de = (struct ext2_dir_entry *) (lbuf + off);
			de->inode = target_ino;
			de->rec_len = rlen;
		}
		ext2fs_initialize_dirent_tail(fs,
			EXT2_DIRENT_TAIL(lbuf, fs->blocksize));
		CHECK_RC(ext2fs_write_dir_block4(fs, hb1, lbuf, 0, hdir2), 0,
			 "write corrupt nameless htree2 leaf");
		CHECK_RC(ext2fs_link(fs, hdir2, "another_entry", target_ino,
				     EXT2_FT_REG_FILE), EXT2_ET_DIR_CORRUPTED,
			 "dx_link rejects corrupt nameless leaf");
		CHECK_RC(ext2fs_read_inode(fs, hdir2, &inode), 0,
			 "re-read hdir2 after corrupt rejection");
		CHECK(EXT2_I_SIZE(&inode) == 2048,
		      "corrupt leaf rejection did not grow hdir2 i_size");
	}

	ext2fs_free_mem(&lbuf);
	ext2fs_free_mem(&rbuf);
	teardown_fs(&fs);
	printf("  test_htree_dx_link: ok\n");
}

/*
 * Suite 5: ext2fs_symlink, ext2fs_is_fast_symlink, ext2fs_namei,
 *          ext2fs_namei_follow, ext2fs_follow_link, and ext2fs_get_pathname.
 */
static void test_symlinks_namei_and_pathname(void)
{
	ext2_filsys		fs;
	ext2_ino_t		dir_a = 0, dir_b = 0, file_ino, orphan_dir = 0;
	ext2_ino_t		sym_ino = 0, resolved = 0;
	struct ext2_inode	inode;
	char			long_target[1100], med_target[96], big_target[240];
	char			*path = NULL;

	/* First test fast + slow block symlinks on a non-inline filesystem */
	fs = setup_fs(1024, 1024, 256, 256,
		      EXT2_FEATURE_COMPAT_EXT_ATTR,
		      EXT3_FEATURE_INCOMPAT_EXTENTS |
		      EXT2_FEATURE_INCOMPAT_FILETYPE,
		      EXT4_FEATURE_RO_COMPAT_METADATA_CSUM);

	CHECK_RC(ext2fs_mkdir2(fs, EXT2_ROOT_INO, 0, 0, 0, "a", &dir_a), 0,
		 "mkdir /a");
	CHECK_RC(ext2fs_mkdir2(fs, dir_a, 0, 0, 0, "b", &dir_b), 0,
		 "mkdir /a/b");
	file_ino = alloc_reg_inode(fs);
	CHECK_RC(ext2fs_link(fs, dir_b, "hello.txt", file_ino,
			     EXT2_FT_REG_FILE), 0, "link /a/b/hello.txt");

	/* Overlong target (>= blocksize) rejected */
	memset(long_target, 'x', 1024);
	long_target[1024] = '\0';
	CHECK_RC(ext2fs_symlink(fs, EXT2_ROOT_INO, 0, "too_long", long_target),
		 EXT2_ET_INVALID_ARGUMENT, "symlink >= blocksize rejected");

	/* Fast symlink (< 60 bytes) & duplicate rejection */
	CHECK_RC(ext2fs_symlink(fs, EXT2_ROOT_INO, 0, "fast_file",
				"a/b/hello.txt"), 0, "create fast symlink");
	CHECK_RC(ext2fs_symlink(fs, EXT2_ROOT_INO, 0, "fast_file",
				"a/b/hello.txt"), EXT2_ET_FILE_EXISTS,
		 "duplicate symlink name rejected");
	CHECK_RC(ext2fs_lookup(fs, EXT2_ROOT_INO, "fast_file", 9,
			       NULL, &sym_ino), 0, "lookup fast_file");
	CHECK_RC(ext2fs_read_inode(fs, sym_ino, &inode), 0, "read fast_file");
	CHECK(ext2fs_is_fast_symlink(&inode) == 1, "is_fast_symlink == 1");

	/* Slow block-backed symlink (>= 60 bytes, padded with ././...) */
	memset(med_target, 0, sizeof(med_target));
	while (strlen(med_target) < 64)
		strcat(med_target, "./");
	strcat(med_target, "a/b/hello.txt");
	CHECK_RC(ext2fs_symlink(fs, EXT2_ROOT_INO, 0, "slow_file", med_target),
		 0, "create slow symlink");
	CHECK_RC(ext2fs_lookup(fs, EXT2_ROOT_INO, "slow_file", 9,
			       NULL, &sym_ino), 0, "lookup slow_file");
	CHECK_RC(ext2fs_read_inode(fs, sym_ino, &inode), 0, "read slow_file");
	CHECK(ext2fs_is_fast_symlink(&inode) == 0, "is_fast_symlink == 0");
	CHECK(ext2fs_inode_i_blocks(fs, &inode) == 2, "slow symlink 1 block");

	/* Directory symlink for intermediate traversal + symlink loop */
	CHECK_RC(ext2fs_symlink(fs, EXT2_ROOT_INO, 0, "link_a", "/a"), 0,
		 "create /link_a -> /a");
	CHECK_RC(ext2fs_symlink(fs, EXT2_ROOT_INO, 0, "loop1", "loop2"), 0,
		 "create loop1 -> loop2");
	CHECK_RC(ext2fs_symlink(fs, EXT2_ROOT_INO, 0, "loop2", "loop1"), 0,
		 "create loop2 -> loop1");

	/* ext2fs_namei, ext2fs_namei_follow, ext2fs_follow_link */
	CHECK_RC(ext2fs_namei(fs, EXT2_ROOT_INO, EXT2_ROOT_INO,
			      "/a/b/hello.txt", &resolved), 0,
		 "namei /a/b/hello.txt");
	CHECK(resolved == file_ino, "namei resolved file_ino");

	CHECK_RC(ext2fs_namei(fs, EXT2_ROOT_INO, dir_a, "b/", &resolved), 0,
		 "namei relative trailing slash b/");
	CHECK(resolved == dir_b, "namei b/ -> dir_b");

	CHECK_RC(ext2fs_namei(fs, EXT2_ROOT_INO, EXT2_ROOT_INO,
			      "/link_a/b/hello.txt", &resolved), 0,
		 "namei traverses intermediate symlink");
	CHECK(resolved == file_ino, "intermediate symlink resolved");

	CHECK_RC(ext2fs_namei(fs, EXT2_ROOT_INO, EXT2_ROOT_INO,
			      "/slow_file", &resolved), 0,
		 "namei does not follow terminal symlink");
	CHECK(resolved == sym_ino, "namei returned symlink inode");

	CHECK_RC(ext2fs_namei_follow(fs, EXT2_ROOT_INO, EXT2_ROOT_INO,
				     "/slow_file", &resolved), 0,
		 "namei_follow slow symlink");
	CHECK(resolved == file_ino, "namei_follow resolved slow symlink");

	CHECK_RC(ext2fs_namei_follow(fs, EXT2_ROOT_INO, EXT2_ROOT_INO,
				     "/fast_file", &resolved), 0,
		 "namei_follow fast symlink");
	CHECK(resolved == file_ino, "namei_follow resolved fast symlink");

	CHECK_RC(ext2fs_follow_link(fs, EXT2_ROOT_INO, EXT2_ROOT_INO,
				    file_ino, &resolved), 0,
		 "follow_link on non-symlink");
	CHECK(resolved == file_ino, "follow_link on non-symlink returns self");

	CHECK_RC(ext2fs_namei_follow(fs, EXT2_ROOT_INO, EXT2_ROOT_INO,
				     "/loop1", &resolved),
		 EXT2_ET_SYMLINK_LOOP, "symlink loop detected");

	/*
	 * Corrupted slow symlinks: i_size == 0, i_size >= blocksize,
	 * i_size_high != 0, sparse hole (blk == 0, even if boot block 0
	 * contains a valid path), and out-of-range pblk must all be rejected
	 * with EXT2_ET_DIR_CORRUPTED without over-reading buffer or block 0.
	 */
	{
		ext2_ino_t	bad_sym = 0;
		blk64_t		sblk = 0;
		char		*sbuf = NULL;

		/* Plant a valid 64B symlink target in boot block 0 */
		memcpy(mem_disk, med_target, strlen(med_target));

		CHECK_RC(ext2fs_new_inode(fs, EXT2_ROOT_INO,
					  LINUX_S_IFLNK | 0777, 0, &bad_sym),
			 0, "alloc bad_sym inode");
		ext2fs_inode_alloc_stats2(fs, bad_sym, +1, 0);
		memset(&inode, 0, sizeof(inode));
		inode.i_mode = LINUX_S_IFLNK | 0777;
		inode.i_links_count = 1;

		/* 1) i_size == 0 must not silently resolve to cwd */
		inode.i_size = 0;
		CHECK_RC(ext2fs_write_new_inode(fs, bad_sym, &inode), 0,
			 "write zero-size symlink");
		CHECK_RC(ext2fs_follow_link(fs, EXT2_ROOT_INO, EXT2_ROOT_INO,
					    bad_sym, &resolved),
			 EXT2_ET_DIR_CORRUPTED,
			 "zero-size symlink rejected");

		/* 2) Sparse slow symlink (blk == 0) must not read block 0 */
		inode.i_size = strlen(med_target);
		CHECK_RC(ext2fs_write_inode(fs, bad_sym, &inode), 0,
			 "write sparse slow symlink");
		CHECK_RC(ext2fs_follow_link(fs, EXT2_ROOT_INO, EXT2_ROOT_INO,
					    bad_sym, &resolved),
			 EXT2_ET_DIR_CORRUPTED,
			 "sparse slow symlink (blk 0) rejected");
		memset(mem_disk, 0, strlen(med_target));

		/* 3) Out-of-range physical block rejected */
		inode.i_block[0] = ext2fs_blocks_count(fs->super) + 10;
		CHECK_RC(ext2fs_write_inode(fs, bad_sym, &inode), 0,
			 "write oob-blk slow symlink");
		CHECK_RC(ext2fs_follow_link(fs, EXT2_ROOT_INO, EXT2_ROOT_INO,
					    bad_sym, &resolved),
			 EXT2_ET_DIR_CORRUPTED,
			 "out-of-range symlink blk rejected");

		/* 4) i_size >= fs->blocksize and i_size_high != 0 rejected */
		CHECK_RC(ext2fs_new_block2(fs, 0, NULL, &sblk), 0,
			 "alloc bad_sym block");
		ext2fs_block_alloc_stats2(fs, sblk, +1);
		CHECK_RC(ext2fs_get_memzero(fs->blocksize, &sbuf), 0,
			 "alloc sbuf");
		for (resolved = 0; resolved + 2 <= fs->blocksize - 2;
		     resolved += 2) {
			sbuf[resolved] = '.';
			sbuf[resolved + 1] = '/';
		}
		sbuf[fs->blocksize - 2] = 'a';
		sbuf[fs->blocksize - 1] = '/';
		CHECK_RC(io_channel_write_blk64(fs->io, sblk, 1, sbuf), 0,
			 "write 1024B non-NUL path block");
		ext2fs_free_mem(&sbuf);

		inode.i_block[0] = (blk_t) sblk;
		ext2fs_iblk_set(fs, &inode, 1);
		inode.i_size = fs->blocksize;
		CHECK_RC(ext2fs_write_inode(fs, bad_sym, &inode), 0,
			 "write blocksize symlink");
		CHECK_RC(ext2fs_follow_link(fs, EXT2_ROOT_INO, EXT2_ROOT_INO,
					    bad_sym, &resolved),
			 EXT2_ET_DIR_CORRUPTED,
			 "i_size == blocksize symlink rejected");

		inode.i_size = 10;
		inode.i_size_high = 1;
		CHECK_RC(ext2fs_write_inode(fs, bad_sym, &inode), 0,
			 "write i_size_high symlink");
		CHECK_RC(ext2fs_follow_link(fs, EXT2_ROOT_INO, EXT2_ROOT_INO,
					    bad_sym, &resolved),
			 EXT2_ET_DIR_CORRUPTED,
			 "i_size_high != 0 symlink rejected");
	}

	/* ext2fs_get_pathname */
	CHECK_RC(ext2fs_get_pathname(fs, EXT2_ROOT_INO, EXT2_ROOT_INO, &path),
		 0, "get_pathname root");
	CHECK(strcmp(path, "/") == 0, "root pathname is /");
	ext2fs_free_mem(&path);

	CHECK_RC(ext2fs_get_pathname(fs, dir_b, 0, &path), 0,
		 "get_pathname /a/b");
	CHECK(strcmp(path, "/a/b") == 0, "dir_b pathname is /a/b");
	ext2fs_free_mem(&path);

	CHECK_RC(ext2fs_get_pathname(fs, dir_b, file_ino, &path), 0,
		 "get_pathname /a/b/hello.txt");
	CHECK(strcmp(path, "/a/b/hello.txt") == 0,
	      "file pathname is /a/b/hello.txt");
	ext2fs_free_mem(&path);

	CHECK_RC(ext2fs_get_pathname(fs, dir_b, 99999, &path), 0,
		 "get_pathname missing child");
	CHECK(strcmp(path, "/a/b/???") == 0, "missing child -> /a/b/???");
	ext2fs_free_mem(&path);

	CHECK_RC(ext2fs_get_pathname(fs, file_ino, sym_ino, &path), 0,
		 "get_pathname non-dir parent");
	CHECK(path[0] == '<', "non-dir parent formatted as <dir>/<ino>");
	ext2fs_free_mem(&path);

	/* Orphaned directory (.. inode == 0) produces ".../child" */
	{
		struct dir_walk_info owi;

		CHECK_RC(ext2fs_mkdir2(fs, EXT2_ROOT_INO, 0, 0, 0, NULL,
				       &orphan_dir), 0,
			 "mkdir unlinked dir");
		memset(&owi, 0, sizeof(owi));
		owi.mutate_name = "..";
		owi.new_ino = 0;
		CHECK_RC(ext2fs_dir_iterate2(fs, orphan_dir, 0, NULL,
					     dir_walk_cb, &owi), 0,
			 "zero .. in orphan_dir");
	}
	CHECK_RC(ext2fs_link(fs, orphan_dir, "orphan_f", file_ino,
			     EXT2_FT_REG_FILE), 0, "link in orphan_dir");
	CHECK_RC(ext2fs_get_pathname(fs, orphan_dir, file_ino, &path), 0,
		 "get_pathname in orphan_dir");
	CHECK(strcmp(path, ".../orphan_f") == 0,
	      "orphan_dir pathname is .../orphan_f");
	ext2fs_free_mem(&path);

	teardown_fs(&fs);

	/* Inline-data symlink & fallback to block symlink when > ibody space */
	fs = setup_fs(1024, 512, 128, 256,
		      EXT2_FEATURE_COMPAT_EXT_ATTR,
		      EXT4_FEATURE_INCOMPAT_INLINE_DATA |
		      EXT3_FEATURE_INCOMPAT_EXTENTS |
		      EXT2_FEATURE_INCOMPAT_FILETYPE,
		      EXT4_FEATURE_RO_COMPAT_METADATA_CSUM);
	file_ino = alloc_reg_inode(fs);
	CHECK_RC(ext2fs_link(fs, EXT2_ROOT_INO, "tgt", file_ino,
			     EXT2_FT_REG_FILE | EXT2FS_LINK_EXPAND), 0,
		 "link /tgt");

	memset(med_target, 0, sizeof(med_target));
	while (strlen(med_target) < 70)
		strcat(med_target, "./");
	strcat(med_target, "tgt");
	CHECK_RC(ext2fs_symlink(fs, EXT2_ROOT_INO, 0, "inl_sym", med_target),
		 0, "create inline symlink");
	CHECK_RC(ext2fs_lookup(fs, EXT2_ROOT_INO, "inl_sym", 7,
			       NULL, &sym_ino), 0, "lookup inl_sym");
	CHECK_RC(ext2fs_read_inode(fs, sym_ino, &inode), 0, "read inl_sym");
	CHECK((inode.i_flags & EXT4_INLINE_DATA_FL) != 0,
	      "73B symlink stored as inline data");
	CHECK_RC(ext2fs_follow_link(fs, EXT2_ROOT_INO, EXT2_ROOT_INO,
				    sym_ino, &resolved), 0,
		 "follow_link inline symlink");
	CHECK(resolved == file_ino, "inline symlink resolved to /tgt");

	/* Corrupt inline-data symlink i_size > inline_size rejected */
	inode.i_size = 512;
	CHECK_RC(ext2fs_write_inode(fs, sym_ino, &inode), 0,
		 "corrupt inl_sym i_size = 512");
	CHECK_RC(ext2fs_follow_link(fs, EXT2_ROOT_INO, EXT2_ROOT_INO,
				    sym_ino, &resolved),
		 EXT2_ET_DIR_CORRUPTED,
		 "inline symlink with i_size > inline_size rejected");

	memset(big_target, 0, sizeof(big_target));
	while (strlen(big_target) < 220)
		strcat(big_target, "./");
	strcat(big_target, "tgt");
	CHECK_RC(ext2fs_symlink(fs, EXT2_ROOT_INO, 0, "fallback_sym",
				big_target), 0,
		 "create >ibody symlink falling back to block");
	CHECK_RC(ext2fs_lookup(fs, EXT2_ROOT_INO, "fallback_sym", 12,
			       NULL, &sym_ino), 0, "lookup fallback_sym");
	CHECK_RC(ext2fs_read_inode(fs, sym_ino, &inode), 0,
		 "read fallback_sym");
	CHECK((inode.i_flags & EXT4_INLINE_DATA_FL) == 0 &&
	      ext2fs_inode_i_blocks(fs, &inode) == 2,
	      "223B symlink fell back to slow block storage");
	CHECK_RC(ext2fs_follow_link(fs, EXT2_ROOT_INO, EXT2_ROOT_INO,
				    sym_ino, &resolved), 0,
		 "follow_link fallback symlink");
	CHECK(resolved == file_ino, "fallback symlink resolved to /tgt");

	teardown_fs(&fs);
	printf("  test_symlinks_namei_and_pathname: ok\n");
}

int main(int argc EXT2FS_ATTR((unused)), char *argv[] EXT2FS_ATTR((unused)))
{
	add_error_table(&et_ext2_error_table);
	printf("Running tst_dir unit tests...\n");

	test_dirblock_and_reclen();
	test_mkdir_and_expanddir();
	test_link_unlink_and_iterate();
	test_htree_dx_link();
	test_symlinks_namei_and_pathname();

	remove_error_table(&et_ext2_error_table);
	printf("tst_dir: %d checks run, %d failed\n", test_count, fail_count);
	return fail_count ? 1 : 0;
}
