/*
 * This testing program verifies checksumming operations
 *
 * Copyright (C) 2006, 2007 by Andreas Dilger <adilger@clusterfs.com>
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU General Public
 * License, Version 2.  See the file COPYING for more details.
 * %End-Header%
 */

#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#include "ext2fs/ext2_fs.h"
#include "ext2fs/ext2fs.h"
#include "ext2fs/crc16.h"

#ifndef offsetof
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#endif

void print_csum(const char *msg, ext2_filsys fs, dgrp_t group)
{
	__u16 crc1, crc2, crc3;
	dgrp_t swabgroup;
	struct ext2_group_desc *desc = &fs->group_desc[group];
	struct ext2_super_block *sb = fs->super;

#ifdef WORDS_BIGENDIAN
	struct ext2_group_desc swabdesc = fs->group_desc[group];

	/* Have to swab back to little-endian to do the checksum */
	ext2fs_swap_group_desc(&swabdesc);
	desc = &swabdesc;

	swabgroup = ext2fs_swab32(group);
#else
	swabgroup = group;
#endif

	crc1 = ext2fs_crc16(~0, sb->s_uuid, sizeof(fs->super->s_uuid));
	crc2 = ext2fs_crc16(crc1, &swabgroup, sizeof(swabgroup));
	crc3 = ext2fs_crc16(crc2, desc,
			    offsetof(struct ext2_group_desc, bg_checksum));
	printf("%s: UUID %016Lx%016Lx(%04x), grp %u(%04x): %04x=%04x\n",
	       msg, *(long long *)&sb->s_uuid, *(long long *)&sb->s_uuid[8],
	       crc1, group, crc2, crc3, ext2fs_group_desc_csum(fs, group));
}

unsigned char sb_uuid[16] = { 0x4f, 0x25, 0xe8, 0xcf, 0xe7, 0x97, 0x48, 0x23,
			      0xbe, 0xfa, 0xa7, 0x88, 0x4b, 0xae, 0xec, 0xdb };

int main(int argc, char **argv)
{
	struct ext2_super_block param;
	errcode_t		retval;
	ext2_filsys		fs;
	int			i;
	__u16 csum1, csum2, csum_known = 0xd3a4;

	memset(&param, 0, sizeof(param));
	param.s_blocks_count = 32768;

	retval = ext2fs_initialize("test fs", 0, &param,
				   test_io_manager, &fs);
	if (retval) {
		com_err("setup", retval,
			"While initializing filesystem");
		exit(1);
	}
	memcpy(fs->super->s_uuid, sb_uuid, 16);
	fs->super->s_feature_ro_compat = EXT4_FEATURE_RO_COMPAT_GDT_CSUM;

	for (i=0; i < fs->group_desc_count; i++) {
		fs->group_desc[i].bg_block_bitmap = 124;
		fs->group_desc[i].bg_inode_bitmap = 125;
		fs->group_desc[i].bg_inode_table = 126;
		fs->group_desc[i].bg_free_blocks_count = 31119;
		fs->group_desc[i].bg_free_inodes_count = 15701;
		fs->group_desc[i].bg_used_dirs_count = 2;
		fs->group_desc[i].bg_flags = 0;
	};

	csum1 = ext2fs_group_desc_csum(fs, 0);
	print_csum("csum0000", fs, 0);

	if (csum1 != csum_known) {
		printf("checksum for group 0 should be %04x\n", csum_known);
		exit(1);
	}
	csum2 = ext2fs_group_desc_csum(fs, 1);
	print_csum("csum0001", fs, 1);
	if (csum1 == csum2) {
		printf("checksums for different groups shouldn't match\n");
		exit(1);
	}
	csum2 = ext2fs_group_desc_csum(fs, 2);
	print_csum("csumffff", fs, 2);
	if (csum1 == csum2) {
		printf("checksums for different groups shouldn't match\n");
		exit(1);
	}
	fs->group_desc[0].bg_checksum = csum1;
	csum2 = ext2fs_group_desc_csum(fs, 0);
	print_csum("csum_set", fs, 0);
	if (csum1 != csum2) {
		printf("checksums should not depend on checksum field\n");
		exit(1);
	}
	if (!ext2fs_group_desc_csum_verify(fs, 0)) {
		printf("checksums should verify against gd_checksum\n");
		exit(1);
	}
	memset(fs->super->s_uuid, 0x30, sizeof(fs->super->s_uuid));
	print_csum("new_uuid", fs, 0);
	if (ext2fs_group_desc_csum_verify(fs, 0) != 0) {
		printf("checksums for different filesystems shouldn't match\n");
		exit(1);
	}
	csum1 = fs->group_desc[0].bg_checksum = ext2fs_group_desc_csum(fs, 0);
	print_csum("csum_new", fs, 0);
	fs->group_desc[0].bg_free_blocks_count = 1;
	csum2 = ext2fs_group_desc_csum(fs, 0);
	print_csum("csum_blk", fs, 0);
	if (csum1 == csum2) {
		printf("checksums for different data shouldn't match\n");
		exit(1);
	}

	return 0;
}
