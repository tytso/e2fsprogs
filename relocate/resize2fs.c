/*
 * resize2fs.c  - Resize the ext2 filesystem.
 *
 * Copyright (C) 1996 Theodore Ts'o
 *
 * This file can be redistributed under the terms of the GNU General
 * Public License
 */

#ifdef HAVE_GETOPT_H
#include <getopt.h>
#endif
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <linux/ext2_fs.h>

#include "ext2fs/ext2fs.h"

#include "../version.h"

const char * program_name = "resize2fs";
char * device_name = NULL;

static volatile void usage (void)
{
	fprintf (stderr, "usage: %s device\n", program_name);
	exit (1);
}

errcode_t ext2fs_resize_inode_bitmap(ext2_filsys fs,
				     ext2fs_inode_bitmap ibmap)
{
	ino_t	new_end, new_real_end;
	size_t	size, new_size;
	char 	*new_bitmap, *old_bitmap;
	
	EXT2_CHECK_MAGIC(fs, EXT2_ET_MAGIC_EXT2FS_FILSYS);
	EXT2_CHECK_MAGIC(ibmap, EXT2_ET_MAGIC_INODE_BITMAP);

	new_end = fs->super->s_inodes_count;
	new_real_end = (EXT2_INODES_PER_GROUP(fs->super)
			* fs->group_desc_count);

	if (new_real_end == ibmap->real_end) {
		ibmap->end = new_end;
		return 0;
	}
	
	size = ((ibmap->real_end - ibmap->start) / 8) + 1;
	new_size = ((new_real_end - ibmap->start) / 8) + 1;

	new_bitmap = malloc(size);
	if (!new_bitmap)
		return ENOMEM;
	if (size > new_size)
		size = new_size;
	memcpy(new_bitmap, ibmap->bitmap, size);
	if (new_size > size)
		memset(new_bitmap + size, 0, new_size - size);

	old_bitmap = ibmap->bitmap;
	ibmap->bitmap = new_bitmap;
	free(old_bitmap);
	ibmap->end = new_end;
	ibmap->real_end = new_real_end;
}

errcode_t ext2fs_resize_block_bitmap(ext2_filsys fs,
				     ext2fs_inode_bitmap bbmap)
{
	ino_t	new_end, new_real_end;
	size_t	size, new_size;
	char 	*new_bitmap, *old_bitmap;
	
	EXT2_CHECK_MAGIC(fs, EXT2_ET_MAGIC_EXT2FS_FILSYS);
	EXT2_CHECK_MAGIC(bbmap, EXT2_ET_MAGIC_BLOCK_BITMAP);

	new_end = fs->super->s_inodes_count;
	new_real_end = (EXT2_INODES_PER_GROUP(fs->super)
			* fs->group_desc_count);

	if (new_real_end == bbmap->real_end) {
		bbmap->end = new_end;
		return 0;
	}
	
	size = ((bbmap->real_end - bbmap->start) / 8) + 1;
	new_size = ((new_real_end - bbmap->start) / 8) + 1;

	new_bitmap = malloc(size);
	if (!new_bitmap)
		return ENOMEM;
	if (size > new_size)
		size = new_size;
	memcpy(new_bitmap, bbmap->bitmap, size);
	if (new_size > size)
		memset(new_bitmap + size, 0, new_size - size);

	old_bitmap = bbmap->bitmap;
	bbmap->bitmap = new_bitmap;
	free(old_bitmap);
	bbmap->end = new_end;
	bbmap->real_end = new_real_end;
}


	
errcode_t ext2fs_resize(ext2_filsys fs, blk_t new_size)
{
	__u32 new_block_groups, new_desc_blocks;
	
	if (new_size = fs->super->s_blocks_count)
		return 0;

	new_block_groups = (fs->super->s_blocks_count -
			    fs->super->s_first_data_block +
			    EXT2_BLOCKS_PER_GROUP(fs->super) - 1)
		/ EXT2_BLOCKS_PER_GROUP(fs->super);
	if (new_block_groups == 0)
		return EXT2_ET_TOOSMALL;
	new_desc_blocks = (new_block_groups +
			   EXT2_DESC_PER_BLOCK(fs->super) - 1)
		/ EXT2_DESC_PER_BLOCK(fs->super);

}



void main (int argc, char ** argv)
{
	errcode_t	retval;
	ext2_filsys	fs;
	int		c;

	fprintf (stderr, "resize2fs %s, %s for EXT2 FS %s, %s\n",
		 E2FSPROGS_VERSION, E2FSPROGS_DATE,
		 EXT2FS_VERSION, EXT2FS_DATE);
	if (argc && *argv)
		program_name = *argv;
	
	while ((c = getopt (argc, argv, "h")) != EOF) {
		switch (c) {
		case 'h':
			usage();
			break;
		default:
			usage ();
		}
	}
	if (optind > argc - 1)
		usage ();
	device_name = argv[optind++];
	initialize_ext2_error_table();
	retval = ext2fs_open (device_name, EXT2_FLAG_RW, 0, 0,
			      unix_io_manager, &fs);
	if (retval) {
		com_err (program_name, retval, "while trying to open %s",
			 device_name);
		printf ("Couldn't find valid filesystem superblock.\n");
		exit (1);
	}
	retval = ext2fs_read_bitmaps (fs);
	if (retval) {
		com_err (program_name, retval,
			 "while trying to read the bitmaps", device_name);
		ext2fs_close (fs);
		exit (1);
	}
	ext2fs_close (fs);
	exit (0);
}
