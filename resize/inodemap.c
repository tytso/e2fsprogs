/*
 * inodemap.c --- ext2resizer indoe mapper
 *
 * Copyright (C) 1997 Theodore Ts'o
 * 
 * %Begin-Header%
 * All rights reserved.
 * %End-Header%
 */

#include "resize2fs.h"

struct inode_map_entry {
	ino_t	old, new;
};

struct inode_map_struct {
	struct inode_map_entry *list;
	int	size;
	int	num;
	int	sorted;
};

typedef struct inode_map_struct *inode_map;

/*
 * Create inode map table
 */
static errcode_t create_inode_map(inode_map *imap, int size) 
{
	inode_map	new;

	new = malloc(sizeof(struct inode_map_struct));
	if (!new)
		return ENOMEM;
	memset(new, 0, sizeof(struct inode_map_struct));

	new->size = size ? size : 50;
	new->num = 0;
	new->sorted = 1;

	new->list = malloc(sizeof(struct inode_map_struct) * new->size);
	if (!new->list) {
		free(new);
		return ENOMEM;
	}
	*imap = new;
	return 0;
}

/*
 * Add an entry to the inode table map
 */
static errcode_t add_inode_map_entry(inode_map imap, ino_t old, ino_t new)
{
	struct inode_map_entry *p;
	int	newsize;

	if (imap->num >= imap->size) {
		newsize = imap->size + 100;
		p = realloc(imap->list,
			    sizeof(struct inode_map_struct) * newsize);
		if (!p)
			return ENOMEM;
		imap->list = p;
		imap->size = newsize;
	}
	if (imap->num) {
		if (imap->list[imap->num-1].old > old)
			imap->sorted = 0;
	}
	imap->list[imap->num].old = old;
	imap->list[imap->num].new = new;
	imap->num++;
	return 0;
}

/*
 * Helper function for qsort
 */
static int inode_map_cmp(const void *a, const void *b)
{
	const struct inode_map_entry *db_a =
		(const struct inode_map_entry *) a;
	const struct inode_map_entry *db_b =
		(const struct inode_map_entry *) b;
	
	return (db_a->old - db_b->old);
}	

/*
 * Given an inode map and inode number, look up the old inode number
 * and return the new inode number
 */
static ino_t inode_map_translate(inode_map imap, ino_t old)
{
	int	low, high, mid;
	ino_t	lowval, highval;
	float	range;

	if (!imap->sorted) {
		qsort(imap->list, imap->num,
		      sizeof(struct inode_map_entry), inode_map_cmp);
		imap->sorted = 1;
	}
	low = 0;
	high = imap->num-1;
	while (low <= high) {
#if 0
		mid = (low+high)/2;
#else
		if (low == high)
			mid = low;
		else {
			/* Interpolate for efficiency */
			lowval = imap->list[low].old;
			highval = imap->list[high].old;

			if (old < lowval)
				range = 0;
			else if (old > highval)
				range = 1;
			else 
				range = ((float) (old - lowval)) /
					(highval - lowval);
			mid = low + ((int) (range * (high-low)));
		}
#endif
		if (old == imap->list[mid].old)
			return imap->list[mid].new;
		if (old < imap->list[mid].old)
			high = mid-1;
		else
			low = mid+1;
	}
	return 0;
}

struct istruct {
	inode_map	imap;
	int		flags;
};

int check_and_change_inodes(ino_t dir, int entry,
			    struct ext2_dir_entry *dirent, int offset,
			    int	blocksize, char *buf, void *private)
{
	struct istruct *is = private;
	ino_t	new;

	if (!dirent->inode)
		return 0;

	new = inode_map_translate(is->imap, dirent->inode);

	if (!new)
		return 0;
	if (is->flags & RESIZE_DEBUG_INODEMAP)
		printf("Inode translate (dir=%ld, name=%.*s, %ld->%ld)\n",
		       dir, dirent->name_len, dirent->name,
		       dirent->inode, new);

	dirent->inode = new;

	return DIRENT_CHANGED;
}

errcode_t ext2fs_inode_move(ext2_resize_t rfs)
{
	ino_t			ino, start, end, new;
	struct ext2_inode 	inode;
	ext2_inode_scan 	scan;
	inode_map		imap;
	errcode_t		retval;
	int			group;
	struct istruct 		is;
	
	if (rfs->old_fs->group_desc_count <=
	    rfs->new_fs->group_desc_count)
		return 0;

	retval = create_inode_map(&imap, 0);
	if (retval)
		return retval;

	retval = ext2fs_open_inode_scan(rfs->old_fs, 0, &scan);
	if (retval)
		return retval;

	retval = ext2fs_inode_scan_goto_blockgroup(scan,
				   rfs->new_fs->group_desc_count);
	if (retval) {
		ext2fs_close_inode_scan(scan);
		return retval;
	}

	new = EXT2_FIRST_INODE(rfs->new_fs->super);

	/*
	 * First, copy all of the inodes that need to be moved
	 * elsewhere in the inode table
	 */
	while (1) {
		retval = ext2fs_get_next_inode(scan, &ino, &inode);
		if (retval)
			return retval;
		if (!ino)
			break;
		
		if (!ext2fs_test_inode_bitmap(rfs->old_fs->inode_map, ino)) 
			continue;

		/*
		 * Find a new inode
		 */
		while (1) { 
			if (!ext2fs_test_inode_bitmap(rfs->new_fs->inode_map, 
						      new))
				break;
			new++;
			if (new > rfs->new_fs->super->s_inodes_count)
				return ENOSPC;
		}
		ext2fs_mark_inode_bitmap(rfs->new_fs->inode_map, new);
		retval = ext2fs_write_inode(rfs->old_fs, new, &inode);
		if (retval)
			return retval;
		group = (new-1) / EXT2_INODES_PER_GROUP(rfs->new_fs->super);
		if (LINUX_S_ISDIR(inode.i_mode))
			rfs->new_fs->group_desc[group].bg_used_dirs_count++;
		
		if (rfs->flags & RESIZE_DEBUG_INODEMAP)
			printf("Inode moved %ld->%ld\n", ino, new);

		add_inode_map_entry(imap, ino, new);
	}
	/*
	 * Now, we iterate over all of the directories to update the
	 * inode references
	 */
	is.imap = imap;
	is.flags = rfs->flags;
	retval = ext2fs_dblist_dir_iterate(rfs->old_fs->dblist, 0, 0,
					   check_and_change_inodes, &is);
	if (retval)
		return retval;

	return 0;
}

