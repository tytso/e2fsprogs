/*
 * icount.c --- an efficient inode count abstraction
 *
 * Copyright (C) 1997 Theodore Ts'o.
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 */

#include <et/com_err.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif

#include <linux/ext2_fs.h>
#include "ext2fs.h"

/*
 * The data storage strategy used by icount relies on the observation
 * that most inode counts are either zero (for non-allocated inodes),
 * one (for most files), and only a few that are two or more
 * (directories and files that are linked to more than one directory).
 *
 * Also, e2fsck tends to load the icount data sequentially.
 *
 * So, we use an inode bitmap to indicate which inodes have a count of
 * one, and then use a sorted list to store the counts for inodes
 * which are greater than one.
 *
 * We also use an optional bitmap to indicate which inodes are already
 * in the sorted list, to speed up the use of this abstraction by
 * e2fsck's pass 2.  Pass 2 increments inode counts as it finds them,
 * so this extra bitmap avoids searching the sorted list to see if a
 * particular inode is on the sorted list already.
 */

struct ext2_icount_el {
	ino_t	ino;
	__u16	count;
};

struct ext2_icount {
	int			magic;
	ext2fs_inode_bitmap	single;
	ext2fs_inode_bitmap	multiple;
	ino_t			count;
	ino_t			size;
	ino_t			num_inodes;
	int			cursor;
	struct ext2_icount_el	*list;
};

void ext2fs_free_icount(ext2_icount_t icount)
{
	if (!icount)
		return;

	icount->magic = 0;
	if (icount->list)
		free(icount->list);
	if (icount->single)
		ext2fs_free_inode_bitmap(icount->single);
	if (icount->multiple)
		ext2fs_free_inode_bitmap(icount->multiple);
	free(icount);
}

errcode_t ext2fs_create_icount(ext2_filsys fs, int flags, int size,
			       ext2_icount_t *ret)
{
	ext2_icount_t	icount;
	errcode_t	retval;
	size_t		bytes;

	icount = malloc(sizeof(struct ext2_icount));
	if (!icount)
		return ENOMEM;
	memset(icount, 0, sizeof(struct ext2_icount));

	retval = ext2fs_allocate_inode_bitmap(fs, 0, 
					      &icount->single);
	if (retval)
		goto errout;

	if (flags & EXT2_ICOUNT_OPT_INCREMENT) {
		retval = ext2fs_allocate_inode_bitmap(fs, 0, 
						      &icount->multiple);
		if (retval)
			goto errout;
	} else
		icount->multiple = 0;

	if (size) {
		icount->size = size;
	} else {
		/*
		 * Figure out how many special case inode counts we will
		 * have.  We know we will need one for each directory;
		 * we also need to reserve some extra room for file links
		 */
		retval = ext2fs_get_num_dirs(fs, &icount->size);
		if (retval)
			goto errout;
		icount->size += fs->super->s_inodes_count / 50;
	}
	
	bytes = icount->size * sizeof(struct ext2_icount_el);
#if 0
	printf("Icount allocated %d entries, %d bytes.\n",
	       icount->size, bytes);
#endif
	icount->list = malloc(bytes);
	if (!icount->list)
		goto errout;
	memset(icount->list, 0, bytes);

	icount->magic = EXT2_ET_MAGIC_ICOUNT;
	icount->count = 0;
	icount->cursor = 0;
	icount->num_inodes = fs->super->s_inodes_count;

	*ret = icount;

	return 0;

errout:
	ext2fs_free_icount(icount);
	return(retval);
}

/*
 * get_icount_el() --- given an inode number, try to find icount
 * 	information in the sorted list.  We use a binary search...
 */
static struct ext2_icount_el *get_icount_el(ext2_icount_t icount, ino_t ino)
{
	int	low, high, mid;

	if (!icount || !icount->list || !icount->count)
		return 0;

	if (icount->multiple &&
	    !ext2fs_test_inode_bitmap(icount->multiple, ino))
		return 0;

	low = 0;
	high = icount->count-1;
	if (ino == icount->list[low].ino) {
		icount->cursor = low+1;
		return &icount->list[low];
	}
	if  (ino == icount->list[high].ino) {
		icount->cursor = 0;
		return &icount->list[high];
	}
	if (icount->cursor >= icount->count)
		icount->cursor = 0;
	if (ino == icount->list[icount->cursor].ino)
		return &icount->list[icount->cursor++];
#if 0
	printf("Non-cursor get_icount_el: %u\n", ino);
#endif
	
	while (low < high) {
		mid = (low+high)/2;
		if (mid == low || mid == high)
			break;
		if (ino == icount->list[mid].ino) {
			icount->cursor = mid;
			return &icount->list[mid];
		}
		if (ino < icount->list[mid].ino)
			high = mid;
		else
			low = mid;
	}
	return 0;
}

/*
 * put_icount_el() --- given an inode number, create a new entry in
 * 	the sorted list.  This function is optimized for adding values
 * 	in ascending order.
 */
static struct ext2_icount_el *put_icount_el(ext2_icount_t icount, ino_t ino)
{
	struct ext2_icount_el *el, *new_list;
	ino_t			new_size = 0;
	int			i, j;

	if (icount->count >= icount->size) {
		if (icount->count) {
			new_size = icount->list[icount->count-1].ino;
			new_size = icount->count * 
				((float) new_size / icount->num_inodes);
		}
		if (new_size < (icount->size + 100))
			new_size = icount->size + 100;
#if 0
		printf("Reallocating icount %d entries...\n", new_size);
#endif	
		new_list = realloc(icount->list,
			new_size * sizeof(struct ext2_icount_el));
		if (!new_list)
			return 0;
		icount->size = new_size;
		icount->list = new_list;
	}

	/*
	 * Normally, get_icount_el is called with each inode in
	 * sequential order; but once in a while (like when pass 3
	 * needs to recreate the root directory or lost+found
	 * directory) it is called out of order.
	 */
	if (icount->count && icount->list[icount->count-1].ino >= ino) {
		for (i = icount->count-1; i > 0; i--)
			if (icount->list[i-1].ino < ino)
				break;
		el = &icount->list[i];
		if (el->ino != ino) {
			for (j = icount->count++; j > i; j--)
				icount->list[j] = icount->list[j-1];
			el->count = 0;
		}
	} else {
		el = &icount->list[icount->count++];
		el->count = 0;
	}
	el->ino = ino;
	return el;
}
	

errcode_t ext2fs_icount_fetch(ext2_icount_t icount, ino_t ino, __u16 *ret)
{
	struct ext2_icount_el	*el;
	
	EXT2_CHECK_MAGIC(icount, EXT2_ET_MAGIC_ICOUNT);

	if (ext2fs_test_inode_bitmap(icount->single, ino)) {
		*ret = 1;
		return 0;
	}
	el = get_icount_el(icount, ino);
	if (!el) {
		*ret = 0;
		return 0;
	}
	*ret = el->count;
	return 0;
}

errcode_t ext2fs_icount_increment(ext2_icount_t icount, ino_t ino,
				  __u16 *ret)
{
	struct ext2_icount_el	*el;

	EXT2_CHECK_MAGIC(icount, EXT2_ET_MAGIC_ICOUNT);

	if (ext2fs_test_inode_bitmap(icount->single, ino)) {
		/*
		 * If the existing count is 1, then we know there is
		 * no entry in the list, so use put_icount_el().
		 */
		el = put_icount_el(icount, ino);
		if (!el)
			return ENOMEM;
	} else if (icount->multiple) {
		/*
		 * The count is either zero or greater than 1; if the
		 * inode is set in icount->multiple, then there should
		 * be an entry in the list, so find it using
		 * get_icount_el().
		 */
		if (ext2fs_test_inode_bitmap(icount->multiple, ino)) {
			el = get_icount_el(icount, ino);
			if (!el) {
				/* should never happen */
				el = put_icount_el(icount, ino);
				if (!el)
					return ENOMEM;
			}
		} else {
			/*
			 * The count was zero; mark the single bitmap
			 * and return.
			 */
		zero_count:
			ext2fs_mark_inode_bitmap(icount->single, ino);
			if (ret)
				*ret = 1;
			return 0;
		}
	} else {
		/*
		 * The count is either zero or greater than 1; try to
		 * find an entry in the list to determine which.
		 */
		el = get_icount_el(icount, ino);
		if (!el) {
			/* No entry means the count was zero */
			goto zero_count;
		}
		el = put_icount_el(icount, ino);
		if (!el)
			return ENOMEM;
	}
	if (ext2fs_test_inode_bitmap(icount->single, ino)) {
		ext2fs_unmark_inode_bitmap(icount->single, ino);
		el->count = 2;
	} else
		el->count++;
	if (icount->multiple)
		ext2fs_mark_inode_bitmap(icount->multiple, ino);
	if (ret)
		*ret = el->count;
	return 0;
}

errcode_t ext2fs_icount_decrement(ext2_icount_t icount, ino_t ino,
				  __u16 *ret)
{
	struct ext2_icount_el	*el;

	EXT2_CHECK_MAGIC(icount, EXT2_ET_MAGIC_ICOUNT);

	if (ext2fs_test_inode_bitmap(icount->single, ino)) {
		ext2fs_unmark_inode_bitmap(icount->single, ino);
		if (icount->multiple)
			ext2fs_unmark_inode_bitmap(icount->multiple, ino);
		else {
			el = get_icount_el(icount, ino);
			if (el)
				el->count = 0;
		}
		if (ret)
			*ret = 0;
		return 0;
	}

	el = get_icount_el(icount, ino);
	if (!el)
		return EINVAL;

	el->count--;
	if (el->count == 1)
		ext2fs_mark_inode_bitmap(icount->single, ino);
	if ((el->count == 0) && icount->multiple)
		ext2fs_unmark_inode_bitmap(icount->multiple, ino);

	if (ret)
		*ret = el->count;
	return 0;
}

errcode_t ext2fs_icount_store(ext2_icount_t icount, ino_t ino,
			      __u16 count)
{
	struct ext2_icount_el	*el;

	EXT2_CHECK_MAGIC(icount, EXT2_ET_MAGIC_ICOUNT);

	if (count == 1) {
		ext2fs_mark_inode_bitmap(icount->single, ino);
		if (icount->multiple)
			ext2fs_unmark_inode_bitmap(icount->multiple, ino);
		return 0;
	}
	if (count == 0) {
		ext2fs_unmark_inode_bitmap(icount->single, ino);
		if (icount->multiple) {
			/*
			 * If the icount->multiple bitmap is enabled,
			 * we can just clear both bitmaps and we're done
			 */
			ext2fs_unmark_inode_bitmap(icount->multiple, ino);
		} else {
			el = get_icount_el(icount, ino);
			if (el)
				el->count = 0;
		}
		return 0;
	}

	/*
	 * Get the icount element
	 */
	el = put_icount_el(icount, ino);
	if (!el)
		return ENOMEM;
	el->count = count;
	ext2fs_unmark_inode_bitmap(icount->single, ino);
	if (icount->multiple)
		ext2fs_mark_inode_bitmap(icount->multiple, ino);
	return 0;
}

ino_t ext2fs_get_icount_size(ext2_icount_t icount)
{
	if (!icount || icount->magic != EXT2_ET_MAGIC_ICOUNT)
		return 0;

	return icount->size;
}
