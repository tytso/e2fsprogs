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

/* F, G and H are basic MD4 functions: selection, majority, parity */
#define F(x, y, z) ((z) ^ ((x) & ((y) ^ (z))))
#define G(x, y, z) (((x) & (y)) + (((x) ^ (y)) & (z)))
#define H(x, y, z) ((x) ^ (y) ^ (z))

/*
 * The generic round function.  The application is so specific that
 * we don't bother protecting all the arguments with parens, as is generally
 * good macro practice, in favor of extra legibility.
 * Rotation is separate from addition to prevent recomputation
 */
#define ROUND(f, a, b, c, d, x, s)	\
	(a += f(b, c, d) + x, a = (a << s) | (a >> (32-s)))
#define K1 0
#define K2 013240474631UL
#define K3 015666365641UL

/*
 * Basic cut-down MD4 transform.  Returns only 32 bits of result.
 */
static __u32 halfMD4Transform (__u32 buf[4], __u32 const in[])
{
	__u32	a = buf[0], b = buf[1], c = buf[2], d = buf[3];

	/* Round 1 */
	ROUND(F, a, b, c, d, in[0] + K1,  3);
	ROUND(F, d, a, b, c, in[1] + K1,  7);
	ROUND(F, c, d, a, b, in[2] + K1, 11);
	ROUND(F, b, c, d, a, in[3] + K1, 19);
	ROUND(F, a, b, c, d, in[4] + K1,  3);
	ROUND(F, d, a, b, c, in[5] + K1,  7);
	ROUND(F, c, d, a, b, in[6] + K1, 11);
	ROUND(F, b, c, d, a, in[7] + K1, 19);

	/* Round 2 */
	ROUND(G, a, b, c, d, in[1] + K2,  3);
	ROUND(G, d, a, b, c, in[3] + K2,  5);
	ROUND(G, c, d, a, b, in[5] + K2,  9);
	ROUND(G, b, c, d, a, in[7] + K2, 13);
	ROUND(G, a, b, c, d, in[0] + K2,  3);
	ROUND(G, d, a, b, c, in[2] + K2,  5);
	ROUND(G, c, d, a, b, in[4] + K2,  9);
	ROUND(G, b, c, d, a, in[6] + K2, 13);

	/* Round 3 */
	ROUND(H, a, b, c, d, in[3] + K3,  3);
	ROUND(H, d, a, b, c, in[7] + K3,  9);
	ROUND(H, c, d, a, b, in[2] + K3, 11);
	ROUND(H, b, c, d, a, in[6] + K3, 15);
	ROUND(H, a, b, c, d, in[1] + K3,  3);
	ROUND(H, d, a, b, c, in[5] + K3,  9);
	ROUND(H, c, d, a, b, in[0] + K3, 11);
	ROUND(H, b, c, d, a, in[4] + K3, 15);

	buf[0] += a;
	buf[1] += b;
	buf[2] += c;
	buf[3] += d;

	return buf[1];	/* "most hashed" word */
	/* Alternative: return sum of all words? */
}

#undef ROUND
#undef F
#undef G
#undef H
#undef K1
#undef K2
#undef K3

/* The old legacy hash */
static ext2_dirhash_t dx_hack_hash (const char *name, int len)
{
	__u32 hash0 = 0x12a3fe2d, hash1 = 0x37abe8f9;
	while (len--) {
		__u32 hash = hash1 + (hash0 ^ (*name++ * 7152373));
		
		if (hash & 0x80000000) hash -= 0x7fffffff;
		hash1 = hash0;
		hash0 = hash;
	}
	return (hash0 << 1);
}

/*
 * Returns the hash of a filename.  If len is 0 and name is NULL, then
 * this function can be used to test whether or not a hash version is
 * supported.
 * 
 * The seed is an 4 longword (32 bits) "secret" which can be used to
 * uniquify a hash.  If the seed is all zero's, then some default seed
 * may be used.
 * 
 * A particular hash version specifies whether or not the seed is
 * represented, and whether or not the returned hash is 32 bits or 64
 * bits.  32 bit hashes will return 0 for the minor hash.
 */
errcode_t ext2fs_dirhash(int version, const char *name, int len,
			 const __u32 seed[4],
			 ext2_dirhash_t *ret_hash,
			 ext2_dirhash_t *ret_minor_hash)
{
	__u32	hash;
	__u32	minor_hash = 0;
	char	*p;
	int	i;

	/* Check to see if the seed is all zero's */
	for (i=0; i < 4; i++) {
		if (seed[i])
			break;
	}
	
	if (version == EXT2_HASH_LEGACY)
		hash = dx_hack_hash(name, len);
	else if ((version == EXT2_HASH_HALF_MD4) ||
		 (version == EXT2_HASH_HALF_MD4_SEED) ||
		 (version == EXT2_HASH_HALF_MD4_64)) {
		char in[32];
		__u32 buf[4];

		if ((i == 4) || (version == EXT2_HASH_HALF_MD4)) {
			buf[0] = 0x67452301;
			buf[1] = 0xefcdab89;
			buf[2] = 0x98badcfe;
			buf[3] = 0x10325476;
		} else
			memcpy(buf, in, sizeof(buf));
		while (len) {
			if (len < 32) {
				memcpy(in, name, len);
				memset(in+len, 0, 32-len);
				hash = halfMD4Transform(buf, (__u32 *) in);
				break;
			}
			hash = halfMD4Transform(buf, (__u32 *) p);
			len -= 32;
			p += 32;
		}
		if (version == EXT2_HASH_HALF_MD4_64)
			minor_hash = buf[2];
	} else {
		*ret_hash = 0;
		return EXT2_ET_DIRHASH_UNSUPP;
	}
	*ret_hash = hash;
	if (ret_minor_hash)
		*ret_minor_hash = minor_hash;
	return 0;
		
}



