/*
 * badblocks.c --- replace/append bad blocks to the bad block inode
 * 
 * Copyright (C) 1993, 1994 Theodore Ts'o.  This file may be
 * redistributed under the terms of the GNU Public License.
 */

#include <time.h>

#include <et/com_err.h>
#include "e2fsck.h"

static void invalid_block(ext2_filsys fs, blk_t blk)
{
	printf("Bad block %lu out of range; ignored.\n", blk);
	return;
}

void read_bad_blocks_file(ext2_filsys fs, const char *bad_blocks_file,
			  int replace_bad_blocks)
{
	errcode_t	retval;
	badblocks_list	bb_list = 0;
	FILE		*f;

	read_bitmaps(fs);
	
	/*
	 * If we're appending to the bad blocks inode, read in the
	 * current bad blocks.
	 */
	if (!replace_bad_blocks) {
		retval = ext2fs_read_bb_inode(fs, &bb_list);
		if (retval) {
			com_err("ext2fs_read_bb_inode", retval,
				"while reading the bad blocks inode");
			fatal_error(0);
		}
	}
	
	/*
	 * Now read in the bad blocks from the file.
	 */
	f = fopen(bad_blocks_file, "r");
	if (!f) {
		com_err("read_bad_blocks_file", errno,
			"while trying to open %s", bad_blocks_file);
		fatal_error(0);
	}
	retval = ext2fs_read_bb_FILE(fs, f, &bb_list, invalid_block);
	fclose (f);
	if (retval) {
		com_err("ext2fs_read_bb_FILE", retval,
			"while reading in list of bad blocks from file");
		fatal_error(0);
	}
	
	/*
	 * Finally, update the bad blocks from the bad_block_map
	 */
	retval = ext2fs_update_bb_inode(fs, bb_list);
	if (retval) {
		com_err("ext2fs_update_bb_inode", retval,
			"while updating bad block inode");
		fatal_error(0);
	}

	badblocks_list_free(bb_list);
	return;
}

void test_disk(ext2_filsys fs)
{
	errcode_t	retval;
	badblocks_list	bb_list = 0;
	FILE		*f;
	char		buf[1024];

	read_bitmaps(fs);
	
	/*
	 * Always read in the current list of bad blocks.
	 */
	retval = ext2fs_read_bb_inode(fs, &bb_list);
	if (retval) {
		com_err("ext2fs_read_bb_inode", retval,
			"while reading the bad blocks inode");
		fatal_error(0);
	}
	
	/*
	 * Now run the bad blocks program
	 */
	sprintf(buf, "badblocks %s%s %ld", preen ? "" : "-s ",
		fs->device_name,
		fs->super->s_blocks_count);
	if (verbose)
		printf("Running command: %s\n", buf);
	f = popen(buf, "r");
	if (!f) {
		com_err("popen", errno,
			"while trying to run %s", buf);
		fatal_error(0);
	}
	retval = ext2fs_read_bb_FILE(fs, f, &bb_list, invalid_block);
	fclose (f);
	if (retval) {
		com_err("ext2fs_read_bb_FILE", retval,
			"while processing list of bad blocks from program");
		fatal_error(0);
	}
	
	/*
	 * Finally, update the bad blocks from the bad_block_map
	 */
	retval = ext2fs_update_bb_inode(fs, bb_list);
	if (retval) {
		com_err("ext2fs_update_bb_inode", retval,
			"while updating bad block inode");
		fatal_error(0);
	}

	badblocks_list_free(bb_list);
	return;
}

