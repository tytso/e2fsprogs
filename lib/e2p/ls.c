/*
 * ls.c			- List the contents of an ext2fs superblock
 *
 * Copyright (C) 1992, 1993, 1994  Remy Card <card@masi.ibp.fr>
 *                                 Laboratoire MASI, Institut Blaise Pascal
 *                                 Universite Pierre et Marie Curie (Paris VI)
 *
 * This file can be redistributed under the terms of the GNU Library General
 * Public License
 */

#include <stdio.h>
#include <time.h>

#include <linux/ext2_fs.h>

#include "e2p.h"

void list_super (struct ext2_super_block * s)
{
	printf ("Filesystem magic number:  0x%04X\n", s->s_magic);
	printf ("Filesystem state:        ");
	print_fs_state (stdout, s->s_state);
	printf ("\n");
	printf ("Errors behavior:          ");
	print_fs_errors (stdout, s->s_errors);
	printf ("\n");
	printf ("Inode count:              %lu\n", s->s_inodes_count);
	printf ("Block count:              %lu\n", s->s_blocks_count);
	printf ("Reserved block count:     %lu\n", s->s_r_blocks_count);
	printf ("Free blocks:              %lu\n", s->s_free_blocks_count);
	printf ("Free inodes:              %lu\n", s->s_free_inodes_count);
	printf ("First block:              %lu\n", s->s_first_data_block);
	printf ("Block size:               %u\n", EXT2_BLOCK_SIZE(s));
	printf ("Fragment size:            %u\n", EXT2_FRAG_SIZE(s));
	printf ("Blocks per group:         %lu\n", s->s_blocks_per_group);
	printf ("Fragments per group:      %lu\n", s->s_frags_per_group);
	printf ("Inodes per group:         %lu\n", s->s_inodes_per_group);
	printf ("Last mount time:          %s", ctime ((time_t *) &s->s_mtime));
	printf ("Last write time:          %s", ctime ((time_t *) &s->s_wtime));
	printf ("Mount count:              %u\n", s->s_mnt_count);
	printf ("Maximum mount count:      %d\n", s->s_max_mnt_count);
	printf ("Last checked:             %s", ctime ((time_t *) &s->s_lastcheck));
	printf ("Check interval:           %lu\n", s->s_checkinterval);
	if (s->s_checkinterval)
	{
		time_t next;

		next = s->s_lastcheck + s->s_checkinterval;
		printf ("Next check after:         %s", ctime (&next));
	}
}
