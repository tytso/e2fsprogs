/*
 * nls.h - Header for encoding support functions
 *
 * Copyright (C) 2017 Collabora Ltd.
 *
 *
 * This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or (at
 *  your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef EXT2FS_NLS_H
#define EXT2FS_NLS_H

#include <unistd.h>
#include <string.h>
#include <stdio.h>

#include "ext2_fs.h"

struct nls_table;

#define ARRAY_SIZE(array)			\
        (sizeof(array) / sizeof(array[0]))

struct nls_ops {
	int (*normalize)(const struct nls_table *charset,
			 const unsigned char *str, size_t len,
			 unsigned char *dest, size_t dlen);

	int (*casefold)(const struct nls_table *charset,
			const unsigned char *str, size_t len,
			unsigned char *dest, size_t dlen);
};

struct nls_table {
	int version;
	const struct nls_ops *ops;
};

extern const struct nls_table nls_ascii;
extern const struct nls_table nls_utf8_11_0;

static const struct {
	int encoding_magic;
	const struct nls_table *tbl;
} nls_map[] = {
	{ EXT4_ENC_ASCII, &nls_ascii },
	{ EXT4_ENC_UTF8_11_0, &nls_utf8_11_0 },
};

static const struct nls_table *nls_load_table(int encoding)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(nls_map); i++) {
		if (encoding == nls_map[i].encoding_magic)
			return nls_map[i].tbl;
	}
	return NULL;
}

#endif
