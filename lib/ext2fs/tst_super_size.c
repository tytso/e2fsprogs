/*
 * This testing program makes sure superblock size is 1024 bytes long
 *
 * Copyright (C) 2007 by Theodore Ts'o.
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#include "ext2_fs.h"

int main(int argc, char **argv)
{
	int s = sizeof(struct ext2_super_block);

	printf("Size of struct ext2_super_block is %d\n", s);
	if (s != 1024) {
		exit(1);
	}
	exit(0);
}

