/*
 * extent.c --- ext2 extent abstraction
 *
 * This abstraction is used to provide a compact way of representing a
 * translation table, for moving multiple contiguous ranges (extents)
 * of blocks or inodes.
 *
 * Copyright (C) 1997 Theodore Ts'o
 * 
 * %Begin-Header%
 * All rights reserved.
 * %End-Header%
 */

#include "resize2fs.h"

struct ext2_extent_entry {
	__u32	old, new;
	int	size;
};

struct _ext2_extent {
	struct ext2_extent_entry *list;
	int	cursor;
	int	size;
	int	num;
	int	sorted;
};

/*
 * Create an extent table
 */
errcode_t ext2fs_create_extent_table(ext2_extent *ret_extent, int size) 
{
	ext2_extent	new;

	new = malloc(sizeof(struct _ext2_extent));
	if (!new)
		return ENOMEM;
	memset(new, 0, sizeof(struct _ext2_extent));

	new->size = size ? size : 50;
	new->cursor = 0;
	new->num = 0;
	new->sorted = 1;

	new->list = malloc(sizeof(struct ext2_extent_entry) * new->size);
	if (!new->list) {
		free(new);
		return ENOMEM;
	}
	memset(new->list, 0, sizeof(struct ext2_extent_entry) * new->size);
	*ret_extent = new;
	return 0;
}

/*
 * Free an extent table
 */
void ext2fs_free_extent_table(ext2_extent extent)
{
	if (extent->list)
		free(extent->list);
	extent->list = 0;
	extent->size = 0;
	extent->num = 0;
	free(extent);
}

/*
 * Add an entry to the extent table
 */
errcode_t ext2fs_add_extent_entry(ext2_extent extent, __u32 old, __u32 new)
{
	struct ext2_extent_entry *p;
	int	newsize;
	int	curr;
	struct	ext2_extent_entry *ent;

	if (extent->num >= extent->size) {
		newsize = extent->size + 100;
		p = realloc(extent->list,
			    sizeof(struct ext2_extent_entry) * newsize);
		if (!p)
			return ENOMEM;
		extent->list = p;
		extent->size = newsize;
	}
	curr = extent->num;
	ent = extent->list + curr;
	if (curr) {
		/*
		 * Check to see if this can be coalesced with the last
		 * extent
		 */
		ent--;
		if ((ent->old + ent->size == old) &&
		    (ent->new + ent->size == new)) {
			ent->size++;
			return 0;
		}
		/*
		 * Now see if we're going to ruin the sorting
		 */
		if (ent->old + ent->size > old)
			extent->sorted = 0;
		ent++;
	}
	ent->old = old;
	ent->new = new;
	ent->size = 1;
	extent->num++;
	return 0;
}

/*
 * Helper function for qsort
 */
static int extent_cmp(const void *a, const void *b)
{
	const struct ext2_extent_entry *db_a = a;
	const struct ext2_extent_entry *db_b = b;
	
	return (db_a->old - db_b->old);
}	

/*
 * Given an inode map and inode number, look up the old inode number
 * and return the new inode number
 */
__u32 ext2fs_extent_translate(ext2_extent extent, __u32 old)
{
	int	low, high, mid;
	ino_t	lowval, highval;
	float	range;

	if (!extent->sorted) {
		qsort(extent->list, extent->num,
		      sizeof(struct ext2_extent_entry), extent_cmp);
		extent->sorted = 1;
	}
	low = 0;
	high = extent->num-1;
	while (low <= high) {
#if 0
		mid = (low+high)/2;
#else
		if (low == high)
			mid = low;
		else {
			/* Interpolate for efficiency */
			lowval = extent->list[low].old;
			highval = extent->list[high].old;

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
		if ((old >= extent->list[mid].old) &&
		    (old < extent->list[mid].old + extent->list[mid].size))
			return (extent->list[mid].new +
				(old - extent->list[mid].old));
		if (old < extent->list[mid].old)
			high = mid-1;
		else
			low = mid+1;
	}
	return 0;
}

/*
 * For debugging only
 */
void ext2fs_extent_dump(ext2_extent extent, FILE *out)
{
	int	i;
	struct ext2_extent_entry *ent;
	
	fputs("# Extent dump:\n", out);
	fprintf(out, "#\tNum=%d, Size=%d, Cursor=%d, Sorted=%d\n",
	       extent->num, extent->size, extent->cursor, extent->sorted);
	for (i=0, ent=extent->list; i < extent->num; i++, ent++) {
		fprintf(out, "#\t\t %u -> %u (%d)\n", ent->old,
			ent->new, ent->size);
	}
}

/*
 * Iterate over the contents of the extent table
 */
errcode_t ext2fs_iterate_extent(ext2_extent extent, __u32 *old,
				__u32 *new, int *size)
{
	struct ext2_extent_entry *ent;
	
	if (!old) {
		extent->cursor = 0;
		return 0;
	}

	if (extent->cursor >= extent->num) {
		*old = 0;
		*new = 0;
		*size = 0;
		return 0;
	}

	ent = extent->list + extent->cursor++;

	*old = ent->old;
	*new = ent->new;
	*size = ent->size;
	return 0;
}
	
	
		
	       
