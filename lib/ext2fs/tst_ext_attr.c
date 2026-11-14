/*
 * tst_ext_attr.c --- unit tests for ext_attr.c
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
#include "ext2_ext_attr.h"
#include "ext4_acl.h"
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

static const unsigned char test_uuid[16] = {
	0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
	0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10
};

static void setup_fs(ext2_filsys *ret_fs, unsigned int blocks,
		     unsigned int inodes, unsigned int inode_size,
		     __u32 compat, __u32 incompat, __u32 ro_compat)
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
	param.s_rev_level = EXT2_DYNAMIC_REV;
	ext2fs_blocks_count_set(&param, blocks);
	param.s_inodes_count = inodes;
	param.s_inode_size = inode_size;
	param.s_creator_os = EXT2_OS_LINUX;
	param.s_feature_compat = compat;
	param.s_feature_incompat = incompat | EXT4_FEATURE_INCOMPAT_64BIT;
	param.s_feature_ro_compat = ro_compat;
	param.s_desc_size = EXT2_MIN_DESC_SIZE_64BIT;
	memcpy(param.s_uuid, test_uuid, sizeof(test_uuid));

	retval = ext2fs_initialize("test_fs",
				   EXT2_FLAG_RW | EXT2_FLAG_64BITS |
				   EXT2_FLAG_EXCLUSIVE,
				   &param, mem_io_manager, ret_fs);
	if (retval) {
		com_err("setup_fs", retval, "initializing filesystem");
		exit(1);
	}
	memcpy((*ret_fs)->super->s_uuid, test_uuid, sizeof(test_uuid));
	(*ret_fs)->now = 1700000000;
	ext2fs_init_csum_seed(*ret_fs);

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

static void check_u32(const char *test_name, __u32 actual, __u32 expected)
{
	if (actual != expected) {
		printf("FAIL [%s]: got 0x%08x (%u), expected 0x%08x (%u)\n",
		       test_name, actual, actual, expected, expected);
		test_fail++;
	}
}

static void check_u64(const char *test_name, __u64 actual, __u64 expected)
{
	if (actual != expected) {
		printf("FAIL [%s]: got 0x%llx, expected 0x%llx\n",
		       test_name, (unsigned long long) actual,
		       (unsigned long long) expected);
		test_fail++;
	}
}

struct full_inode {
	struct ext2_inode_large	inode;
	char			extra[128];
};

static void init_reg_inode(ext2_filsys fs, ext2_ino_t ino)
{
	struct full_inode	fino;
	errcode_t		retval;

	memset(&fino, 0, sizeof(fino));
	fino.inode.i_mode = LINUX_S_IFREG | 0644;
	fino.inode.i_links_count = 1;
	fino.inode.i_generation = 0x11223344;
	if (EXT2_INODE_SIZE(fs->super) > EXT2_GOOD_OLD_INODE_SIZE)
		fino.inode.i_extra_isize = 32;
	retval = ext2fs_write_inode_full(fs, ino, EXT2_INODE(&fino.inode),
					 EXT2_INODE_SIZE(fs->super));
	check_err("init_reg_inode", retval, 0);
	ext2fs_inode_alloc_stats2(fs, ino, +1, 0);
}

/*
 * 1. Entry & Block Hash Computation and EA Inode Accessors
 */
static void test_entry_and_block_hashes(void)
{
	ext2_filsys			fs = NULL;
	struct ext2_inode		inode;
	unsigned char			entry_buf[64];
	unsigned char			val_buf[16];
	unsigned char			blk_buf[256];
	struct ext2_ext_attr_entry	*entry, *e1, *e2, *end;
	struct ext2_ext_attr_header	*hdr;
	__u32				h_unsig, h_sig, h2, h3, h3_sig;
	__u32				exp_name_unsig, exp_name_sig;
	__u32				exp_val_hash, exp_ea_ino_hash;
	__u32				exp_ea_ino_sig_hash, exp_blk_hash;
	errcode_t			retval;

	/* 1a. EA inode hash & 64-bit refcount accessors */
	memset(&inode, 0, sizeof(inode));
	ext2fs_set_ea_inode_hash(&inode, 0xa55a1234U);
	check_u32("ea_inode_hash_get", ext2fs_get_ea_inode_hash(&inode),
		  0xa55a1234U);
	check_u32("ea_inode_hash_field", inode.i_atime, 0xa55a1234U);

	ext2fs_set_ea_inode_ref(&inode, 0x123456789abcdef0ULL);
	check_u64("ea_inode_ref_get", ext2fs_get_ea_inode_ref(&inode),
		  0x123456789abcdef0ULL);
	check_u32("ea_inode_ref_hi_ctime", inode.i_ctime, 0x12345678U);
	check_u32("ea_inode_ref_lo_version",
		  inode.osd1.linux1.l_i_version, 0x9abcdef0U);

	/* 1b. Entry hash on ASCII name with empty value */
	memset(entry_buf, 0, sizeof(entry_buf));
	entry = (struct ext2_ext_attr_entry *) entry_buf;
	entry->e_name_len = 2;
	entry->e_name_index = 1;
	entry->e_value_inum = 0;
	entry->e_value_size = 0;
	memcpy(entry_buf + sizeof(*entry), "ab", 2);

	/*
	 * n=0 ('a'=0x61): hash = 0x61
	 * n=1 ('b'=0x62): hash = (0x61 << 5) ^ 0 ^ 0x62 = 0x0c20 ^ 0x62 = 0x0c42
	 */
	exp_name_unsig = 0x00000c42U;
	h_unsig = ext2fs_ext_attr_hash_entry(entry, NULL);
	h_sig = ext2fs_ext_attr_hash_entry_signed(entry, NULL);
	check_u32("hash_ascii_empty_val_unsig", h_unsig, exp_name_unsig);
	check_u32("hash_ascii_empty_val_sig", h_sig, exp_name_unsig);

	/* 1c. Entry hash with 5-byte value (padded to 8 bytes = 2 le32 words) */
	memset(val_buf, 0, sizeof(val_buf));
	/* Word 0: 0x04030201, Word 1: 0x00000005 */
	val_buf[0] = 0x01;
	val_buf[1] = 0x02;
	val_buf[2] = 0x03;
	val_buf[3] = 0x04;
	val_buf[4] = 0x05;
	entry->e_value_size = 5;

	/*
	 * After word 0: (0x0c42 << 16) ^ 0 ^ 0x04030201 = 0x08410201
	 * After word 1: (0x0201 << 16) ^ 0x0841 ^ 0x00000005 = 0x02010844
	 */
	exp_val_hash = 0x02010844U;
	h_unsig = ext2fs_ext_attr_hash_entry(entry, val_buf);
	h_sig = ext2fs_ext_attr_hash_entry_signed(entry, val_buf);
	check_u32("hash_ascii_5byte_val_unsig", h_unsig, exp_val_hash);
	check_u32("hash_ascii_5byte_val_sig", h_sig, exp_val_hash);

	/* 1d. Entry hash on non-ASCII name (high bit set: 0x80) */
	memset(entry_buf, 0, sizeof(entry_buf));
	entry->e_name_len = 1;
	entry->e_name_index = 1;
	entry->e_value_inum = 0;
	entry->e_value_size = 0;
	entry_buf[sizeof(*entry)] = 0x80;

	/* Unsigned: 0 ^ 0x80 = 0x00000080; Signed: 0 ^ (int)(signed char)0x80 = 0xffffff80 */
	exp_name_unsig = 0x00000080U;
	exp_name_sig = 0xffffff80U;
	h_unsig = ext2fs_ext_attr_hash_entry(entry, val_buf);
	h_sig = ext2fs_ext_attr_hash_entry_signed(entry, val_buf);
	check_u32("hash_highbit_unsig", h_unsig, exp_name_unsig);
	check_u32("hash_highbit_sig", h_sig, exp_name_sig);

	/* 1e. ext2fs_ext_attr_hash_entry2 and ext2fs_ext_attr_hash_entry3 with ea_inode */
	setup_fs(&fs, 128, 32, 256, EXT2_FEATURE_COMPAT_EXT_ATTR,
		 EXT4_FEATURE_INCOMPAT_EA_INODE, 0);

	memset(&inode, 0, sizeof(inode));
	inode.i_mode = LINUX_S_IFREG | 0600;
	inode.i_flags = EXT4_EA_INODE_FL;
	inode.i_links_count = 1;
	ext2fs_set_ea_inode_hash(&inode, 0x11223344U);
	retval = ext2fs_write_inode(fs, 15, &inode);
	check_err("write_ea_inode_15", retval, 0);

	entry->e_value_inum = 15;
	entry->e_value_size = 100;
	/*
	 * When e_value_inum != 0, ext2fs_ext_attr_hash_entry ignores data,
	 * returning 0x80 (unsigned) / 0xffffff80 (signed), and hash_entry2/3
	 * mixes in ea_inode_hash (0x11223344):
	 * Unsigned: (0x00000080 << 16) ^ 0x11223344 = 0x11a23344
	 * Signed:   (0xffffff80 << 16) ^ 0xffff ^ 0x11223344 = 0xeea2ccbb
	 */
	exp_ea_ino_hash = 0x11a23344U;
	exp_ea_ino_sig_hash = 0xeea2ccbbU;

	retval = ext2fs_ext_attr_hash_entry2(fs, entry, val_buf, &h2);
	check_err("hash_entry2_ea_ino", retval, 0);
	check_u32("hash_entry2_val", h2, exp_ea_ino_hash);

	retval = ext2fs_ext_attr_hash_entry3(fs, entry, val_buf, &h3, &h3_sig);
	check_err("hash_entry3_ea_ino", retval, 0);
	check_u32("hash_entry3_unsig_val", h3, exp_ea_ino_hash);
	check_u32("hash_entry3_sig_val", h3_sig, exp_ea_ino_sig_hash);

	/* Invalid e_value_inum returns EXT2_ET_BAD_INODE_NUM */
	entry->e_value_inum = 9999;
	retval = ext2fs_ext_attr_hash_entry2(fs, entry, NULL, &h2);
	check_err("hash_entry2_bad_ino", retval, EXT2_ET_BAD_INODE_NUM);

	teardown_fs(&fs);

	/* 1f. ext2fs_ext_attr_block_rehash */
	memset(blk_buf, 0, sizeof(blk_buf));
	hdr = (struct ext2_ext_attr_header *) blk_buf;
	end = (struct ext2_ext_attr_entry *) (blk_buf + sizeof(blk_buf));

	/* Empty block -> h_hash = 0 */
	hdr->h_hash = 0xdeadbeefU;
	ext2fs_ext_attr_block_rehash(hdr, end);
	check_u32("block_rehash_empty", hdr->h_hash, 0);

	/* Two entries with non-zero e_hash */
	e1 = (struct ext2_ext_attr_entry *) (hdr + 1);
	e1->e_name_len = 3;
	e1->e_name_index = 1;
	e1->e_hash = 0x12345678U;
	e2 = EXT2_EXT_ATTR_NEXT(e1);
	e2->e_name_len = 4;
	e2->e_name_index = 1;
	e2->e_hash = 0x9abcdef0U;

	/*
	 * After e1: 0x12345678
	 * After e2: (0x56781234) ^ 0x9abcdef0 = 0xccc4ccc4
	 */
	exp_blk_hash = 0xccc4ccc4U;
	ext2fs_ext_attr_block_rehash(hdr, end);
	check_u32("block_rehash_two_entries", hdr->h_hash, exp_blk_hash);

	/* Respects 'end' pointer stopping after e1 */
	ext2fs_ext_attr_block_rehash(hdr, e2);
	check_u32("block_rehash_bounded_by_end", hdr->h_hash, 0x12345678U);

	/* Entry with e_hash == 0 resets block h_hash to 0 */
	e2->e_hash = 0;
	ext2fs_ext_attr_block_rehash(hdr, end);
	check_u32("block_rehash_zero_entry_hash", hdr->h_hash, 0);

	printf("test_entry_and_block_hashes: completed\n");
}

/*
 * 2. Raw EA Block Read/Write, Metadata Checksums, and Refcount Adjustment
 */
static void test_raw_block_io_and_refcount(void)
{
	ext2_filsys			fs = NULL;
	char				*buf = NULL;
	char				*read_buf = NULL;
	struct ext2_ext_attr_header	*hdr;
	__u32				newcount = 0;
	blk64_t				blk = 40;
	ext2_ino_t			ino = 12;
	errcode_t			retval;

	setup_fs(&fs, 128, 32, 256, EXT2_FEATURE_COMPAT_EXT_ATTR, 0,
		 EXT4_FEATURE_RO_COMPAT_METADATA_CSUM);

	retval = ext2fs_get_memzero(fs->blocksize, &buf);
	check_err("alloc_buf", retval, 0);
	retval = ext2fs_get_memzero(fs->blocksize, &read_buf);
	check_err("alloc_read_buf", retval, 0);

	hdr = (struct ext2_ext_attr_header *) buf;
	hdr->h_magic = EXT2_EXT_ATTR_MAGIC;
	hdr->h_refcount = 1;
	hdr->h_blocks = 1;
	hdr->h_hash = 0x11223344;

	fs->flags &= ~EXT2_FLAG_CHANGED;
	retval = ext2fs_write_ext_attr3(fs, blk, buf, ino);
	check_err("write_ext_attr3", retval, 0);
	if (!(fs->flags & EXT2_FLAG_CHANGED)) {
		printf("FAIL [write_ext_attr3_changed]: EXT2_FLAG_CHANGED not set\n");
		test_fail++;
	}

	/* Read back with valid checksum succeeds */
	retval = ext2fs_read_ext_attr3(fs, blk, read_buf, ino);
	check_err("read_ext_attr3_ok", retval, 0);

	/*
	 * Corrupting h_checksum on disk fails checksum verification.  Read
	 * the block raw before writing it back raw: ext2fs_read_ext_attr3()
	 * returned it in CPU byte order.
	 */
	retval = io_channel_read_blk64(fs->io, blk, 1, read_buf);
	check_err("read_ext_attr_raw", retval, 0);
	((struct ext2_ext_attr_header *) read_buf)->h_checksum ^= 1;
	retval = io_channel_write_blk64(fs->io, blk, 1, read_buf);
	check_err("write_corrupt_csum_raw", retval, 0);

	retval = ext2fs_read_ext_attr3(fs, blk, read_buf, ino);
	check_err("read_ext_attr3_bad_csum", retval,
		  EXT2_ET_EXT_ATTR_CSUM_INVALID);

	/* EXT2_FLAG_IGNORE_CSUM_ERRORS bypasses checksum mismatch */
	fs->flags |= EXT2_FLAG_IGNORE_CSUM_ERRORS;
	retval = ext2fs_read_ext_attr3(fs, blk, read_buf, ino);
	check_err("read_ext_attr3_ignore_csum", retval, 0);
	fs->flags &= ~EXT2_FLAG_IGNORE_CSUM_ERRORS;

	/* With h_refcount > 1, csum uses block number rather than ino */
	hdr->h_refcount = 2;
	retval = ext2fs_write_ext_attr2(fs, blk, buf);
	check_err("write_ext_attr2_shared", retval, 0);
	retval = ext2fs_read_ext_attr2(fs, blk, read_buf);
	check_err("read_ext_attr2_shared", retval, 0);
	retval = ext2fs_read_ext_attr(fs, (blk_t) blk, read_buf);
	check_err("read_ext_attr_shared", retval, 0);
	retval = ext2fs_read_ext_attr3(fs, blk, read_buf, 99);
	check_err("read_ext_attr3_shared_any_ino", retval, 0);

	/* v1 magic header is accepted by check_ext_attr_header */
	hdr->h_magic = EXT2_EXT_ATTR_MAGIC_v1;
	retval = ext2fs_write_ext_attr(fs, (blk_t) blk, buf);
	check_err("write_ext_attr_v1", retval, 0);
	retval = ext2fs_read_ext_attr(fs, (blk_t) blk, read_buf);
	check_err("read_ext_attr_v1", retval, 0);

	/* Bad h_magic returns EXT2_ET_BAD_EA_HEADER */
	hdr->h_magic = 0xdeadbeefU;
	retval = ext2fs_write_ext_attr3(fs, blk, buf, ino);
	check_err("write_bad_magic", retval, 0);
	retval = ext2fs_read_ext_attr3(fs, blk, read_buf, ino);
	check_err("read_bad_magic", retval, EXT2_ET_BAD_EA_HEADER);

	/* Bad h_blocks (0 and 2) returns EXT2_ET_BAD_EA_HEADER */
	hdr->h_magic = EXT2_EXT_ATTR_MAGIC;
	hdr->h_blocks = 0;
	retval = ext2fs_write_ext_attr3(fs, blk, buf, ino);
	check_err("write_0_blocks", retval, 0);
	retval = ext2fs_read_ext_attr3(fs, blk, read_buf, ino);
	check_err("read_0_blocks", retval, EXT2_ET_BAD_EA_HEADER);

	hdr->h_blocks = 2;
	retval = ext2fs_write_ext_attr3(fs, blk, buf, ino);
	check_err("write_2_blocks", retval, 0);
	retval = ext2fs_read_ext_attr3(fs, blk, read_buf, ino);
	check_err("read_2_blocks", retval, EXT2_ET_BAD_EA_HEADER);

	/* Restore valid v2 header with refcount = 1 */
	hdr->h_blocks = 1;
	hdr->h_refcount = 1;
	retval = ext2fs_write_ext_attr3(fs, blk, buf, ino);
	check_err("restore_valid_v2", retval, 0);

	/* I/O error injection on read and write */
	fail_read_blk = blk;
	retval = ext2fs_read_ext_attr3(fs, blk, read_buf, ino);
	check_err("read_ext_attr3_eio", retval, EIO);
	fail_read_blk = 0;

	fail_write_blk = blk;
	retval = ext2fs_write_ext_attr3(fs, blk, buf, ino);
	check_err("write_ext_attr3_eio", retval, EIO);
	fail_write_blk = 0;

	/* ext2fs_adjust_ea_refcount* boundary and normal tests */
	retval = ext2fs_adjust_ea_refcount3(fs, 0, NULL, +1, &newcount, ino);
	check_err("adjust_refcount_blk_0", retval, EXT2_ET_BAD_EA_BLOCK_NUM);

	retval = ext2fs_adjust_ea_refcount3(fs, 128, NULL, +1, &newcount, ino);
	check_err("adjust_refcount_blk_oob", retval, EXT2_ET_BAD_EA_BLOCK_NUM);

	/* Adjust 1 -> 3 with block_buf == NULL */
	retval = ext2fs_adjust_ea_refcount3(fs, blk, NULL, +2, &newcount, ino);
	check_err("adjust_refcount3_plus2", retval, 0);
	check_u32("adjust_refcount3_val3", newcount, 3);

	/* Adjust 3 -> 2 with caller-supplied block_buf and newcount == NULL */
	retval = ext2fs_adjust_ea_refcount2(fs, blk, read_buf, -1, NULL);
	check_err("adjust_refcount2_minus1", retval, 0);

	/* Adjust 2 -> 4 via legacy ext2fs_adjust_ea_refcount */
	retval = ext2fs_adjust_ea_refcount(fs, (blk_t) blk, NULL, +2,
					   &newcount);
	check_err("adjust_refcount_plus2", retval, 0);
	check_u32("adjust_refcount_val4", newcount, 4);

	/* Adjust 4 -> 1 with ino so refcount==1 checksum is computed with ino */
	retval = ext2fs_adjust_ea_refcount3(fs, blk, NULL, -3, &newcount, ino);
	check_err("adjust_refcount3_to_1", retval, 0);
	check_u32("adjust_refcount3_val1", newcount, 1);

	retval = ext2fs_read_ext_attr3(fs, blk, read_buf, ino);
	check_err("verify_after_adjust_refcount", retval, 0);
	check_u32("verify_hdr_refcount",
		  ((struct ext2_ext_attr_header *) read_buf)->h_refcount, 1);

	/* I/O errors during adjust_ea_refcount3 */
	fail_read_blk = blk;
	retval = ext2fs_adjust_ea_refcount3(fs, blk, NULL, +1, &newcount, ino);
	check_err("adjust_refcount_read_eio", retval, EIO);
	fail_read_blk = 0;

	fail_write_blk = blk;
	retval = ext2fs_adjust_ea_refcount3(fs, blk, NULL, +1, &newcount, ino);
	check_err("adjust_refcount_write_eio", retval, EIO);
	fail_write_blk = 0;

	ext2fs_free_mem(&buf);
	ext2fs_free_mem(&read_buf);
	teardown_fs(&fs);
	printf("test_raw_block_io_and_refcount: completed\n");
}

/*
 * 3. ext2fs_free_ext_attr() behavior on unshared, shared, and invalid blocks
 */
static void test_free_ext_attr(void)
{
	ext2_filsys			fs = NULL;
	struct ext2_inode_large		inode;
	char				*buf = NULL;
	struct ext2_ext_attr_header	*hdr;
	blk64_t				blk = 45;
	ext2_ino_t			ino = 12;
	errcode_t			retval;

	setup_fs(&fs, 128, 32, 256, EXT2_FEATURE_COMPAT_EXT_ATTR, 0, 0);
	init_reg_inode(fs, ino);

	retval = ext2fs_get_memzero(fs->blocksize, &buf);
	check_err("alloc_buf", retval, 0);

	/* 3a. Inode with no EA block (file_acl == 0) is a no-op */
	retval = ext2fs_free_ext_attr(fs, ino, NULL);
	check_err("free_ext_attr_no_blk_null", retval, 0);

	retval = ext2fs_read_inode_full(fs, ino, EXT2_INODE(&inode),
					sizeof(inode));
	check_err("read_ino_12", retval, 0);
	retval = ext2fs_free_ext_attr(fs, ino, &inode);
	check_err("free_ext_attr_no_blk_ptr", retval, 0);

	/* 3b. Out-of-bounds EA block number returns EXT2_ET_BAD_EA_BLOCK_NUM */
	ext2fs_file_acl_block_set(fs, EXT2_INODE(&inode), 999);
	retval = ext2fs_free_ext_attr(fs, ino, &inode);
	check_err("free_ext_attr_oob_blk", retval, EXT2_ET_BAD_EA_BLOCK_NUM);

	/* 3c. v1 EA block is rejected by ext2fs_free_ext_attr (only v2 supported) */
	hdr = (struct ext2_ext_attr_header *) buf;
	hdr->h_magic = EXT2_EXT_ATTR_MAGIC_v1;
	hdr->h_refcount = 1;
	hdr->h_blocks = 1;
	retval = ext2fs_write_ext_attr3(fs, blk, buf, ino);
	check_err("write_v1_blk", retval, 0);

	ext2fs_file_acl_block_set(fs, EXT2_INODE(&inode), blk);
	retval = ext2fs_free_ext_attr(fs, ino, &inode);
	check_err("free_ext_attr_v1_rejected", retval, EXT2_ET_BAD_EA_HEADER);

	/* 3d. Shared v2 EA block (h_refcount == 2) with inode == NULL */
	hdr->h_magic = EXT2_EXT_ATTR_MAGIC;
	hdr->h_refcount = 2;
	hdr->h_blocks = 1;
	retval = ext2fs_write_ext_attr3(fs, blk, buf, ino);
	check_err("write_shared_v2_blk", retval, 0);
	ext2fs_block_alloc_stats2(fs, blk, +1);

	ext2fs_file_acl_block_set(fs, EXT2_INODE(&inode), blk);
	inode.i_blocks = 2; /* 1x 1024-byte block = 2x 512-byte sectors */
	retval = ext2fs_write_inode_full(fs, ino, EXT2_INODE(&inode),
					 sizeof(inode));
	check_err("write_ino_with_shared_acl", retval, 0);

	retval = ext2fs_free_ext_attr(fs, ino, NULL);
	check_err("free_ext_attr_shared", retval, 0);

	/* Verify inode's file_acl and i_blocks were cleared on disk */
	retval = ext2fs_read_inode_full(fs, ino, EXT2_INODE(&inode),
					sizeof(inode));
	check_err("read_ino_after_shared_free", retval, 0);
	check_u64("shared_free_file_acl",
		  ext2fs_file_acl_block(fs, EXT2_INODE(&inode)), 0);
	check_u32("shared_free_i_blocks", inode.i_blocks, 0);

	/* Block must remain allocated in block_map with h_refcount == 1 */
	if (!ext2fs_test_block_bitmap2(fs->block_map, blk)) {
		printf("FAIL [shared_free_bitmap]: block %llu was freed while refcount=1\n",
		       (unsigned long long) blk);
		test_fail++;
	}
	retval = ext2fs_read_ext_attr3(fs, blk, buf, ino);
	check_err("read_blk_after_shared_free", retval, 0);
	check_u32("shared_free_new_refcount", hdr->h_refcount, 1);

	/* 3e. Unshared v2 EA block (h_refcount == 1) with explicit inode ptr */
	ext2fs_file_acl_block_set(fs, EXT2_INODE(&inode), blk);
	inode.i_blocks = 2;
	retval = ext2fs_free_ext_attr(fs, ino, &inode);
	check_err("free_ext_attr_unshared", retval, 0);
	check_u64("unshared_free_file_acl",
		  ext2fs_file_acl_block(fs, EXT2_INODE(&inode)), 0);
	check_u32("unshared_free_i_blocks", inode.i_blocks, 0);

	if (ext2fs_test_block_bitmap2(fs->block_map, blk)) {
		printf("FAIL [unshared_free_bitmap]: block %llu still marked in-use\n",
		       (unsigned long long) blk);
		test_fail++;
	}

	ext2fs_free_mem(&buf);
	teardown_fs(&fs);
	printf("test_free_ext_attr: completed\n");
}

/*
 * 4. Handle Lifecycle, Magic Validation, Flags, and Corrupt Buffer Detection
 */
static int dummy_iter_cb(char *name EXT2FS_ATTR((unused)),
			 char *value EXT2FS_ATTR((unused)),
			 size_t value_len EXT2FS_ATTR((unused)),
			 void *data EXT2FS_ATTR((unused)))
{
	return 0;
}

static void test_handle_lifecycle_and_errors(void)
{
	ext2_filsys			fs = NULL;
	struct ext2_xattr_handle	*h = NULL;
	errcode_t			bad_magic = 0;
	struct ext2_xattr_handle	*bad_h =
		(struct ext2_xattr_handle *) &bad_magic;
	struct full_inode		fino;
	struct ext2_inode_large		*inode = &fino.inode;
	struct ext2_ext_attr_entry	*entry;
	struct ext2_ext_attr_header	*hdr;
	char				*blk_buf = NULL;
	void				*val = NULL;
	size_t				val_len = 0, count = 0;
	unsigned int			new_flags, old_flags;
	ext2_ino_t			ino = 12;
	blk64_t				blk = 46;
	errcode_t			retval;

	/* 4a. Filesystem missing both EXT_ATTR and INLINE_DATA features */
	setup_fs(&fs, 128, 32, 256, 0, 0, 0);
	retval = ext2fs_xattrs_open(fs, ino, &h);
	check_err("open_missing_ea_feature", retval,
		  EXT2_ET_MISSING_EA_FEATURE);
	teardown_fs(&fs);

	/* 4b. Filesystem with only INLINE_DATA feature allows xattrs_open */
	setup_fs(&fs, 128, 32, 256, 0, EXT4_FEATURE_INCOMPAT_INLINE_DATA, 0);
	retval = ext2fs_xattrs_open(fs, ino, &h);
	check_err("open_with_inline_data_only", retval, 0);
	retval = ext2fs_xattrs_close(&h);
	check_err("close_inline_data_handle", retval, 0);
	teardown_fs(&fs);

	/* 4c. Magic validation across all handle functions */
	setup_fs(&fs, 128, 32, 256, EXT2_FEATURE_COMPAT_EXT_ATTR, 0, 0);
	init_reg_inode(fs, ino);

	retval = ext2fs_xattrs_open(fs, ino, &h);
	check_err("open_valid_handle", retval, 0);

	/* Temporarily corrupt h->magic (first field of handle) */
	bad_magic = *((errcode_t *) h);
	*((errcode_t *) h) = 0;
	bad_h = h;
	memset(&fino, 0, sizeof(fino));

	check_err("magic_write", ext2fs_xattrs_write(bad_h),
		  EXT2_ET_MAGIC_EA_HANDLE);
	check_err("magic_read", ext2fs_xattrs_read(bad_h),
		  EXT2_ET_MAGIC_EA_HANDLE);
	check_err("magic_read_inode", ext2fs_xattrs_read_inode(bad_h, inode),
		  EXT2_ET_MAGIC_EA_HANDLE);
	check_err("magic_iterate",
		  ext2fs_xattrs_iterate(bad_h, dummy_iter_cb, NULL),
		  EXT2_ET_MAGIC_EA_HANDLE);
	check_err("magic_get",
		  ext2fs_xattr_get(bad_h, "user.a", &val, &val_len),
		  EXT2_ET_MAGIC_EA_HANDLE);
	check_err("magic_set",
		  ext2fs_xattr_set(bad_h, "user.a", "v", 1),
		  EXT2_ET_MAGIC_EA_HANDLE);
	check_err("magic_remove", ext2fs_xattr_remove(bad_h, "user.a"),
		  EXT2_ET_MAGIC_EA_HANDLE);
	check_err("magic_remove_all", ext2fs_xattr_remove_all(bad_h),
		  EXT2_ET_MAGIC_EA_HANDLE);
	check_err("magic_close", ext2fs_xattrs_close(&bad_h),
		  EXT2_ET_MAGIC_EA_HANDLE);
	check_err("magic_count", ext2fs_xattrs_count(bad_h, &count),
		  EXT2_ET_MAGIC_EA_HANDLE);
	check_err("magic_flags",
		  ext2fs_xattrs_flags(bad_h, &new_flags, &old_flags),
		  EXT2_ET_MAGIC_EA_HANDLE);

	/* Restore h->magic */
	*((errcode_t *) h) = bad_magic;

	/* 4d. Flags and corrupt on-disk structures */

	new_flags = XATTR_HANDLE_FLAG_RAW;
	old_flags = 0xdeadU;
	retval = ext2fs_xattrs_flags(h, &new_flags, &old_flags);
	check_err("xattrs_flags_set_raw", retval, 0);
	check_u32("xattrs_flags_old_0", old_flags, 0);

	new_flags = 0;
	retval = ext2fs_xattrs_flags(h, &new_flags, &old_flags);
	check_err("xattrs_flags_clear_raw", retval, 0);
	check_u32("xattrs_flags_old_raw", old_flags, XATTR_HANDLE_FLAG_RAW);

	/* Unaligned i_extra_isize (e.g. 30 & 3 != 0) -> EXT2_ET_INODE_CORRUPTED */
	retval = ext2fs_read_inode_full(fs, ino, EXT2_INODE(inode),
					EXT2_INODE_SIZE(fs->super));
	check_err("read_ino_for_corrupt_extra", retval, 0);
	inode->i_extra_isize = 30;
	retval = ext2fs_write_inode_full(fs, ino, EXT2_INODE(inode),
					 EXT2_INODE_SIZE(fs->super));
	check_err("write_ino_corrupt_extra", retval, 0);

	retval = ext2fs_xattrs_read(h);
	check_err("read_unaligned_extra_isize", retval,
		  EXT2_ET_INODE_CORRUPTED);
	retval = ext2fs_xattrs_write(h);
	check_err("write_unaligned_extra_isize", retval,
		  EXT2_ET_INODE_CORRUPTED);

	/* Craft corrupt entries in in-memory inode for ext2fs_xattrs_read_inode */
	memset(&fino, 0, sizeof(fino));
	inode->i_mode = LINUX_S_IFREG | 0644;
	inode->i_extra_isize = 32;
	*((__u32 *) ((char *) inode + 128 + 32)) = EXT2_EXT_ATTR_MAGIC;
	entry = (struct ext2_ext_attr_entry *) ((char *) inode + 128 + 32 + 4);

	/* Case 1: e_name_len exceeds remaining storage -> EXT2_ET_EA_BAD_NAME_LEN */
	memset(entry, 0, 92);
	entry->e_name_len = 200;
	entry->e_name_index = 1;
	retval = ext2fs_xattrs_read_inode(h, inode);
	check_err("read_bad_name_len", retval, EXT2_ET_EA_BAD_NAME_LEN);

	/* Case 2: e_value_inum != 0 when EA_INODE feature is disabled */
	memset(entry, 0, 92);
	entry->e_name_len = 1;
	entry->e_name_index = 1;
	entry->e_value_inum = 15;
	*((char *) entry + sizeof(*entry)) = 'a';
	retval = ext2fs_xattrs_read_inode(h, inode);
	check_err("read_ea_inum_without_feature", retval,
		  EXT2_ET_BAD_EA_BLOCK_NUM);

	/* Case 3: e_value_size > remain -> EXT2_ET_EA_BAD_VALUE_SIZE */
	memset(entry, 0, 92);
	entry->e_name_len = 1;
	entry->e_name_index = 1;
	entry->e_value_size = 500;
	*((char *) entry + sizeof(*entry)) = 'a';
	retval = ext2fs_xattrs_read_inode(h, inode);
	check_err("read_bad_value_size", retval, EXT2_ET_EA_BAD_VALUE_SIZE);

	/* Case 4: e_value_offs + e_value_size > values_size -> EXT2_ET_EA_BAD_VALUE_OFFSET */
	memset(entry, 0, 92);
	entry->e_name_len = 1;
	entry->e_name_index = 1;
	entry->e_value_size = 4;
	entry->e_value_offs = 90; /* 90 + 4 > 92 */
	*((char *) entry + sizeof(*entry)) = 'a';
	retval = ext2fs_xattrs_read_inode(h, inode);
	check_err("read_value_offs_past_end", retval,
		  EXT2_ET_EA_BAD_VALUE_OFFSET);

	/* Case 5: e_value_offs overlaps entry list & terminator -> EXT2_ET_EA_BAD_VALUE_OFFSET */
	memset(entry, 0, 92);
	entry->e_name_len = 1;
	entry->e_name_index = 1;
	entry->e_value_size = 4;
	entry->e_value_offs = 20; /* end + 4 is at offset 24 */
	*((char *) entry + sizeof(*entry)) = 'a';
	retval = ext2fs_xattrs_read_inode(h, inode);
	check_err("read_value_offs_overlaps_entries", retval,
		  EXT2_ET_EA_BAD_VALUE_OFFSET);

	/* Case 6: Out-of-bounds file_acl block on inode -> EXT2_ET_BAD_EA_BLOCK_NUM */
	memset(&fino, 0, sizeof(fino));
	inode->i_mode = LINUX_S_IFREG | 0644;
	inode->i_extra_isize = 32;
	ext2fs_file_acl_block_set(fs, EXT2_INODE(inode), 999);
	retval = ext2fs_xattrs_read_inode(h, inode);
	check_err("read_inode_bad_acl_blk", retval, EXT2_ET_BAD_EA_BLOCK_NUM);

	/* Case 7: External EA block with v1 magic -> EXT2_ET_BAD_EA_HEADER */
	retval = ext2fs_get_memzero(fs->blocksize, &blk_buf);
	check_err("alloc_blk_buf", retval, 0);
	hdr = (struct ext2_ext_attr_header *) blk_buf;
	hdr->h_magic = EXT2_EXT_ATTR_MAGIC_v1;
	hdr->h_refcount = 1;
	hdr->h_blocks = 1;
	retval = ext2fs_write_ext_attr3(fs, blk, blk_buf, ino);
	check_err("write_v1_ea_blk", retval, 0);

	ext2fs_file_acl_block_set(fs, EXT2_INODE(inode), blk);
	retval = ext2fs_xattrs_read_inode(h, inode);
	check_err("read_inode_v1_ea_blk", retval, EXT2_ET_BAD_EA_HEADER);

	retval = ext2fs_xattrs_close(&h);
	check_err("close_handle", retval, 0);
	ext2fs_free_mem(&blk_buf);
	teardown_fs(&fs);
	printf("test_handle_lifecycle_and_errors: completed\n");
}

/*
 * 5. Inode Body & External Block Xattrs: Prefixes, Sorting, Movement,
 *    CoW, Iteration, and Removal
 */
struct iter_record {
	char	names[16][64];
	char	vals[16][64];
	size_t	lens[16];
	int	count;
	int	abort_at;
	int	modify_at;
};

static int record_xattr_cb(char *name, char *value, size_t value_len,
			   void *data)
{
	struct iter_record *rec = (struct iter_record *) data;
	int idx = rec->count;
	int ret = 0;

	if (idx < 16) {
		strncpy(rec->names[idx], name, sizeof(rec->names[idx]) - 1);
		rec->lens[idx] = value_len;
		if (value_len < sizeof(rec->vals[idx]))
			memcpy(rec->vals[idx], value, value_len);
	}
	rec->count++;

	if (rec->modify_at == idx && value_len >= 1) {
		value[0] = 'Z';
		ret |= XATTR_CHANGED;
	}
	if (rec->abort_at == idx)
		ret |= XATTR_ABORT;
	return ret;
}

static void verify_xattr_val(struct ext2_xattr_handle *h, const char *key,
			     const void *exp_val, size_t exp_len)
{
	void		*val = NULL;
	size_t		len = 0;
	errcode_t	retval;

	retval = ext2fs_xattr_get(h, key, &val, &len);
	if (retval != 0) {
		printf("FAIL [get %s]: returned %ld (%s)\n", key,
		       (long) retval, error_message(retval));
		test_fail++;
		return;
	}
	if (len != exp_len || (exp_len > 0 && memcmp(val, exp_val, exp_len))) {
		printf("FAIL [get %s]: value/len mismatch (got len=%zu, expected %zu)\n",
		       key, len, exp_len);
		test_fail++;
	}
	ext2fs_free_mem(&val);
}

static void test_ibody_and_block_xattrs(void)
{
	ext2_filsys			fs = NULL;
	struct ext2_xattr_handle	*h = NULL;
	struct full_inode		fino;
	struct ext2_inode_large		*inode = &fino.inode;
	struct iter_record		rec;
	void				*val = NULL;
	size_t				val_len = 0, count = 0;
	blk64_t				acl_blk, new_acl_blk;
	__u32				refcount = 0;
	ext2_ino_t			ino = 12;
	char				big_val[120];
	char				huge_val[950];
	errcode_t			retval;

	setup_fs(&fs, 128, 32, 256, EXT2_FEATURE_COMPAT_EXT_ATTR, 0,
		 EXT4_FEATURE_RO_COMPAT_METADATA_CSUM);

	/*
	 * Start with i_extra_isize == 0 on a 256-byte inode to verify
	 * ext2fs_xattrs_write initializes i_extra_isize automatically.
	 */
	memset(&fino, 0, sizeof(fino));
	inode->i_mode = LINUX_S_IFREG | 0644;
	inode->i_links_count = 1;
	retval = ext2fs_write_inode_full(fs, ino, EXT2_INODE(inode),
					 EXT2_INODE_SIZE(fs->super));
	check_err("write_initial_ino", retval, 0);
	ext2fs_inode_alloc_stats2(fs, ino, +1, 0);

	retval = ext2fs_xattrs_open(fs, ino, &h);
	check_err("xattrs_open", retval, 0);
	retval = ext2fs_xattrs_read(h);
	check_err("xattrs_read_empty", retval, 0);
	retval = ext2fs_xattrs_count(h, &count);
	check_err("xattrs_count_empty", retval, 0);
	check_u32("count_is_0", (unsigned int) count, 0);

	/* Looking up a missing key returns EXT2_ET_EA_KEY_NOT_FOUND */
	retval = ext2fs_xattr_get(h, "user.missing", &val, &val_len);
	check_err("get_missing_key", retval, EXT2_ET_EA_KEY_NOT_FOUND);

	/*
	 * Set attributes across multiple namespaces and lengths:
	 * With 256-byte inode and s_want_extra_isize=32,
	 * ibody has 256 - 128 - 32 - 8 = 88 bytes of usable space.
	 * Entry 1: "user.k1" (short="k1", len 2 -> entry 20B) + val "v1" (4B) = 24B (ibody: 64B left)
	 * Entry 2: "trusted.t1" (short="t1", len 2 -> 20B) + val "tv1" (4B) = 24B (ibody: 40B left)
	 * Entry 3: "security.s1" (short="s1", len 2 -> 20B) + val "sv1" (4B) = 24B (ibody: 16B left)
	 * Now ibody has only 16B left (< 20B minimum entry size). Subsequent entries
	 * spill to the external EA block and trigger ext2fs_xattrs_expand!
	 */
	retval = ext2fs_xattr_set(h, "user.k1", "v1", 2);
	check_err("set_user_k1", retval, 0);
	retval = ext2fs_xattr_set(h, "trusted.t1", "tv1", 3);
	check_err("set_trusted_t1", retval, 0);
	retval = ext2fs_xattr_set(h, "security.s1", "sv1", 3);
	check_err("set_security_s1", retval, 0);

	/* Verify all 3 fit in ibody and no external EA block was allocated yet */
	retval = ext2fs_read_inode_full(fs, ino, EXT2_INODE(inode),
					EXT2_INODE_SIZE(fs->super));
	check_err("read_ino_ibody_only", retval, 0);
	check_u64("no_acl_blk_yet",
		  ext2fs_file_acl_block(fs, EXT2_INODE(inode)), 0);

	/*
	 * Add 6 entries that spill to the external EA block in unsorted order
	 * to test xattr_find_position sorting:
	 * - "gnu.g1"     (name_idx=10, short="g1")
	 * - "system.sys" (name_idx=7,  short="sys")
	 * - "user.bb"    (name_idx=1,  short="bb", len=2)
	 * - "user.ba"    (name_idx=1,  short="ba", len=2)
	 * - "user.z"     (name_idx=1,  short="z",  len=1)
	 * - "custom.raw" (name_idx=0,  short="custom.raw", len=10)
	 * Expected sorted order in EA block:
	 *   0: "custom.raw" (idx 0)
	 *   1: "user.z"     (idx 1, len 1)
	 *   2: "user.ba"    (idx 1, len 2, "ba" < "bb")
	 *   3: "user.bb"    (idx 1, len 2)
	 *   4: "system.sys" (idx 7)
	 *   5: "gnu.g1"     (idx 10)
	 */
	retval = ext2fs_xattr_set(h, "gnu.g1", "gv1", 3);
	check_err("set_gnu_g1", retval, 0);
	retval = ext2fs_xattr_set(h, "system.sys", "sysval", 6);
	check_err("set_system_sys", retval, 0);
	retval = ext2fs_xattr_set(h, "user.bb", "val_bb", 6);
	check_err("set_user_bb", retval, 0);
	retval = ext2fs_xattr_set(h, "user.ba", "val_ba", 6);
	check_err("set_user_ba", retval, 0);
	retval = ext2fs_xattr_set(h, "user.z", "val_z", 5);
	check_err("set_user_z", retval, 0);
	retval = ext2fs_xattr_set(h, "custom.raw", "rawval", 6);
	check_err("set_custom_raw", retval, 0);

	retval = ext2fs_xattrs_count(h, &count);
	check_err("xattrs_count_9", retval, 0);
	check_u32("count_is_9", (unsigned int) count, 9);

	/* Close and reopen to verify on-disk persistence and sorting */
	retval = ext2fs_xattrs_close(&h);
	check_err("close_after_9", retval, 0);

	retval = ext2fs_read_inode_full(fs, ino, EXT2_INODE(inode),
					EXT2_INODE_SIZE(fs->super));
	check_err("read_ino_with_acl", retval, 0);
	acl_blk = ext2fs_file_acl_block(fs, EXT2_INODE(inode));
	if (acl_blk == 0) {
		printf("FAIL [acl_blk_allocated]: expected non-zero file_acl\n");
		test_fail++;
	}
	check_u32("i_blocks_with_acl", inode->i_blocks, 2);

	retval = ext2fs_xattrs_open(fs, ino, &h);
	check_err("reopen_9", retval, 0);
	retval = ext2fs_xattrs_read(h);
	check_err("reread_9", retval, 0);

	memset(&rec, 0, sizeof(rec));
	rec.abort_at = -1;
	rec.modify_at = -1;
	retval = ext2fs_xattrs_iterate(h, record_xattr_cb, &rec);
	check_err("iterate_9", retval, 0);
	check_u32("iter_count_9", rec.count, 9);

	/* First 3 are ibody in insertion order, next 6 are EA block in sorted order */
	if (strcmp(rec.names[0], "user.k1") != 0 ||
	    strcmp(rec.names[1], "trusted.t1") != 0 ||
	    strcmp(rec.names[2], "security.s1") != 0 ||
	    strcmp(rec.names[3], "custom.raw") != 0 ||
	    strcmp(rec.names[4], "user.z") != 0 ||
	    strcmp(rec.names[5], "user.ba") != 0 ||
	    strcmp(rec.names[6], "user.bb") != 0 ||
	    strcmp(rec.names[7], "system.sys") != 0 ||
	    strcmp(rec.names[8], "gnu.g1") != 0) {
		printf("FAIL [xattr_ordering]: got [%s, %s, %s | %s, %s, %s, %s, %s, %s]\n",
		       rec.names[0], rec.names[1], rec.names[2],
		       rec.names[3], rec.names[4], rec.names[5],
		       rec.names[6], rec.names[7], rec.names[8]);
		test_fail++;
	}

	verify_xattr_val(h, "user.k1", "v1", 2);
	verify_xattr_val(h, "trusted.t1", "tv1", 3);
	verify_xattr_val(h, "security.s1", "sv1", 3);
	verify_xattr_val(h, "gnu.g1", "gv1", 3);
	verify_xattr_val(h, "custom.raw", "rawval", 6);
	verify_xattr_val(h, "user.z", "val_z", 5);
	verify_xattr_val(h, "user.ba", "val_ba", 6);
	verify_xattr_val(h, "user.bb", "val_bb", 6);
	verify_xattr_val(h, "system.sys", "sysval", 6);

	/* Setting identical value is a fast no-op */
	retval = ext2fs_xattr_set(h, "user.k1", "v1", 2);
	check_err("set_same_value_noop", retval, 0);

	/*
	 * Update "user.k1" (currently in ibody at index 0) with a 60-byte value
	 * so it no longer fits in ibody (which has 16B free + 24B old = 40B)
	 * and must move from ibody into its sorted position in the EA block!
	 */
	memset(big_val, 'X', sizeof(big_val));
	retval = ext2fs_xattr_set(h, "user.k1", big_val, 60);
	check_err("update_ibody_to_block", retval, 0);
	verify_xattr_val(h, "user.k1", big_val, 60);

	/*
	 * Now ibody has 40B free. Update "user.z" (currently in EA block) with
	 * a 4-byte value "znew" (needs 20B + 4B = 24B <= 40B), which moves it
	 * from the EA block into the end of ibody!
	 */
	retval = ext2fs_xattr_set(h, "user.z", "znew", 4);
	check_err("update_block_to_ibody", retval, 0);
	verify_xattr_val(h, "user.z", "znew", 4);

	/*
	 * Test Copy-on-Write (CoW) when EA block is shared (h_refcount == 2):
	 * Bump acl_blk refcount to 2, then modify an EA block entry.
	 */
	retval = ext2fs_adjust_ea_refcount3(fs, acl_blk, NULL, +1, &refcount,
					    ino);
	check_err("bump_acl_refcount_to_2", retval, 0);
	check_u32("acl_refcount_is_2", refcount, 2);

	retval = ext2fs_xattr_set(h, "user.ba", "val_ba_cow", 10);
	check_err("set_triggers_cow", retval, 0);

	retval = ext2fs_read_inode_full(fs, ino, EXT2_INODE(inode),
					EXT2_INODE_SIZE(fs->super));
	check_err("read_ino_after_cow", retval, 0);
	new_acl_blk = ext2fs_file_acl_block(fs, EXT2_INODE(inode));
	if (new_acl_blk == 0 || new_acl_blk == acl_blk) {
		printf("FAIL [cow_new_blk]: expected new block != %llu, got %llu\n",
		       (unsigned long long) acl_blk,
		       (unsigned long long) new_acl_blk);
		test_fail++;
	}
	check_u32("i_blocks_unchanged_after_cow", inode->i_blocks, 2);

	/* Old block's refcount was decremented back to 1 */
	retval = ext2fs_adjust_ea_refcount3(fs, acl_blk, NULL, 0, &refcount,
					    ino);
	check_err("check_old_blk_refcount", retval, 0);
	check_u32("old_blk_refcount_is_1", refcount, 1);

	/* Setting an attribute too large for the EA block returns EXT2_ET_EA_NO_SPACE */
	memset(huge_val, 'H', sizeof(huge_val));
	retval = ext2fs_xattr_set(h, "user.too_big", huge_val,
				  sizeof(huge_val));
	check_err("set_too_big_no_space", retval, EXT2_ET_EA_NO_SPACE);

	/* Test ext2fs_xattrs_iterate with XATTR_CHANGED and XATTR_ABORT */
	memset(&rec, 0, sizeof(rec));
	rec.modify_at = 1;
	rec.abort_at = 2;
	retval = ext2fs_xattrs_iterate(h, record_xattr_cb, &rec);
	check_err("iterate_modify_and_abort", retval, 0);
	check_u32("iterate_aborted_at_3", rec.count, 3);
	/* Entry at index 1 ("security.s1") had its first byte changed to 'Z' */
	verify_xattr_val(h, "security.s1", "Zv1", 3);

	/* Remove a non-existent key -> returns 0 */
	retval = ext2fs_xattr_remove(h, "user.nonexistent");
	check_err("remove_nonexistent", retval, 0);

	/*
	 * Remove all EA-block-resident keys one by one ("custom.raw", "user.ba",
	 * "user.bb", "user.k1", "system.sys", "gnu.g1") and verify only the
	 * 3 ibody entries remain when re-read from disk.
	 */
	retval = ext2fs_xattr_remove(h, "custom.raw");
	check_err("remove_custom_raw", retval, 0);
	retval = ext2fs_xattr_remove(h, "user.ba");
	check_err("remove_user_ba", retval, 0);
	retval = ext2fs_xattr_remove(h, "user.bb");
	check_err("remove_user_bb", retval, 0);
	retval = ext2fs_xattr_remove(h, "user.k1");
	check_err("remove_user_k1", retval, 0);
	retval = ext2fs_xattr_remove(h, "system.sys");
	check_err("remove_system_sys", retval, 0);
	retval = ext2fs_xattr_remove(h, "gnu.g1");
	check_err("remove_gnu_g1", retval, 0);

	retval = ext2fs_xattrs_close(&h);
	check_err("close_after_remove_block_keys", retval, 0);
	retval = ext2fs_xattrs_open(fs, ino, &h);
	check_err("reopen_after_remove_block_keys", retval, 0);
	retval = ext2fs_xattrs_read(h);
	check_err("reread_after_remove_block_keys", retval, 0);
	retval = ext2fs_xattrs_count(h, &count);
	check_err("count_after_remove_block_keys", retval, 0);
	check_u32("count_3_ibody_only", (unsigned int) count, 3);

	/* Remove remaining ibody attributes with ext2fs_xattr_remove_all */
	retval = ext2fs_xattr_remove_all(h);
	check_err("xattr_remove_all", retval, 0);
	retval = ext2fs_xattrs_count(h, &count);
	check_err("count_after_remove_all", retval, 0);
	check_u32("count_0_after_remove_all", (unsigned int) count, 0);

	retval = ext2fs_xattrs_close(&h);
	check_err("close_after_remove_all", retval, 0);
	teardown_fs(&fs);

	/*
	 * Test 128-byte inode (EXT2_GOOD_OLD_INODE_SIZE): ibody has 0 space,
	 * so setting an attribute immediately allocates an external EA block.
	 */
	setup_fs(&fs, 128, 32, 128, EXT2_FEATURE_COMPAT_EXT_ATTR, 0, 0);
	init_reg_inode(fs, ino);
	retval = ext2fs_xattrs_open(fs, ino, &h);
	check_err("open_128b_ino", retval, 0);
	retval = ext2fs_xattrs_read(h);
	check_err("read_128b_ino", retval, 0);
	retval = ext2fs_xattr_set(h, "user.small", "val", 3);
	check_err("set_128b_ino", retval, 0);
	retval = ext2fs_xattrs_close(&h);
	check_err("close_128b_ino", retval, 0);

	retval = ext2fs_read_inode(fs, ino, EXT2_INODE(inode));
	check_err("read_raw_128b_ino", retval, 0);
	if (ext2fs_file_acl_block(fs, EXT2_INODE(inode)) == 0) {
		printf("FAIL [128b_ino_acl]: expected external EA block\n");
		test_fail++;
	}

	/* Reopen and read back from 128-byte inode */
	retval = ext2fs_xattrs_open(fs, ino, &h);
	check_err("reopen_128b_ino", retval, 0);
	retval = ext2fs_xattrs_read(h);
	check_err("reread_128b_ino", retval, 0);
	verify_xattr_val(h, "user.small", "val", 3);
	retval = ext2fs_xattrs_close(&h);
	check_err("reclose_128b_ino", retval, 0);

	teardown_fs(&fs);
	printf("test_ibody_and_block_xattrs: completed\n");
}

/*
 * 6. POSIX ACL Conversion (system.posix_acl_access & system.posix_acl_default)
 */
static void test_posix_acl_conversion(void)
{
	ext2_filsys			fs = NULL;
	struct ext2_xattr_handle	*h = NULL;
	unsigned char			posix_buf[sizeof(posix_acl_xattr_header) +
						  6 * sizeof(posix_acl_xattr_entry)];
	posix_acl_xattr_header		*p_hdr;
	posix_acl_xattr_entry		*p_ent;
	ext4_acl_header			bad_disk_acl;
	unsigned char			bad_disk_tag[sizeof(ext4_acl_header) +
						     sizeof(ext4_acl_entry_short)];
	void				*out_buf = NULL;
	size_t				out_len = 0;
	unsigned int			flags;
	ext2_ino_t			ino = 12;
	errcode_t			retval;

	setup_fs(&fs, 128, 32, 256, EXT2_FEATURE_COMPAT_EXT_ATTR, 0, 0);
	init_reg_inode(fs, ino);

	retval = ext2fs_xattrs_open(fs, ino, &h);
	check_err("open_acl_handle", retval, 0);
	retval = ext2fs_xattrs_read(h);
	check_err("read_acl_handle", retval, 0);

	/* Build a 6-entry POSIX ACL covering all valid ACL_* tags */
	memset(posix_buf, 0, sizeof(posix_buf));
	p_hdr = (posix_acl_xattr_header *) posix_buf;
	p_hdr->a_version = ext2fs_cpu_to_le32(POSIX_ACL_XATTR_VERSION);
	p_ent = (posix_acl_xattr_entry *) (p_hdr + 1);

	p_ent[0].e_tag = ext2fs_cpu_to_le16(ACL_USER_OBJ);
	p_ent[0].e_perm = ext2fs_cpu_to_le16(7);
	p_ent[0].e_id = ext2fs_cpu_to_le32(ACL_UNDEFINED_ID);

	p_ent[1].e_tag = ext2fs_cpu_to_le16(ACL_USER);
	p_ent[1].e_perm = ext2fs_cpu_to_le16(5);
	p_ent[1].e_id = ext2fs_cpu_to_le32(1001);

	p_ent[2].e_tag = ext2fs_cpu_to_le16(ACL_GROUP_OBJ);
	p_ent[2].e_perm = ext2fs_cpu_to_le16(4);
	p_ent[2].e_id = ext2fs_cpu_to_le32(ACL_UNDEFINED_ID);

	p_ent[3].e_tag = ext2fs_cpu_to_le16(ACL_GROUP);
	p_ent[3].e_perm = ext2fs_cpu_to_le16(6);
	p_ent[3].e_id = ext2fs_cpu_to_le32(2002);

	p_ent[4].e_tag = ext2fs_cpu_to_le16(ACL_MASK);
	p_ent[4].e_perm = ext2fs_cpu_to_le16(7);
	p_ent[4].e_id = ext2fs_cpu_to_le32(ACL_UNDEFINED_ID);

	p_ent[5].e_tag = ext2fs_cpu_to_le16(ACL_OTHER);
	p_ent[5].e_perm = ext2fs_cpu_to_le16(0);
	p_ent[5].e_id = ext2fs_cpu_to_le32(ACL_UNDEFINED_ID);

	retval = ext2fs_xattr_set(h, "system.posix_acl_access",
				  posix_buf, sizeof(posix_buf));
	check_err("set_posix_acl_access", retval, 0);

	retval = ext2fs_xattr_set(h, "system.posix_acl_default",
				  posix_buf, sizeof(posix_buf));
	check_err("set_posix_acl_default", retval, 0);

	/* Verify compact on-disk representation via XATTR_HANDLE_FLAG_RAW (36 bytes) */
	flags = XATTR_HANDLE_FLAG_RAW;
	retval = ext2fs_xattrs_flags(h, &flags, NULL);
	check_err("set_raw_flag", retval, 0);

	retval = ext2fs_xattr_get(h, "system.posix_acl_access",
				  &out_buf, &out_len);
	check_err("get_raw_acl_access", retval, 0);
	/* 4B header + 4 * 4B short entries + 2 * 8B user/group entries = 36B */
	check_u32("raw_acl_disk_len", (unsigned int) out_len, 36);
	check_u32("raw_acl_disk_version",
		  ext2fs_le32_to_cpu(((ext4_acl_header *) out_buf)->a_version),
		  EXT4_ACL_VERSION);
	ext2fs_free_mem(&out_buf);

	/* Clear RAW flag, reopen from disk, and verify round-trip back to POSIX ACL */
	retval = ext2fs_xattrs_close(&h);
	check_err("close_acl_handle", retval, 0);

	retval = ext2fs_xattrs_open(fs, ino, &h);
	check_err("reopen_acl_handle", retval, 0);
	retval = ext2fs_xattrs_read(h);
	check_err("reread_acl_handle", retval, 0);

	/* For user/group_obj/mask/other, e_id is normalized to 0 on readback */
	p_ent[0].e_id = 0;
	p_ent[2].e_id = 0;
	p_ent[4].e_id = 0;
	p_ent[5].e_id = 0;
	verify_xattr_val(h, "system.posix_acl_access",
			 posix_buf, sizeof(posix_buf));
	verify_xattr_val(h, "system.posix_acl_default",
			 posix_buf, sizeof(posix_buf));

	/* Error paths in convert_posix_acl_to_disk_buffer */
	retval = ext2fs_xattr_set(h, "system.posix_acl_access", posix_buf, 2);
	check_err("set_acl_too_small", retval, ENOMEM);

	retval = ext2fs_xattr_set(h, "system.posix_acl_access", posix_buf,
				  sizeof(posix_acl_xattr_header));
	check_err("set_acl_zero_entries", retval, EINVAL);

	retval = ext2fs_xattr_set(h, "system.posix_acl_access", posix_buf,
				  sizeof(posix_acl_xattr_header) + 3);
	check_err("set_acl_misaligned_size", retval, EINVAL);

	p_hdr->a_version = ext2fs_cpu_to_le32(99);
	retval = ext2fs_xattr_set(h, "system.posix_acl_access",
				  posix_buf, sizeof(posix_buf));
	check_err("set_acl_bad_version", retval, EINVAL);
	p_hdr->a_version = ext2fs_cpu_to_le32(POSIX_ACL_XATTR_VERSION);

	p_ent[0].e_tag = ext2fs_cpu_to_le16(0x99);
	retval = ext2fs_xattr_set(h, "system.posix_acl_access",
				  posix_buf, sizeof(posix_buf));
	check_err("set_acl_bad_tag", retval, EINVAL);

	/* Error paths in convert_disk_buffer_to_posix_acl */
	flags = XATTR_HANDLE_FLAG_RAW;
	ext2fs_xattrs_flags(h, &flags, NULL);

	/* 1) Disk ACL smaller than ext4_acl_header */
	retval = ext2fs_xattr_set(h, "system.posix_acl_access", "ab", 2);
	check_err("set_raw_short_acl", retval, 0);
	flags = 0;
	ext2fs_xattrs_flags(h, &flags, NULL);
	retval = ext2fs_xattr_get(h, "system.posix_acl_access",
				  &out_buf, &out_len);
	check_err("get_acl_disk_too_short", retval, EINVAL);

	/* 2) Disk ACL with wrong a_version */
	flags = XATTR_HANDLE_FLAG_RAW;
	ext2fs_xattrs_flags(h, &flags, NULL);
	bad_disk_acl.a_version = ext2fs_cpu_to_le32(99);
	retval = ext2fs_xattr_set(h, "system.posix_acl_access",
				  &bad_disk_acl, sizeof(bad_disk_acl));
	check_err("set_raw_bad_ver_acl", retval, 0);
	flags = 0;
	ext2fs_xattrs_flags(h, &flags, NULL);
	retval = ext2fs_xattr_get(h, "system.posix_acl_access",
				  &out_buf, &out_len);
	check_err("get_acl_disk_bad_ver", retval, EINVAL);

	/* 3) Disk ACL with invalid e_tag */
	flags = XATTR_HANDLE_FLAG_RAW;
	ext2fs_xattrs_flags(h, &flags, NULL);
	((ext4_acl_header *) bad_disk_tag)->a_version =
		ext2fs_cpu_to_le32(EXT4_ACL_VERSION);
	((ext4_acl_entry_short *) (bad_disk_tag + sizeof(ext4_acl_header)))->e_tag =
		ext2fs_cpu_to_le16(0x99);
	((ext4_acl_entry_short *) (bad_disk_tag + sizeof(ext4_acl_header)))->e_perm = 0;
	retval = ext2fs_xattr_set(h, "system.posix_acl_access",
				  bad_disk_tag, sizeof(bad_disk_tag));
	check_err("set_raw_bad_tag_acl", retval, 0);
	flags = 0;
	ext2fs_xattrs_flags(h, &flags, NULL);
	retval = ext2fs_xattr_get(h, "system.posix_acl_access",
				  &out_buf, &out_len);
	check_err("get_acl_disk_bad_tag", retval, EINVAL);

	retval = ext2fs_xattrs_close(&h);
	check_err("close_acl_final", retval, 0);
	teardown_fs(&fs);
	printf("test_posix_acl_conversion: completed\n");
}

/*
 * 7. Inline Data (system.data) Constraints and ext2fs_xattr_inode_max_size()
 */
static void test_inline_data_and_max_size(void)
{
	ext2_filsys			fs = NULL;
	struct ext2_xattr_handle	*h = NULL;
	struct ext2_inode_large		inode;
	char				*blk_buf = NULL;
	struct ext2_ext_attr_header	*hdr;
	struct ext2_ext_attr_entry	*entry;
	size_t				max_size = 0;
	char				big_data[128];
	ext2_ino_t			ino = 12;
	blk64_t				blk = 48;
	errcode_t			retval;

	/* 7a. 128-byte inode returns EXT2_ET_INLINE_DATA_NO_SPACE */
	setup_fs(&fs, 128, 32, 128, EXT2_FEATURE_COMPAT_EXT_ATTR,
		 EXT4_FEATURE_INCOMPAT_INLINE_DATA, 0);
	init_reg_inode(fs, ino);
	retval = ext2fs_xattr_inode_max_size(fs, ino, &max_size);
	check_err("max_size_128b_inode", retval, EXT2_ET_INLINE_DATA_NO_SPACE);
	teardown_fs(&fs);

	/* 7b. 256-byte inode with i_extra_isize == 32 */
	setup_fs(&fs, 128, 32, 256, EXT2_FEATURE_COMPAT_EXT_ATTR,
		 EXT4_FEATURE_INCOMPAT_INLINE_DATA, 0);
	init_reg_inode(fs, ino);

	/*
	 * With no xattrs:
	 * minoff = 256 - 160 - 4 = 92
	 * max_size = EXT2_EXT_ATTR_SIZE(92 - 20 - 3 - 4) = EXT2_EXT_ATTR_SIZE(65) = 68
	 */
	retval = ext2fs_xattr_inode_max_size(fs, ino, &max_size);
	check_err("max_size_empty_256b", retval, 0);
	check_u32("max_size_empty_val", (unsigned int) max_size, 68);

	/* Add an in-inode attribute ("user.a" = "1234", 20B entry + 4B val = 24B) */
	retval = ext2fs_xattrs_open(fs, ino, &h);
	check_err("open_inline_h", retval, 0);
	retval = ext2fs_xattrs_read(h);
	check_err("read_inline_h", retval, 0);
	retval = ext2fs_xattr_set(h, "user.a", "1234", 4);
	check_err("set_user_a", retval, 0);

	/*
	 * With "user.a" in ibody:
	 * storage_size = 92, value of "user.a" is at offs = 92 - 4 = 88,
	 * entry list takes 20B, so size = 88 - 20 - 4 = 64.
	 */
	retval = ext2fs_xattr_inode_max_size(fs, ino, &max_size);
	check_err("max_size_with_xattr", retval, 0);
	check_u32("max_size_with_xattr_val", (unsigned int) max_size, 64);

	/* Setting system.data within ibody space succeeds */
	retval = ext2fs_xattr_set(h, "system.data", "inline_payload", 14);
	check_err("set_system_data_ok", retval, 0);
	verify_xattr_val(h, "system.data", "inline_payload", 14);

	/* Setting system.data larger than ibody space returns EXT2_ET_EA_NO_SPACE */
	memset(big_data, 'D', sizeof(big_data));
	retval = ext2fs_xattr_set(h, "system.data", big_data, sizeof(big_data));
	check_err("set_system_data_no_space", retval, EXT2_ET_EA_NO_SPACE);

	retval = ext2fs_xattrs_close(&h);
	check_err("close_inline_h", retval, 0);

	/*
	 * 7c. If "system.data" is corrupted to reside in the external EA block
	 * (old_idx >= h->ibody_count), updating it returns EXT2_ET_FILESYSTEM_CORRUPTED.
	 */
	init_reg_inode(fs, 13);
	retval = ext2fs_get_memzero(fs->blocksize, &blk_buf);
	check_err("alloc_corrupt_blk_buf", retval, 0);
	hdr = (struct ext2_ext_attr_header *) blk_buf;
	hdr->h_magic = EXT2_EXT_ATTR_MAGIC;
	hdr->h_refcount = 1;
	hdr->h_blocks = 1;
	entry = (struct ext2_ext_attr_entry *) (hdr + 1);
	entry->e_name_len = 4;
	entry->e_name_index = 7; /* "system." */
	entry->e_value_size = 4;
	entry->e_value_offs = fs->blocksize - 4;
	memcpy((char *) entry + sizeof(*entry), "data", 4);
	memcpy(blk_buf + entry->e_value_offs, "abcd", 4);
	entry->e_hash = ext2fs_ext_attr_hash_entry(entry,
					blk_buf + entry->e_value_offs);
	retval = ext2fs_write_ext_attr3(fs, blk, blk_buf, 13);
	check_err("write_corrupt_sys_data_blk", retval, 0);

	retval = ext2fs_read_inode_full(fs, 13, EXT2_INODE(&inode),
					sizeof(inode));
	check_err("read_ino_13", retval, 0);
	ext2fs_file_acl_block_set(fs, EXT2_INODE(&inode), blk);
	retval = ext2fs_write_inode_full(fs, 13, EXT2_INODE(&inode),
					 sizeof(inode));
	check_err("write_ino_13_acl", retval, 0);

	retval = ext2fs_xattrs_open(fs, 13, &h);
	check_err("open_ino_13", retval, 0);
	retval = ext2fs_xattrs_read(h);
	check_err("read_ino_13_xattrs", retval, 0);

	retval = ext2fs_xattr_set(h, "system.data", "efgh", 4);
	check_err("update_blk_system_data_corrupted", retval,
		  EXT2_ET_FILESYSTEM_CORRUPTED);

	retval = ext2fs_xattrs_close(&h);
	check_err("close_ino_13", retval, 0);
	ext2fs_free_mem(&blk_buf);
	teardown_fs(&fs);
	printf("test_inline_data_and_max_size: completed\n");
}

/*
 * 8. Large Xattrs in External EA Inodes (EXT4_FEATURE_INCOMPAT_EA_INODE),
 *    Refcount Decrement/Free, Lustre Compatibility, and Corruption Checks
 */
static void test_ea_inode_feature(void)
{
	ext2_filsys			fs = NULL;
	struct ext2_xattr_handle	*h = NULL;
	struct full_inode		fino, fea_ino;
	struct ext2_inode_large		*inode = &fino.inode;
	struct ext2_inode_large		*ea_inode = &fea_ino.inode;
	struct ext2_ext_attr_entry	*entry;
	char				large_val1[1000];
	char				large_val2[1200];
	char				med_val1[500];
	char				med_val2[500];
	ext2_ino_t			ino = 12;
	ext2_ino_t			ea_ino1 = 0, ea_ino2 = 0;
	__u32				expected_hash;
	errcode_t			retval;

	setup_fs(&fs, 256, 64, 256, EXT2_FEATURE_COMPAT_EXT_ATTR,
		 EXT3_FEATURE_INCOMPAT_EXTENTS | EXT4_FEATURE_INCOMPAT_EA_INODE,
		 EXT4_FEATURE_RO_COMPAT_METADATA_CSUM);
	init_reg_inode(fs, ino);

	memset(large_val1, 'A', sizeof(large_val1));
	memset(large_val2, 'B', sizeof(large_val2));
	memset(med_val1, 'M', sizeof(med_val1));
	memset(med_val2, 'N', sizeof(med_val2));

	retval = ext2fs_xattrs_open(fs, ino, &h);
	check_err("open_ea_inode_h", retval, 0);
	retval = ext2fs_xattrs_read(h);
	check_err("read_ea_inode_h", retval, 0);

	/*
	 * 8a. Value > EXT4_XATTR_MIN_LARGE_EA_SIZE(1024) = 968 bytes
	 * goes directly to a newly created EA inode (in_inode = 1).
	 */
	retval = ext2fs_xattr_set(h, "user.large", large_val1,
				  sizeof(large_val1));
	check_err("set_large_xattr", retval, 0);

	/* Find the allocated EA inode from the ibody entry */
	retval = ext2fs_read_inode_full(fs, ino, EXT2_INODE(inode),
					EXT2_INODE_SIZE(fs->super));
	check_err("read_parent_ino", retval, 0);
	entry = (struct ext2_ext_attr_entry *) ((char *) inode + 128 +
						inode->i_extra_isize + 4);
	ea_ino1 = entry->e_value_inum;
	if (ea_ino1 == 0) {
		printf("FAIL [ea_ino1_allocated]: expected non-zero e_value_inum\n");
		test_fail++;
	}

	/* Verify EA inode flags, refcount, hash, and bitmap state */
	if (!ext2fs_test_inode_bitmap2(fs->inode_map, ea_ino1)) {
		printf("FAIL [ea_ino1_bitmap]: inode %u not marked in-use\n",
		       ea_ino1);
		test_fail++;
	}
	retval = ext2fs_read_inode_full(fs, ea_ino1, EXT2_INODE(ea_inode),
					EXT2_INODE_SIZE(fs->super));
	check_err("read_ea_ino1", retval, 0);
	if (!(ea_inode->i_flags & EXT4_EA_INODE_FL) ||
	    !(ea_inode->i_flags & EXT4_EXTENTS_FL)) {
		printf("FAIL [ea_ino1_flags]: i_flags=0x%x\n",
		       ea_inode->i_flags);
		test_fail++;
	}
	check_u64("ea_ino1_refcount",
		  ext2fs_get_ea_inode_ref(EXT2_INODE(ea_inode)), 1);
	expected_hash = ext2fs_crc32c_le(fs->csum_seed,
					 (const unsigned char *) large_val1,
					 sizeof(large_val1));
	check_u32("ea_ino1_hash",
		  ext2fs_get_ea_inode_hash(EXT2_INODE(ea_inode)),
		  expected_hash);

	/*
	 * 8b. Fallback to EA inode when value <= EXT4_XATTR_MIN_LARGE_EA_SIZE
	 * (500 bytes <= 968) overflows the EA block:
	 * First 500B entry ("user.med1") goes into the EA block; second 500B
	 * entry ("user.med2") gets EXT2_ET_EA_NO_SPACE with in_inode=0 and
	 * falls back to in_inode=1!
	 */
	retval = ext2fs_xattr_set(h, "user.med1", med_val1, sizeof(med_val1));
	check_err("set_med1_in_block", retval, 0);
	retval = ext2fs_xattr_set(h, "user.med2", med_val2, sizeof(med_val2));
	check_err("set_med2_fallback_ea_inode", retval, 0);

	/* Close, reopen, and verify all 3 attributes read back from disk */
	retval = ext2fs_xattrs_close(&h);
	check_err("close_ea_inode_h", retval, 0);

	retval = ext2fs_xattrs_open(fs, ino, &h);
	check_err("reopen_ea_inode_h", retval, 0);
	retval = ext2fs_xattrs_read(h);
	check_err("reread_ea_inode_h", retval, 0);

	verify_xattr_val(h, "user.large", large_val1, sizeof(large_val1));
	verify_xattr_val(h, "user.med1", med_val1, sizeof(med_val1));
	verify_xattr_val(h, "user.med2", med_val2, sizeof(med_val2));

	/*
	 * 8c. Updating "user.large" allocates a new EA inode and decrements
	 * ea_ino1's refcount to 0, freeing ea_ino1 and its blocks!
	 */
	retval = ext2fs_xattr_set(h, "user.large", large_val2,
				  sizeof(large_val2));
	check_err("update_large_xattr", retval, 0);

	if (ext2fs_test_inode_bitmap2(fs->inode_map, ea_ino1)) {
		printf("FAIL [ea_ino1_freed]: old EA inode %u still in-use\n",
		       ea_ino1);
		test_fail++;
	}
	retval = ext2fs_read_inode_full(fs, ea_ino1, EXT2_INODE(ea_inode),
					EXT2_INODE_SIZE(fs->super));
	check_err("read_freed_ea_ino1", retval, 0);
	check_u32("freed_ea_ino1_links", ea_inode->i_links_count, 0);
	check_u32("freed_ea_ino1_blocks", ea_inode->i_blocks, 0);
	if (ea_inode->i_flags & EXT4_EA_INODE_FL) {
		printf("FAIL [freed_ea_ino1_flag]: EXT4_EA_INODE_FL still set\n");
		test_fail++;
	}

	/*
	 * 8d. Shared EA inode (refcount == 2): removing the attribute
	 * decrements refcount to 1 without freeing the EA inode.
	 */
	retval = ext2fs_read_inode_full(fs, ino, EXT2_INODE(inode),
					EXT2_INODE_SIZE(fs->super));
	check_err("read_parent_for_ea_ino2", retval, 0);
	entry = (struct ext2_ext_attr_entry *) ((char *) inode + 128 +
						inode->i_extra_isize + 4);
	ea_ino2 = entry->e_value_inum;

	retval = ext2fs_read_inode_full(fs, ea_ino2, EXT2_INODE(ea_inode),
					EXT2_INODE_SIZE(fs->super));
	check_err("read_ea_ino2", retval, 0);
	ext2fs_set_ea_inode_ref(EXT2_INODE(ea_inode), 2);
	retval = ext2fs_write_inode_full(fs, ea_ino2, EXT2_INODE(ea_inode),
					 EXT2_INODE_SIZE(fs->super));
	check_err("write_ea_ino2_ref2", retval, 0);

	retval = ext2fs_xattr_remove(h, "user.large");
	check_err("remove_shared_large_xattr", retval, 0);

	if (!ext2fs_test_inode_bitmap2(fs->inode_map, ea_ino2)) {
		printf("FAIL [shared_ea_ino2_kept]: inode %u was freed at ref=1\n",
		       ea_ino2);
		test_fail++;
	}
	retval = ext2fs_read_inode_full(fs, ea_ino2, EXT2_INODE(ea_inode),
					EXT2_INODE_SIZE(fs->super));
	check_err("read_ea_ino2_after_dec", retval, 0);
	check_u64("ea_ino2_ref_is_1",
		  ext2fs_get_ea_inode_ref(EXT2_INODE(ea_inode)), 1);

	/* Remove remaining attributes (including "user.med2" EA inode) */
	retval = ext2fs_xattr_remove_all(h);
	check_err("remove_all_ea_inodes", retval, 0);
	retval = ext2fs_xattrs_close(&h);
	check_err("close_after_ea_remove_all", retval, 0);

	/*
	 * 8e. Test read_xattrs_from_buffer validation on EA inode entries
	 * and Lustre-style ea_inode hash fallback.
	 */
	retval = ext2fs_xattrs_open(fs, ino, &h);
	check_err("open_for_ea_inode_corrupt_tests", retval, 0);

	memset(&fino, 0, sizeof(fino));
	inode->i_mode = LINUX_S_IFREG | 0644;
	inode->i_extra_isize = 32;
	inode->i_generation = 0x55667788;
	*((__u32 *) ((char *) inode + 128 + 32)) = EXT2_EXT_ATTR_MAGIC;
	entry = (struct ext2_ext_attr_entry *) ((char *) inode + 128 + 32 + 4);
	entry->e_name_len = 1;
	entry->e_name_index = 1;
	entry->e_value_inum = ea_ino2;
	entry->e_value_size = sizeof(large_val2);
	*((char *) entry + sizeof(*entry)) = 'x';

	/* 1) Non-zero e_value_offs with e_value_inum != 0 -> EXT2_ET_EA_BAD_VALUE_OFFSET */
	entry->e_value_offs = 4;
	retval = ext2fs_xattrs_read_inode(h, inode);
	check_err("ea_inode_nonzero_offs", retval,
		  EXT2_ET_EA_BAD_VALUE_OFFSET);
	entry->e_value_offs = 0;

	/* 2) e_value_size > 64KB -> EXT2_ET_EA_BAD_VALUE_SIZE */
	entry->e_value_size = 65537;
	retval = ext2fs_xattrs_read_inode(h, inode);
	check_err("ea_inode_size_gt_64k", retval, EXT2_ET_EA_BAD_VALUE_SIZE);

	/* 3) e_value_size != ea_inode.i_size -> EXT2_ET_EA_BAD_VALUE_SIZE */
	entry->e_value_size = sizeof(large_val2) - 1;
	retval = ext2fs_xattrs_read_inode(h, inode);
	check_err("ea_inode_size_mismatch", retval, EXT2_ET_EA_BAD_VALUE_SIZE);
	entry->e_value_size = sizeof(large_val2);

	/* 4) Corrupt e_hash: neither hash matches nor Lustre mtime/generation -> EXT2_ET_BAD_EA_HASH */
	entry->e_hash = 0xdeadbeefU;
	retval = ext2fs_xattrs_read_inode(h, inode);
	check_err("ea_inode_bad_hash", retval, EXT2_ET_BAD_EA_HASH);

	/* 5) Lustre-style ea_inode: mtime == parent ino && generation == parent generation */
	retval = ext2fs_read_inode_full(fs, ea_ino2, EXT2_INODE(ea_inode),
					EXT2_INODE_SIZE(fs->super));
	check_err("read_ea_ino2_lustre", retval, 0);
	ea_inode->i_mtime = ino;
	ea_inode->i_generation = inode->i_generation;
	retval = ext2fs_write_inode_full(fs, ea_ino2, EXT2_INODE(ea_inode),
					 EXT2_INODE_SIZE(fs->super));
	check_err("write_ea_ino2_lustre", retval, 0);

	retval = ext2fs_xattrs_read_inode(h, inode);
	check_err("ea_inode_lustre_compat_ok", retval, 0);
	verify_xattr_val(h, "user.x", large_val2, sizeof(large_val2));

	/* 6) Missing EXT4_EA_INODE_FL on target inode -> EXT2_ET_EA_INODE_CORRUPTED */
	ea_inode->i_flags &= ~EXT4_EA_INODE_FL;
	retval = ext2fs_write_inode_full(fs, ea_ino2, EXT2_INODE(ea_inode),
					 EXT2_INODE_SIZE(fs->super));
	check_err("write_ea_ino2_no_flag", retval, 0);

	retval = ext2fs_xattrs_read_inode(h, inode);
	check_err("ea_inode_missing_flag_corrupted", retval,
		  EXT2_ET_EA_INODE_CORRUPTED);

	/* 7) i_links_count == 0 on target EA inode -> EXT2_ET_EA_INODE_CORRUPTED */
	ea_inode->i_flags |= EXT4_EA_INODE_FL;
	ea_inode->i_links_count = 0;
	retval = ext2fs_write_inode_full(fs, ea_ino2, EXT2_INODE(ea_inode),
					 EXT2_INODE_SIZE(fs->super));
	check_err("write_ea_ino2_zero_links", retval, 0);

	retval = ext2fs_xattrs_read_inode(h, inode);
	check_err("ea_inode_zero_links_corrupted", retval,
		  EXT2_ET_EA_INODE_CORRUPTED);

	retval = ext2fs_xattrs_close(&h);
	check_err("close_corrupt_test_h", retval, 0);
	teardown_fs(&fs);
	printf("test_ea_inode_feature: completed\n");
}

/*
 * 9. Legacy Signed-Char Hash Compatibility on Read
 */
static void test_signed_hash_compat_on_read(void)
{
	ext2_filsys			fs = NULL;
	struct ext2_xattr_handle	*h = NULL;
	struct full_inode		fino;
	struct ext2_inode_large		*inode = &fino.inode;
	struct ext2_ext_attr_entry	*entry;
	ext2_ino_t			ino = 12;
	errcode_t			retval;

	setup_fs(&fs, 128, 32, 256, EXT2_FEATURE_COMPAT_EXT_ATTR, 0, 0);
	init_reg_inode(fs, ino);

	memset(&fino, 0, sizeof(fino));
	inode->i_mode = LINUX_S_IFREG | 0644;
	inode->i_extra_isize = 32;
	*((__u32 *) ((char *) inode + 128 + 32)) = EXT2_EXT_ATTR_MAGIC;
	entry = (struct ext2_ext_attr_entry *) ((char *) inode + 128 + 32 + 4);

	/* Name with high-bit characters: "user.\x80\xff" */
	entry->e_name_len = 2;
	entry->e_name_index = 1;
	entry->e_value_size = 4;
	entry->e_value_offs = 92 - 4;
	((unsigned char *) entry)[sizeof(*entry)] = 0x80;
	((unsigned char *) entry)[sizeof(*entry) + 1] = 0xff;
	memcpy((char *) entry + entry->e_value_offs, "data", 4);

	/* Store the legacy signed-char hash in e_hash */
	entry->e_hash = ext2fs_ext_attr_hash_entry_signed(entry,
				(char *) entry + entry->e_value_offs);

	retval = ext2fs_xattrs_open(fs, ino, &h);
	check_err("open_signed_hash_h", retval, 0);
	retval = ext2fs_xattrs_read_inode(h, inode);
	check_err("read_inode_signed_hash_ok", retval, 0);
	verify_xattr_val(h, "user.\x80\xff", "data", 4);

	retval = ext2fs_xattrs_close(&h);
	check_err("close_signed_hash_h", retval, 0);
	teardown_fs(&fs);
	printf("test_signed_hash_compat_on_read: completed\n");
}

int main(int argc EXT2FS_ATTR((unused)), char *argv[] EXT2FS_ATTR((unused)))
{
	add_error_table(&et_ext2_error_table);

	test_entry_and_block_hashes();
	test_raw_block_io_and_refcount();
	test_free_ext_attr();
	test_handle_lifecycle_and_errors();
	test_ibody_and_block_xattrs();
	test_posix_acl_conversion();
	test_inline_data_and_max_size();
	test_ea_inode_feature();
	test_signed_hash_compat_on_read();

	if (test_fail == 0)
		printf("libext2fs ext_attr unit tests passed!\n");
	else
		printf("libext2fs ext_attr unit tests FAILED (%d errors)\n",
		       test_fail);

	return test_fail ? 1 : 0;
}
