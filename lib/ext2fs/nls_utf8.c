/*
 * Copyright (c) 2018 Collabora Ltd.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

/*
 * This code is adapted from the Linux Kernel.  We have a
 * userspace version here such that the hashes will match that
 * implementation.
 */

#include "config.h"
#include "ext2_fs.h"
#include "ext2fs.h"

#include "utf8n.h"

#include <limits.h>
#include <errno.h>

static int utf8_casefold(const struct ext2fs_nls_table *table,
			  const unsigned char *str, size_t len,
			  unsigned char *dest, size_t dlen)
{
	const struct utf8data *data = utf8nfdicf(table->version);
	struct utf8cursor cur;
	size_t nlen = 0;

	if (utf8ncursor(&cur, data, str, len) < 0)
		goto invalid_seq;

	for (nlen = 0; nlen < dlen; nlen++) {
		dest[nlen] = utf8byte(&cur);
		if (!dest[nlen])
			return nlen;
		if (dest[nlen] == -1)
			break;
	}

	return -ENAMETOOLONG;

invalid_seq:
	if (dlen < len)
		return -ENAMETOOLONG;

	/* Signal invalid sequence */
	return -EINVAL;
}

const static struct ext2fs_nls_ops utf8_ops = {
	.casefold = utf8_casefold,
};

static const struct ext2fs_nls_table nls_utf8 = {
	.ops = &utf8_ops,
	.version = UNICODE_AGE(12, 1, 0),
};

const struct ext2fs_nls_table *ext2fs_load_nls_table(int encoding)
{
	int i;

	if (encoding == EXT4_ENC_UTF8_12_1)
		return &nls_utf8;

	return NULL;
}
