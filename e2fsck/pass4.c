/*
 * pass4.c -- pass #4 of e2fsck: Check reference counts
 *
 * Copyright (C) 1993 Theodore Ts'o.  This file may be redistributed
 * under the terms of the GNU Public License.
 * 
 */

#include "e2fsck.h"

void pass4(ext2_filsys fs)
{
	int	i;
	struct ext2_inode	inode;
	struct resource_track	rtrack;
	
	init_resource_track(&rtrack);

#ifdef MTRACE
	mtrace_print("Pass 4");
#endif

	if (!preen)
		printf("Pass 4: Check reference counts.\n");
	for (i=1; i <= fs->super->s_inodes_count; i++) {
		if (i == EXT2_BAD_INO ||
		    (i > EXT2_ROOT_INO && i < EXT2_FIRST_INO))
			continue;
		if (!(ext2fs_test_inode_bitmap(fs, inode_used_map, i)))
			continue;
		if (inode_count[i] == 0) {
			/*
			 * Inode isn't attached to the filesystem;
			 * prompt to reconnect.
			 */
			printf("Unattached inode %d\n", i);
			preenhalt();
			if (ask("Connect to /lost+found", 1)) {
				if (reconnect_file(fs, i))
					ext2fs_unmark_valid(fs);
			} else
				ext2fs_unmark_valid(fs);
		}
		if (inode_count[i] != inode_link_info[i]) {
			ext2fs_read_inode(fs, i, &inode);
			if (inode_link_info[i] != inode.i_links_count) {
				printf("WARNING: PROGRAMMING BUG IN E2FSCK!\n");
				printf("inode_link_info[%d] is %d, "
				       "inode.i_links_count is %d.  "
				       "They should be the same!\n",
				       i, inode_link_info[i],
				       inode.i_links_count);
			}
			printf("Inode %d has ref count %d, expecting %d.\n",
			       i, inode.i_links_count, inode_count[i]);
			if (ask("Set i_nlinks to count", 1)) {
				inode.i_links_count = inode_count[i];
				ext2fs_write_inode(fs, i, &inode);
			} else
				ext2fs_unmark_valid(fs);
		}
	}
	free(inode_link_info);	inode_link_info = 0;
	free(inode_count);	inode_count = 0;
	if (tflag > 1) {
		printf("Pass 4: ");
		print_resource_track(&rtrack);
	}
}

