/*
 * csum.c --- checksumming of ext3 structures
 *
 * Copyright (C) 2006 Cluster File Systems, Inc.
 * Copyright (C) 2006, 2007 by Andreas Dilger <adilger@clusterfs.com>
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Library
 * General Public License, version 2.
 * %End-Header%
 */

#include "config.h"
#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#include "ext2_fs.h"
#include "ext2fs.h"
#include "crc16.h"
#include <assert.h>

#ifndef offsetof
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#endif

#ifdef DEBUG
#define STATIC
#else
#define STATIC static
#endif

void ext2fs_init_csum_seed(ext2_filsys fs)
{
	if (ext2fs_has_feature_csum_seed(fs->super))
		fs->csum_seed = fs->super->s_checksum_seed;
	else if (ext2fs_has_feature_metadata_csum(fs->super) ||
		 ext2fs_has_feature_ea_inode(fs->super))
		fs->csum_seed = ext2fs_crc32c_le(~0, fs->super->s_uuid,
						 sizeof(fs->super->s_uuid));
}

static __u32 ext2fs_mmp_csum(ext2_filsys fs, struct mmp_struct *mmp)
{
	int offset = offsetof(struct mmp_struct, mmp_checksum);

	return ext2fs_crc32c_le(fs->csum_seed, (unsigned char *)mmp, offset);
}

int ext2fs_mmp_csum_verify(ext2_filsys fs, struct mmp_struct *mmp)
{
	__u32 calculated;

	if (!ext2fs_has_feature_metadata_csum(fs->super))
		return 1;

	calculated = ext2fs_mmp_csum(fs, mmp);

	return ext2fs_le32_to_cpu(mmp->mmp_checksum) == calculated;
}

errcode_t ext2fs_mmp_csum_set(ext2_filsys fs, struct mmp_struct *mmp)
{
	__u32 crc;

	if (!ext2fs_has_feature_metadata_csum(fs->super))
		return 0;

	crc = ext2fs_mmp_csum(fs, mmp);
	mmp->mmp_checksum = ext2fs_cpu_to_le32(crc);

	return 0;
}

int ext2fs_verify_csum_type(ext2_filsys fs, struct ext2_super_block *sb)
{
	if (!ext2fs_has_feature_metadata_csum(fs->super))
		return 1;

	return sb->s_checksum_type == EXT2_CRC32C_CHKSUM;
}

static __u32 ext2fs_superblock_csum(ext2_filsys fs EXT2FS_ATTR((unused)),
				    struct ext2_super_block *sb)
{
	int offset = offsetof(struct ext2_super_block, s_checksum);

	return ext2fs_crc32c_le(~0, (unsigned char *)sb, offset);
}

/* NOTE: The input to this function MUST be in LE order */
int ext2fs_superblock_csum_verify(ext2_filsys fs, struct ext2_super_block *sb)
{
	__u32 flag, calculated;

	if (fs->flags & EXT2_FLAG_SWAP_BYTES)
		flag = EXT4_FEATURE_RO_COMPAT_METADATA_CSUM;
	else
		flag = ext2fs_cpu_to_le32(EXT4_FEATURE_RO_COMPAT_METADATA_CSUM);

	if (!EXT2_HAS_RO_COMPAT_FEATURE(fs->super, flag))
		return 1;

	calculated = ext2fs_superblock_csum(fs, sb);

	return ext2fs_le32_to_cpu(sb->s_checksum) == calculated;
}

/* NOTE: The input to this function MUST be in LE order */
errcode_t ext2fs_superblock_csum_set(ext2_filsys fs,
				     struct ext2_super_block *sb)
{
	__u32 flag, crc;

	if (fs->flags & EXT2_FLAG_SWAP_BYTES)
		flag = EXT4_FEATURE_RO_COMPAT_METADATA_CSUM;
	else
		flag = ext2fs_cpu_to_le32(EXT4_FEATURE_RO_COMPAT_METADATA_CSUM);

	if (!EXT2_HAS_RO_COMPAT_FEATURE(fs->super, flag))
		return 0;

	crc = ext2fs_superblock_csum(fs, sb);
	sb->s_checksum = ext2fs_cpu_to_le32(crc);

	return 0;
}

static errcode_t ext2fs_ext_attr_block_csum(ext2_filsys fs,
					    ext2_ino_t inum EXT2FS_ATTR((unused)),
					    blk64_t block,
					    struct ext2_ext_attr_header *hdr,
					    __u32 *crc)
{
	char *buf = (char *)hdr;
	__u32 old_crc = hdr->h_checksum;

	hdr->h_checksum = 0;
	block = ext2fs_cpu_to_le64(block);
	*crc = ext2fs_crc32c_le(fs->csum_seed, (unsigned char *)&block,
				sizeof(block));
	*crc = ext2fs_crc32c_le(*crc, (unsigned char *)buf, fs->blocksize);
	hdr->h_checksum = old_crc;

	return 0;
}

int ext2fs_ext_attr_block_csum_verify(ext2_filsys fs, ext2_ino_t inum,
				      blk64_t block,
				      struct ext2_ext_attr_header *hdr)
{
	__u32 calculated;
	errcode_t retval;

	if (!ext2fs_has_feature_metadata_csum(fs->super))
		return 1;

	retval = ext2fs_ext_attr_block_csum(fs, inum, block, hdr, &calculated);
	if (retval)
		return 0;

	return ext2fs_le32_to_cpu(hdr->h_checksum) == calculated;
}

errcode_t ext2fs_ext_attr_block_csum_set(ext2_filsys fs, ext2_ino_t inum,
					 blk64_t block,
					 struct ext2_ext_attr_header *hdr)
{
	errcode_t retval;
	__u32 crc;

	if (!ext2fs_has_feature_metadata_csum(fs->super))
		return 0;

	retval = ext2fs_ext_attr_block_csum(fs, inum, block, hdr, &crc);
	if (retval)
		return retval;
	hdr->h_checksum = ext2fs_cpu_to_le32(crc);
	return 0;
}

static __u16 do_nothing16(__u16 x)
{
	return x;
}

static __u16 disk_to_host16(__u16 x)
{
	return ext2fs_le16_to_cpu(x);
}

static errcode_t __get_dx_countlimit(ext2_filsys fs,
				     struct ext2_dir_entry *dirent,
				     struct ext2_dx_countlimit **cc,
				     int *offset,
				     int need_swab)
{
	struct ext2_dir_entry *dp;
	struct ext2_dx_root_info *root;
	struct ext2_dx_countlimit *c;
	int count_offset, max_sane_entries;
	unsigned int rec_len;
	__u16 (*translate)(__u16) = (need_swab ? disk_to_host16 : do_nothing16);

	rec_len = translate(dirent->rec_len);

	if (rec_len == fs->blocksize && translate(dirent->name_len) == 0)
		count_offset = 8;
	else if (rec_len == 12) {
		dp = (struct ext2_dir_entry *)(((char *)dirent) + rec_len);
		rec_len = translate(dp->rec_len);
		if (rec_len != fs->blocksize - 12)
			return EXT2_ET_DB_NOT_FOUND;
		root = (struct ext2_dx_root_info *)(((char *)dp + 12));
		if (root->reserved_zero ||
		    root->info_length != sizeof(struct ext2_dx_root_info))
			return EXT2_ET_DB_NOT_FOUND;
		count_offset = 32;
	} else
		return EXT2_ET_DB_NOT_FOUND;

	c = (struct ext2_dx_countlimit *)(((char *)dirent) + count_offset);
	max_sane_entries = (fs->blocksize - count_offset) /
			   sizeof(struct ext2_dx_entry);
	if (ext2fs_le16_to_cpu(c->limit) > max_sane_entries ||
	    ext2fs_le16_to_cpu(c->count) > max_sane_entries)
		return EXT2_ET_DIR_NO_SPACE_FOR_CSUM;

	if (offset)
		*offset = count_offset;
	if (cc)
		*cc = c;

	return 0;
}

errcode_t ext2fs_get_dx_countlimit(ext2_filsys fs,
				   struct ext2_dir_entry *dirent,
				   struct ext2_dx_countlimit **cc,
				   int *offset)
{
	return __get_dx_countlimit(fs, dirent, cc, offset, 0);
}

void ext2fs_initialize_dirent_tail(ext2_filsys fs,
				   struct ext2_dir_entry_tail *t)
{
	memset(t, 0, sizeof(struct ext2_dir_entry_tail));
	ext2fs_set_rec_len(fs, sizeof(struct ext2_dir_entry_tail),
			   (struct ext2_dir_entry *)t);
	t->det_reserved_name_len = EXT2_DIR_NAME_LEN_CSUM;
}

static errcode_t __get_dirent_tail(ext2_filsys fs,
				   struct ext2_dir_entry *dirent,
				   struct ext2_dir_entry_tail **tt,
				   int need_swab)
{
	struct ext2_dir_entry *d;
	void *top;
	struct ext2_dir_entry_tail *t;
	unsigned int rec_len;
	errcode_t retval = 0;
	__u16 (*translate)(__u16) = (need_swab ? disk_to_host16 : do_nothing16);

	if (fs->blocksize < 1024)
		return EXT2_FILSYS_CORRUPTED; /* Should never happen */

	d = dirent;
	top = EXT2_DIRENT_TAIL(dirent, fs->blocksize);

	while ((void *) d < top) {
		rec_len = translate(d->rec_len);
		if ((rec_len < 8) || (rec_len & 0x03))
			return EXT2_ET_DIR_CORRUPTED;
		d = (struct ext2_dir_entry *)(((char *)d) + rec_len);
	}

	if ((char *)d > ((char *)dirent + fs->blocksize))
			return EXT2_ET_DIR_CORRUPTED;
	if (d != top)
		return EXT2_ET_DIR_NO_SPACE_FOR_CSUM;

	t = (struct ext2_dir_entry_tail *)d;
	if (t->det_reserved_zero1 ||
	    translate(t->det_rec_len) != sizeof(struct ext2_dir_entry_tail) ||
	    translate(t->det_reserved_name_len) != EXT2_DIR_NAME_LEN_CSUM)
		return EXT2_ET_DIR_NO_SPACE_FOR_CSUM;

	if (tt)
		*tt = t;
	return retval;
}

int ext2fs_dirent_has_tail(ext2_filsys fs, struct ext2_dir_entry *dirent)
{
	return __get_dirent_tail(fs, dirent, NULL, 0) !=
		EXT2_ET_DIR_NO_SPACE_FOR_CSUM;
}

static errcode_t ext2fs_dirent_csum(ext2_filsys fs, ext2_ino_t inum,
				    struct ext2_dir_entry *dirent, __u32 *crc,
				    int size)
{
	errcode_t retval;
	char *buf = (char *)dirent;
	__u32 gen;
	struct ext2_inode inode;

	retval = ext2fs_read_inode(fs, inum, &inode);
	if (retval)
		return retval;

	inum = ext2fs_cpu_to_le32(inum);
	gen = ext2fs_cpu_to_le32(inode.i_generation);
	*crc = ext2fs_crc32c_le(fs->csum_seed, (unsigned char *)&inum,
				sizeof(inum));
	*crc = ext2fs_crc32c_le(*crc, (unsigned char *)&gen, sizeof(gen));
	*crc = ext2fs_crc32c_le(*crc, (unsigned char *)buf, size);

	return 0;
}

int ext2fs_dirent_csum_verify(ext2_filsys fs, ext2_ino_t inum,
			      struct ext2_dir_entry *dirent)
{
	errcode_t retval;
	__u32 calculated;
	struct ext2_dir_entry_tail *t;

	retval = __get_dirent_tail(fs, dirent, &t, 1);
	if (retval)
		return 1;

	/*
	 * The checksum field is overlaid with the dirent->name field
	 * so the swapfs.c functions won't change the endianness.
	 */
	retval = ext2fs_dirent_csum(fs, inum, dirent, &calculated,
				    (char *)t - (char *)dirent);
	if (retval)
		return 0;
	return ext2fs_le32_to_cpu(t->det_checksum) == calculated;
}

static errcode_t ext2fs_dirent_csum_set(ext2_filsys fs, ext2_ino_t inum,
					struct ext2_dir_entry *dirent)
{
	errcode_t retval;
	__u32 crc;
	struct ext2_dir_entry_tail *t;

	retval = __get_dirent_tail(fs, dirent, &t, 1);
	if (retval)
		return retval;

	/* swapfs.c functions don't change the checksum endianness */
	retval = ext2fs_dirent_csum(fs, inum, dirent, &crc,
				    (char *)t - (char *)dirent);
	if (retval)
		return retval;
	t->det_checksum = ext2fs_cpu_to_le32(crc);
	return 0;
}

errcode_t ext2fs_dx_csum(ext2_filsys fs, ext2_ino_t inum,
			 struct ext2_dir_entry *dirent,
			 __u32 *crc, struct ext2_dx_tail **ret_t)
{
	errcode_t retval;
	char *buf = (char *)dirent;
	int size;
	__u32 gen, dummy_csum = 0;
	struct ext2_inode inode;
	struct ext2_dx_tail *t;
	struct ext2_dx_countlimit *c;
	int count_offset, limit, count;

	retval = __get_dx_countlimit(fs, dirent, &c, &count_offset, 1);
	if (retval)
		return retval;
	limit = ext2fs_le16_to_cpu(c->limit);
	count = ext2fs_le16_to_cpu(c->count);
	if (count_offset + (limit * sizeof(struct ext2_dx_entry)) >
	    fs->blocksize - sizeof(struct ext2_dx_tail))
		return EXT2_ET_DIR_NO_SPACE_FOR_CSUM;
	/* htree structs are accessed in LE order */
	t = (struct ext2_dx_tail *)(((struct ext2_dx_entry *)c) + limit);

	size = count_offset + (count * sizeof(struct ext2_dx_entry));

	retval = ext2fs_read_inode(fs, inum, &inode);
	if (retval)
		return retval;

	inum = ext2fs_cpu_to_le32(inum);
	gen = ext2fs_cpu_to_le32(inode.i_generation);
	*crc = ext2fs_crc32c_le(fs->csum_seed, (unsigned char *)&inum,
				sizeof(inum));
	*crc = ext2fs_crc32c_le(*crc, (unsigned char *)&gen, sizeof(gen));
	*crc = ext2fs_crc32c_le(*crc, (unsigned char *)buf, size);
	*crc = ext2fs_crc32c_le(*crc, (unsigned char *)t, 4);
	*crc = ext2fs_crc32c_le(*crc, (unsigned char *)&dummy_csum, 4);

	if (ret_t)
		*ret_t = t;
	return 0;
}

static int ext2fs_dx_csum_verify(ext2_filsys fs, ext2_ino_t inum,
				 struct ext2_dir_entry *dirent)
{
	__u32 calculated;
	errcode_t retval;
	struct ext2_dx_tail *t;

	retval = ext2fs_dx_csum(fs, inum, dirent, &calculated, &t);
	if (retval)
		return 0;

	return ext2fs_le32_to_cpu(t->dt_checksum) == calculated;
}

static errcode_t ext2fs_dx_csum_set(ext2_filsys fs, ext2_ino_t inum,
				    struct ext2_dir_entry *dirent)
{
	__u32 crc;
	errcode_t retval = 0;
	struct ext2_dx_tail *t;

	retval = ext2fs_dx_csum(fs, inum, dirent, &crc, &t);
	if (retval)
		return retval;
	t->dt_checksum = ext2fs_cpu_to_le32(crc);
	return retval;
}

int ext2fs_dir_block_csum_verify(ext2_filsys fs, ext2_ino_t inum,
				 struct ext2_dir_entry *dirent)
{
	if (!ext2fs_has_feature_metadata_csum(fs->super))
		return 1;

	if (__get_dirent_tail(fs, dirent, NULL, 1) == 0)
		return ext2fs_dirent_csum_verify(fs, inum, dirent);
	if (__get_dx_countlimit(fs, dirent, NULL, NULL, 1) == 0)
		return ext2fs_dx_csum_verify(fs, inum, dirent);

	return 0;
}

errcode_t ext2fs_dir_block_csum_set(ext2_filsys fs, ext2_ino_t inum,
				    struct ext2_dir_entry *dirent)
{
	if (!ext2fs_has_feature_metadata_csum(fs->super))
		return 0;

	if (__get_dirent_tail(fs, dirent, NULL, 1) == 0)
		return ext2fs_dirent_csum_set(fs, inum, dirent);
	if (__get_dx_countlimit(fs, dirent, NULL, NULL, 1) == 0)
		return ext2fs_dx_csum_set(fs, inum, dirent);

	if (fs->flags & EXT2_FLAG_IGNORE_CSUM_ERRORS)
		return 0;
	return EXT2_ET_DIR_NO_SPACE_FOR_CSUM;
}

#define EXT3_EXTENT_TAIL_OFFSET(hdr)	(sizeof(struct ext3_extent_header) + \
	(sizeof(struct ext3_extent) * ext2fs_le16_to_cpu((hdr)->eh_max)))

static struct ext3_extent_tail *get_extent_tail(struct ext3_extent_header *h)
{
	return (struct ext3_extent_tail *)(((char *)h) +
					   EXT3_EXTENT_TAIL_OFFSET(h));
}

static errcode_t ext2fs_extent_block_csum(ext2_filsys fs, ext2_ino_t inum,
					  struct ext3_extent_header *eh,
					  __u32 *crc)
{
	int size;
	__u32 gen;
	errcode_t retval;
	struct ext2_inode inode;

	size = EXT3_EXTENT_TAIL_OFFSET(eh) + offsetof(struct ext3_extent_tail,
						      et_checksum);

	retval = ext2fs_read_inode(fs, inum, &inode);
	if (retval)
		return retval;
	inum = ext2fs_cpu_to_le32(inum);
	gen = ext2fs_cpu_to_le32(inode.i_generation);
	*crc = ext2fs_crc32c_le(fs->csum_seed, (unsigned char *)&inum,
				sizeof(inum));
	*crc = ext2fs_crc32c_le(*crc, (unsigned char *)&gen, sizeof(gen));
	*crc = ext2fs_crc32c_le(*crc, (unsigned char *)eh, size);

	return 0;
}

int ext2fs_extent_block_csum_verify(ext2_filsys fs, ext2_ino_t inum,
				    struct ext3_extent_header *eh)
{
	errcode_t retval;
	__u32 provided, calculated;
	struct ext3_extent_tail *t = get_extent_tail(eh);

	/*
	 * The extent tree structures are accessed in LE order, so we must
	 * swap the checksum bytes here.
	 */
	if (!ext2fs_has_feature_metadata_csum(fs->super))
		return 1;

	provided = ext2fs_le32_to_cpu(t->et_checksum);
	retval = ext2fs_extent_block_csum(fs, inum, eh, &calculated);
	if (retval)
		return 0;

	return provided == calculated;
}

errcode_t ext2fs_extent_block_csum_set(ext2_filsys fs, ext2_ino_t inum,
				       struct ext3_extent_header *eh)
{
	errcode_t retval;
	__u32 crc;
	struct ext3_extent_tail *t = get_extent_tail(eh);

	if (!ext2fs_has_feature_metadata_csum(fs->super))
		return 0;

	/*
	 * The extent tree structures are accessed in LE order, so we must
	 * swap the checksum bytes here.
	 */
	retval = ext2fs_extent_block_csum(fs, inum, eh, &crc);
	if (retval)
		return retval;
	t->et_checksum = ext2fs_cpu_to_le32(crc);
	return retval;
}

int ext2fs_inode_bitmap_csum_verify(ext2_filsys fs, dgrp_t group,
				    char *bitmap, int size)
{
	struct ext4_group_desc *gdp = (struct ext4_group_desc *)
			ext2fs_group_desc(fs, fs->group_desc, group);
	__u32 provided, calculated;

	if (!ext2fs_has_feature_metadata_csum(fs->super))
		return 1;
	provided = gdp->bg_inode_bitmap_csum_lo;
	calculated = ext2fs_crc32c_le(fs->csum_seed, (unsigned char *)bitmap,
				      size);
	if (EXT2_DESC_SIZE(fs->super) >= EXT4_BG_INODE_BITMAP_CSUM_HI_END)
		provided |= (__u32)gdp->bg_inode_bitmap_csum_hi << 16;
	else
		calculated &= 0xFFFF;

	return provided == calculated;
}

errcode_t ext2fs_inode_bitmap_csum_set(ext2_filsys fs, dgrp_t group,
				       char *bitmap, int size)
{
	__u32 crc;
	struct ext4_group_desc *gdp = (struct ext4_group_desc *)
			ext2fs_group_desc(fs, fs->group_desc, group);

	if (!ext2fs_has_feature_metadata_csum(fs->super))
		return 0;

	crc = ext2fs_crc32c_le(fs->csum_seed, (unsigned char *)bitmap, size);
	gdp->bg_inode_bitmap_csum_lo = crc & 0xFFFF;
	if (EXT2_DESC_SIZE(fs->super) >= EXT4_BG_INODE_BITMAP_CSUM_HI_END)
		gdp->bg_inode_bitmap_csum_hi = crc >> 16;

	return 0;
}

int ext2fs_block_bitmap_csum_verify(ext2_filsys fs, dgrp_t group,
				    char *bitmap, int size)
{
	struct ext4_group_desc *gdp = (struct ext4_group_desc *)
			ext2fs_group_desc(fs, fs->group_desc, group);
	__u32 provided, calculated;

	if (!ext2fs_has_feature_metadata_csum(fs->super))
		return 1;
	provided = gdp->bg_block_bitmap_csum_lo;
	calculated = ext2fs_crc32c_le(fs->csum_seed, (unsigned char *)bitmap,
				      size);
	if (EXT2_DESC_SIZE(fs->super) >= EXT4_BG_BLOCK_BITMAP_CSUM_HI_LOCATION)
		provided |= (__u32)gdp->bg_block_bitmap_csum_hi << 16;
	else
		calculated &= 0xFFFF;

	return provided == calculated;
}

errcode_t ext2fs_block_bitmap_csum_set(ext2_filsys fs, dgrp_t group,
				       char *bitmap, int size)
{
	__u32 crc;
	struct ext4_group_desc *gdp = (struct ext4_group_desc *)
			ext2fs_group_desc(fs, fs->group_desc, group);

	if (!ext2fs_has_feature_metadata_csum(fs->super))
		return 0;

	crc = ext2fs_crc32c_le(fs->csum_seed, (unsigned char *)bitmap, size);
	gdp->bg_block_bitmap_csum_lo = crc & 0xFFFF;
	if (EXT2_DESC_SIZE(fs->super) >= EXT4_BG_BLOCK_BITMAP_CSUM_HI_LOCATION)
		gdp->bg_block_bitmap_csum_hi = crc >> 16;

	return 0;
}

static errcode_t ext2fs_inode_csum(ext2_filsys fs, ext2_ino_t inum,
			       struct ext2_inode_large *inode,
			       __u32 *crc, int has_hi)
{
	__u32 gen;
	struct ext2_inode_large *desc = inode;
	size_t size = EXT2_INODE_SIZE(fs->super);
	__u16 old_lo;
	__u16 old_hi = 0;

	old_lo = inode->i_checksum_lo;
	inode->i_checksum_lo = 0;
	if (has_hi) {
		old_hi = inode->i_checksum_hi;
		inode->i_checksum_hi = 0;
	}

	inum = ext2fs_cpu_to_le32(inum);
	gen = inode->i_generation;
	*crc = ext2fs_crc32c_le(fs->csum_seed, (unsigned char *)&inum,
				sizeof(inum));
	*crc = ext2fs_crc32c_le(*crc, (unsigned char *)&gen, sizeof(gen));
	*crc = ext2fs_crc32c_le(*crc, (unsigned char *)desc, size);

	inode->i_checksum_lo = old_lo;
	if (has_hi)
		inode->i_checksum_hi = old_hi;
	return 0;
}

int ext2fs_inode_csum_verify(ext2_filsys fs, ext2_ino_t inum,
			     struct ext2_inode_large *inode)
{
	errcode_t retval;
	__u32 provided, calculated;
	unsigned int i, has_hi;
	char *cp;

	if (!ext2fs_has_feature_metadata_csum(fs->super))
		return 1;

	has_hi = (EXT2_INODE_SIZE(fs->super) > EXT2_GOOD_OLD_INODE_SIZE &&
		  inode->i_extra_isize >= EXT4_INODE_CSUM_HI_EXTRA_END);

	provided = ext2fs_le16_to_cpu(inode->i_checksum_lo);
	retval = ext2fs_inode_csum(fs, inum, inode, &calculated, has_hi);
	if (retval)
		return 0;
	if (has_hi) {
		__u32 hi = ext2fs_le16_to_cpu(inode->i_checksum_hi);
		provided |= hi << 16;
	} else
		calculated &= 0xFFFF;

	if (provided == calculated)
		return 1;

	/*
	 * If the checksum didn't match, it's possible it was due to
	 * the inode being all zero's.  It's unlikely this is the
	 * case, but it can happen.  So check for it here.  (We only
	 * check the base inode since that's good enough, and it's not
	 * worth the bother to figure out how much of the extended
	 * inode, if any, is present.)
	 */
	for (cp = (char *) inode, i = 0;
	     i < sizeof(struct ext2_inode);
	     cp++, i++)
		if (*cp)
			return 0;
	return 1;		/* Inode must have been all zero's */
}

errcode_t ext2fs_inode_csum_set(ext2_filsys fs, ext2_ino_t inum,
			   struct ext2_inode_large *inode)
{
	errcode_t retval;
	__u32 crc;
	int has_hi;

	if (!ext2fs_has_feature_metadata_csum(fs->super))
		return 0;

	has_hi = (EXT2_INODE_SIZE(fs->super) > EXT2_GOOD_OLD_INODE_SIZE &&
		  inode->i_extra_isize >= EXT4_INODE_CSUM_HI_EXTRA_END);

	retval = ext2fs_inode_csum(fs, inum, inode, &crc, has_hi);
	if (retval)
		return retval;
	inode->i_checksum_lo = ext2fs_cpu_to_le16(crc & 0xFFFF);
	if (has_hi)
		inode->i_checksum_hi = ext2fs_cpu_to_le16(crc >> 16);
	return 0;
}

__u16 ext2fs_group_desc_csum(ext2_filsys fs, dgrp_t group)
{
	struct ext2_group_desc *desc = ext2fs_group_desc(fs, fs->group_desc,
							 group);
	size_t offset, size = EXT2_DESC_SIZE(fs->super);
	__u16 crc = 0;
#ifdef WORDS_BIGENDIAN
	struct ext4_group_desc swabdesc;
	size_t save_size = size;
	const size_t ext4_bg_size = sizeof(struct ext4_group_desc);
	struct ext2_group_desc *save_desc = desc;

	/* Have to swab back to little-endian to do the checksum */
	if (size > ext4_bg_size)
		size = ext4_bg_size;
	memcpy(&swabdesc, desc, size);
	ext2fs_swap_group_desc2(fs, (struct ext2_group_desc *) &swabdesc);
	desc = (struct ext2_group_desc *) &swabdesc;
	group = ext2fs_swab32(group);
#endif

	if (ext2fs_has_feature_metadata_csum(fs->super)) {
		/* new metadata csum code */
		__u16 old_crc;
		__u32 crc32;

		old_crc = desc->bg_checksum;
		desc->bg_checksum = 0;
		crc32 = ext2fs_crc32c_le(fs->csum_seed, (unsigned char *)&group,
					 sizeof(group));
		crc32 = ext2fs_crc32c_le(crc32, (unsigned char *)desc,
					 size);
		desc->bg_checksum = old_crc;
#ifdef WORDS_BIGENDIAN
		if (save_size > ext4_bg_size)
			crc32 = ext2fs_crc32c_le(crc32,
				     (unsigned char *)save_desc + ext4_bg_size,
	                             save_size - ext4_bg_size);
#endif
		crc = crc32 & 0xFFFF;
		goto out;
	}

	/* old crc16 code */
	offset = offsetof(struct ext2_group_desc, bg_checksum);
	crc = ext2fs_crc16(~0, fs->super->s_uuid,
			   sizeof(fs->super->s_uuid));
	crc = ext2fs_crc16(crc, &group, sizeof(group));
	crc = ext2fs_crc16(crc, desc, offset);
	offset += sizeof(desc->bg_checksum); /* skip checksum */
	/* for checksum of struct ext4_group_desc do the rest...*/
	if (offset < size) {
		crc = ext2fs_crc16(crc, (char *)desc + offset,
				   size - offset);
	}
#ifdef WORDS_BIGENDIAN
	/*
	 * If the size of the bg descriptor is greater than 64
	 * bytes, which is the size of the traditional ext4 bg
	 * descriptor, checksum the rest of the descriptor here
	 */
	if (save_size > ext4_bg_size)
		crc = ext2fs_crc16(crc, (char *)save_desc + ext4_bg_size,
				   save_size - ext4_bg_size);
#endif

out:
	return crc;
}

int ext2fs_group_desc_csum_verify(ext2_filsys fs, dgrp_t group)
{
	if (ext2fs_has_group_desc_csum(fs) &&
	    (ext2fs_bg_checksum(fs, group) !=
	     ext2fs_group_desc_csum(fs, group)))
		return 0;

	return 1;
}

void ext2fs_group_desc_csum_set(ext2_filsys fs, dgrp_t group)
{
	if (!ext2fs_has_group_desc_csum(fs))
		return;

	/* ext2fs_bg_checksum_set() sets the actual checksum field but
	 * does not calculate the checksum itself. */
	ext2fs_bg_checksum_set(fs, group, ext2fs_group_desc_csum(fs, group));
}

static __u32 find_last_inode_ingrp(ext2fs_inode_bitmap bitmap,
				   __u32 inodes_per_grp, dgrp_t grp_no)
{
	ext2_ino_t i, start_ino, end_ino;

	start_ino = grp_no * inodes_per_grp + 1;
	end_ino = start_ino + inodes_per_grp - 1;

	for (i = end_ino; i >= start_ino; i--) {
		if (ext2fs_fast_test_inode_bitmap2(bitmap, i))
			return i - start_ino + 1;
	}
	return inodes_per_grp;
}

/* update the bitmap flags, set the itable high watermark, and calculate
 * checksums for the group descriptors */
errcode_t ext2fs_set_gdt_csum(ext2_filsys fs)
{
	struct ext2_super_block *sb = fs->super;
	int dirty = 0;
	dgrp_t i;

	if (!fs->inode_map)
		return EXT2_ET_NO_INODE_BITMAP;

	if (!ext2fs_has_group_desc_csum(fs))
		return 0;

	for (i = 0; i < fs->group_desc_count; i++) {
		__u32 old_csum = ext2fs_bg_checksum(fs, i);
		__u32 old_unused = ext2fs_bg_itable_unused(fs, i);
		__u32 old_flags = ext2fs_bg_flags(fs, i);
		__u32 old_free_inodes_count = ext2fs_bg_free_inodes_count(fs, i);
		__u32 old_free_blocks_count = ext2fs_bg_free_blocks_count(fs, i);

		if (old_free_blocks_count == sb->s_blocks_per_group &&
		    i != fs->group_desc_count - 1)
			ext2fs_bg_flags_set(fs, i, EXT2_BG_BLOCK_UNINIT);

		if (old_free_inodes_count == sb->s_inodes_per_group) {
			ext2fs_bg_flags_set(fs, i, EXT2_BG_INODE_UNINIT);
			ext2fs_bg_itable_unused_set(fs, i, sb->s_inodes_per_group);
		} else {
			int unused =
				sb->s_inodes_per_group -
				find_last_inode_ingrp(fs->inode_map,
						      sb->s_inodes_per_group, i);

			ext2fs_bg_flags_clear(fs, i, EXT2_BG_INODE_UNINIT);
			ext2fs_bg_itable_unused_set(fs, i, unused);
		}

		ext2fs_group_desc_csum_set(fs, i);
		if (old_flags != ext2fs_bg_flags(fs, i))
			dirty = 1;
		if (old_unused != ext2fs_bg_itable_unused(fs, i))
			dirty = 1;
		if (old_csum != ext2fs_bg_checksum(fs, i))
			dirty = 1;
	}
	if (dirty)
		ext2fs_mark_super_dirty(fs);
	return 0;
}

#ifdef DEBUG
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#include "e2p/e2p.h"

static int test_fail = 0;

/*
 * Lightweight in-memory io_manager for persisting inode table blocks
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

static unsigned char sb_uuid[16] = {
	0x4f, 0x25, 0xe8, 0xcf, 0xe7, 0x97, 0x48, 0x23,
	0xbe, 0xfa, 0xa7, 0x88, 0x4b, 0xae, 0xec, 0xdb
};

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

static void check_int(const char *test_name, int actual, int expected)
{
	if (actual != expected) {
		printf("FAIL [%s]: got %d, expected %d\n",
		       test_name, actual, expected);
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

static void setup_fs(ext2_filsys *ret_fs, unsigned int blocks,
		     unsigned int inodes, unsigned int inode_size,
		     unsigned int desc_size, __u32 incompat, __u32 ro_compat)
{
	struct ext2_super_block	param;
	errcode_t		retval;

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
	param.s_feature_incompat = incompat;
	param.s_feature_ro_compat = ro_compat;
	param.s_desc_size = desc_size;
	memcpy(param.s_uuid, sb_uuid, sizeof(sb_uuid));

	retval = ext2fs_initialize("test_fs",
				   EXT2_FLAG_RW | EXT2_FLAG_64BITS |
				   EXT2_FLAG_EXCLUSIVE,
				   &param, mem_io_manager, ret_fs);
	if (retval) {
		com_err("setup_fs", retval, "initializing filesystem");
		exit(1);
	}
	memcpy((*ret_fs)->super->s_uuid, sb_uuid, sizeof(sb_uuid));
	ext2fs_init_csum_seed(*ret_fs);

	retval = ext2fs_allocate_tables(*ret_fs);
	if (retval) {
		com_err("setup_fs", retval, "allocating tables");
		exit(1);
	}
}

static void teardown_fs(ext2_filsys *fs)
{
	if (*fs)
		ext2fs_close_free(fs);
	if (mem_disk)
		ext2fs_free_mem(&mem_disk);
	mem_disk_size = 0;
}

static void write_test_inode(ext2_filsys fs, ext2_ino_t ino, __u16 mode,
			     __u32 generation)
{
	struct ext2_inode	inode;
	errcode_t		retval;

	memset(&inode, 0, sizeof(inode));
	inode.i_mode = mode;
	inode.i_links_count = 1;
	inode.i_generation = generation;
	retval = ext2fs_write_inode(fs, ino, &inode);
	check_err("write_test_inode", retval, 0);
}

static void print_csum(const char *msg, ext2_filsys fs, dgrp_t group)
{
	__u16 crc1, crc2, crc3;
	dgrp_t swabgroup;
	struct ext2_group_desc *desc = ext2fs_group_desc(fs, fs->group_desc,
							 group);
	size_t size = EXT2_DESC_SIZE(fs->super);
	struct ext2_super_block *sb = fs->super;
	size_t offset = offsetof(struct ext2_group_desc, bg_checksum);
#ifdef WORDS_BIGENDIAN
	struct ext4_group_desc swabdesc;
	struct ext2_group_desc *save_desc = desc;
	const size_t ext4_bg_size = sizeof(struct ext4_group_desc);
	size_t save_size = size;
#endif

#ifdef WORDS_BIGENDIAN
	/* Have to swab back to little-endian to do the checksum */
	if (size > ext4_bg_size)
		size = ext4_bg_size;
	memcpy(&swabdesc, desc, size);
	ext2fs_swap_group_desc2(fs, (struct ext2_group_desc *) &swabdesc);
	desc = (struct ext2_group_desc *) &swabdesc;

	swabgroup = ext2fs_swab32(group);
#else
	swabgroup = group;
#endif

	crc1 = ext2fs_crc16(~0, sb->s_uuid, sizeof(fs->super->s_uuid));
	crc2 = ext2fs_crc16(crc1, &swabgroup, sizeof(swabgroup));
	crc3 = ext2fs_crc16(crc2, desc, offset);
	offset += sizeof(desc->bg_checksum); /* skip checksum */
	/* for checksum of struct ext4_group_desc do the rest...*/
	if (offset < size)
		crc3 = ext2fs_crc16(crc3, (char *)desc + offset, size - offset);
#ifdef WORDS_BIGENDIAN
	if (save_size > ext4_bg_size)
		crc3 = ext2fs_crc16(crc3, (char *)save_desc + ext4_bg_size,
				    save_size - ext4_bg_size);
#endif

	printf("%s UUID %s=%04x, grp %u=%04x: %04x=%04x\n",
	       msg, e2p_uuid2str(sb->s_uuid), crc1, group, crc2, crc3,
	       ext2fs_group_desc_csum(fs, group));
}

/*
 * 1. Legacy CRC16 GDT checksum known vectors (32-byte and 128-byte descriptors)
 */
static void test_legacy_gdt_csum(void)
{
	struct ext2_super_block	param;
	errcode_t		retval;
	ext2_filsys		fs;
	dgrp_t			i;
	__u16			csum1, csum2, csum_known = 0xd3a4;

	/* 1a. 32-byte group descriptors */
	memset(&param, 0, sizeof(param));
	ext2fs_blocks_count_set(&param, 32768);

	retval = ext2fs_initialize("test fs", EXT2_FLAG_64BITS, &param,
				   test_io_manager, &fs);
	if (retval) {
		com_err("setup", retval, "While initializing filesystem");
		exit(1);
	}
	memcpy(fs->super->s_uuid, sb_uuid, 16);
	fs->super->s_feature_ro_compat = EXT4_FEATURE_RO_COMPAT_GDT_CSUM;

	for (i = 0; i < fs->group_desc_count; i++) {
		ext2fs_block_bitmap_loc_set(fs, i, 124);
		ext2fs_inode_bitmap_loc_set(fs, i, 125);
		ext2fs_inode_table_loc_set(fs, i, 126);
		ext2fs_bg_free_blocks_count_set(fs, i, 31119);
		ext2fs_bg_free_inodes_count_set(fs, i, 15701);
		ext2fs_bg_used_dirs_count_set(fs, i, 2);
		ext2fs_bg_flags_zap(fs, i);
	}

	csum1 = ext2fs_group_desc_csum(fs, 0);
	print_csum("csum0000", fs, 0);
	check_u32("gdt_crc16_32b_grp0", csum1, csum_known);

	csum2 = ext2fs_group_desc_csum(fs, 1);
	print_csum("csum0001", fs, 1);
	if (csum1 == csum2) {
		printf("FAIL [gdt_crc16_grp1_diff]: group 0 and 1 matched\n");
		test_fail++;
	}

	csum2 = ext2fs_group_desc_csum(fs, 2);
	print_csum("csumffff", fs, 2);
	if (csum1 == csum2) {
		printf("FAIL [gdt_crc16_grp2_diff]: group 0 and 2 matched\n");
		test_fail++;
	}

	ext2fs_bg_checksum_set(fs, 0, csum1);
	csum2 = ext2fs_group_desc_csum(fs, 0);
	print_csum("csum_set", fs, 0);
	check_u32("gdt_crc16_indep_of_csum_field", csum2, csum1);
	check_int("gdt_crc16_verify_ok",
		  ext2fs_group_desc_csum_verify(fs, 0), 1);

	memset(fs->super->s_uuid, 0x30, sizeof(fs->super->s_uuid));
	print_csum("new_uuid", fs, 0);
	check_int("gdt_crc16_verify_diff_uuid",
		  ext2fs_group_desc_csum_verify(fs, 0), 0);

	csum1 = ext2fs_group_desc_csum(fs, 0);
	ext2fs_bg_checksum_set(fs, 0, csum1);
	print_csum("csum_new", fs, 0);
	ext2fs_bg_free_blocks_count_set(fs, 0, 1);
	csum2 = ext2fs_group_desc_csum(fs, 0);
	print_csum("csum_blk", fs, 0);
	if (csum1 == csum2) {
		printf("FAIL [gdt_crc16_diff_data]: checksums matched\n");
		test_fail++;
	}

	/* Without GDT_CSUM or METADATA_CSUM, set is no-op and verify is 1 */
	fs->super->s_feature_ro_compat = 0;
	ext2fs_bg_checksum_set(fs, 0, 0xbeef);
	ext2fs_group_desc_csum_set(fs, 0);
	check_u32("gdt_disabled_set_noop", ext2fs_bg_checksum(fs, 0), 0xbeef);
	check_int("gdt_disabled_verify_1",
		  ext2fs_group_desc_csum_verify(fs, 0), 1);

	ext2fs_free(fs);

	/* 1b. 128-byte 64-bit group descriptors (known vector 0x5b6e) */
	memset(&param, 0, sizeof(param));
	ext2fs_blocks_count_set(&param, 32768);
	param.s_feature_incompat |= EXT4_FEATURE_INCOMPAT_64BIT;
	param.s_desc_size = 128;
	csum_known = 0x5b6e;

	retval = ext2fs_initialize("test fs 64", EXT2_FLAG_64BITS, &param,
				   test_io_manager, &fs);
	if (retval) {
		com_err("setup", retval, "While initializing 64-bit fs");
		exit(1);
	}
	memcpy(fs->super->s_uuid, sb_uuid, 16);
	fs->super->s_feature_ro_compat = EXT4_FEATURE_RO_COMPAT_GDT_CSUM;

	for (i = 0; i < fs->group_desc_count; i++) {
		ext2fs_block_bitmap_loc_set(fs, i, 124);
		ext2fs_inode_bitmap_loc_set(fs, i, 125);
		ext2fs_inode_table_loc_set(fs, i, 126);
		ext2fs_bg_free_blocks_count_set(fs, i, 31119);
		ext2fs_bg_free_inodes_count_set(fs, i, 15701);
		ext2fs_bg_used_dirs_count_set(fs, i, 2);
		ext2fs_bg_flags_zap(fs, i);
	}

	csum1 = ext2fs_group_desc_csum(fs, 0);
	print_csum("csum_128b", fs, 0);
	check_u32("gdt_crc16_128b_grp0", csum1, csum_known);

	ext2fs_group_desc_csum_set(fs, 0);
	check_u32("gdt_crc16_128b_set", ext2fs_bg_checksum(fs, 0), csum_known);
	check_int("gdt_crc16_128b_verify",
		  ext2fs_group_desc_csum_verify(fs, 0), 1);

	ext2fs_free(fs);
}

/*
 * 2. ext2fs_init_csum_seed() across feature combinations
 */
static void test_init_csum_seed(void)
{
	ext2_filsys	fs = NULL;
	__u32		expected_uuid_crc;

	setup_fs(&fs, 256, 32, 256, 64, EXT4_FEATURE_INCOMPAT_64BIT, 0);
	expected_uuid_crc = ext2fs_crc32c_le(~0, sb_uuid, sizeof(sb_uuid));

	/* 2a. No checksum or ea_inode features -> csum_seed unchanged */
	fs->super->s_feature_incompat = EXT4_FEATURE_INCOMPAT_64BIT;
	fs->super->s_feature_ro_compat = 0;
	fs->csum_seed = 0x11223344U;
	ext2fs_init_csum_seed(fs);
	check_u32("init_seed_no_features", fs->csum_seed, 0x11223344U);

	/* 2b. METADATA_CSUM -> CRC32C(~0, s_uuid, 16) */
	fs->super->s_feature_ro_compat = EXT4_FEATURE_RO_COMPAT_METADATA_CSUM;
	fs->csum_seed = 0;
	ext2fs_init_csum_seed(fs);
	check_u32("init_seed_metadata_csum", fs->csum_seed, expected_uuid_crc);

	/* 2c. EA_INODE without METADATA_CSUM -> CRC32C(~0, s_uuid, 16) */
	fs->super->s_feature_ro_compat = 0;
	fs->super->s_feature_incompat = EXT4_FEATURE_INCOMPAT_64BIT |
					EXT4_FEATURE_INCOMPAT_EA_INODE;
	fs->csum_seed = 0;
	ext2fs_init_csum_seed(fs);
	check_u32("init_seed_ea_inode", fs->csum_seed, expected_uuid_crc);

	/* 2d. CSUM_SEED overrides s_uuid even when METADATA_CSUM is set */
	fs->super->s_feature_ro_compat = EXT4_FEATURE_RO_COMPAT_METADATA_CSUM;
	fs->super->s_feature_incompat = EXT4_FEATURE_INCOMPAT_64BIT |
					EXT4_FEATURE_INCOMPAT_CSUM_SEED;
	fs->super->s_checksum_seed = 0xdeadc0deU;
	fs->csum_seed = 0;
	ext2fs_init_csum_seed(fs);
	check_u32("init_seed_csum_seed_feature", fs->csum_seed, 0xdeadc0deU);

	teardown_fs(&fs);
	printf("test_init_csum_seed: completed\n");
}

/*
 * 3. MMP checksums, superblock checksums, and s_checksum_type verification
 */
static void test_mmp_and_superblock_csum(void)
{
	ext2_filsys		fs = NULL;
	struct mmp_struct	mmp;
	struct ext2_super_block	le_sb;
	__u32			exp_crc, saved_seed;
	errcode_t		retval;

	setup_fs(&fs, 256, 32, 256, 64, EXT4_FEATURE_INCOMPAT_64BIT, 0);

	/* 3a. MMP checksum with METADATA_CSUM disabled */
	memset(&mmp, 0, sizeof(mmp));
	mmp.mmp_magic = EXT4_MMP_MAGIC;
	mmp.mmp_seq = EXT4_MMP_SEQ_CLEAN;
	mmp.mmp_time = 1700000000ULL;
	memcpy(mmp.mmp_nodename, "testnode", 8);
	memcpy(mmp.mmp_bdevname, "loop0", 5);
	mmp.mmp_check_interval = 5;
	mmp.mmp_checksum = ext2fs_cpu_to_le32(0x12345678U);

	retval = ext2fs_mmp_csum_set(fs, &mmp);
	check_err("mmp_set_disabled", retval, 0);
	check_u32("mmp_set_disabled_noop",
		  ext2fs_le32_to_cpu(mmp.mmp_checksum), 0x12345678U);
	check_int("mmp_verify_disabled", ext2fs_mmp_csum_verify(fs, &mmp), 1);

	/* 3b. MMP checksum with METADATA_CSUM enabled */
	fs->super->s_feature_ro_compat |= EXT4_FEATURE_RO_COMPAT_METADATA_CSUM;
	ext2fs_init_csum_seed(fs);

	retval = ext2fs_mmp_csum_set(fs, &mmp);
	check_err("mmp_set_enabled", retval, 0);
	exp_crc = ext2fs_crc32c_le(fs->csum_seed, (unsigned char *) &mmp,
				   offsetof(struct mmp_struct, mmp_checksum));
	check_u32("mmp_set_crc_val",
		  ext2fs_le32_to_cpu(mmp.mmp_checksum), exp_crc);
	check_int("mmp_verify_ok", ext2fs_mmp_csum_verify(fs, &mmp), 1);

	/* Corrupt mmp_checksum */
	mmp.mmp_checksum ^= ext2fs_cpu_to_le32(1);
	check_int("mmp_verify_bad_csum", ext2fs_mmp_csum_verify(fs, &mmp), 0);
	mmp.mmp_checksum ^= ext2fs_cpu_to_le32(1);

	/* Corrupt mmp_seq */
	mmp.mmp_seq ^= 1;
	check_int("mmp_verify_bad_seq", ext2fs_mmp_csum_verify(fs, &mmp), 0);
	mmp.mmp_seq ^= 1;

	/* Different csum_seed fails verification */
	saved_seed = fs->csum_seed;
	fs->csum_seed ^= 0xffffffffU;
	check_int("mmp_verify_bad_seed", ext2fs_mmp_csum_verify(fs, &mmp), 0);
	fs->csum_seed = saved_seed;

	/* 3c. ext2fs_verify_csum_type() */
	memcpy(&le_sb, fs->super, sizeof(le_sb));
	fs->super->s_feature_ro_compat &= ~EXT4_FEATURE_RO_COMPAT_METADATA_CSUM;
	le_sb.s_checksum_type = 0;
	check_int("csum_type_disabled_0",
		  ext2fs_verify_csum_type(fs, &le_sb), 1);
	le_sb.s_checksum_type = 0x99;
	check_int("csum_type_disabled_99",
		  ext2fs_verify_csum_type(fs, &le_sb), 1);

	fs->super->s_feature_ro_compat |= EXT4_FEATURE_RO_COMPAT_METADATA_CSUM;
	le_sb.s_checksum_type = EXT2_CRC32C_CHKSUM;
	check_int("csum_type_crc32c_ok",
		  ext2fs_verify_csum_type(fs, &le_sb), 1);
	le_sb.s_checksum_type = 0;
	check_int("csum_type_bad_0",
		  ext2fs_verify_csum_type(fs, &le_sb), 0);
	le_sb.s_checksum_type = EXT2_CRC32C_CHKSUM + 1;
	check_int("csum_type_bad_other",
		  ext2fs_verify_csum_type(fs, &le_sb), 0);

	/* 3d. Superblock checksum with METADATA_CSUM disabled */
	fs->super->s_feature_ro_compat &= ~EXT4_FEATURE_RO_COMPAT_METADATA_CSUM;
	memcpy(&le_sb, fs->super, sizeof(le_sb));
#ifdef WORDS_BIGENDIAN
	ext2fs_swap_super(&le_sb);
#endif
	le_sb.s_checksum = ext2fs_cpu_to_le32(0xdeadbeefU);
	retval = ext2fs_superblock_csum_set(fs, &le_sb);
	check_err("sb_csum_set_disabled", retval, 0);
	check_u32("sb_csum_set_disabled_noop",
		  ext2fs_le32_to_cpu(le_sb.s_checksum), 0xdeadbeefU);
	check_int("sb_csum_verify_disabled",
		  ext2fs_superblock_csum_verify(fs, &le_sb), 1);

	/* 3e. Superblock checksum with METADATA_CSUM enabled (native order) */
	fs->super->s_feature_ro_compat |=
		ext2fs_cpu_to_le32(EXT4_FEATURE_RO_COMPAT_METADATA_CSUM);
	retval = ext2fs_superblock_csum_set(fs, &le_sb);
	check_err("sb_csum_set_enabled", retval, 0);
	exp_crc = ext2fs_crc32c_le(~0, (unsigned char *) &le_sb,
				   offsetof(struct ext2_super_block,
					    s_checksum));
	check_u32("sb_csum_crc_val",
		  ext2fs_le32_to_cpu(le_sb.s_checksum), exp_crc);
	check_int("sb_csum_verify_ok",
		  ext2fs_superblock_csum_verify(fs, &le_sb), 1);

	/* Superblock csum uses ~0 directly, independent of fs->csum_seed */
	fs->csum_seed ^= 0x55555555U;
	check_int("sb_csum_indep_of_fs_seed",
		  ext2fs_superblock_csum_verify(fs, &le_sb), 1);
	fs->csum_seed = saved_seed;

	/* Corrupt s_checksum and a field before s_checksum */
	le_sb.s_checksum ^= ext2fs_cpu_to_le32(1);
	check_int("sb_csum_verify_bad_csum",
		  ext2fs_superblock_csum_verify(fs, &le_sb), 0);
	le_sb.s_checksum ^= ext2fs_cpu_to_le32(1);

	le_sb.s_free_blocks_count ^= ext2fs_cpu_to_le32(1);
	check_int("sb_csum_verify_bad_field",
		  ext2fs_superblock_csum_verify(fs, &le_sb), 0);
	le_sb.s_free_blocks_count ^= ext2fs_cpu_to_le32(1);

	/* 3f. Superblock checksum with EXT2_FLAG_SWAP_BYTES set */
	fs->flags |= EXT2_FLAG_SWAP_BYTES;
	fs->super->s_feature_ro_compat = EXT4_FEATURE_RO_COMPAT_METADATA_CSUM;
	le_sb.s_checksum = 0;
	retval = ext2fs_superblock_csum_set(fs, &le_sb);
	check_err("sb_csum_set_swap_flag", retval, 0);
	check_u32("sb_csum_val_swap_flag",
		  ext2fs_le32_to_cpu(le_sb.s_checksum), exp_crc);
	check_int("sb_csum_verify_swap_flag",
		  ext2fs_superblock_csum_verify(fs, &le_sb), 1);

	fs->super->s_feature_ro_compat = 0;
	le_sb.s_checksum = ext2fs_cpu_to_le32(0x11111111U);
	retval = ext2fs_superblock_csum_set(fs, &le_sb);
	check_err("sb_csum_set_swap_disabled", retval, 0);
	check_u32("sb_csum_set_swap_disabled_noop",
		  ext2fs_le32_to_cpu(le_sb.s_checksum), 0x11111111U);
	check_int("sb_csum_verify_swap_disabled",
		  ext2fs_superblock_csum_verify(fs, &le_sb), 1);
	fs->flags &= ~EXT2_FLAG_SWAP_BYTES;

	teardown_fs(&fs);
	printf("test_mmp_and_superblock_csum: completed\n");
}

/*
 * 4. Extended attribute block checksums (including 64-bit block numbers)
 */
static void test_ext_attr_block_csum(void)
{
	ext2_filsys			fs = NULL;
	char				*buf = NULL;
	struct ext2_ext_attr_header	*hdr;
	blk64_t				blk = 0x200000033ULL;
	ext2_ino_t			ino = 15;
	__u32				saved_csum;
	errcode_t			retval;

	setup_fs(&fs, 256, 32, 256, 64, EXT4_FEATURE_INCOMPAT_64BIT, 0);
	retval = ext2fs_get_memzero(fs->blocksize, &buf);
	check_err("alloc_ea_buf", retval, 0);

	hdr = (struct ext2_ext_attr_header *) buf;
	hdr->h_magic = ext2fs_cpu_to_le32(EXT2_EXT_ATTR_MAGIC);
	hdr->h_refcount = ext2fs_cpu_to_le32(1);
	hdr->h_blocks = ext2fs_cpu_to_le32(1);
	hdr->h_hash = ext2fs_cpu_to_le32(0x55aa55aaU);
	buf[fs->blocksize - 1] = 0x7f;

	/* 4a. METADATA_CSUM disabled */
	hdr->h_checksum = ext2fs_cpu_to_le32(0xdeadbeefU);
	retval = ext2fs_ext_attr_block_csum_set(fs, ino, blk, hdr);
	check_err("ea_csum_set_disabled", retval, 0);
	check_u32("ea_csum_set_disabled_noop",
		  ext2fs_le32_to_cpu(hdr->h_checksum), 0xdeadbeefU);
	check_int("ea_csum_verify_disabled",
		  ext2fs_ext_attr_block_csum_verify(fs, ino, blk, hdr), 1);

	/* 4b. METADATA_CSUM enabled */
	fs->super->s_feature_ro_compat |= EXT4_FEATURE_RO_COMPAT_METADATA_CSUM;
	ext2fs_init_csum_seed(fs);

	retval = ext2fs_ext_attr_block_csum_set(fs, ino, blk, hdr);
	check_err("ea_csum_set_enabled", retval, 0);
	saved_csum = hdr->h_checksum;
	check_int("ea_csum_verify_ok",
		  ext2fs_ext_attr_block_csum_verify(fs, ino, blk, hdr), 1);
	/* Verify h_checksum was restored after verification */
	check_u32("ea_csum_preserved_after_verify", hdr->h_checksum,
		  saved_csum);

	/* inum parameter is unused in EA block checksum */
	check_int("ea_csum_verify_diff_ino",
		  ext2fs_ext_attr_block_csum_verify(fs, 999, blk, hdr), 1);

	/* Sensitive to full 64-bit block number (including upper 32 bits) */
	check_int("ea_csum_verify_diff_blk",
		  ext2fs_ext_attr_block_csum_verify(fs, ino, blk + 1, hdr), 0);
	check_int("ea_csum_verify_truncated_32b_blk",
		  ext2fs_ext_attr_block_csum_verify(fs, ino, (blk_t) blk, hdr),
		  0);

	/* Sensitive to h_checksum and payload corruption */
	hdr->h_checksum ^= ext2fs_cpu_to_le32(1);
	check_int("ea_csum_verify_bad_csum",
		  ext2fs_ext_attr_block_csum_verify(fs, ino, blk, hdr), 0);
	hdr->h_checksum = saved_csum;

	buf[fs->blocksize - 1] ^= 1;
	check_int("ea_csum_verify_bad_payload",
		  ext2fs_ext_attr_block_csum_verify(fs, ino, blk, hdr), 0);

	ext2fs_free_mem(&buf);
	teardown_fs(&fs);
	printf("test_ext_attr_block_csum: completed\n");
}

/*
 * 5. Directory entry tail helpers and linear directory block checksums
 */
static void test_dirent_tail_and_csum(void)
{
	ext2_filsys			fs = NULL;
	char				*buf = NULL;
	struct ext2_dir_entry		*d1, *d2, *d3;
	struct ext2_dir_entry_tail	tail, *t, *ret_t;
	ext2_ino_t			dir_ino = 12;
	unsigned int			tail_off;
	errcode_t			retval;

	setup_fs(&fs, 256, 32, 256, 64, EXT4_FEATURE_INCOMPAT_64BIT,
		 EXT4_FEATURE_RO_COMPAT_METADATA_CSUM);
	write_test_inode(fs, dir_ino, LINUX_S_IFDIR | 0755, 0x2468ace0U);

	retval = ext2fs_get_memzero(fs->blocksize, &buf);
	check_err("alloc_dir_buf", retval, 0);
	tail_off = fs->blocksize - sizeof(struct ext2_dir_entry_tail);

	/* 5a. ext2fs_initialize_dirent_tail() */
	memset(&tail, 0xa5, sizeof(tail));
	ext2fs_initialize_dirent_tail(fs, &tail);
	check_u32("init_tail_zero1", tail.det_reserved_zero1, 0);
	check_u32("init_tail_rec_len", tail.det_rec_len,
		  sizeof(struct ext2_dir_entry_tail));
	check_u32("init_tail_name_len", tail.det_reserved_name_len,
		  EXT2_DIR_NAME_LEN_CSUM);
	check_u32("init_tail_checksum", tail.det_checksum, 0);

	/* 5b. ext2fs_dirent_has_tail() and __get_dirent_tail() (host order) */
	memset(buf, 0, fs->blocksize);
	d1 = (struct ext2_dir_entry *) buf;
	d1->inode = dir_ino;
	d1->rec_len = 12;
	d1->name_len = 1;
	d1->name[0] = '.';

	d2 = (struct ext2_dir_entry *) (buf + 12);
	d2->inode = EXT2_ROOT_INO;
	d2->rec_len = tail_off - 12;
	d2->name_len = 2;
	d2->name[0] = '.';
	d2->name[1] = '.';

	t = EXT2_DIRENT_TAIL(buf, fs->blocksize);
	ext2fs_initialize_dirent_tail(fs, t);

	check_int("dirent_has_tail_valid",
		  ext2fs_dirent_has_tail(fs, d1), 1);
	ret_t = NULL;
	retval = __get_dirent_tail(fs, d1, &ret_t, 0);
	check_err("get_dirent_tail_valid", retval, 0);
	if (ret_t != t) {
		printf("FAIL [get_dirent_tail_ptr]: got %p, expected %p\n",
		       (void *) ret_t, (void *) t);
		test_fail++;
	}

	/* Tail field validation failures */
	t->det_reserved_zero1 = 1;
	check_err("tail_bad_zero1",
		  __get_dirent_tail(fs, d1, NULL, 0),
		  EXT2_ET_DIR_NO_SPACE_FOR_CSUM);
	check_int("has_tail_bad_zero1", ext2fs_dirent_has_tail(fs, d1), 0);
	t->det_reserved_zero1 = 0;

	t->det_rec_len = 8;
	check_err("tail_bad_rec_len",
		  __get_dirent_tail(fs, d1, NULL, 0),
		  EXT2_ET_DIR_NO_SPACE_FOR_CSUM);
	check_int("has_tail_bad_rec_len", ext2fs_dirent_has_tail(fs, d1), 0);
	t->det_rec_len = sizeof(struct ext2_dir_entry_tail);

	t->det_reserved_name_len = 0x0100;
	check_err("tail_bad_name_len",
		  __get_dirent_tail(fs, d1, NULL, 0),
		  EXT2_ET_DIR_NO_SPACE_FOR_CSUM);
	check_int("has_tail_bad_name_len", ext2fs_dirent_has_tail(fs, d1), 0);
	t->det_reserved_name_len = EXT2_DIR_NAME_LEN_CSUM;

	/* Entry steps over top (no space reserved for tail) */
	d2->rec_len = fs->blocksize - 12;
	check_err("tail_stepped_over_top",
		  __get_dirent_tail(fs, d1, NULL, 0),
		  EXT2_ET_DIR_NO_SPACE_FOR_CSUM);
	check_int("has_tail_stepped_over_top",
		  ext2fs_dirent_has_tail(fs, d1), 0);

	/* Corrupt rec_len: < 8, unaligned, and > blocksize */
	d2->rec_len = 4;
	check_err("tail_corrupt_rec_len_small",
		  __get_dirent_tail(fs, d1, NULL, 0), EXT2_ET_DIR_CORRUPTED);

	d2->rec_len = 14;
	check_err("tail_corrupt_rec_len_unaligned",
		  __get_dirent_tail(fs, d1, NULL, 0), EXT2_ET_DIR_CORRUPTED);

	d2->rec_len = 2048;
	check_err("tail_corrupt_rec_len_past_end",
		  __get_dirent_tail(fs, d1, NULL, 0), EXT2_ET_DIR_CORRUPTED);

	/* Invalid fs->blocksize < 1024 */
	d2->rec_len = tail_off - 12;
	fs->blocksize = 512;
	check_err("tail_blocksize_too_small",
		  __get_dirent_tail(fs, d1, NULL, 0), EXT2_FILSYS_CORRUPTED);
	fs->blocksize = 1024;

	/* 5c. On-disk (LE) directory block checksum set & verify */
	memset(buf, 0, fs->blocksize);
	d1 = (struct ext2_dir_entry *) buf;
	d1->inode = ext2fs_cpu_to_le32(dir_ino);
	d1->rec_len = ext2fs_cpu_to_le16(12);
	d1->name_len = ext2fs_cpu_to_le16(1);
	d1->name[0] = '.';

	d2 = (struct ext2_dir_entry *) (buf + 12);
	d2->inode = ext2fs_cpu_to_le32(EXT2_ROOT_INO);
	d2->rec_len = ext2fs_cpu_to_le16(12);
	d2->name_len = ext2fs_cpu_to_le16(2);
	d2->name[0] = '.';
	d2->name[1] = '.';

	d3 = (struct ext2_dir_entry *) (buf + 24);
	d3->inode = ext2fs_cpu_to_le32(13);
	d3->rec_len = ext2fs_cpu_to_le16(tail_off - 24);
	d3->name_len = ext2fs_cpu_to_le16(5);
	memcpy(d3->name, "file1", 5);

	t = EXT2_DIRENT_TAIL(buf, fs->blocksize);
	memset(t, 0, sizeof(*t));
	t->det_rec_len = ext2fs_cpu_to_le16(sizeof(struct ext2_dir_entry_tail));
	t->det_reserved_name_len = ext2fs_cpu_to_le16(EXT2_DIR_NAME_LEN_CSUM);

	/* When METADATA_CSUM is disabled, dir_block_csum_set/verify are no-ops */
	fs->super->s_feature_ro_compat &= ~EXT4_FEATURE_RO_COMPAT_METADATA_CSUM;
	t->det_checksum = ext2fs_cpu_to_le32(0xdeadbeefU);
	retval = ext2fs_dir_block_csum_set(fs, dir_ino, d1);
	check_err("dir_csum_set_disabled", retval, 0);
	check_u32("dir_csum_set_disabled_noop",
		  ext2fs_le32_to_cpu(t->det_checksum), 0xdeadbeefU);
	check_int("dir_csum_verify_disabled",
		  ext2fs_dir_block_csum_verify(fs, dir_ino, d1), 1);

	/* Enable METADATA_CSUM */
	fs->super->s_feature_ro_compat |= EXT4_FEATURE_RO_COMPAT_METADATA_CSUM;
	retval = ext2fs_dir_block_csum_set(fs, dir_ino, d1);
	check_err("dir_csum_set_enabled", retval, 0);
	check_int("dirent_csum_verify_ok",
		  ext2fs_dirent_csum_verify(fs, dir_ino, d1), 1);
	check_int("dir_block_csum_verify_ok",
		  ext2fs_dir_block_csum_verify(fs, dir_ino, d1), 1);

	/* Corrupt checksum and entry name */
	t->det_checksum ^= ext2fs_cpu_to_le32(1);
	check_int("dirent_csum_verify_bad_csum",
		  ext2fs_dirent_csum_verify(fs, dir_ino, d1), 0);
	check_int("dir_block_csum_verify_bad_csum",
		  ext2fs_dir_block_csum_verify(fs, dir_ino, d1), 0);
	t->det_checksum ^= ext2fs_cpu_to_le32(1);

	d3->name[4] = '2';
	check_int("dirent_csum_verify_bad_name",
		  ext2fs_dirent_csum_verify(fs, dir_ino, d1), 0);
	check_int("dir_block_csum_verify_bad_name",
		  ext2fs_dir_block_csum_verify(fs, dir_ino, d1), 0);
	d3->name[4] = '1';

	/* Changing inode generation invalidates directory checksum */
	write_test_inode(fs, dir_ino, LINUX_S_IFDIR | 0755, 0x99999999U);
	check_int("dir_block_csum_verify_bad_gen",
		  ext2fs_dir_block_csum_verify(fs, dir_ino, d1), 0);
	write_test_inode(fs, dir_ino, LINUX_S_IFDIR | 0755, 0x2468ace0U);
	check_int("dir_block_csum_verify_restored_gen",
		  ext2fs_dir_block_csum_verify(fs, dir_ino, d1), 1);

	/* Invalid inode number */
	check_err("dir_block_csum_set_bad_ino",
		  ext2fs_dir_block_csum_set(fs, 9999, d1),
		  EXT2_ET_BAD_INODE_NUM);
	check_int("dirent_csum_verify_bad_ino",
		  ext2fs_dirent_csum_verify(fs, 9999, d1), 0);
	check_int("dir_block_csum_verify_bad_ino",
		  ext2fs_dir_block_csum_verify(fs, 9999, d1), 0);

	/* Directory block with neither tail nor DX header */
	d3->rec_len = ext2fs_cpu_to_le16(fs->blocksize - 24);
	check_int("dirent_csum_verify_no_tail_returns_1",
		  ext2fs_dirent_csum_verify(fs, dir_ino, d1), 1);
	check_int("dir_block_csum_verify_no_tail_returns_0",
		  ext2fs_dir_block_csum_verify(fs, dir_ino, d1), 0);
	check_err("dir_block_csum_set_no_tail",
		  ext2fs_dir_block_csum_set(fs, dir_ino, d1),
		  EXT2_ET_DIR_NO_SPACE_FOR_CSUM);

	fs->flags |= EXT2_FLAG_IGNORE_CSUM_ERRORS;
	check_err("dir_block_csum_set_no_tail_ignore",
		  ext2fs_dir_block_csum_set(fs, dir_ino, d1), 0);
	fs->flags &= ~EXT2_FLAG_IGNORE_CSUM_ERRORS;

	ext2fs_free_mem(&buf);
	teardown_fs(&fs);
	printf("test_dirent_tail_and_csum: completed\n");
}

/*
 * 6. HTree (DX) countlimit parsing and DX directory block checksums
 */
static void test_dx_countlimit_and_csum(void)
{
	ext2_filsys			fs = NULL;
	char				*buf = NULL;
	struct ext2_dir_entry		*d1, *d2;
	struct ext2_dx_root_info	*root;
	struct ext2_dx_countlimit	*c, *ret_c;
	struct ext2_dx_entry		*entries;
	struct ext2_dx_tail		*ret_tail;
	ext2_ino_t			dir_ino = 12;
	int				offset;
	__u32				crc;
	errcode_t			retval;

	setup_fs(&fs, 256, 32, 256, 64, EXT4_FEATURE_INCOMPAT_64BIT,
		 EXT4_FEATURE_RO_COMPAT_METADATA_CSUM);
	write_test_inode(fs, dir_ino, LINUX_S_IFDIR | 0755, 0xabcdef01U);

	retval = ext2fs_get_memzero(fs->blocksize, &buf);
	check_err("alloc_dx_buf", retval, 0);

	/*
	 * 6a. ext2fs_get_dx_countlimit() on Root DX block (host order rec_len,
	 *     LE16 c->limit and c->count; max_sane_entries = (1024-32)/8 = 124)
	 */
	memset(buf, 0, fs->blocksize);
	d1 = (struct ext2_dir_entry *) buf;
	d1->inode = dir_ino;
	d1->rec_len = 12;
	d1->name_len = 1;
	d1->name[0] = '.';

	d2 = (struct ext2_dir_entry *) (buf + 12);
	d2->inode = EXT2_ROOT_INO;
	d2->rec_len = fs->blocksize - 12;
	d2->name_len = 2;
	d2->name[0] = '.';
	d2->name[1] = '.';

	root = (struct ext2_dx_root_info *) (buf + 24);
	root->reserved_zero = 0;
	root->hash_version = EXT2_HASH_HALF_MD4;
	root->info_length = sizeof(struct ext2_dx_root_info);
	root->indirect_levels = 0;

	c = (struct ext2_dx_countlimit *) (buf + 32);
	c->limit = ext2fs_cpu_to_le16(123);
	c->count = ext2fs_cpu_to_le16(2);

	ret_c = NULL;
	offset = 0;
	retval = ext2fs_get_dx_countlimit(fs, d1, &ret_c, &offset);
	check_err("get_dx_countlimit_root_ok", retval, 0);
	check_int("get_dx_countlimit_root_offset", offset, 32);
	if (ret_c != c) {
		printf("FAIL [get_dx_countlimit_root_ptr]: got %p, expected %p\n",
		       (void *) ret_c, (void *) c);
		test_fail++;
	}
	check_err("get_dx_countlimit_root_null_out",
		  ext2fs_get_dx_countlimit(fs, d1, NULL, NULL), 0);

	/* Root DX validation errors */
	d2->rec_len = fs->blocksize - 16;
	check_err("dx_root_bad_d2_rec_len",
		  ext2fs_get_dx_countlimit(fs, d1, NULL, NULL),
		  EXT2_ET_DB_NOT_FOUND);
	d2->rec_len = fs->blocksize - 12;

	root->reserved_zero = 1;
	check_err("dx_root_bad_reserved_zero",
		  ext2fs_get_dx_countlimit(fs, d1, NULL, NULL),
		  EXT2_ET_DB_NOT_FOUND);
	root->reserved_zero = 0;

	root->info_length = 12;
	check_err("dx_root_bad_info_len",
		  ext2fs_get_dx_countlimit(fs, d1, NULL, NULL),
		  EXT2_ET_DB_NOT_FOUND);
	root->info_length = sizeof(struct ext2_dx_root_info);

	c->limit = ext2fs_cpu_to_le16(125);
	check_err("dx_root_limit_too_large",
		  ext2fs_get_dx_countlimit(fs, d1, NULL, NULL),
		  EXT2_ET_DIR_NO_SPACE_FOR_CSUM);
	c->limit = ext2fs_cpu_to_le16(123);

	c->count = ext2fs_cpu_to_le16(125);
	check_err("dx_root_count_too_large",
		  ext2fs_get_dx_countlimit(fs, d1, NULL, NULL),
		  EXT2_ET_DIR_NO_SPACE_FOR_CSUM);
	c->count = ext2fs_cpu_to_le16(2);

	/*
	 * 6b. ext2fs_get_dx_countlimit() on Interior DX node
	 *     (max_sane_entries = (1024-8)/8 = 127)
	 */
	memset(buf, 0, fs->blocksize);
	d1->rec_len = fs->blocksize;
	d1->name_len = 0;
	c = (struct ext2_dx_countlimit *) (buf + 8);
	c->limit = ext2fs_cpu_to_le16(126);
	c->count = ext2fs_cpu_to_le16(3);

	ret_c = NULL;
	offset = 0;
	retval = ext2fs_get_dx_countlimit(fs, d1, &ret_c, &offset);
	check_err("get_dx_countlimit_interior_ok", retval, 0);
	check_int("get_dx_countlimit_interior_offset", offset, 8);
	if (ret_c != c) {
		printf("FAIL [get_dx_countlimit_interior_ptr]: got %p != %p\n",
		       (void *) ret_c, (void *) c);
		test_fail++;
	}

	d1->name_len = 1;
	check_err("dx_interior_nonzero_name_len",
		  ext2fs_get_dx_countlimit(fs, d1, NULL, NULL),
		  EXT2_ET_DB_NOT_FOUND);
	d1->name_len = 0;

	d1->rec_len = 20;
	check_err("dx_neither_root_nor_interior",
		  ext2fs_get_dx_countlimit(fs, d1, NULL, NULL),
		  EXT2_ET_DB_NOT_FOUND);
	d1->rec_len = fs->blocksize;

	c->limit = ext2fs_cpu_to_le16(128);
	check_err("dx_interior_limit_too_large",
		  ext2fs_get_dx_countlimit(fs, d1, NULL, NULL),
		  EXT2_ET_DIR_NO_SPACE_FOR_CSUM);
	c->limit = ext2fs_cpu_to_le16(126);

	/*
	 * 6c. On-disk (LE) Root DX block checksums via ext2fs_dx_csum(),
	 *     ext2fs_dir_block_csum_set(), and ext2fs_dir_block_csum_verify()
	 */
	memset(buf, 0, fs->blocksize);
	d1 = (struct ext2_dir_entry *) buf;
	d1->inode = ext2fs_cpu_to_le32(dir_ino);
	d1->rec_len = ext2fs_cpu_to_le16(12);
	d1->name_len = ext2fs_cpu_to_le16(1);
	d1->name[0] = '.';

	d2 = (struct ext2_dir_entry *) (buf + 12);
	d2->inode = ext2fs_cpu_to_le32(EXT2_ROOT_INO);
	d2->rec_len = ext2fs_cpu_to_le16(fs->blocksize - 12);
	d2->name_len = ext2fs_cpu_to_le16(2);
	d2->name[0] = '.';
	d2->name[1] = '.';

	root = (struct ext2_dx_root_info *) (buf + 24);
	root->info_length = sizeof(struct ext2_dx_root_info);
	c = (struct ext2_dx_countlimit *) (buf + 32);
	c->limit = ext2fs_cpu_to_le16(123);
	c->count = ext2fs_cpu_to_le16(2);
	entries = (struct ext2_dx_entry *) c;
	entries[1].hash = ext2fs_cpu_to_le32(0x12345678U);
	entries[1].block = ext2fs_cpu_to_le32(2);

	retval = ext2fs_dir_block_csum_set(fs, dir_ino, d1);
	check_err("dx_root_csum_set", retval, 0);
	check_int("dx_root_csum_verify_ok",
		  ext2fs_dir_block_csum_verify(fs, dir_ino, d1), 1);

	ret_tail = NULL;
	crc = 0;
	retval = ext2fs_dx_csum(fs, dir_ino, d1, &crc, &ret_tail);
	check_err("dx_csum_direct_call", retval, 0);
	check_u32("dx_csum_matches_tail",
		  ext2fs_le32_to_cpu(ret_tail->dt_checksum), crc);
	check_err("dx_csum_null_ret_t",
		  ext2fs_dx_csum(fs, dir_ino, d1, &crc, NULL), 0);

	/* Modifying active entry (index 1 < count) invalidates checksum */
	entries[1].block = ext2fs_cpu_to_le32(3);
	check_int("dx_root_csum_active_entry_modified",
		  ext2fs_dir_block_csum_verify(fs, dir_ino, d1), 0);
	entries[1].block = ext2fs_cpu_to_le32(2);

	/* Modifying inactive entry (index 5 >= count) does NOT affect csum */
	entries[5].hash = ext2fs_cpu_to_le32(0xdeadbeefU);
	entries[5].block = ext2fs_cpu_to_le32(99);
	check_int("dx_root_csum_inactive_entry_ignored",
		  ext2fs_dir_block_csum_verify(fs, dir_ino, d1), 1);

	/* Modifying dt_reserved in ext2_dx_tail invalidates checksum */
	ret_tail->dt_reserved = ext2fs_cpu_to_le32(1);
	check_int("dx_root_csum_dt_reserved_modified",
		  ext2fs_dir_block_csum_verify(fs, dir_ino, d1), 0);
	ret_tail->dt_reserved = 0;

	/*
	 * 6d. On-disk (LE) Interior DX node checksums and boundary where
	 *     limit == 127 leaves no space for ext2_dx_tail
	 */
	memset(buf, 0, fs->blocksize);
	d1->rec_len = ext2fs_cpu_to_le16(fs->blocksize);
	d1->name_len = 0;
	c = (struct ext2_dx_countlimit *) (buf + 8);
	c->limit = ext2fs_cpu_to_le16(126);
	c->count = ext2fs_cpu_to_le16(2);
	entries = (struct ext2_dx_entry *) c;
	entries[1].hash = ext2fs_cpu_to_le32(0x87654321U);
	entries[1].block = ext2fs_cpu_to_le32(5);

	retval = ext2fs_dir_block_csum_set(fs, dir_ino, d1);
	check_err("dx_interior_csum_set", retval, 0);
	check_int("dx_interior_csum_verify_ok",
		  ext2fs_dir_block_csum_verify(fs, dir_ino, d1), 1);

	/* Invalid inode number on DX block */
	check_err("dx_csum_bad_ino",
		  ext2fs_dx_csum(fs, 9999, d1, &crc, NULL),
		  EXT2_ET_BAD_INODE_NUM);
	check_err("dx_dir_block_csum_set_bad_ino",
		  ext2fs_dir_block_csum_set(fs, 9999, d1),
		  EXT2_ET_BAD_INODE_NUM);
	check_int("dx_dir_block_csum_verify_bad_ino",
		  ext2fs_dir_block_csum_verify(fs, 9999, d1), 0);

	/*
	 * limit = 127 passes __get_dx_countlimit (127 <= max_sane_entries),
	 * but 8 + 127*8 = 1024 > 1024 - sizeof(ext2_dx_tail), so ext2fs_dx_csum
	 * rejects it with EXT2_ET_DIR_NO_SPACE_FOR_CSUM.
	 */
	c->limit = ext2fs_cpu_to_le16(127);
	check_err("dx_interior_no_space_for_tail_csum",
		  ext2fs_dx_csum(fs, dir_ino, d1, &crc, NULL),
		  EXT2_ET_DIR_NO_SPACE_FOR_CSUM);
	check_err("dx_interior_no_space_for_tail_set",
		  ext2fs_dir_block_csum_set(fs, dir_ino, d1),
		  EXT2_ET_DIR_NO_SPACE_FOR_CSUM);
	check_int("dx_interior_no_space_for_tail_verify",
		  ext2fs_dir_block_csum_verify(fs, dir_ino, d1), 0);

	ext2fs_free_mem(&buf);
	teardown_fs(&fs);
	printf("test_dx_countlimit_and_csum: completed\n");
}

/*
 * 7. Extent block checksums
 */
static void test_extent_block_csum(void)
{
	ext2_filsys			fs = NULL;
	char				*buf = NULL;
	struct ext3_extent_header	*eh;
	struct ext3_extent		*ext;
	struct ext3_extent_tail		*tail;
	ext2_ino_t			ino = 12;
	errcode_t			retval;

	setup_fs(&fs, 256, 32, 256, 64, EXT4_FEATURE_INCOMPAT_64BIT, 0);
	write_test_inode(fs, ino, LINUX_S_IFREG | 0644, 0x33445566U);

	retval = ext2fs_get_memzero(fs->blocksize, &buf);
	check_err("alloc_extent_buf", retval, 0);

	eh = (struct ext3_extent_header *) buf;
	eh->eh_magic = ext2fs_cpu_to_le16(EXT3_EXT_MAGIC);
	eh->eh_entries = ext2fs_cpu_to_le16(2);
	eh->eh_max = ext2fs_cpu_to_le16(4);
	eh->eh_depth = 0;

	ext = (struct ext3_extent *) (eh + 1);
	ext[0].ee_block = ext2fs_cpu_to_le32(0);
	ext[0].ee_len = ext2fs_cpu_to_le16(10);
	ext[0].ee_start = ext2fs_cpu_to_le32(100);
	ext[1].ee_block = ext2fs_cpu_to_le32(10);
	ext[1].ee_len = ext2fs_cpu_to_le16(5);
	ext[1].ee_start = ext2fs_cpu_to_le32(200);

	tail = get_extent_tail(eh);

	/* 7a. METADATA_CSUM disabled */
	tail->et_checksum = ext2fs_cpu_to_le32(0xdeadbeefU);
	retval = ext2fs_extent_block_csum_set(fs, ino, eh);
	check_err("extent_csum_set_disabled", retval, 0);
	check_u32("extent_csum_set_disabled_noop",
		  ext2fs_le32_to_cpu(tail->et_checksum), 0xdeadbeefU);
	check_int("extent_csum_verify_disabled",
		  ext2fs_extent_block_csum_verify(fs, ino, eh), 1);

	/* 7b. METADATA_CSUM enabled */
	fs->super->s_feature_ro_compat |= EXT4_FEATURE_RO_COMPAT_METADATA_CSUM;
	ext2fs_init_csum_seed(fs);

	retval = ext2fs_extent_block_csum_set(fs, ino, eh);
	check_err("extent_csum_set_enabled", retval, 0);
	check_int("extent_csum_verify_ok",
		  ext2fs_extent_block_csum_verify(fs, ino, eh), 1);

	/* Corrupt et_checksum and extent entry */
	tail->et_checksum ^= ext2fs_cpu_to_le32(1);
	check_int("extent_csum_verify_bad_csum",
		  ext2fs_extent_block_csum_verify(fs, ino, eh), 0);
	tail->et_checksum ^= ext2fs_cpu_to_le32(1);

	ext[1].ee_len = ext2fs_cpu_to_le16(6);
	check_int("extent_csum_verify_bad_entry",
		  ext2fs_extent_block_csum_verify(fs, ino, eh), 0);
	ext[1].ee_len = ext2fs_cpu_to_le16(5);

	/* Changing inode generation invalidates extent block checksum */
	write_test_inode(fs, ino, LINUX_S_IFREG | 0644, 0x778899aaU);
	check_int("extent_csum_verify_bad_gen",
		  ext2fs_extent_block_csum_verify(fs, ino, eh), 0);
	write_test_inode(fs, ino, LINUX_S_IFREG | 0644, 0x33445566U);
	check_int("extent_csum_verify_restored_gen",
		  ext2fs_extent_block_csum_verify(fs, ino, eh), 1);

	/* Invalid inode number */
	check_err("extent_csum_set_bad_ino",
		  ext2fs_extent_block_csum_set(fs, 9999, eh),
		  EXT2_ET_BAD_INODE_NUM);
	check_int("extent_csum_verify_bad_ino",
		  ext2fs_extent_block_csum_verify(fs, 9999, eh), 0);

	ext2fs_free_mem(&buf);
	teardown_fs(&fs);
	printf("test_extent_block_csum: completed\n");
}

/*
 * 8. Block and inode bitmap checksums (64-byte vs. 32-byte group descriptors)
 */
static void test_bitmap_csum(void)
{
	ext2_filsys		fs = NULL;
	struct ext4_group_desc	*gdp;
	unsigned char		bmap[128];
	__u32			exp_crc;
	unsigned int		i;
	errcode_t		retval;

	for (i = 0; i < sizeof(bmap); i++)
		bmap[i] = (unsigned char) (i * 7 + 3);

	/* 8a. 64-byte group descriptors (stores both lo and hi 16-bit halves) */
	setup_fs(&fs, 256, 32, 256, 64, EXT4_FEATURE_INCOMPAT_64BIT, 0);
	gdp = (struct ext4_group_desc *) ext2fs_group_desc(fs, fs->group_desc,
							   0);

	/* Disabled when METADATA_CSUM is not set */
	gdp->bg_inode_bitmap_csum_lo = 0x1111;
	gdp->bg_block_bitmap_csum_lo = 0x2222;
	check_err("ibmap_set_disabled",
		  ext2fs_inode_bitmap_csum_set(fs, 0, (char *) bmap, 32), 0);
	check_u32("ibmap_set_disabled_noop",
		  gdp->bg_inode_bitmap_csum_lo, 0x1111);
	check_int("ibmap_verify_disabled",
		  ext2fs_inode_bitmap_csum_verify(fs, 0, (char *) bmap, 32), 1);

	check_err("bbmap_set_disabled",
		  ext2fs_block_bitmap_csum_set(fs, 0, (char *) bmap, 128), 0);
	check_u32("bbmap_set_disabled_noop",
		  gdp->bg_block_bitmap_csum_lo, 0x2222);
	check_int("bbmap_verify_disabled",
		  ext2fs_block_bitmap_csum_verify(fs, 0, (char *) bmap, 128), 1);

	/* Enable METADATA_CSUM on 64-byte group descriptors */
	fs->super->s_feature_ro_compat |= EXT4_FEATURE_RO_COMPAT_METADATA_CSUM;
	ext2fs_init_csum_seed(fs);

	retval = ext2fs_inode_bitmap_csum_set(fs, 0, (char *) bmap, 32);
	check_err("ibmap_set_64b", retval, 0);
	exp_crc = ext2fs_crc32c_le(fs->csum_seed, bmap, 32);
	check_u32("ibmap_64b_lo", gdp->bg_inode_bitmap_csum_lo,
		  exp_crc & 0xFFFF);
	check_u32("ibmap_64b_hi", gdp->bg_inode_bitmap_csum_hi, exp_crc >> 16);
	check_int("ibmap_verify_64b_ok",
		  ext2fs_inode_bitmap_csum_verify(fs, 0, (char *) bmap, 32), 1);

	gdp->bg_inode_bitmap_csum_lo ^= 1;
	check_int("ibmap_verify_64b_bad_lo",
		  ext2fs_inode_bitmap_csum_verify(fs, 0, (char *) bmap, 32), 0);
	gdp->bg_inode_bitmap_csum_lo ^= 1;

	gdp->bg_inode_bitmap_csum_hi ^= 1;
	check_int("ibmap_verify_64b_bad_hi",
		  ext2fs_inode_bitmap_csum_verify(fs, 0, (char *) bmap, 32), 0);
	gdp->bg_inode_bitmap_csum_hi ^= 1;

	retval = ext2fs_block_bitmap_csum_set(fs, 0, (char *) bmap, 128);
	check_err("bbmap_set_64b", retval, 0);
	exp_crc = ext2fs_crc32c_le(fs->csum_seed, bmap, 128);
	check_u32("bbmap_64b_lo", gdp->bg_block_bitmap_csum_lo,
		  exp_crc & 0xFFFF);
	check_u32("bbmap_64b_hi", gdp->bg_block_bitmap_csum_hi, exp_crc >> 16);
	check_int("bbmap_verify_64b_ok",
		  ext2fs_block_bitmap_csum_verify(fs, 0, (char *) bmap, 128), 1);

	gdp->bg_block_bitmap_csum_lo ^= 1;
	check_int("bbmap_verify_64b_bad_lo",
		  ext2fs_block_bitmap_csum_verify(fs, 0, (char *) bmap, 128), 0);
	gdp->bg_block_bitmap_csum_lo ^= 1;

	gdp->bg_block_bitmap_csum_hi ^= 1;
	check_int("bbmap_verify_64b_bad_hi",
		  ext2fs_block_bitmap_csum_verify(fs, 0, (char *) bmap, 128), 0);
	gdp->bg_block_bitmap_csum_hi ^= 1;

	bmap[10] ^= 0x80;
	check_int("ibmap_verify_64b_bad_data",
		  ext2fs_inode_bitmap_csum_verify(fs, 0, (char *) bmap, 32), 0);
	check_int("bbmap_verify_64b_bad_data",
		  ext2fs_block_bitmap_csum_verify(fs, 0, (char *) bmap, 128), 0);
	bmap[10] ^= 0x80;

	/* 8b. 32-byte group descriptors (only low 16 bits used) */
	fs->super->s_desc_size = 32;
	gdp->bg_inode_bitmap_csum_hi = 0xaaaa;
	gdp->bg_block_bitmap_csum_hi = 0xbbbb;

	retval = ext2fs_inode_bitmap_csum_set(fs, 0, (char *) bmap, 32);
	check_err("ibmap_set_32b", retval, 0);
	check_u32("ibmap_32b_hi_untouched",
		  gdp->bg_inode_bitmap_csum_hi, 0xaaaa);
	check_int("ibmap_verify_32b_ok",
		  ext2fs_inode_bitmap_csum_verify(fs, 0, (char *) bmap, 32), 1);
	gdp->bg_inode_bitmap_csum_lo ^= 1;
	check_int("ibmap_verify_32b_bad_lo",
		  ext2fs_inode_bitmap_csum_verify(fs, 0, (char *) bmap, 32), 0);

	retval = ext2fs_block_bitmap_csum_set(fs, 0, (char *) bmap, 128);
	check_err("bbmap_set_32b", retval, 0);
	check_u32("bbmap_32b_hi_untouched",
		  gdp->bg_block_bitmap_csum_hi, 0xbbbb);
	check_int("bbmap_verify_32b_ok",
		  ext2fs_block_bitmap_csum_verify(fs, 0, (char *) bmap, 128), 1);
	gdp->bg_block_bitmap_csum_lo ^= 1;
	check_int("bbmap_verify_32b_bad_lo",
		  ext2fs_block_bitmap_csum_verify(fs, 0, (char *) bmap, 128), 0);

	teardown_fs(&fs);
	printf("test_bitmap_csum: completed\n");
}

/*
 * 9. Inode checksums (256-byte with/without hi csum, 128-byte, and all-zeros)
 */
static void test_inode_csum(void)
{
	ext2_filsys	fs = NULL;
	union {
		struct ext2_inode_large	inode;
		unsigned char		raw[256];
	} u;
	ext2_ino_t	ino = 12;
	__u16		saved_lo, saved_hi;
	errcode_t	retval;

	setup_fs(&fs, 256, 32, 256, 64, EXT4_FEATURE_INCOMPAT_64BIT, 0);

	memset(&u, 0, sizeof(u));
	u.inode.i_mode = ext2fs_cpu_to_le16(LINUX_S_IFREG | 0644);
	u.inode.i_uid = ext2fs_cpu_to_le16(1000);
	u.inode.i_size = ext2fs_cpu_to_le32(4096);
	u.inode.i_links_count = ext2fs_cpu_to_le16(1);
	u.inode.i_generation = ext2fs_cpu_to_le32(0x13579bdfU);
	u.inode.i_extra_isize = 32;
	u.raw[200] = 0x5a;

	/* 9a. METADATA_CSUM disabled */
	u.inode.i_checksum_lo = ext2fs_cpu_to_le16(0x1234);
	u.inode.i_checksum_hi = ext2fs_cpu_to_le16(0x5678);
	retval = ext2fs_inode_csum_set(fs, ino, &u.inode);
	check_err("inode_csum_set_disabled", retval, 0);
	check_u32("inode_csum_disabled_lo_noop",
		  ext2fs_le16_to_cpu(u.inode.i_checksum_lo), 0x1234);
	check_int("inode_csum_verify_disabled",
		  ext2fs_inode_csum_verify(fs, ino, &u.inode), 1);

	/* 9b. 256-byte inode with i_extra_isize >= EXT4_INODE_CSUM_HI_EXTRA_END */
	fs->super->s_feature_ro_compat |= EXT4_FEATURE_RO_COMPAT_METADATA_CSUM;
	ext2fs_init_csum_seed(fs);

	retval = ext2fs_inode_csum_set(fs, ino, &u.inode);
	check_err("inode_csum_set_256b_hi", retval, 0);
	saved_lo = u.inode.i_checksum_lo;
	saved_hi = u.inode.i_checksum_hi;
	check_int("inode_csum_verify_256b_hi_ok",
		  ext2fs_inode_csum_verify(fs, ino, &u.inode), 1);
	check_u32("inode_csum_lo_preserved", u.inode.i_checksum_lo, saved_lo);
	check_u32("inode_csum_hi_preserved", u.inode.i_checksum_hi, saved_hi);

	/* Corrupting lo, hi, base field, extra space, or ino fails verify */
	u.inode.i_checksum_lo ^= ext2fs_cpu_to_le16(1);
	check_int("inode_csum_verify_bad_lo",
		  ext2fs_inode_csum_verify(fs, ino, &u.inode), 0);
	u.inode.i_checksum_lo = saved_lo;

	u.inode.i_checksum_hi ^= ext2fs_cpu_to_le16(1);
	check_int("inode_csum_verify_bad_hi",
		  ext2fs_inode_csum_verify(fs, ino, &u.inode), 0);
	u.inode.i_checksum_hi = saved_hi;

	u.raw[200] ^= 1;
	check_int("inode_csum_verify_bad_extra_byte",
		  ext2fs_inode_csum_verify(fs, ino, &u.inode), 0);
	u.raw[200] ^= 1;

	check_int("inode_csum_verify_wrong_ino",
		  ext2fs_inode_csum_verify(fs, ino + 1, &u.inode), 0);

	/* 9c. 256-byte inode with i_extra_isize == 0 (< csum_hi offset 4) */
	u.inode.i_extra_isize = 0;
	u.inode.i_checksum_hi = ext2fs_cpu_to_le16(0xbeef);
	retval = ext2fs_inode_csum_set(fs, ino, &u.inode);
	check_err("inode_csum_set_small_extra", retval, 0);
	check_u32("inode_csum_small_extra_hi_untouched",
		  ext2fs_le16_to_cpu(u.inode.i_checksum_hi), 0xbeef);
	check_int("inode_csum_verify_small_extra_ok",
		  ext2fs_inode_csum_verify(fs, ino, &u.inode), 1);
	u.inode.i_checksum_lo ^= ext2fs_cpu_to_le16(1);
	check_int("inode_csum_verify_small_extra_bad_lo",
		  ext2fs_inode_csum_verify(fs, ino, &u.inode), 0);

	/* 9d. 128-byte EXT2_GOOD_OLD_INODE_SIZE inode */
	fs->super->s_inode_size = EXT2_GOOD_OLD_INODE_SIZE;
	retval = ext2fs_inode_csum_set(fs, ino, &u.inode);
	check_err("inode_csum_set_128b", retval, 0);
	check_int("inode_csum_verify_128b_ok",
		  ext2fs_inode_csum_verify(fs, ino, &u.inode), 1);
	u.inode.i_checksum_lo ^= ext2fs_cpu_to_le16(1);
	check_int("inode_csum_verify_128b_bad_lo",
		  ext2fs_inode_csum_verify(fs, ino, &u.inode), 0);
	fs->super->s_inode_size = 256;

	/* 9e. All-zero inode special case in ext2fs_inode_csum_verify() */
	memset(&u, 0, sizeof(u));
	check_int("inode_csum_verify_all_zeros",
		  ext2fs_inode_csum_verify(fs, ino, &u.inode), 1);
	/* Non-zero byte in base inode with zero checksum fails verification */
	u.inode.i_links_count = ext2fs_cpu_to_le16(1);
	check_int("inode_csum_verify_nonzero_with_zero_csum",
		  ext2fs_inode_csum_verify(fs, ino, &u.inode), 0);

	teardown_fs(&fs);
	printf("test_inode_csum: completed\n");
}

/*
 * 10. METADATA_CSUM group descriptor checksums and ext2fs_set_gdt_csum()
 */
static void test_metadata_gdt_and_set_gdt_csum(void)
{
	ext2_filsys		fs = NULL;
	ext2fs_inode_bitmap	saved_imap;
	dgrp_t			grp;
	ext2_ino_t		ino;
	__u16			csum;
	errcode_t		retval;

	/* 3 block groups: 3 * 8192 = 24576 blocks, 3 * 64 = 192 inodes */
	setup_fs(&fs, 24576, 192, 256, 64, EXT4_FEATURE_INCOMPAT_64BIT,
		 EXT4_FEATURE_RO_COMPAT_METADATA_CSUM);
	check_u32("multi_grp_count", fs->group_desc_count, 3);

	/* 10a. METADATA_CSUM group_desc_csum / set / verify */
	ext2fs_group_desc_csum_set(fs, 0);
	csum = ext2fs_bg_checksum(fs, 0);
	check_u32("meta_gdt_csum_preserved",
		  ext2fs_group_desc_csum(fs, 0), csum);
	check_int("meta_gdt_csum_verify_ok",
		  ext2fs_group_desc_csum_verify(fs, 0), 1);
	ext2fs_bg_checksum_set(fs, 0, csum ^ 1);
	check_int("meta_gdt_csum_verify_bad",
		  ext2fs_group_desc_csum_verify(fs, 0), 0);

	/* 10b. ext2fs_set_gdt_csum() with missing inode_map */
	saved_imap = fs->inode_map;
	fs->inode_map = NULL;
	check_err("set_gdt_csum_no_inode_map",
		  ext2fs_set_gdt_csum(fs), EXT2_ET_NO_INODE_BITMAP);
	fs->inode_map = saved_imap;

	/* 10c. ext2fs_set_gdt_csum() when group desc csums are disabled */
	fs->super->s_feature_ro_compat = 0;
	fs->flags &= ~EXT2_FLAG_DIRTY;
	check_err("set_gdt_csum_disabled", ext2fs_set_gdt_csum(fs), 0);
	check_int("set_gdt_csum_disabled_not_dirty",
		  (fs->flags & EXT2_FLAG_DIRTY) != 0, 0);
	fs->super->s_feature_ro_compat = EXT4_FEATURE_RO_COMPAT_METADATA_CSUM;

	/*
	 * 10d. Configure 3 groups to exercise all branches of ext2fs_set_gdt_csum:
	 * - Group 0 (non-last): partially used blocks & inodes; highest allocated
	 *   inode in group 0 is 20 -> unused = 64 - 20 = 44; clears INODE_UNINIT.
	 * - Group 1 (non-last): all blocks free & all inodes free -> sets both
	 *   BLOCK_UNINIT and INODE_UNINIT, unused = 64.
	 * - Group 2 (last group): all blocks free (must NOT set BLOCK_UNINIT on
	 *   last group), free_inodes_count < 64 but no bit set in inode_map
	 *   (find_last_inode_ingrp returns 64 -> unused = 0).
	 */
	ext2fs_clear_inode_bitmap(fs->inode_map);

	/* Group 0 */
	ext2fs_bg_free_blocks_count_set(fs, 0,
					fs->super->s_blocks_per_group - 10);
	ext2fs_bg_free_inodes_count_set(fs, 0,
					fs->super->s_inodes_per_group - 5);
	ext2fs_bg_flags_zap(fs, 0);
	ext2fs_bg_flags_set(fs, 0, EXT2_BG_INODE_UNINIT);
	for (ino = 1; ino <= 11; ino++)
		ext2fs_mark_inode_bitmap2(fs->inode_map, ino);
	ext2fs_mark_inode_bitmap2(fs->inode_map, 20);

	/* Group 1 (non-last group) */
	ext2fs_bg_free_blocks_count_set(fs, 1, fs->super->s_blocks_per_group);
	ext2fs_bg_free_inodes_count_set(fs, 1, fs->super->s_inodes_per_group);
	ext2fs_bg_flags_zap(fs, 1);

	/* Group 2 (last group) */
	ext2fs_bg_free_blocks_count_set(fs, 2, fs->super->s_blocks_per_group);
	ext2fs_bg_free_inodes_count_set(fs, 2,
					fs->super->s_inodes_per_group - 1);
	ext2fs_bg_flags_zap(fs, 2);
	ext2fs_bg_flags_set(fs, 2, EXT2_BG_INODE_UNINIT);

	fs->flags &= ~EXT2_FLAG_DIRTY;
	retval = ext2fs_set_gdt_csum(fs);
	check_err("set_gdt_csum_active", retval, 0);
	check_int("set_gdt_csum_marks_dirty",
		  (fs->flags & EXT2_FLAG_DIRTY) != 0, 1);

	/* Verify Group 0 */
	check_int("grp0_no_block_uninit",
		  ext2fs_bg_flags_test(fs, 0, EXT2_BG_BLOCK_UNINIT), 0);
	check_int("grp0_cleared_inode_uninit",
		  ext2fs_bg_flags_test(fs, 0, EXT2_BG_INODE_UNINIT), 0);
	check_u32("grp0_itable_unused", ext2fs_bg_itable_unused(fs, 0), 44);

	/* Verify Group 1 */
	check_int("grp1_has_block_uninit",
		  ext2fs_bg_flags_test(fs, 1, EXT2_BG_BLOCK_UNINIT) != 0, 1);
	check_int("grp1_has_inode_uninit",
		  ext2fs_bg_flags_test(fs, 1, EXT2_BG_INODE_UNINIT) != 0, 1);
	check_u32("grp1_itable_unused", ext2fs_bg_itable_unused(fs, 1), 64);

	/* Verify Group 2 (last group) */
	check_int("grp2_last_no_block_uninit",
		  ext2fs_bg_flags_test(fs, 2, EXT2_BG_BLOCK_UNINIT), 0);
	check_int("grp2_cleared_inode_uninit",
		  ext2fs_bg_flags_test(fs, 2, EXT2_BG_INODE_UNINIT), 0);
	check_u32("grp2_itable_unused_empty_bmap",
		  ext2fs_bg_itable_unused(fs, 2), 0);

	for (grp = 0; grp < fs->group_desc_count; grp++)
		check_int("grp_csum_valid",
			  ext2fs_group_desc_csum_verify(fs, grp), 1);

	/* 10e. Second call with no changes does not mark superblock dirty */
	fs->flags &= ~EXT2_FLAG_DIRTY;
	retval = ext2fs_set_gdt_csum(fs);
	check_err("set_gdt_csum_idempotent", retval, 0);
	check_int("set_gdt_csum_idempotent_not_dirty",
		  (fs->flags & EXT2_FLAG_DIRTY) != 0, 0);

	teardown_fs(&fs);
	printf("test_metadata_gdt_and_set_gdt_csum: completed\n");
}

int main(int argc EXT2FS_ATTR((unused)), char **argv EXT2FS_ATTR((unused)))
{
	add_error_table(&et_ext2_error_table);

	test_legacy_gdt_csum();
	test_init_csum_seed();
	test_mmp_and_superblock_csum();
	test_ext_attr_block_csum();
	test_dirent_tail_and_csum();
	test_dx_countlimit_and_csum();
	test_extent_block_csum();
	test_bitmap_csum();
	test_inode_csum();
	test_metadata_gdt_and_set_gdt_csum();

	if (test_fail) {
		printf("tst_csum: %d failure(s)\n", test_fail);
		return 1;
	}
	printf("tst_csum: all tests passed\n");
	return 0;
}
#endif
