/*
 * encoding.c --- convert between encoding magic numbers and strings
 *
 * Copyright (C) 2018  Collabora Ltd.
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
#include <ctype.h>
#include <errno.h>
#include <stdio.h>

#include "e2p.h"

#define ARRAY_SIZE(array)			\
        (sizeof(array) / sizeof(array[0]))

static const struct {
	char *name;
	__u16 encoding_magic;
	__u16 default_flags;

} ext4_encoding_map[] = {
	{
		.encoding_magic = EXT4_ENC_ASCII,
		.name = "ascii",
		.default_flags = 0
	},
	{
		.encoding_magic = EXT4_ENC_UTF8_11_0,
		.name = "utf8",
		.default_flags = (EXT4_UTF8_NORMALIZATION_TYPE_NFKD |
				  EXT4_UTF8_CASEFOLD_TYPE_NFKDCF)
	},
};

static const struct enc_flags {
	__u16 flag;
	char *param;
} encoding_flags[] = {
	{ EXT4_ENC_STRICT_MODE_FL, "strict" },
};

/* Return a positive number < 0xff indicating the encoding magic number
 * or a negative value indicating error. */
int e2p_str2encoding(const char *string)
{
	int i;

	for (i = 0 ; i < ARRAY_SIZE(ext4_encoding_map); i++)
		if (!strcmp(string, ext4_encoding_map[i].name))
			return ext4_encoding_map[i].encoding_magic;

	return -EINVAL;
}

int e2p_get_encoding_flags(int encoding)
{
	int i;

	for (i = 0 ; i < ARRAY_SIZE(ext4_encoding_map); i++)
		if (ext4_encoding_map[i].encoding_magic == encoding)
			return ext4_encoding_map[encoding].default_flags;

	return 0;
}

int e2p_str2encoding_flags(int encoding, char *param, __u16 *flags)
{
	char *f = strtok(param, "-");
	const struct enc_flags *fl;
	int i, neg = 0;

	while (f) {
		neg = 0;
		if (!strncmp("no", f, 2)) {
			neg = 1;
			f += 2;
		}

		for (i = 0; i < ARRAY_SIZE(encoding_flags); i++) {
			fl = &encoding_flags[i];
			if (!strcmp(fl->param, f)) {
				if (neg)
					*flags &= ~fl->flag;
				else
					*flags |= fl->flag;

				goto next_flag;
			}
		}
		return -EINVAL;
	next_flag:
		f = strtok(NULL, "-");
	}
	return 0;
}
