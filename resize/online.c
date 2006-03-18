/*
 * online.c --- Do on-line resizing of the ext3 filesystem
 *
 * Copyright (C) 2006 by Theodore Ts'o 
 * 
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 */

#include "resize2fs.h"
#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif
#include <sys/stat.h>
#include <fcntl.h>

extern char *program_name;

errcode_t online_resize_fs(ext2_filsys fs, const char *mtpt, 
			   blk_t *new_size, int flags)
{
	struct ext2_new_group_input input;
	struct ext2_super_block *sb = fs->super;
	ext2_filsys 		new_fs;
	errcode_t 		retval;
	dgrp_t			i;
	blk_t			size;
	int			fd, r_frac, overhead;

	printf(_("Filesystem at %s is mounted on %s; "
		 "on-line resizing required\n"), fs->device_name, mtpt);

	if (*new_size < sb->s_blocks_count) {
		printf(_("On-line shrinking from %u to %u not supported.\n"),
		       sb->s_blocks_count, *new_size);
		exit(1);
	}

	fd = open(mtpt, O_RDONLY);
	if (fd < 0) {
		com_err(program_name, errno, 
			_("while trying to open mountpoint %s"), mtpt);
		exit(1);
	}

	size=sb->s_blocks_count;
	if (ioctl(fd, EXT2_IOC_GROUP_EXTEND, &size)) {
		if (errno == EPERM)
			com_err(program_name, 0, 
				_("Permission denied to resize filesystem"));
		else if (errno == ENOTTY)
			com_err(program_name, 0, 
			_("Filesystem does not support online resizing"));
		else 
			com_err(program_name, errno, 
			_("While checking for on-line resizing support"));
		exit(1);
	}

	r_frac = ((100 * sb->s_r_blocks_count) + sb->s_blocks_count-1) /
		sb->s_blocks_count;

	retval = ext2fs_read_bitmaps(fs);
	if (retval)
		return retval;

	retval = ext2fs_dup_handle(fs, &new_fs);
	if (retval)
		return retval;

	retval = adjust_fs_info(new_fs, fs, *new_size);
	if (retval)
		return retval;

	printf(_("Performing an on-line resize of %s to %d (%dk) blocks.\n"), 
	       fs->device_name, *new_size, fs->blocksize / 1024);

	size = fs->group_desc_count * sb->s_blocks_per_group + 
		sb->s_first_data_block;
	if (size > *new_size)
		size = *new_size;

	if (ioctl(fd, EXT2_IOC_GROUP_EXTEND, &size)) {
		com_err(program_name, errno, 
			_("While trying to extend the last group"));
		exit(1);
	}

	for (i = fs->group_desc_count;
	     i < new_fs->group_desc_count; i++) {

		overhead = (int) (2 + new_fs->inode_blocks_per_group);

		if (ext2fs_bg_has_super(new_fs, new_fs->group_desc_count - 1))
			overhead += 1 + new_fs->desc_blocks + 
				new_fs->super->s_reserved_gdt_blocks;

		input.group = i;
		input.block_bitmap = new_fs->group_desc[i].bg_block_bitmap;
		input.inode_bitmap = new_fs->group_desc[i].bg_inode_bitmap;
		input.inode_table = new_fs->group_desc[i].bg_inode_table;
		input.blocks_count = sb->s_blocks_per_group;
		if (i == new_fs->group_desc_count-1) {
			input.blocks_count = new_fs->super->s_blocks_count -
				sb->s_first_data_block - 
				(i * sb->s_blocks_per_group);
		}
		input.reserved_blocks = input.blocks_count * r_frac / 100;

#if 0
		printf("new block bitmap is at 0x%04x\n", input.block_bitmap);
		printf("new inode bitmap is at 0x%04x\n", input.inode_bitmap);
		printf("new inode table is at 0x%04x-0x%04x\n", 
		       input.inode_table,
		       input.inode_table + new_fs->inode_blocks_per_group-1);
		printf("new group has %d blocks\n", input.blocks_count);
		printf("new group will reserve %d blocks\n", 
		       input.reserved_blocks);
		printf("new group has %d free blocks\n", 
		       new_fs->group_desc[i].bg_free_blocks_count);
		printf("new group has %d free inodes (%d blocks)\n",
		       new_fs->group_desc[i].bg_free_inodes_count, 
		       new_fs->inode_blocks_per_group);
		printf("Adding group #%d\n", input.group);
#endif

		if (ioctl(fd, EXT2_IOC_GROUP_ADD, &input) < 0) {
			com_err(program_name, errno, 
				_("While trying to add group #%d"), 
				input.group);
			exit(1);
		}
	}

	ext2fs_free(new_fs);
	close(fd);

	return 0;
}
