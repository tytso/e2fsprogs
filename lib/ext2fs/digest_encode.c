/*
 * lib/ext2fs/digest_encode.c
 *
 * A function to encode a digest using 64 characters that are valid in a
 * filename per ext2fs rules.
 *
 * Written by Uday Savagaonkar, 2014.
 *
 * Copyright 2014 Google Inc.  All Rights Reserved.
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
#include "ext2fs.h"

/**
 * ext2fs_digest_encode() -
 *
 * Encodes the input digest using characters from the set [a-zA-Z0-9_+].
 * The encoded string is roughly 4/3 times the size of the input string.
 */
int ext2fs_digest_encode(const char *src, unsigned long len, char *dst)
{
	static const char *lookup_table =
		"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_+";
	unsigned num_chunks, i;
	char tmp_buf[3];
	unsigned c0, c1, c2, c3;

	num_chunks = len/3;
	for (i = 0; i < num_chunks; i++) {
		c0 = src[3*i] & 0x3f;
		c1 = (((src[3*i]>>6)&0x3) | ((src[3*i+1] & 0xf)<<2)) & 0x3f;
		c2 = (((src[3*i+1]>>4)&0xf) | ((src[3*i+2] & 0x3)<<4)) & 0x3f;
		c3 = (src[3*i+2]>>2) & 0x3f;
		dst[4*i] = lookup_table[c0];
		dst[4*i+1] = lookup_table[c1];
		dst[4*i+2] = lookup_table[c2];
		dst[4*i+3] = lookup_table[c3];
	}
	if (i*3 < len) {
		memset(tmp_buf, 0, 3);
		memcpy(tmp_buf, &src[3*i], len-3*i);
		c0 = tmp_buf[0] & 0x3f;
		c1 = (((tmp_buf[0]>>6)&0x3) | ((tmp_buf[1] & 0xf)<<2)) & 0x3f;
		c2 = (((tmp_buf[1]>>4)&0xf) | ((tmp_buf[2] & 0x3)<<4)) & 0x3f;
		c3 = (tmp_buf[2]>>2) & 0x3f;
		dst[4*i] = lookup_table[c0];
		dst[4*i+1] = lookup_table[c1];
		dst[4*i+2] = lookup_table[c2];
		dst[4*i+3] = lookup_table[c3];
		i++;
	}
	return (i * 4);
}

#ifdef UNITTEST
static const struct {
	unsigned char d[32];
	unsigned int len;
	const unsigned char *ed;
} tests[] = {
	{ { 0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14,
	    0x9a, 0xfb, 0xf4, 0xc8, 0x99, 0x6f, 0xb9, 0x24,
	    0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c,
	    0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55 }, 32,
	"JdlXcHj+CqHM7tpYz_wUKCIRbrozBojtKwzMBGNu4wfa"
	},
	{ { 0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
	    0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
	    0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
	    0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad }, 32,
	"6INf+_yapREqbbK3D5QiJa7aHnQLxOhN0cX+Hjpav0ka"
	},
	{ { 0x24, 0x8d, 0x6a, 0x61, 0xd2, 0x06, 0x38, 0xb8,
	    0xe5, 0xc0, 0x26, 0x93, 0x0c, 0x3e, 0x60, 0x39,
	    0xa3, 0x3c, 0xe4, 0x59, 0x64, 0xff, 0x21, 0x67,
	    0xf6, 0xec, 0xed, 0xd4, 0x19, 0xdb, 0x06, 0xc1 }, 32,
	"K0OAHjTb4GB5aBYKm4dy5mkpKNfz+hYz2ZE7uNX2gema"
	},
	{ { 0x00, }, 1,
	"aaaa"
	},
	{ { 0x01, }, 1,
	"baaa"
	},
	{ { 0x01, 0x02 }, 2,
	"biaa"
	},
	{ { 0x01, 0x02, 0x03 }, 3,
	"biWa"
	},
	{ { 0x01, 0x02, 0x03, 0x04 }, 4,
	"biWaeaaa"
	},
	{ { 0x01, 0x02, 0x03, 0x04, 0xff }, 5,
	"biWae8pa"
	},
	{ { 0x01, 0x02, 0x03, 0x04, 0xff, 0xfe }, 6,
	"biWae8V+"
	},
	{ { 0x01, 0x02, 0x03, 0x04, 0xff, 0xfe, 0xfd }, 7,
	"biWae8V+9daa"
	},
};

int main(int argc, char **argv)
{
	int i, ret, len;
	int errors = 0;
	unsigned char tmp[1024];

	for (i = 0; i < (int)(sizeof(tests) / sizeof(tests[0])); i++) {
		memset(tmp, 0, sizeof(tmp));
		ret = ext2fs_digest_encode(tests[i].d, tests[i].len, tmp);
		len = strlen(tmp);
		printf("Test Digest %d (returned %d): ", i, ret);
		if (ret != len) {
			printf("FAILED returned %d, string length was %d\n",
			       ret, len);
			errors++;
		} else if (memcmp(tmp, tests[i].ed, ret) != 0) {
			printf("FAILED: got %s, expected %s\n", tmp,
			       tests[i].ed);
			errors++;
		} else
			printf("OK\n");
	}
	for (i = 1; i < argc; i++) {
		memset(tmp, 0, sizeof(tmp));
		ret = ext2fs_digest_encode(argv[i], strlen(argv[i]), tmp);
		len = strlen(tmp);
		printf("Digest of '%s' is '%s' (returned %d, length %d)\n",
		       argv[i], tmp, ret, len);
	}
	return errors;
}

#endif /* UNITTEST */
