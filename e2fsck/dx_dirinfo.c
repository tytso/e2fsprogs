/*
 * dirinfo.c --- maintains the directory information table for e2fsck.
 *
 * Copyright (C) 1993 Theodore Ts'o.  This file may be redistributed
 * under the terms of the GNU Public License.
 */

#include <assert.h>
#include "config.h"
#include "e2fsck.h"

/*
 * This subroutine is called during pass1 to create a directory info
 * entry.  During pass1, the passed-in parent is 0; it will get filled
 * in during pass2.
 */
void e2fsck_add_dx_dir(e2fsck_t ctx, ext2_ino_t ino, struct ext2_inode *inode,
		       int num_blocks)
{
	struct dx_dir_info *dir;
	ext2_ino_t	i, j;
	errcode_t	retval;
	unsigned long	old_size;

#if 0
	printf("add_dx_dir_info for inode %lu...\n", ino);
#endif
	if (!ctx->dx_dir_info) {
		ctx->dx_dir_info_count = 0;
		ctx->dx_dir_info_size = 100; /* Guess */
		ctx->dx_dir_info  = (struct dx_dir_info *)
			e2fsck_allocate_memory(ctx, ctx->dx_dir_info_size
					       * sizeof (struct dx_dir_info),
					       "directory map");
	}

	if (ctx->dx_dir_info_count >= ctx->dx_dir_info_size) {
		old_size = ctx->dx_dir_info_size * sizeof(struct dx_dir_info);
		ctx->dx_dir_info_size += 10;
		retval = ext2fs_resize_mem(old_size, ctx->dx_dir_info_size *
					   sizeof(struct dx_dir_info),
					   &ctx->dx_dir_info);
		if (retval) {
			fprintf(stderr, "Couldn't reallocate dx_dir_info "
				"structure to %u entries\n",
				ctx->dx_dir_info_size);
			fatal_error(ctx, 0);
			ctx->dx_dir_info_size -= 10;
			return;
		}
	}

	/*
	 * Normally, add_dx_dir_info is called with each inode in
	 * sequential order; but once in a while (like when pass 3
	 * needs to recreate the root directory or lost+found
	 * directory) it is called out of order.  In those cases, we
	 * need to move the dx_dir_info entries down to make room, since
	 * the dx_dir_info array needs to be sorted by inode number for
	 * get_dx_dir_info()'s sake.
	 */
	if (ctx->dx_dir_info_count &&
	    ctx->dx_dir_info[ctx->dx_dir_info_count-1].ino >= ino) {
		for (i = ctx->dx_dir_info_count-1; i > 0; i--)
			if (ctx->dx_dir_info[i-1].ino < ino)
				break;
		dir = &ctx->dx_dir_info[i];
		if (dir->ino != ino)
			for (j = ctx->dx_dir_info_count++; j > i; j--)
				ctx->dx_dir_info[j] = ctx->dx_dir_info[j-1];
	} else
		dir = &ctx->dx_dir_info[ctx->dx_dir_info_count++];

	dir->ino = ino;
	dir->numblocks = num_blocks;
	dir->hashversion = 0;
	dir->casefolded_hash = !!(inode->i_flags & EXT4_CASEFOLD_FL);
	dir->dx_block = e2fsck_allocate_memory(ctx, num_blocks
				       * sizeof (struct dx_dirblock_info),
				       "dx_block info array");
}

/*
 * Merge two sorted dir info to @dest
 */
void e2fsck_merge_dx_dir(e2fsck_t global_ctx, e2fsck_t thread_ctx)
{
	struct dx_dir_info *src_array = thread_ctx->dx_dir_info;
	struct dx_dir_info *dest_array = global_ctx->dx_dir_info;
	size_t size_dx_info = sizeof(struct dx_dir_info);
	ext2_ino_t size = global_ctx->dx_dir_info_size;
	ext2_ino_t src_count = thread_ctx->dx_dir_info_count;
	ext2_ino_t dest_count = global_ctx->dx_dir_info_count;
	ext2_ino_t total_count = src_count + dest_count;
	struct dx_dir_info *array;
	struct dx_dir_info *array_ptr;
	ext2_ino_t src_index = 0, dest_index = 0;

	if (thread_ctx->dx_dir_info_count == 0)
		return;

	if (size < total_count)
		size = total_count;

	array = e2fsck_allocate_memory(global_ctx, size * size_dx_info,
				       "directory map");
	array_ptr = array;
	/*
	 * This can be improved by binary search and memcpy, but codes
	 * would be more complex. And if the groups distributed to each
	 * thread are strided, this implementation won't be too bad
	 * comparing to the optimiztion.
	 */
	while (src_index < src_count || dest_index < dest_count) {
		if (src_index >= src_count) {
			memcpy(array_ptr, &dest_array[dest_index],
			       (dest_count - dest_index) * size_dx_info);
			break;
		}
		if (dest_index >= dest_count) {
			memcpy(array_ptr, &src_array[src_index],
			       (src_count - src_index) * size_dx_info);
			break;
		}
		if (src_array[src_index].ino < dest_array[dest_index].ino) {
			*array_ptr = src_array[src_index];
			src_index++;
		} else {
			assert(src_array[src_index].ino >
			       dest_array[dest_index].ino);
			*array_ptr = dest_array[dest_index];
			dest_index++;
		}
		array_ptr++;
	}

	if (global_ctx->dx_dir_info)
		ext2fs_free_mem(&global_ctx->dx_dir_info);
	if (thread_ctx->dx_dir_info)
		ext2fs_free_mem(&thread_ctx->dx_dir_info);
	global_ctx->dx_dir_info = array;
	global_ctx->dx_dir_info_size = size;
	global_ctx->dx_dir_info_count = total_count;
}

/*
 * get_dx_dir_info() --- given an inode number, try to find the directory
 * information entry for it.
 */
struct dx_dir_info *e2fsck_get_dx_dir_info(e2fsck_t ctx, ext2_ino_t ino)
{
	ext2_ino_t low, high, mid;

	low = 0;
	high = ctx->dx_dir_info_count-1;
	if (!ctx->dx_dir_info)
		return 0;
	if (ino == ctx->dx_dir_info[low].ino)
		return &ctx->dx_dir_info[low];
	if  (ino == ctx->dx_dir_info[high].ino)
		return &ctx->dx_dir_info[high];

	while (low < high) {
		/* sum may overflow, but result will fit into mid again */
		mid = (unsigned long long)(low + high) / 2;
		if (mid == low || mid == high)
			break;
		if (ino == ctx->dx_dir_info[mid].ino)
			return &ctx->dx_dir_info[mid];
		if (ino < ctx->dx_dir_info[mid].ino)
			high = mid;
		else
			low = mid;
	}
	return 0;
}

/*
 * Free the dx_dir_info structure when it isn't needed any more.
 */
void e2fsck_free_dx_dir_info(e2fsck_t ctx)
{
	struct dx_dir_info *dir;
	ext2_ino_t i;

	if (ctx->dx_dir_info) {
		dir = ctx->dx_dir_info;
		for (i=0; i < ctx->dx_dir_info_count; i++,dir++) {
			if (dir->dx_block) {
				ext2fs_free_mem(&dir->dx_block);
				dir->dx_block = 0;
			}
		}
		ext2fs_free_mem(&ctx->dx_dir_info);
		ctx->dx_dir_info = 0;
	}
	ctx->dx_dir_info_size = 0;
	ctx->dx_dir_info_count = 0;
}

/*
 * Return the count of number of directories in the dx_dir_info structure
 */
ext2_ino_t e2fsck_get_num_dx_dirinfo(e2fsck_t ctx)
{
	return ctx->dx_dir_info_count;
}

/*
 * A simple interator function
 */
struct dx_dir_info *e2fsck_dx_dir_info_iter(e2fsck_t ctx, ext2_ino_t *control)
{
	if (*control >= ctx->dx_dir_info_count)
		return 0;

	return ctx->dx_dir_info + (*control)++;
}
