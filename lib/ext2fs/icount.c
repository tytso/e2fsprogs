/*
 * icount.c --- an efficient inode count abstraction
 *
 * Copyright (C) 1997 Theodore Ts'o.
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Library
 * General Public License, version 2.
 * %End-Header%
 */

#include "config.h"
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include "ext2_fs.h"
#include "ext2fs.h"
#include "tdb.h"

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
	ext2_ino_t	ino;
	__u32		count;
};

struct ext2_icount {
	errcode_t		magic;
	ext2fs_inode_bitmap	single;
	ext2fs_inode_bitmap	multiple;
	ext2_ino_t		count;
	ext2_ino_t		size;
	ext2_ino_t		num_inodes;
	ext2_ino_t		cursor;
	struct ext2_icount_el	*list;
	struct ext2_icount_el	*last_lookup;
#ifdef CONFIG_TDB
	char			*tdb_fn;
	TDB_CONTEXT		*tdb;
#endif
	__u16			*fullmap;
};

/*
 * We now use a 32-bit counter field because it doesn't cost us
 * anything extra for the in-memory data structure, due to alignment
 * padding.  But there's no point changing the interface if most of
 * the time we only care if the number is bigger than 65,000 or not.
 * So use the following translation function to return a 16-bit count.
 */
#define icount_16_xlate(x) (((x) > 65500) ? 65500 : (x))

void ext2fs_free_icount(ext2_icount_t icount)
{
	if (!icount)
		return;

	icount->magic = 0;
	if (icount->list)
		ext2fs_free_mem(&icount->list);
	if (icount->single)
		ext2fs_free_inode_bitmap(icount->single);
	if (icount->multiple)
		ext2fs_free_inode_bitmap(icount->multiple);
#ifdef CONFIG_TDB
	if (icount->tdb)
		tdb_close(icount->tdb);
	if (icount->tdb_fn) {
		(void) unlink(icount->tdb_fn);
		free(icount->tdb_fn);
	}
#endif

	if (icount->fullmap)
		ext2fs_free_mem(&icount->fullmap);

	ext2fs_free_mem(&icount);
}

static errcode_t alloc_icount(ext2_filsys fs, int flags, ext2_icount_t *ret)
{
	ext2_icount_t	icount;
	errcode_t	retval;

	*ret = 0;

	retval = ext2fs_get_mem(sizeof(struct ext2_icount), &icount);
	if (retval)
		return retval;
	memset(icount, 0, sizeof(struct ext2_icount));
	icount->magic = EXT2_ET_MAGIC_ICOUNT;
	icount->num_inodes = fs->super->s_inodes_count;

	if ((flags & EXT2_ICOUNT_OPT_FULLMAP) &&
	    (flags & EXT2_ICOUNT_OPT_INCREMENT) &&
	    (icount->num_inodes < (ext2_ino_t) ~0U)) {
		retval = ext2fs_get_arrayzero(icount->num_inodes + 1,
					      sizeof(*icount->fullmap),
					      &icount->fullmap);
		/* If we can't allocate, fall back */
		if (!retval) {
			*ret = icount;
			return 0;
		}
	}

	retval = ext2fs_allocate_inode_bitmap(fs, "icount", &icount->single);
	if (retval)
		goto errout;

	if (flags & EXT2_ICOUNT_OPT_INCREMENT) {
		retval = ext2fs_allocate_inode_bitmap(fs, "icount_inc",
						      &icount->multiple);
		if (retval)
			goto errout;
	} else
		icount->multiple = 0;

	*ret = icount;
	return 0;

errout:
	ext2fs_free_icount(icount);
	return(retval);
}

#ifdef CONFIG_TDB
struct uuid {
	__u32	time_low;
	__u16	time_mid;
	__u16	time_hi_and_version;
	__u16	clock_seq;
	__u8	node[6];
};

static void unpack_uuid(void *in, struct uuid *uu)
{
	__u8	*ptr = in;
	__u32	tmp;

	tmp = *ptr++;
	tmp = (tmp << 8) | *ptr++;
	tmp = (tmp << 8) | *ptr++;
	tmp = (tmp << 8) | *ptr++;
	uu->time_low = tmp;

	tmp = *ptr++;
	tmp = (tmp << 8) | *ptr++;
	uu->time_mid = tmp;

	tmp = *ptr++;
	tmp = (tmp << 8) | *ptr++;
	uu->time_hi_and_version = tmp;

	tmp = *ptr++;
	tmp = (tmp << 8) | *ptr++;
	uu->clock_seq = tmp;

	memcpy(uu->node, ptr, 6);
}

static void uuid_unparse(void *uu, char *out)
{
	struct uuid uuid;

	unpack_uuid(uu, &uuid);
	sprintf(out,
		"%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
		uuid.time_low, uuid.time_mid, uuid.time_hi_and_version,
		uuid.clock_seq >> 8, uuid.clock_seq & 0xFF,
		uuid.node[0], uuid.node[1], uuid.node[2],
		uuid.node[3], uuid.node[4], uuid.node[5]);
}
#endif

errcode_t ext2fs_create_icount_tdb(ext2_filsys fs EXT2FS_NO_TDB_UNUSED,
				   char *tdb_dir EXT2FS_NO_TDB_UNUSED,
				   int flags EXT2FS_NO_TDB_UNUSED,
				   ext2_icount_t *ret EXT2FS_NO_TDB_UNUSED)
{
#ifdef CONFIG_TDB
	ext2_icount_t	icount;
	errcode_t	retval;
	char 		*fn, uuid[40];
	ext2_ino_t	num_inodes;
	mode_t		save_umask;
	int		fd;

	retval = alloc_icount(fs, flags,  &icount);
	if (retval)
		return retval;

	retval = ext2fs_get_mem(strlen(tdb_dir) + 64, &fn);
	if (retval)
		goto errout;
	uuid_unparse(fs->super->s_uuid, uuid);
	sprintf(fn, "%s/%s-icount-XXXXXX", tdb_dir, uuid);
	save_umask = umask(077);
	fd = mkstemp(fn);
	if (fd < 0) {
		retval = errno;
		ext2fs_free_mem(&fn);
		goto errout;
	}
	icount->tdb_fn = fn;
	umask(save_umask);
	/*
	 * This is an overestimate of the size that we will need; the
	 * ideal value is the number of used inodes with a count
	 * greater than 1.  OTOH the times when we really need this is
	 * with the backup programs that use lots of hard links, in
	 * which case the number of inodes in use approaches the ideal
	 * value.
	 */
	num_inodes = fs->super->s_inodes_count - fs->super->s_free_inodes_count;

	icount->tdb = tdb_open(fn, num_inodes, TDB_NOLOCK | TDB_NOSYNC,
			       O_RDWR | O_CREAT | O_TRUNC, 0600);
	close(fd);
	if (icount->tdb == NULL) {
		retval = errno;
		goto errout;
	}
	*ret = icount;
	return 0;
errout:
	ext2fs_free_icount(icount);
	return(retval);
#else
	return EXT2_ET_UNIMPLEMENTED;
#endif
}

errcode_t ext2fs_create_icount2(ext2_filsys fs, int flags, unsigned int size,
				ext2_icount_t hint, ext2_icount_t *ret)
{
	ext2_icount_t	icount;
	errcode_t	retval;
	size_t		bytes;
	ext2_ino_t	i;

	if (hint) {
		EXT2_CHECK_MAGIC(hint, EXT2_ET_MAGIC_ICOUNT);
		if (hint->size > size)
			size = (size_t) hint->size;
	}

	retval = alloc_icount(fs, flags, &icount);
	if (retval)
		return retval;

	if (icount->fullmap)
		goto successout;

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

	bytes = (size_t) (icount->size * sizeof(struct ext2_icount_el));
#if 0
	printf("Icount allocated %u entries, %d bytes.\n",
	       icount->size, bytes);
#endif
	retval = ext2fs_get_array(icount->size, sizeof(struct ext2_icount_el),
			 &icount->list);
	if (retval)
		goto errout;
	memset(icount->list, 0, bytes);

	icount->count = 0;
	icount->cursor = 0;

	/*
	 * Populate the sorted list with those entries which were
	 * found in the hint icount (since those are ones which will
	 * likely need to be in the sorted list this time around).
	 */
	if (hint) {
		for (i=0; i < hint->count; i++)
			icount->list[i].ino = hint->list[i].ino;
		icount->count = hint->count;
	}

successout:
	*ret = icount;
	return 0;

errout:
	ext2fs_free_icount(icount);
	return(retval);
}

errcode_t ext2fs_create_icount(ext2_filsys fs, int flags,
			       unsigned int size,
			       ext2_icount_t *ret)
{
	return ext2fs_create_icount2(fs, flags, size, 0, ret);
}

/*
 * insert_icount_el() --- Insert a new entry into the sorted list at a
 * 	specified position.
 */
static struct ext2_icount_el *insert_icount_el(ext2_icount_t icount,
					    ext2_ino_t ino, int pos)
{
	struct ext2_icount_el 	*el;
	errcode_t		retval;
	ext2_ino_t		new_size = 0;
	int			num;

	if (icount->last_lookup && icount->last_lookup->ino == ino)
		return icount->last_lookup;

	if (icount->count >= icount->size) {
		if (icount->count) {
			new_size = icount->list[(unsigned)icount->count-1].ino;
			new_size = (ext2_ino_t) (icount->count *
				((float) icount->num_inodes / new_size));
		}
		if (new_size < (icount->size + 100))
			new_size = icount->size + 100;
#if 0
		printf("Reallocating icount %u entries...\n", new_size);
#endif
		retval = ext2fs_resize_mem((size_t) icount->size *
					   sizeof(struct ext2_icount_el),
					   (size_t) new_size *
					   sizeof(struct ext2_icount_el),
					   &icount->list);
		if (retval)
			return 0;
		icount->size = new_size;
	}
	num = (int) icount->count - pos;
	if (num < 0)
		return 0;	/* should never happen */
	if (num) {
		memmove(&icount->list[pos+1], &icount->list[pos],
			sizeof(struct ext2_icount_el) * num);
	}
	icount->count++;
	el = &icount->list[pos];
	el->count = 0;
	el->ino = ino;
	icount->last_lookup = el;
	return el;
}

/*
 * get_icount_el() --- given an inode number, try to find icount
 * 	information in the sorted list.  If the create flag is set,
 * 	and we can't find an entry, create one in the sorted list.
 */
static struct ext2_icount_el *get_icount_el(ext2_icount_t icount,
					    ext2_ino_t ino, int create)
{
	int	low, high, mid;

	if (!icount || !icount->list)
		return 0;

	if (create && ((icount->count == 0) ||
		       (ino > icount->list[(unsigned)icount->count-1].ino))) {
		return insert_icount_el(icount, ino, (unsigned) icount->count);
	}
	if (icount->count == 0)
		return 0;

	if (icount->cursor >= icount->count)
		icount->cursor = 0;
	if (ino == icount->list[icount->cursor].ino)
		return &icount->list[icount->cursor++];
#if 0
	printf("Non-cursor get_icount_el: %u\n", ino);
#endif
	low = 0;
	high = (int) icount->count-1;
	while (low <= high) {
		mid = ((unsigned)low + (unsigned)high) >> 1;
		if (ino == icount->list[mid].ino) {
			icount->cursor = mid+1;
			return &icount->list[mid];
		}
		if (ino < icount->list[mid].ino)
			high = mid-1;
		else
			low = mid+1;
	}
	/*
	 * If we need to create a new entry, it should be right at
	 * low (where high will be left at low-1).
	 */
	if (create)
		return insert_icount_el(icount, ino, low);
	return 0;
}

static errcode_t set_inode_count(ext2_icount_t icount, ext2_ino_t ino,
				 __u32 count)
{
	struct ext2_icount_el 	*el;
#ifdef CONFIG_TDB
	TDB_DATA key, data;

	if (icount->tdb) {
		key.dptr = (unsigned char *) &ino;
		key.dsize = sizeof(ext2_ino_t);
		data.dptr = (unsigned char *) &count;
		data.dsize = sizeof(__u32);
		if (count) {
			if (tdb_store(icount->tdb, key, data, TDB_REPLACE))
				return tdb_error(icount->tdb) +
					EXT2_ET_TDB_SUCCESS;
		} else {
			if (tdb_delete(icount->tdb, key))
				return tdb_error(icount->tdb) +
					EXT2_ET_TDB_SUCCESS;
		}
		return 0;
	}
#endif
	if (icount->fullmap) {
		icount->fullmap[ino] = icount_16_xlate(count);
		return 0;
	}

	el = get_icount_el(icount, ino, 1);
	if (!el)
		return EXT2_ET_NO_MEMORY;

	el->count = count;
	return 0;
}

static errcode_t get_inode_count(ext2_icount_t icount, ext2_ino_t ino,
				 __u32 *count)
{
	struct ext2_icount_el 	*el;
#ifdef CONFIG_TDB
	TDB_DATA key, data;

	if (icount->tdb) {
		key.dptr = (unsigned char *) &ino;
		key.dsize = sizeof(ext2_ino_t);

		data = tdb_fetch(icount->tdb, key);
		if (data.dptr == NULL) {
			*count = 0;
			return tdb_error(icount->tdb) + EXT2_ET_TDB_SUCCESS;
		}

		*count = *((__u32 *) data.dptr);
		free(data.dptr);
		return 0;
	}
#endif
	if (icount->fullmap) {
		*count = icount->fullmap[ino];
		return 0;
	}

	el = get_icount_el(icount, ino, 0);
	if (!el) {
		*count = 0;
		return ENOENT;
	}

	*count = el->count;
	return 0;
}

errcode_t ext2fs_icount_validate(ext2_icount_t icount, FILE *out)
{
	errcode_t	ret = 0;
	unsigned int	i;
	const char *bad = "bad icount";

	EXT2_CHECK_MAGIC(icount, EXT2_ET_MAGIC_ICOUNT);

	if (icount->count > icount->size) {
		fprintf(out, "%s: count > size\n", bad);
		return EXT2_ET_INVALID_ARGUMENT;
	}
	for (i=1; i < icount->count; i++) {
		if (icount->list[i-1].ino >= icount->list[i].ino) {
			fprintf(out, "%s: list[%d].ino=%u, list[%d].ino=%u\n",
				bad, i-1, icount->list[i-1].ino,
				i, icount->list[i].ino);
			ret = EXT2_ET_INVALID_ARGUMENT;
		}
	}
	return ret;
}

errcode_t ext2fs_icount_fetch(ext2_icount_t icount, ext2_ino_t ino, __u16 *ret)
{
	__u32	val;
	EXT2_CHECK_MAGIC(icount, EXT2_ET_MAGIC_ICOUNT);

	if (!ino || (ino > icount->num_inodes))
		return EXT2_ET_INVALID_ARGUMENT;

	if (!icount->fullmap) {
		if (ext2fs_test_inode_bitmap2(icount->single, ino)) {
			*ret = 1;
			return 0;
		}
		if (icount->multiple &&
			!ext2fs_test_inode_bitmap2(icount->multiple, ino)) {
			*ret = 0;
			return 0;
		}
	}
	get_inode_count(icount, ino, &val);
	*ret = icount_16_xlate(val);
	return 0;
}

errcode_t ext2fs_icount_increment(ext2_icount_t icount, ext2_ino_t ino,
				  __u16 *ret)
{
	__u32			curr_value;

	EXT2_CHECK_MAGIC(icount, EXT2_ET_MAGIC_ICOUNT);

	if (!ino || (ino > icount->num_inodes))
		return EXT2_ET_INVALID_ARGUMENT;

	if (icount->fullmap) {
		curr_value = icount_16_xlate(icount->fullmap[ino] + 1);
		icount->fullmap[ino] = curr_value;
	} else if (ext2fs_test_inode_bitmap2(icount->single, ino)) {
		/*
		 * If the existing count is 1, then we know there is
		 * no entry in the list.
		 */
		if (set_inode_count(icount, ino, 2))
			return EXT2_ET_NO_MEMORY;
		curr_value = 2;
		ext2fs_unmark_inode_bitmap2(icount->single, ino);
	} else if (icount->multiple) {
		/*
		 * The count is either zero or greater than 1; if the
		 * inode is set in icount->multiple, then there should
		 * be an entry in the list, so we need to fix it.
		 */
		if (ext2fs_test_inode_bitmap2(icount->multiple, ino)) {
			get_inode_count(icount, ino, &curr_value);
			curr_value++;
			if (set_inode_count(icount, ino, curr_value))
				return EXT2_ET_NO_MEMORY;
		} else {
			/*
			 * The count was zero; mark the single bitmap
			 * and return.
			 */
			ext2fs_mark_inode_bitmap2(icount->single, ino);
			if (ret)
				*ret = 1;
			return 0;
		}
	} else {
		/*
		 * The count is either zero or greater than 1; try to
		 * find an entry in the list to determine which.
		 */
		get_inode_count(icount, ino, &curr_value);
		curr_value++;
		if (set_inode_count(icount, ino, curr_value))
			return EXT2_ET_NO_MEMORY;
	}
	if (icount->multiple)
		ext2fs_mark_inode_bitmap2(icount->multiple, ino);
	if (ret)
		*ret = icount_16_xlate(curr_value);
	return 0;
}

errcode_t ext2fs_icount_decrement(ext2_icount_t icount, ext2_ino_t ino,
				  __u16 *ret)
{
	__u32			curr_value;

	EXT2_CHECK_MAGIC(icount, EXT2_ET_MAGIC_ICOUNT);

	if (!ino || (ino > icount->num_inodes))
		return EXT2_ET_INVALID_ARGUMENT;

	if (icount->fullmap) {
		if (!icount->fullmap[ino])
			return EXT2_ET_INVALID_ARGUMENT;

		curr_value = --icount->fullmap[ino];
		if (ret)
			*ret = icount_16_xlate(curr_value);
		return 0;
	}

	if (ext2fs_test_inode_bitmap2(icount->single, ino)) {
		ext2fs_unmark_inode_bitmap2(icount->single, ino);
		if (icount->multiple)
			ext2fs_unmark_inode_bitmap2(icount->multiple, ino);
		else {
			set_inode_count(icount, ino, 0);
		}
		if (ret)
			*ret = 0;
		return 0;
	}

	if (icount->multiple &&
	    !ext2fs_test_inode_bitmap2(icount->multiple, ino))
		return EXT2_ET_INVALID_ARGUMENT;

	get_inode_count(icount, ino, &curr_value);
	if (!curr_value)
		return EXT2_ET_INVALID_ARGUMENT;
	curr_value--;
	if (set_inode_count(icount, ino, curr_value))
		return EXT2_ET_NO_MEMORY;

	if (curr_value == 1)
		ext2fs_mark_inode_bitmap2(icount->single, ino);
	if ((curr_value == 0) && icount->multiple)
		ext2fs_unmark_inode_bitmap2(icount->multiple, ino);

	if (ret)
		*ret = icount_16_xlate(curr_value);
	return 0;
}

errcode_t ext2fs_icount_store(ext2_icount_t icount, ext2_ino_t ino,
			      __u16 count)
{
	EXT2_CHECK_MAGIC(icount, EXT2_ET_MAGIC_ICOUNT);

	if (!ino || (ino > icount->num_inodes))
		return EXT2_ET_INVALID_ARGUMENT;

	if (icount->fullmap)
		return set_inode_count(icount, ino, count);

	if (count == 1) {
		ext2fs_mark_inode_bitmap2(icount->single, ino);
		if (icount->multiple)
			ext2fs_unmark_inode_bitmap2(icount->multiple, ino);
		return 0;
	}
	if (count == 0) {
		ext2fs_unmark_inode_bitmap2(icount->single, ino);
		if (icount->multiple) {
			/*
			 * If the icount->multiple bitmap is enabled,
			 * we can just clear both bitmaps and we're done
			 */
			ext2fs_unmark_inode_bitmap2(icount->multiple, ino);
		} else
			set_inode_count(icount, ino, 0);
		return 0;
	}

	if (set_inode_count(icount, ino, count))
		return EXT2_ET_NO_MEMORY;
	ext2fs_unmark_inode_bitmap2(icount->single, ino);
	if (icount->multiple)
		ext2fs_mark_inode_bitmap2(icount->multiple, ino);
	return 0;
}

ext2_ino_t ext2fs_get_icount_size(ext2_icount_t icount)
{
	if (!icount || icount->magic != EXT2_ET_MAGIC_ICOUNT)
		return 0;

	return icount->size;
}

#ifdef DEBUG

ext2_filsys	test_fs;
ext2_icount_t	icount;

#define EXIT		0x00
#define FETCH		0x01
#define STORE		0x02
#define INCREMENT	0x03
#define DECREMENT	0x04

struct test_program {
	int		cmd;
	ext2_ino_t	ino;
	__u16		arg;
	__u16		expected;
};

struct test_program prog[] = {
	{ STORE, 42, 42, 42 },
	{ STORE, 1,  1, 1 },
	{ STORE, 2,  2, 2 },
	{ STORE, 3,  3, 3 },
	{ STORE, 10, 1, 1 },
	{ STORE, 42, 0, 0 },
	{ INCREMENT, 5, 0, 1 },
	{ INCREMENT, 5, 0, 2 },
	{ INCREMENT, 5, 0, 3 },
	{ INCREMENT, 5, 0, 4 },
	{ DECREMENT, 5, 0, 3 },
	{ DECREMENT, 5, 0, 2 },
	{ DECREMENT, 5, 0, 1 },
	{ DECREMENT, 5, 0, 0 },
	{ FETCH, 10, 0, 1 },
	{ FETCH, 1, 0, 1 },
	{ FETCH, 2, 0, 2 },
	{ FETCH, 3, 0, 3 },
	{ INCREMENT, 1, 0, 2 },
	{ DECREMENT, 2, 0, 1 },
	{ DECREMENT, 2, 0, 0 },
	{ FETCH, 12, 0, 0 },
	{ EXIT, 0, 0, 0 }
};

struct test_program extended[] = {
	{ STORE, 1,  1, 1 },
	{ STORE, 2,  2, 2 },
	{ STORE, 3,  3, 3 },
	{ STORE, 4,  4, 4 },
	{ STORE, 5,  5, 5 },
	{ STORE, 6,  1, 1 },
	{ STORE, 7,  2, 2 },
	{ STORE, 8,  3, 3 },
	{ STORE, 9,  4, 4 },
	{ STORE, 10, 5, 5 },
	{ STORE, 11, 1, 1 },
	{ STORE, 12, 2, 2 },
	{ STORE, 13, 3, 3 },
	{ STORE, 14, 4, 4 },
	{ STORE, 15, 5, 5 },
	{ STORE, 16, 1, 1 },
	{ STORE, 17, 2, 2 },
	{ STORE, 18, 3, 3 },
	{ STORE, 19, 4, 4 },
	{ STORE, 20, 5, 5 },
	{ STORE, 21, 1, 1 },
	{ STORE, 22, 2, 2 },
	{ STORE, 23, 3, 3 },
	{ STORE, 24, 4, 4 },
	{ STORE, 25, 5, 5 },
	{ STORE, 26, 1, 1 },
	{ STORE, 27, 2, 2 },
	{ STORE, 28, 3, 3 },
	{ STORE, 29, 4, 4 },
	{ STORE, 30, 5, 5 },
	{ EXIT, 0, 0, 0 }
};

static struct test_program nonseq[] = {
	{ STORE, 200, 3, 3 },
	{ STORE, 50,  4, 4 },
	{ STORE, 150, 2, 2 },
	{ STORE, 25,  5, 5 },
	{ STORE, 175, 6, 6 },
	{ STORE, 100, 2, 2 },
	{ STORE, 75,  3, 3 },
	{ STORE, 125, 4, 4 },
	{ STORE, 10,  7, 7 },
	{ STORE, 300, 8, 8 },
	{ INCREMENT, 100, 0, 3 },
	{ INCREMENT, 25,  0, 6 },
	{ DECREMENT, 150, 0, 1 },
	{ DECREMENT, 150, 0, 0 },
	{ FETCH, 200, 0, 3 },
	{ FETCH, 10,  0, 7 },
	{ FETCH, 125, 0, 4 },
	{ FETCH, 75,  0, 3 },
	{ FETCH, 300, 0, 8 },
	{ FETCH, 150, 0, 0 },
	{ EXIT, 0, 0, 0 }
};

#define CHECK_TST(cond, msg) do { \
	if (!(cond)) { \
		fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, msg); \
		problems++; \
	} \
} while (0)

static int run_extra_tests(void)
{
	ext2_icount_t		ic = NULL, hint = NULL;
	struct ext2_icount	bad_ic;
	ext2_ino_t		oob_ino, tmp_ino, saved_cnt;
	__u16			val = 0;
	int			b, problems = 0;
	int			backends[3] = {
		0,
		EXT2_ICOUNT_OPT_INCREMENT,
		EXT2_ICOUNT_OPT_FULLMAP | EXT2_ICOUNT_OPT_INCREMENT
	};
	FILE			*devnull;

	oob_ino = test_fs->super->s_inodes_count + 1;
	memset(&bad_ic, 0, sizeof(bad_ic));
	bad_ic.num_inodes = 10;

	/* 1. NULL and bad-magic lifecycle helpers */
	ext2fs_free_icount(NULL);
	CHECK_TST(ext2fs_get_icount_size(NULL) == 0,
		  "get_icount_size(NULL) == 0");
	CHECK_TST(ext2fs_get_icount_size(&bad_ic) == 0,
		  "get_icount_size(bad_magic) == 0");
	CHECK_TST(ext2fs_icount_fetch(&bad_ic, 1, &val) ==
		  EXT2_ET_MAGIC_ICOUNT, "fetch bad magic");
	CHECK_TST(ext2fs_icount_store(&bad_ic, 1, 2) ==
		  EXT2_ET_MAGIC_ICOUNT, "store bad magic");
	CHECK_TST(ext2fs_icount_increment(&bad_ic, 1, &val) ==
		  EXT2_ET_MAGIC_ICOUNT, "increment bad magic");
	CHECK_TST(ext2fs_icount_decrement(&bad_ic, 1, &val) ==
		  EXT2_ET_MAGIC_ICOUNT, "decrement bad magic");
	CHECK_TST(ext2fs_icount_decrement(&bad_ic, 999, &val) ==
		  EXT2_ET_MAGIC_ICOUNT, "decrement bad magic before bounds");
	CHECK_TST(ext2fs_icount_store(&bad_ic, 999, 2) ==
		  EXT2_ET_MAGIC_ICOUNT, "store bad magic before bounds");
	CHECK_TST(ext2fs_icount_validate(&bad_ic, stdout) ==
		  EXT2_ET_MAGIC_ICOUNT, "validate bad magic");
	CHECK_TST(ext2fs_create_icount2(test_fs, 0, 4, &bad_ic, &ic) ==
		  EXT2_ET_MAGIC_ICOUNT, "create_icount2 bad hint magic");

	/* 2. FULLMAP without INCREMENT falls back to bitmap + sorted list */
	CHECK_TST(ext2fs_create_icount(test_fs, EXT2_ICOUNT_OPT_FULLMAP,
				       4, &ic) == 0,
		  "create_icount(FULLMAP without INCREMENT)");
	CHECK_TST(ic->fullmap == NULL && ic->single != NULL &&
		  ic->multiple == NULL && ic->list != NULL,
		  "FULLMAP without INCREMENT falls back to single+list");
	ext2fs_free_icount(ic);

	/* 3. Bounds, zero-decrement, ret out-param, and 65500 saturation */
	for (b = 0; b < 3; b++) {
		CHECK_TST(ext2fs_create_icount(test_fs, backends[b], 4, &ic) == 0,
			  "create_icount backend");
		if (b == 2)
			CHECK_TST(ic->fullmap != NULL, "fullmap allocated");

		/* Bounds checks: ino == 0 and ino > num_inodes */
		CHECK_TST(ext2fs_icount_fetch(ic, 0, &val) ==
			  EXT2_ET_INVALID_ARGUMENT, "fetch ino 0");
		CHECK_TST(ext2fs_icount_fetch(ic, oob_ino, &val) ==
			  EXT2_ET_INVALID_ARGUMENT, "fetch oob ino");
		CHECK_TST(ext2fs_icount_store(ic, 0, 1) ==
			  EXT2_ET_INVALID_ARGUMENT, "store ino 0");
		CHECK_TST(ext2fs_icount_store(ic, oob_ino, 1) ==
			  EXT2_ET_INVALID_ARGUMENT, "store oob ino");
		CHECK_TST(ext2fs_icount_increment(ic, 0, &val) ==
			  EXT2_ET_INVALID_ARGUMENT, "increment ino 0");
		CHECK_TST(ext2fs_icount_increment(ic, oob_ino, &val) ==
			  EXT2_ET_INVALID_ARGUMENT, "increment oob ino");
		CHECK_TST(ext2fs_icount_decrement(ic, 0, &val) ==
			  EXT2_ET_INVALID_ARGUMENT, "decrement ino 0");
		CHECK_TST(ext2fs_icount_decrement(ic, oob_ino, &val) ==
			  EXT2_ET_INVALID_ARGUMENT, "decrement oob ino");

		/* Decrementing a zero-count inode fails */
		CHECK_TST(ext2fs_icount_decrement(ic, 7, &val) ==
			  EXT2_ET_INVALID_ARGUMENT, "decrement zero count");

		/* Non-NULL ret output across 0 -> 1 -> 2 -> 3 -> 2 -> 1 -> 0 */
		CHECK_TST(ext2fs_icount_increment(ic, 8, &val) == 0 && val == 1,
			  "inc 0->1 ret");
		CHECK_TST(ext2fs_icount_increment(ic, 8, &val) == 0 && val == 2,
			  "inc 1->2 ret");
		CHECK_TST(ext2fs_icount_increment(ic, 8, &val) == 0 && val == 3,
			  "inc 2->3 ret");
		CHECK_TST(ext2fs_icount_decrement(ic, 8, &val) == 0 && val == 2,
			  "dec 3->2 ret");
		CHECK_TST(ext2fs_icount_decrement(ic, 8, &val) == 0 && val == 1,
			  "dec 2->1 ret");
		CHECK_TST(ext2fs_icount_decrement(ic, 8, &val) == 0 && val == 0,
			  "dec 1->0 ret");
		CHECK_TST(ext2fs_icount_decrement(ic, 8, &val) ==
			  EXT2_ET_INVALID_ARGUMENT, "dec after 0 fails");

		/* 16-bit saturation via icount_16_xlate at 65500 */
		CHECK_TST(ext2fs_icount_store(ic, 9, 65535) == 0,
			  "store 65535");
		CHECK_TST(ext2fs_icount_fetch(ic, 9, &val) == 0 &&
			  val == 65500, "fetch 65535 saturates to 65500");
		CHECK_TST(ext2fs_icount_store(ic, 9, 65499) == 0,
			  "store 65499");
		CHECK_TST(ext2fs_icount_increment(ic, 9, &val) == 0 &&
			  val == 65500, "inc 65499 -> 65500");
		CHECK_TST(ext2fs_icount_increment(ic, 9, &val) == 0 &&
			  val == 65500, "inc 65500 saturates at 65500");

		/* Final valid inode (ino == s_inodes_count) boundary */
		tmp_ino = test_fs->super->s_inodes_count;
		CHECK_TST(ext2fs_icount_fetch(ic, tmp_ino, &val) == 0 &&
			  val == 0, "fetch last_ino initial 0");
		CHECK_TST(ext2fs_icount_increment(ic, tmp_ino, &val) == 0 &&
			  val == 1, "inc last_ino 0->1");
		CHECK_TST(ext2fs_icount_store(ic, tmp_ino, 5) == 0,
			  "store last_ino 5");
		CHECK_TST(ext2fs_icount_decrement(ic, tmp_ino, &val) == 0 &&
			  val == 4, "dec last_ino 5->4");
		CHECK_TST(ext2fs_icount_fetch(ic, tmp_ino, &val) == 0 &&
			  val == 4, "fetch last_ino 4");

		ext2fs_free_icount(ic);
	}

	/* 4. Hint icount pre-population and ext2fs_icount_validate corruption */
	CHECK_TST(ext2fs_create_icount2(test_fs, 0, 12, NULL, &hint) == 0,
		  "create hint icount");
	CHECK_TST(ext2fs_icount_store(hint, 30, 3) == 0 &&
		  ext2fs_icount_store(hint, 10, 2) == 0 &&
		  ext2fs_icount_store(hint, 20, 4) == 0,
		  "populate hint list out of order");
	CHECK_TST(hint->count == 3 &&
		  hint->list[0].ino == 10 &&
		  hint->list[1].ino == 20 &&
		  hint->list[2].ino == 30,
		  "hint list kept sorted");

	CHECK_TST(ext2fs_create_icount2(test_fs, EXT2_ICOUNT_OPT_INCREMENT,
					2, hint, &ic) == 0,
		  "create icount from hint");
	CHECK_TST(ext2fs_get_icount_size(ic) == ext2fs_get_icount_size(hint),
		  "icount adopts larger hint size");
	CHECK_TST(ic->count == 3 &&
		  ic->list[0].ino == 10 && ic->list[0].count == 0 &&
		  ic->list[1].ino == 20 && ic->list[1].count == 0 &&
		  ic->list[2].ino == 30 && ic->list[2].count == 0,
		  "icount pre-populated with zeroed hint entries");
	CHECK_TST(ext2fs_icount_validate(ic, stdout) == 0,
		  "validate hinted icount");
	CHECK_TST(ext2fs_icount_fetch(ic, 20, &val) == 0 && val == 0,
		  "pre-populated hint entry fetches 0 before store");
	CHECK_TST(ext2fs_icount_increment(ic, 20, &val) == 0 && val == 1 &&
		  ext2fs_icount_increment(ic, 20, &val) == 0 && val == 2,
		  "incrementing pre-populated hint entry reuses list slot");
	CHECK_TST(ic->count == 3, "no duplicate slot added for hinted ino");

	devnull = fopen("/dev/null", "w");
	if (devnull) {
		saved_cnt = ic->count;
		ic->count = ic->size + 1;
		CHECK_TST(ext2fs_icount_validate(ic, devnull) ==
			  EXT2_ET_INVALID_ARGUMENT,
			  "validate detects count > size");
		ic->count = saved_cnt;

		tmp_ino = ic->list[0].ino;
		ic->list[0].ino = ic->list[1].ino;
		CHECK_TST(ext2fs_icount_validate(ic, devnull) ==
			  EXT2_ET_INVALID_ARGUMENT,
			  "validate detects unsorted/duplicate list");
		ic->list[0].ino = tmp_ino;
		fclose(devnull);
	}
	CHECK_TST(ext2fs_icount_validate(ic, stdout) == 0,
		  "validate succeeds after restoring list");

	ext2fs_free_icount(ic);
	ext2fs_free_icount(hint);
	return problems;
}

/*
 * Setup the variables for doing the inode scan test.
 */
static void setup(void)
{
	errcode_t	retval;
	struct ext2_super_block param;

	initialize_ext2_error_table();

	memset(&param, 0, sizeof(param));
	ext2fs_blocks_count_set(&param, 12000);

	retval = ext2fs_initialize("test fs", EXT2_FLAG_64BITS, &param,
				   test_io_manager, &test_fs);
	if (retval) {
		com_err("setup", retval,
			"while initializing filesystem");
		exit(1);
	}
	retval = ext2fs_allocate_tables(test_fs);
	if (retval) {
		com_err("setup", retval,
			"while allocating tables for test filesystem");
		exit(1);
	}
}

static int run_test(int flags, int size, char *dir, struct test_program *prog)
{
	errcode_t	retval;
	ext2_icount_t	icount;
	struct test_program *pc;
	__u16		result;
	int		problem = 0;

	if (dir) {
#ifdef CONFIG_TDB
		retval = ext2fs_create_icount_tdb(test_fs, dir,
						  flags, &icount);
		if (retval) {
			com_err("run_test", retval,
				"while creating icount using tdb");
			exit(1);
		}
#else
		printf("Skipped\n");
		return 0;
#endif
	} else {
		retval = ext2fs_create_icount2(test_fs, flags, size, 0,
					       &icount);
		if (retval) {
			com_err("run_test", retval, "while creating icount");
			exit(1);
		}
	}
	for (pc = prog; pc->cmd != EXIT; pc++) {
		switch (pc->cmd) {
		case FETCH:
			printf("icount_fetch(%u) = ", pc->ino);
			break;
		case STORE:
			retval = ext2fs_icount_store(icount, pc->ino, pc->arg);
			if (retval) {
				com_err("run_test", retval,
					"while calling icount_store");
				exit(1);
			}
			printf("icount_store(%u, %u) = ", pc->ino, pc->arg);
			break;
		case INCREMENT:
			retval = ext2fs_icount_increment(icount, pc->ino, 0);
			if (retval) {
				com_err("run_test", retval,
					"while calling icount_increment");
				exit(1);
			}
			printf("icount_increment(%u) = ", pc->ino);
			break;
		case DECREMENT:
			retval = ext2fs_icount_decrement(icount, pc->ino, 0);
			if (retval) {
				com_err("run_test", retval,
					"while calling icount_decrement");
				exit(1);
			}
			printf("icount_decrement(%u) = ", pc->ino);
			break;
		}
		retval = ext2fs_icount_fetch(icount, pc->ino, &result);
		if (retval) {
			com_err("run_test", retval,
				"while calling icount_fetch");
			exit(1);
		}
		printf("%u (%s)\n", result, (result == pc->expected) ?
		       "OK" : "NOT OK");
		if (result != pc->expected)
			problem++;
	}
	printf("icount size is %u\n", ext2fs_get_icount_size(icount));
	retval = ext2fs_icount_validate(icount, stdout);
	if (retval) {
		com_err("run_test", retval, "while calling icount_validate");
		exit(1);
	}
	ext2fs_free_icount(icount);
	return problem;
}


int main(int argc EXT2FS_ATTR((unused)), char **argv EXT2FS_ATTR((unused)))
{
	int failed = 0;

	setup();
	printf("Standard icount run:\n");
	failed += run_test(0, 0, 0, prog);
	printf("\nMultiple bitmap test:\n");
	failed += run_test(EXT2_ICOUNT_OPT_INCREMENT, 0, 0, prog);
	printf("\nFullmap icount test:\n");
	failed += run_test(EXT2_ICOUNT_OPT_FULLMAP | EXT2_ICOUNT_OPT_INCREMENT,
			   0, 0, prog);
	printf("\nResizing icount:\n");
	failed += run_test(0, 3, 0, extended);
	printf("\nNon-sequential insertion and binary search:\n");
	failed += run_test(EXT2_ICOUNT_OPT_INCREMENT, 3, 0, nonseq);
	printf("\nStandard icount run with tdb:\n");
	failed += run_test(0, 0, ".", prog);
	printf("\nMultiple bitmap test with tdb:\n");
	failed += run_test(EXT2_ICOUNT_OPT_INCREMENT, 0, ".", prog);
	printf("\nExtra icount unit tests:\n");
	failed += run_extra_tests();
	ext2fs_free(test_fs);
	if (failed)
		printf("FAILED!\n");
	else
		printf("Extra icount unit tests: OK\n");
	return failed ? 1 : 0;
}
#endif
