/*
 * valid_blk.c --- does the inode have valid blocks?
 *
 * Copyright 1997 by Theodore Ts'o
 * 
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 * 
 */

#include <stdio.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <string.h>
#include <time.h>

#if EXT2_FLAT_INCLUDES
#include "ext2_fs.h"
#else
#include <linux/ext2_fs.h>
#endif

#include "ext2fs.h"

/*
 * This function returns 1 if the inode's block entries actually
 * contain block entries.
 */
int ext2fs_inode_has_valid_blocks(struct ext2_inode *inode)
{
	/*
	 * Only directories, regular files, and some symbolic links
	 * have valid block entries.
	 */
	if (!LINUX_S_ISDIR(inode->i_mode) && !LINUX_S_ISREG(inode->i_mode) &&
	    !LINUX_S_ISLNK(inode->i_mode))
		return 0;
	
	/*
	 * If the symbolic link is a "fast symlink", then the symlink
	 * target is stored in the block entries.
	 */
	if (LINUX_S_ISLNK (inode->i_mode) && inode->i_blocks == 0 &&
	    inode->i_size < EXT2_N_BLOCKS * sizeof (unsigned long))
		return 0;

	return 1;
}
