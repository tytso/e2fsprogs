/*
 * badblocks.c --- routines to manipulate the bad block structure
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
#if HAVE_ERRNO_H
#include <errno.h>
#endif

#include <linux/ext2_fs.h>

#include "ext2fs.h"

/*
 * This procedure create an empty badblocks list.
 */
errcode_t badblocks_list_create(badblocks_list *ret, int size)
{
	badblocks_list	bb;

	bb = malloc(sizeof(struct struct_badblocks_list));
	if (!bb)
		return ENOMEM;
	memset(bb, 0, sizeof(struct struct_badblocks_list));
	bb->magic = EXT2_ET_MAGIC_BADBLOCKS_LIST;
	bb->size = size ? size : 10;
	bb->list = malloc(bb->size * sizeof(blk_t));
	if (!bb->list) {
		free(bb);
		return ENOMEM;
	}
	*ret = bb;
	return 0;
}

/*
 * This procedure frees a badblocks list.
 */
void badblocks_list_free(badblocks_list bb)
{
	if (bb->magic != EXT2_ET_MAGIC_BADBLOCKS_LIST)
		return;

	if (bb->list)
		free(bb->list);
	bb->list = 0;
	free(bb);
}

/*
 * This procedure adds a block to a badblocks list.
 */
errcode_t badblocks_list_add(badblocks_list bb, blk_t blk)
{
	int	i;

	EXT2_CHECK_MAGIC(bb, EXT2_ET_MAGIC_BADBLOCKS_LIST);

	for (i=0; i < bb->num; i++)
		if (bb->list[i] == blk)
			return 0;

	if (bb->num >= bb->size) {
		bb->size += 10;
		bb->list = realloc(bb->list, bb->size * sizeof(blk_t));
		if (!bb->list) {
			bb->size = 0;
			bb->num = 0;
			return ENOMEM;
		}
	}

	bb->list[bb->num++] = blk;
	return 0;
}

/*
 * This procedure tests to see if a particular block is on a badblocks
 * list.
 */
int badblocks_list_test(badblocks_list bb, blk_t blk)
{
	int	i;

	EXT2_CHECK_MAGIC(bb, EXT2_ET_MAGIC_BADBLOCKS_LIST);

	for (i=0; i < bb->num; i++)
		if (bb->list[i] == blk)
			return 1;

	return 0;
}

errcode_t badblocks_list_iterate_begin(badblocks_list bb,
				       badblocks_iterate *ret)
{
	badblocks_iterate iter;

	EXT2_CHECK_MAGIC(bb, EXT2_ET_MAGIC_BADBLOCKS_LIST);

	iter = malloc(sizeof(struct struct_badblocks_iterate));
	if (!iter)
		return ENOMEM;

	iter->magic = EXT2_ET_MAGIC_BADBLOCKS_ITERATE;
	iter->bb = bb;
	iter->ptr = 0;
	*ret = iter;
	return 0;
}

int badblocks_list_iterate(badblocks_iterate iter, blk_t *blk)
{
	badblocks_list	bb;

	if (iter->magic != EXT2_ET_MAGIC_BADBLOCKS_ITERATE)
		return 0;

	bb = iter->bb;

	if (bb->magic != EXT2_ET_MAGIC_BADBLOCKS_LIST)
		return 0;
	
	if (iter->ptr < bb->num) {
		*blk = bb->list[iter->ptr++];
		return 1;
	} 
	*blk = 0;
	return 0;
}

void badblocks_list_iterate_end(badblocks_iterate iter)
{
	if (!iter || (iter->magic != EXT2_ET_MAGIC_BADBLOCKS_ITERATE))
		return;

	iter->bb = 0;
	free(iter);
}




		
