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

#include "nls.h"
#include "utf8n.h"

#include <limits.h>
#include <errno.h>

static int utf8_casefold(const struct nls_table *table,
			  const unsigned char *str, size_t len,
			  unsigned char *dest, size_t dlen)
{
	const struct utf8data *data = utf8nfkdicf(table->version);
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

static int utf8_normalize(const struct nls_table *table,
			  const unsigned char *str, size_t len,
			  unsigned char *dest, size_t dlen)
{
	const struct utf8data *data = utf8nfkdi(table->version);
	struct utf8cursor cur;
	ssize_t nlen = 0;

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

const static struct nls_ops utf8_ops = {
	.casefold = utf8_casefold,
	.normalize = utf8_normalize,

};

const struct nls_table nls_utf8_11_0 = {
	.ops = &utf8_ops,
	.version = UNICODE_AGE(11, 0, 0),
};
