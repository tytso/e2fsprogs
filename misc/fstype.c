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
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <fcntl.h>
#include <sys/types.h>

struct fs_magic {
	const char *fs_name;
	int	offset;
	int	len;
	char	*magic;
};

struct fs_magic type_array[] = {
	{ "ext2", 1024+56, 2, "\123\357" },
	{ "minix", 1040, 2, "\177\023" },
	{ "minix", 1040, 2, "\217\023" },
	{ "minix", 1040, 2, "\150\044" },
	{ "minix", 1040, 2, "\170\044" },
	{ "xfs", 0, 4, "XFSB" },
	{ 0, 0, 0, 0 }
};

const char *identify_fs(const char *fs_name)
{
	char	buf[2048];
	struct fs_magic *p;
	int	fd;

	fd = open(fs_name, O_RDONLY);
	if (fd < 0)
		return NULL;
	if (lseek(fd, 0, SEEK_SET) < 0)
		return NULL;
	if (read(fd, buf, sizeof(buf)) != sizeof(buf))
		return NULL;
	for (p = type_array; p->fs_name; p++) {
		if (memcmp(p->magic, buf+p->offset, p->len) == 0)
			return p->fs_name;
	}
	return NULL;
}

#ifdef TEST_PROGRAM
int main(int argc, char **argv)
{
	const char	*type;
	
	if (argc != 2) {
		fprintf(stderr, "Usage: %s device\n", argv[0]);
		exit(1);
	}
	type = identify_fs(argv[1]);
	printf("%s is a %s filesystem\n", argv[1], type);
	return (0);
}
#endif

	
	
