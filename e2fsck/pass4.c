/*
 * pass4.c -- pass #4 of e2fsck: Check reference counts
 *
 * Copyright (C) 1993 Theodore Ts'o.  This file may be redistributed
 * under the terms of the GNU Public License.
 * 
 */

#include "e2fsck.h"

/*
 * This routine is called when an inode is not connected to the
 * directory tree.
 * 
 * This subroutine returns 1 then the caller shouldn't bother with the
 * rest of the pass 4 tests.
 */
int disconnect_inode(ext2_filsys fs, ino_t i)
{
	struct ext2_inode	inode;

	e2fsck_read_inode(fs, i, &inode, "pass4: disconnect_inode");
	if (!inode.i_blocks && (LINUX_S_ISREG(inode.i_mode) ||
				LINUX_S_ISDIR(inode.i_mode))) {
		/*
		 * This is a zero-length file; prompt to delete it...
		 */
		printf("Unattached zero-length inode %lu\n", i);
		if (ask("Clear", 1)) {
			inode_link_info[i] = 0;
			inode.i_links_count = 0;
			inode.i_dtime = time(0);
			e2fsck_write_inode(fs, i, &inode,
					   "disconnect_inode");
			/*
			 * Fix up the bitmaps...
			 */
			read_bitmaps(fs);
			ext2fs_unmark_inode_bitmap(inode_used_map, i);
			ext2fs_unmark_inode_bitmap(inode_dir_map, i);
			ext2fs_unmark_inode_bitmap(fs->inode_map, i);
			ext2fs_mark_ib_dirty(fs);
			return 0;
		}
	}
	
	/*
	 * Prompt to reconnect.
	 */
	printf("Unattached inode %lu\n", i);
	preenhalt(fs);
	if (ask("Connect to /lost+found", 1)) {
		if (reconnect_file(fs, i))
			ext2fs_unmark_valid(fs);
	} else {
		/*
		 * If we don't attach the inode, then skip the
		 * i_links_test since there's no point in trying to
		 * force i_links_count to zero.
		 */
		ext2fs_unmark_valid(fs);
		return 1;
	}
	return 0;
}


void pass4(ext2_filsys fs)
{
	ino_t	i;
	struct ext2_inode	inode;
	struct resource_track	rtrack;
	
	init_resource_track(&rtrack);

#ifdef MTRACE
	mtrace_print("Pass 4");
#endif

	if (!preen)
		printf("Pass 4: Checking reference counts\n");
	for (i=1; i <= fs->super->s_inodes_count; i++) {
		if (i == EXT2_BAD_INO ||
		    (i > EXT2_ROOT_INO && i < EXT2_FIRST_INODE(fs->super)))
			continue;
		if (!(ext2fs_test_inode_bitmap(inode_used_map, i)))
			continue;
		if (inode_count[i] == 0) {
			if (disconnect_inode(fs, i))
				continue;
		}
		if (inode_count[i] != inode_link_info[i]) {
			e2fsck_read_inode(fs, i, &inode, "pass4");
			if (inode_link_info[i] != inode.i_links_count) {
				printf("WARNING: PROGRAMMING BUG IN E2FSCK!\n");
				printf("\tOR SOME BONEHEAD (YOU) IS CHECKING "
				       "A MOUNTED (LIVE) FILESYSTEM.\n"); 
				printf("inode_link_info[%ld] is %u, "
				       "inode.i_links_count is %d.  "
				       "They should be the same!\n",
				       i, inode_link_info[i],
				       inode.i_links_count);
			}
			printf("Inode %lu has ref count %d, expecting %d.\n",
			       i, inode.i_links_count, inode_count[i]);
			if (ask("Set i_nlinks to count", 1)) {
				inode.i_links_count = inode_count[i];
				e2fsck_write_inode(fs, i, &inode, "pass4");
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

