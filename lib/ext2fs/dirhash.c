/*
 * dirhash.c -- Calculate the hash of a directory entry
 *
 * Copyright (c) 2001  Daniel Phillips
 * 
 * Copyright (c) 2002 Theodore Ts'o.
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 */

#include <stdio.h>

#include "ext2_fs.h"
#include "ext2fs.h"

static ext2_dirhash_t dx_hack_hash (const char *name, int len)
{
	__u32 hash0 = 0x12a3fe2d, hash1 = 0x37abe8f9;
	while (len--) {
		__u32 hash = hash1 + (hash0 ^ (*name++ * 7152373));
		
		if (hash & 0x80000000) hash -= 0x7fffffff;
		hash1 = hash0;
		hash0 = hash;
	}
	return hash0;
}

/*
 * Returns the hash of a filename.  If len is 0 and name is NULL, then
 * this function can be used to test whether or not a hash version is
 * supported.
 */
errcode_t ext2fs_dirhash(int version, const char *name, int len,
			 ext2_dirhash_t *ret_hash)
{
	__u32	hash;

	if (version == 0)
		hash = dx_hack_hash(name, len);
	else {
		*ret_hash = 0;
		return EXT2_ET_DIRHASH_UNSUPP;
	}
	*ret_hash = hash;
	return 0;
		
}
