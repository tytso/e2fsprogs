/*
 * fstype.c
 * 
 * Copyright (C) 2001 Theodore Ts'o.
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <fcntl.h>
#include <sys/types.h>

#include "fsck.h"

struct fs_magic {
	const char 	*fs_name;
	int		offset;
	int		len;
	const char	*magic;
};

#define REISERFS_SUPER_MAGIC_STRING "ReIsErFs"
#define REISER2FS_SUPER_MAGIC_STRING "ReIsEr2Fs"
#define REISERFS_DISK_OFFSET_IN_BYTES ((64 * 1024) + 52)
/* the spot for the super in versions 3.5 - 3.5.10 (inclusive) */
#define REISERFS_OLD_DISK_OFFSET_IN_BYTES ((8 * 1024) + 52)

struct fs_magic type_array[] = {
	{ "ext2", 1024+56, 2, "\123\357" },
	{ "ext3", 1024+56, 2, "\123\357" },
	{ "reiserfs", REISERFS_DISK_OFFSET_IN_BYTES, 9,
		  REISER2FS_SUPER_MAGIC_STRING },
	{ "reiserfs", REISERFS_DISK_OFFSET_IN_BYTES, 8,
		  REISERFS_SUPER_MAGIC_STRING },
	{ "reiserfs", REISERFS_OLD_DISK_OFFSET_IN_BYTES, 9,
		  REISER2FS_SUPER_MAGIC_STRING },
	{ "reiserfs", REISERFS_OLD_DISK_OFFSET_IN_BYTES, 8,
		  REISERFS_SUPER_MAGIC_STRING },
	{ "minix", 1040, 2, "\177\023" },
	{ "minix", 1040, 2, "\217\023" },
	{ "minix", 1040, 2, "\150\044" },
	{ "minix", 1040, 2, "\170\044" },
	{ "xfs", 0, 4, "XFSB" },
	{ 0, 0, 0, 0 }
};

const char *identify_fs(const char *fs_name, const char *fs_types)
{
	char	buf[73728], *s;
	const char *t;
	struct fs_magic *p;
	int	fd;

	fd = open(fs_name, O_RDONLY);
	if (fd < 0)
		return NULL;
	if (lseek(fd, 0, SEEK_SET) < 0)
		return NULL;
	if (read(fd, buf, sizeof(buf)) != sizeof(buf))
		return NULL;
	close(fd);
	if (!fs_types || !strcmp(fs_types, "auto")) {
		for (p = type_array; p->fs_name; p++) {
			if (memcmp(p->magic, buf+p->offset, p->len) == 0)
				return p->fs_name;
		}
	} else {
		s = string_copy(fs_types);
		for (t = strtok(s, ","); t; t = strtok(NULL, ",")) {
			for (p = type_array; p->fs_name; p++) {
				if (strcmp(p->fs_name, t))
					continue;
				if (memcmp(p->magic, buf+p->offset,
					   p->len) == 0) {
					free(s);
					return p->fs_name;
				}
			}
		}
		free(s);
	}
	return NULL;
}

#ifdef TEST_PROGRAM
int main(int argc, char **argv)
{
	const char	*type;
	
	if (argc < 2 || argc > 3) {
		fprintf(stderr, "Usage: %s [type list] device\n", argv[0]);
		exit(1);
	}
	if (argc == 2) {
		type = identify_fs(argv[1], NULL);
		printf("%s is a %s filesystem\n", argv[1], type);
	} else {
		type = identify_fs(argv[2],argv[1]);
		printf("%s is a %s filesystem\n", argv[2], type);
	}
	return (0);
}
#endif
