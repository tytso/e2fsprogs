/*
 * dumpe2fs.c		- List the control structures of a second
 *			  extended filesystem
 *
 * Copyright (C) 1992, 1993, 1994  Remy Card <card@masi.ibp.fr>
 *                                 Laboratoire MASI, Institut Blaise Pascal
 *                                 Universite Pierre et Marie Curie (Paris VI)
 *
 * This file can be redistributed under the terms of the GNU General
 * Public License
 */

/*
 * History:
 * 94/01/09	- Creation
 * 94/02/27	- Ported to use the ext2fs library
 */

#include <getopt.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <linux/ext2_fs.h>

#include "ext2fs/ext2fs.h"
#include "e2p/e2p.h"

#include "../version.h"

#define in_use(m, x)	(test_bit ((x), (m)))

const char * program_name = "dumpe2fs";
char * device_name = NULL;

static volatile void usage (void)
{
	fprintf (stderr, "usage: %s device\n", program_name);
	exit (1);
}

static void print_free (unsigned long group, char * bitmap,
			unsigned long nbytes, unsigned long offset)
{
	int p = 0;
	unsigned long i;
	unsigned long j;

	for (i = 0; i < nbytes; i++)
		if (!in_use (bitmap, i))
		{
			if (p)
				printf (", ");
			if (i == nbytes - 1 || in_use (bitmap, i + 1))
				printf ("%lu", group * nbytes + i + offset);
			else
			{
				for (j = i; j < nbytes && !in_use (bitmap, j);
				     j++)
					;
				printf ("%lu-%lu", group * nbytes + i + offset,
					group * nbytes + (j - 1) + offset);
				i = j - 1;
			}
			p = 1;
		}
}

static void list_desc (ext2_filsys fs)
{
	unsigned long i;
	char * block_bitmap = fs->block_map;
	char * inode_bitmap = fs->inode_map;

	printf ("\n");
	for (i = 0; i < fs->group_desc_count; i++)
	{
		printf ("Group %lu:\n", i);
		printf ("  Block bitmap at %lu, Inode bitmap at %lu, "
			"Inode table at %lu\n",
			fs->group_desc[i].bg_block_bitmap,
			fs->group_desc[i].bg_inode_bitmap,
			fs->group_desc[i].bg_inode_table);
		printf ("  %d free blocks, %d free inodes, %d directories\n",
			fs->group_desc[i].bg_free_blocks_count,
			fs->group_desc[i].bg_free_inodes_count,
			fs->group_desc[i].bg_used_dirs_count);
		printf ("  Free blocks: ");
		print_free (i, block_bitmap, fs->super->s_blocks_per_group,
			    fs->super->s_first_data_block);
		block_bitmap += fs->super->s_blocks_per_group / 8;
		printf ("\n");
		printf ("  Free inodes: ");
		print_free (i, inode_bitmap, fs->super->s_inodes_per_group, 1);
		inode_bitmap += fs->super->s_inodes_per_group / 8;
		printf ("\n");
	}
}

static void list_bad_blocks(ext2_filsys fs)
{
	badblocks_list		bb_list = 0;
	badblocks_iterate	bb_iter;
	blk_t			blk;
	errcode_t		retval;

	retval = ext2fs_read_bb_inode(fs, &bb_list);
	if (retval) {
		com_err("ext2fs_read_bb_inode", retval, "");
		exit(1);
	}
	retval = badblocks_list_iterate_begin(bb_list, &bb_iter);
	if (retval) {
		com_err("badblocks_list_iterate_begin", retval,
			"while printing bad block list");
		exit(1);
	}
	if (badblocks_list_iterate(bb_iter, &blk))
		printf("Bad blocks: %ld", blk);
	while (badblocks_list_iterate(bb_iter, &blk))
		printf(", %ld", blk);
	badblocks_list_iterate_end(bb_iter);
	printf("\n");
}

void main (int argc, char ** argv)
{
	errcode_t	retval;
	ext2_filsys	fs;

	fprintf (stderr, "dumpe2fs %s, %s for EXT2 FS %s, %s\n",
		 E2FSPROGS_VERSION, E2FSPROGS_DATE,
		 EXT2FS_VERSION, EXT2FS_DATE);
	if (argc && *argv)
		program_name = *argv;
	if (argc != 2)
		usage ();
	device_name = argv[1];
	retval = ext2fs_open (device_name, 0, 0, 0, unix_io_manager, &fs);
	if (retval)
	{
		com_err (program_name, retval, "while trying to open %s",
			 device_name);
		printf ("Couldn't find valid filesystem superblock.\n");
		exit (1);
	}
	retval = ext2fs_read_bitmaps (fs);
	if (retval)
	{
		com_err (program_name, retval, "while trying to read the bitmaps",
			 device_name);
		ext2fs_close (fs);
		exit (1);
	}
	list_super (fs->super);
	list_bad_blocks (fs);
	list_desc (fs);
	ext2fs_close (fs);
	exit (0);
}
