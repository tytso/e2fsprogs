/*
 * read_bb_file.c --- read a list of bad blocks for a FILE *
 *
 * Copyright (C) 1994 Theodore Ts'o.  This file may be redistributed
 * under the terms of the GNU Public License.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <linux/fs.h>
#include <linux/ext2_fs.h>

#include "ext2fs.h"

/*
 * Reads a list of bad blocks from  a FILE *
 */
errcode_t ext2fs_read_bb_FILE(ext2_filsys fs, FILE *f, 
			      badblocks_list *bb_list,
			      void (*invalid)(ext2_filsys fs, blk_t blk))
{
	errcode_t	retval;
	blk_t		blockno;
	int		count;

	if (!*bb_list) {
		retval = badblocks_list_create(bb_list, 10);
		if (retval)
			return retval;
	}

	while (!feof (f)) {
		count = fscanf (f, "%lu", &blockno);
		if (count <= 0)
			break;
		if ((blockno < fs->super->s_first_data_block) ||
		    (blockno >= fs->super->s_blocks_count)) {
			if (invalid)
				(invalid)(fs, blockno);
			continue;
		}
		retval = badblocks_list_add(*bb_list, blockno);
		return retval;
	}
	return 0;
}


