/*
 * ext_attr.c --- extended attribute blocks
 *
 * Copyright (C) 2001 Andreas Gruenbacher, <a.gruenbacher@computer.org>
 *
 * Copyright (C) 2002 Theodore Ts'o.
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Library
 * General Public License, version 2.
 * %End-Header%
 */

#include "config.h"
#include <stdio.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <string.h>
#include <time.h>

#include "ext2_fs.h"
#include "ext2_ext_attr.h"

#include "ext2fs.h"

#define NAME_HASH_SHIFT 5
#define VALUE_HASH_SHIFT 16

/*
 * ext2_xattr_hash_entry()
 *
 * Compute the hash of an extended attribute.
 */
__u32 ext2fs_ext_attr_hash_entry(struct ext2_ext_attr_entry *entry, void *data)
{
	__u32 hash = 0;
	char *name = ((char *) entry) + sizeof(struct ext2_ext_attr_entry);
	int n;

	for (n = 0; n < entry->e_name_len; n++) {
		hash = (hash << NAME_HASH_SHIFT) ^
		       (hash >> (8*sizeof(hash) - NAME_HASH_SHIFT)) ^
		       *name++;
	}

	/* The hash needs to be calculated on the data in little-endian. */
	if (entry->e_value_block == 0 && entry->e_value_size != 0) {
		__u32 *value = (__u32 *)data;
		for (n = (entry->e_value_size + EXT2_EXT_ATTR_ROUND) >>
			 EXT2_EXT_ATTR_PAD_BITS; n; n--) {
			hash = (hash << VALUE_HASH_SHIFT) ^
			       (hash >> (8*sizeof(hash) - VALUE_HASH_SHIFT)) ^
			       ext2fs_le32_to_cpu(*value++);
		}
	}

	return hash;
}

#undef NAME_HASH_SHIFT
#undef VALUE_HASH_SHIFT

errcode_t ext2fs_read_ext_attr3(ext2_filsys fs, blk64_t block, void *buf,
				ext2_ino_t inum)
{
	errcode_t	retval;

	retval = io_channel_read_blk64(fs->io, block, 1, buf);
	if (retval)
		return retval;

	if (!(fs->flags & EXT2_FLAG_IGNORE_CSUM_ERRORS) &&
	    !ext2fs_ext_attr_block_csum_verify(fs, inum, block, buf))
		retval = EXT2_ET_EXT_ATTR_CSUM_INVALID;

#ifdef WORDS_BIGENDIAN
	ext2fs_swap_ext_attr(buf, buf, fs->blocksize, 1);
#endif

	return retval;
}

errcode_t ext2fs_read_ext_attr2(ext2_filsys fs, blk64_t block, void *buf)
{
	return ext2fs_read_ext_attr3(fs, block, buf, 0);
}

errcode_t ext2fs_read_ext_attr(ext2_filsys fs, blk_t block, void *buf)
{
	return ext2fs_read_ext_attr2(fs, block, buf);
}

errcode_t ext2fs_write_ext_attr3(ext2_filsys fs, blk64_t block, void *inbuf,
				 ext2_ino_t inum)
{
	errcode_t	retval;
	char		*write_buf;

#ifdef WORDS_BIGENDIAN
	retval = ext2fs_get_mem(fs->blocksize, &write_buf);
	if (retval)
		return retval;
	ext2fs_swap_ext_attr(write_buf, inbuf, fs->blocksize, 1);
#else
	write_buf = (char *) inbuf;
#endif

	retval = ext2fs_ext_attr_block_csum_set(fs, inum, block,
			(struct ext2_ext_attr_header *)write_buf);
	if (retval)
		return retval;

	retval = io_channel_write_blk64(fs->io, block, 1, write_buf);
#ifdef WORDS_BIGENDIAN
	ext2fs_free_mem(&write_buf);
#endif
	if (!retval)
		ext2fs_mark_changed(fs);
	return retval;
}

errcode_t ext2fs_write_ext_attr2(ext2_filsys fs, blk64_t block, void *inbuf)
{
	return ext2fs_write_ext_attr3(fs, block, inbuf, 0);
}

errcode_t ext2fs_write_ext_attr(ext2_filsys fs, blk_t block, void *inbuf)
{
	return ext2fs_write_ext_attr2(fs, block, inbuf);
}

/*
 * This function adjusts the reference count of the EA block.
 */
errcode_t ext2fs_adjust_ea_refcount3(ext2_filsys fs, blk64_t blk,
				    char *block_buf, int adjust,
				    __u32 *newcount, ext2_ino_t inum)
{
	errcode_t	retval;
	struct ext2_ext_attr_header *header;
	char	*buf = 0;

	if ((blk >= ext2fs_blocks_count(fs->super)) ||
	    (blk < fs->super->s_first_data_block))
		return EXT2_ET_BAD_EA_BLOCK_NUM;

	if (!block_buf) {
		retval = ext2fs_get_mem(fs->blocksize, &buf);
		if (retval)
			return retval;
		block_buf = buf;
	}

	retval = ext2fs_read_ext_attr3(fs, blk, block_buf, inum);
	if (retval)
		goto errout;

	header = (struct ext2_ext_attr_header *) block_buf;
	header->h_refcount += adjust;
	if (newcount)
		*newcount = header->h_refcount;

	retval = ext2fs_write_ext_attr3(fs, blk, block_buf, inum);
	if (retval)
		goto errout;

errout:
	if (buf)
		ext2fs_free_mem(&buf);
	return retval;
}

errcode_t ext2fs_adjust_ea_refcount2(ext2_filsys fs, blk64_t blk,
				    char *block_buf, int adjust,
				    __u32 *newcount)
{
	return ext2fs_adjust_ea_refcount3(fs, blk, block_buf, adjust,
					  newcount, 0);
}

errcode_t ext2fs_adjust_ea_refcount(ext2_filsys fs, blk_t blk,
					char *block_buf, int adjust,
					__u32 *newcount)
{
	return ext2fs_adjust_ea_refcount2(fs, blk, block_buf, adjust,
					  newcount);
}
