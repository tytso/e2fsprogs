/*
 * This testing program makes sure the ext2_inode structure is 1024 bytes long
 *
 * Copyright (C) 2007 by Theodore Ts'o.
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Library
 * General Public License, version 2.
 * %End-Header%
 */

#include "config.h"
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#include "ext2_fs.h"

struct ext2_inode inode;

int verbose = 0;

#define offsetof(type, member)  __builtin_offsetof (type, member)
#define check_field(x) cur_offset = do_field(#x, sizeof(inode.x),	\
				offsetof(struct ext2_inode, x), \
				cur_offset)

static int do_field(const char *field, size_t size, int offset, int cur_offset)
{
	if (offset != cur_offset) {
		printf("Warning!  Unexpected offset at %s\n", field);
		exit(1);
	}
	printf("%8d %-30s %3u\n", offset, field, (unsigned) size);
	return offset + size;
}

void check_structure_fields()
{
#if (__GNUC__ >= 4)
	int cur_offset = 0;

	printf("%8s %-30s %3s\n", "offset", "field", "size");
	check_field(i_mode);
	check_field(i_uid);
	check_field(i_size);
	check_field(i_atime);
	check_field(i_ctime);
	check_field(i_mtime);
	check_field(i_dtime);
	check_field(i_gid);
	check_field(i_links_count);
	check_field(i_blocks);
	check_field(i_flags);
	check_field(osd1.linux1.l_i_version);
	check_field(i_block);
	check_field(i_generation);
	check_field(i_file_acl);
	check_field(i_size_high);
	check_field(i_faddr);
	check_field(osd2.linux2.l_i_blocks_hi);
	check_field(osd2.linux2.l_i_file_acl_high);
	check_field(osd2.linux2.l_i_uid_high);
	check_field(osd2.linux2.l_i_gid_high);
	check_field(osd2.linux2.l_i_checksum_lo);
	check_field(osd2.linux2.l_i_reserved);
	printf("Ending offset is %d\n\n", cur_offset);
#endif
}


int main(int argc, char **argv)
{
	int l = sizeof(struct ext2_inode);

	check_structure_fields();
	printf("Size of struct ext2_inode is %d\n", l);
	if (l != 128) {
		exit(1);
	}
	exit(0);
}
