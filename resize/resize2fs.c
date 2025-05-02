/*
 * resize2fs.c --- ext2 main routine
 *
 * Copyright (C) 1997, 1998 by Theodore Ts'o and
 * 	PowerQuest, Inc.
 *
 * Copyright (C) 1999, 2000 by Theodore Ts'o
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 */

/*
 * Resizing a filesystem consists of the following phases:
 *
 *	1.  Adjust superblock and write out new parts of the inode
 * 		table
 * 	2.  Determine blocks which need to be relocated, and copy the
 * 		contents of blocks from their old locations to the new ones.
 * 	3.  Scan the inode table, doing the following:
 * 		a.  If blocks have been moved, update the block
 * 			pointers in the inodes and indirect blocks to
 * 			point at the new block locations.
 * 		b.  If parts of the inode table need to be evacuated,
 * 			copy inodes from their old locations to their
 * 			new ones.
 * 		c.  If (b) needs to be done, note which blocks contain
 * 			directory information, since we will need to
 * 			update the directory information.
 * 	4.  Update the directory blocks with the new inode locations.
 * 	5.  Move the inode tables, if necessary.
 */

#include "config.h"
#include "resize2fs.h"
#include <time.h>

#ifdef __linux__			/* Kludge for debugging */
#define RESIZE2FS_DEBUG
#endif

static void fix_uninit_block_bitmaps(ext2_filsys fs);
static errcode_t adjust_superblock(ext2_resize_t rfs, blk64_t new_size);
static errcode_t blocks_to_move(ext2_resize_t rfs);
static errcode_t block_mover(ext2_resize_t rfs);
static errcode_t inode_scan_and_fix(ext2_resize_t rfs);
static errcode_t inode_ref_fix(ext2_resize_t rfs);
static errcode_t move_itables(ext2_resize_t rfs);
static errcode_t fix_resize_inode(ext2_filsys fs);
static errcode_t fix_orphan_file_inode(ext2_filsys fs);
static errcode_t resize2fs_calculate_summary_stats(ext2_filsys fs);
static errcode_t fix_sb_journal_backup(ext2_filsys fs);
static errcode_t mark_table_blocks(ext2_filsys fs,
				   ext2fs_block_bitmap bmap);
static errcode_t clear_sparse_super2_last_group(ext2_resize_t rfs);
static errcode_t reserve_sparse_super2_last_group(ext2_resize_t rfs,
						 ext2fs_block_bitmap meta_bmap);
static errcode_t resize_group_descriptors(ext2_resize_t rfs, blk64_t new_size);
static errcode_t move_bg_metadata(ext2_resize_t rfs);
static errcode_t zero_high_bits_in_inodes(ext2_resize_t rfs);

/*
 * Some helper functions to check if a block is in a metadata area
 */
static inline int is_block_bm(ext2_filsys fs, unsigned int grp, blk64_t blk)
{
	return blk == ext2fs_block_bitmap_loc(fs, grp);
}

static inline int is_inode_bm(ext2_filsys fs, unsigned int grp, blk64_t blk)
{
	return blk == ext2fs_inode_bitmap_loc(fs, grp);
}

static int is_inode_tb(ext2_filsys fs, unsigned int grp, blk64_t blk)
{
	return blk >= ext2fs_inode_table_loc(fs, grp) &&
	       blk < (ext2fs_inode_table_loc(fs, grp) +
		      fs->inode_blocks_per_group);
}

/* Some bigalloc helper macros which are more succinct... */
#define B2C(x)	EXT2FS_B2C(fs, (x))
#define C2B(x)	EXT2FS_C2B(fs, (x))
#define EQ_CLSTR(x, y) (B2C(x) == B2C(y))
#define LE_CLSTR(x, y) (B2C(x) <= B2C(y))
#define LT_CLSTR(x, y) (B2C(x) <  B2C(y))
#define GE_CLSTR(x, y) (B2C(x) >= B2C(y))
#define GT_CLSTR(x, y) (B2C(x) >  B2C(y))

static int lazy_itable_init;

/*
 * This is the top-level routine which does the dirty deed....
 */
errcode_t resize_fs(ext2_filsys fs, blk64_t *new_size, int flags,
	    errcode_t (*progress)(ext2_resize_t rfs, int pass,
					  unsigned long cur,
					  unsigned long max_val))
{
	ext2_resize_t	rfs;
	errcode_t	retval;
	struct resource_track	rtrack, overall_track;

	/*
	 * Create the data structure
	 */
	retval = ext2fs_get_mem(sizeof(struct ext2_resize_struct), &rfs);
	if (retval)
		return retval;

	memset(rfs, 0, sizeof(struct ext2_resize_struct));
	fs->priv_data = rfs;
	rfs->old_fs = fs;
	rfs->flags = flags;
	rfs->itable_buf	 = 0;
	rfs->progress = progress;

	init_resource_track(&overall_track, "overall resize2fs", fs->io);
	init_resource_track(&rtrack, "read_bitmaps", fs->io);
	retval = ext2fs_read_bitmaps(fs);
	if (retval)
		goto errout;
	print_resource_track(rfs, &rtrack, fs->io);

	fs->super->s_state |= EXT2_ERROR_FS;
	ext2fs_mark_super_dirty(fs);
	ext2fs_flush(fs);

	init_resource_track(&rtrack, "fix_uninit_block_bitmaps 1", fs->io);
	fix_uninit_block_bitmaps(fs);
	print_resource_track(rfs, &rtrack, fs->io);
	retval = ext2fs_dup_handle(fs, &rfs->new_fs);
	if (retval)
		goto errout;

	init_resource_track(&rtrack, "resize_group_descriptors", fs->io);
	retval = resize_group_descriptors(rfs, *new_size);
	if (retval)
		goto errout;
	print_resource_track(rfs, &rtrack, fs->io);

	init_resource_track(&rtrack, "move_bg_metadata", fs->io);
	retval = move_bg_metadata(rfs);
	if (retval)
		goto errout;
	print_resource_track(rfs, &rtrack, fs->io);

	init_resource_track(&rtrack, "zero_high_bits_in_metadata", fs->io);
	retval = zero_high_bits_in_inodes(rfs);
	if (retval)
		goto errout;
	print_resource_track(rfs, &rtrack, fs->io);

	init_resource_track(&rtrack, "adjust_superblock", fs->io);
	retval = adjust_superblock(rfs, *new_size);
	if (retval)
		goto errout;
	print_resource_track(rfs, &rtrack, fs->io);

	init_resource_track(&rtrack, "fix_uninit_block_bitmaps 2", fs->io);
	fix_uninit_block_bitmaps(rfs->new_fs);
	print_resource_track(rfs, &rtrack, fs->io);
	/* Clear the block bitmap uninit flag for the last block group */
	ext2fs_bg_flags_clear(rfs->new_fs, rfs->new_fs->group_desc_count - 1,
			     EXT2_BG_BLOCK_UNINIT);

	*new_size = ext2fs_blocks_count(rfs->new_fs->super);

	init_resource_track(&rtrack, "blocks_to_move", fs->io);
	retval = blocks_to_move(rfs);
	if (retval)
		goto errout;
	print_resource_track(rfs, &rtrack, fs->io);

#ifdef RESIZE2FS_DEBUG
	if (rfs->flags & RESIZE_DEBUG_BMOVE)
		printf("Number of free blocks: %llu/%llu, Needed: %llu\n",
		       (unsigned long long) ext2fs_free_blocks_count(rfs->old_fs->super),
		       (unsigned long long) ext2fs_free_blocks_count(rfs->new_fs->super),
		       (unsigned long long) rfs->needed_blocks);
#endif

	init_resource_track(&rtrack, "block_mover", fs->io);
	retval = block_mover(rfs);
	if (retval)
		goto errout;
	print_resource_track(rfs, &rtrack, fs->io);

	init_resource_track(&rtrack, "inode_scan_and_fix", fs->io);
	retval = inode_scan_and_fix(rfs);
	if (retval)
		goto errout;
	print_resource_track(rfs, &rtrack, fs->io);

	init_resource_track(&rtrack, "inode_ref_fix", fs->io);
	retval = inode_ref_fix(rfs);
	if (retval)
		goto errout;
	print_resource_track(rfs, &rtrack, fs->io);

	init_resource_track(&rtrack, "move_itables", fs->io);
	retval = move_itables(rfs);
	if (retval)
		goto errout;
	print_resource_track(rfs, &rtrack, fs->io);

	retval = clear_sparse_super2_last_group(rfs);
	if (retval)
		goto errout;

	init_resource_track(&rtrack, "calculate_summary_stats", fs->io);
	retval = resize2fs_calculate_summary_stats(rfs->new_fs);
	if (retval)
		goto errout;
	print_resource_track(rfs, &rtrack, fs->io);

	init_resource_track(&rtrack, "fix_resize_inode", fs->io);
	retval = fix_resize_inode(rfs->new_fs);
	if (retval)
		goto errout;
	print_resource_track(rfs, &rtrack, fs->io);

	init_resource_track(&rtrack, "fix_orphan_file_inode", fs->io);
	retval = fix_orphan_file_inode(rfs->new_fs);
	if (retval)
		goto errout;
	print_resource_track(rfs, &rtrack, fs->io);

	init_resource_track(&rtrack, "fix_sb_journal_backup", fs->io);
	retval = fix_sb_journal_backup(rfs->new_fs);
	if (retval)
		goto errout;
	print_resource_track(rfs, &rtrack, fs->io);

	retval = ext2fs_set_gdt_csum(rfs->new_fs);
	if (retval)
		goto errout;

	rfs->new_fs->super->s_state &= ~EXT2_ERROR_FS;
	rfs->new_fs->flags &= ~EXT2_FLAG_MASTER_SB_ONLY;

	print_resource_track(rfs, &overall_track, fs->io);
	retval = ext2fs_close_free(&rfs->new_fs);
	if (retval)
		goto errout;

	rfs->flags = flags;

	ext2fs_free(rfs->old_fs);
	rfs->old_fs = NULL;
	if (rfs->itable_buf)
		ext2fs_free_mem(&rfs->itable_buf);
	if (rfs->reserve_blocks)
		ext2fs_free_block_bitmap(rfs->reserve_blocks);
	if (rfs->move_blocks)
		ext2fs_free_block_bitmap(rfs->move_blocks);
	ext2fs_free_mem(&rfs);

	return 0;

errout:
	if (rfs->new_fs) {
		ext2fs_free(rfs->new_fs);
		rfs->new_fs = NULL;
	}
	if (rfs->itable_buf)
		ext2fs_free_mem(&rfs->itable_buf);
	ext2fs_free_mem(&rfs);
	return retval;
}

/* Keep the size of the group descriptor region constant */
static void adjust_reserved_gdt_blocks(ext2_filsys old_fs, ext2_filsys fs)
{
	if (ext2fs_has_feature_resize_inode(fs->super) &&
	    (old_fs->desc_blocks != fs->desc_blocks)) {
		int new;

		new = ((int) fs->super->s_reserved_gdt_blocks) +
			(old_fs->desc_blocks - fs->desc_blocks);
		if (new < 0)
			new = 0;
		if (new > (int) fs->blocksize/4)
			new = fs->blocksize/4;
		fs->super->s_reserved_gdt_blocks = new;
	}
}

/* Toggle 64bit mode */
static errcode_t resize_group_descriptors(ext2_resize_t rfs, blk64_t new_size)
{
	void *o, *n, *new_group_desc;
	dgrp_t i;
	int copy_size;
	errcode_t retval;

	if (!(rfs->flags & (RESIZE_DISABLE_64BIT | RESIZE_ENABLE_64BIT)))
		return 0;

	if (new_size != ext2fs_blocks_count(rfs->new_fs->super) ||
	    ext2fs_blocks_count(rfs->new_fs->super) >= (1ULL << 32) ||
	    (rfs->flags & RESIZE_DISABLE_64BIT &&
	     rfs->flags & RESIZE_ENABLE_64BIT))
		return EXT2_ET_INVALID_ARGUMENT;

	if (rfs->flags & RESIZE_DISABLE_64BIT) {
		ext2fs_clear_feature_64bit(rfs->new_fs->super);
		rfs->new_fs->super->s_desc_size = EXT2_MIN_DESC_SIZE;
	} else if (rfs->flags & RESIZE_ENABLE_64BIT) {
		ext2fs_set_feature_64bit(rfs->new_fs->super);
		rfs->new_fs->super->s_desc_size = EXT2_MIN_DESC_SIZE_64BIT;
	}

	if (EXT2_DESC_SIZE(rfs->old_fs->super) ==
	    EXT2_DESC_SIZE(rfs->new_fs->super))
		return 0;

	o = rfs->new_fs->group_desc;
	rfs->new_fs->desc_blocks = ext2fs_div_ceil(
			rfs->old_fs->group_desc_count,
			EXT2_DESC_PER_BLOCK(rfs->new_fs->super));
	retval = ext2fs_get_arrayzero(rfs->new_fs->desc_blocks,
				      rfs->old_fs->blocksize, &new_group_desc);
	if (retval)
		return retval;

	n = new_group_desc;

	if (EXT2_DESC_SIZE(rfs->old_fs->super) <=
	    EXT2_DESC_SIZE(rfs->new_fs->super))
		copy_size = EXT2_DESC_SIZE(rfs->old_fs->super);
	else
		copy_size = EXT2_DESC_SIZE(rfs->new_fs->super);
	for (i = 0; i < rfs->old_fs->group_desc_count; i++) {
		memcpy(n, o, copy_size);
		n = (char *)n + EXT2_DESC_SIZE(rfs->new_fs->super);
		o = (char *)o + EXT2_DESC_SIZE(rfs->old_fs->super);
	}

	ext2fs_free_mem(&rfs->new_fs->group_desc);
	rfs->new_fs->group_desc = new_group_desc;

	for (i = 0; i < rfs->old_fs->group_desc_count; i++)
		ext2fs_group_desc_csum_set(rfs->new_fs, i);

	adjust_reserved_gdt_blocks(rfs->old_fs, rfs->new_fs);

	return 0;
}

/* Move bitmaps/inode tables out of the way. */
static errcode_t move_bg_metadata(ext2_resize_t rfs)
{
	dgrp_t i;
	blk64_t b, c, d, old_desc_blocks, new_desc_blocks, j;
	ext2fs_block_bitmap old_map, new_map;
	int old, new;
	errcode_t retval;
	int cluster_ratio;

	if (!(rfs->flags & (RESIZE_DISABLE_64BIT | RESIZE_ENABLE_64BIT)))
		return 0;

	retval = ext2fs_allocate_block_bitmap(rfs->old_fs, "oldfs", &old_map);
	if (retval)
		return retval;

	retval = ext2fs_allocate_block_bitmap(rfs->new_fs, "newfs", &new_map);
	if (retval)
		goto out;

	if (ext2fs_has_feature_meta_bg(rfs->old_fs->super)) {
		old_desc_blocks = rfs->old_fs->super->s_first_meta_bg;
		new_desc_blocks = rfs->new_fs->super->s_first_meta_bg;
	} else {
		old_desc_blocks = rfs->old_fs->desc_blocks +
				rfs->old_fs->super->s_reserved_gdt_blocks;
		new_desc_blocks = rfs->new_fs->desc_blocks +
				rfs->new_fs->super->s_reserved_gdt_blocks;
	}

	/* Construct bitmaps of super/descriptor blocks in old and new fs */
	for (i = 0; i < rfs->old_fs->group_desc_count; i++) {
		retval = ext2fs_super_and_bgd_loc2(rfs->old_fs, i, &b, &c, &d,
						   NULL);
		if (retval)
			goto out;
		if (b)
			ext2fs_mark_block_bitmap2(old_map, b);
		for (j = 0; c != 0 && j < old_desc_blocks; j++)
			ext2fs_mark_block_bitmap2(old_map, c + j);
		if (d)
			ext2fs_mark_block_bitmap2(old_map, d);

		retval = ext2fs_super_and_bgd_loc2(rfs->new_fs, i, &b, &c, &d,
						   NULL);
		if (retval)
			goto out;
		if (b)
			ext2fs_mark_block_bitmap2(new_map, b);
		for (j = 0; c != 0 && j < new_desc_blocks; j++)
			ext2fs_mark_block_bitmap2(new_map, c + j);
		if (d)
			ext2fs_mark_block_bitmap2(new_map, d);
	}

	cluster_ratio = EXT2FS_CLUSTER_RATIO(rfs->new_fs);

	/* Find changes in block allocations for bg metadata */
	for (b = EXT2FS_B2C(rfs->old_fs,
			    rfs->old_fs->super->s_first_data_block);
	     b < ext2fs_blocks_count(rfs->new_fs->super);
	     b += cluster_ratio) {
		old = ext2fs_test_block_bitmap2(old_map, b);
		new = ext2fs_test_block_bitmap2(new_map, b);

		if (old && !new) {
			/* mark old_map, unmark new_map */
			if (cluster_ratio == 1)
				ext2fs_unmark_block_bitmap2(
						rfs->new_fs->block_map, b);
		} else if (!old && new)
			; /* unmark old_map, mark new_map */
		else {
			ext2fs_unmark_block_bitmap2(old_map, b);
			ext2fs_unmark_block_bitmap2(new_map, b);
		}
	}

	/*
	 * new_map now shows blocks that have been newly allocated.
	 * old_map now shows blocks that have been newly freed.
	 */

	/*
	 * Move any conflicting bitmaps and inode tables.  Ensure that we
	 * don't try to free clusters associated with bitmaps or tables.
	 */
	for (i = 0; i < rfs->old_fs->group_desc_count; i++) {
		b = ext2fs_block_bitmap_loc(rfs->new_fs, i);
		if (ext2fs_test_block_bitmap2(new_map, b))
			ext2fs_block_bitmap_loc_set(rfs->new_fs, i, 0);
		else if (ext2fs_test_block_bitmap2(old_map, b))
			ext2fs_unmark_block_bitmap2(old_map, b);

		b = ext2fs_inode_bitmap_loc(rfs->new_fs, i);
		if (ext2fs_test_block_bitmap2(new_map, b))
			ext2fs_inode_bitmap_loc_set(rfs->new_fs, i, 0);
		else if (ext2fs_test_block_bitmap2(old_map, b))
			ext2fs_unmark_block_bitmap2(old_map, b);

		c = ext2fs_inode_table_loc(rfs->new_fs, i);
		for (b = 0;
		     b < rfs->new_fs->inode_blocks_per_group;
		     b++) {
			if (ext2fs_test_block_bitmap2(new_map, b + c))
				ext2fs_inode_table_loc_set(rfs->new_fs, i, 0);
			else if (ext2fs_test_block_bitmap2(old_map, b + c))
				ext2fs_unmark_block_bitmap2(old_map, b + c);
		}
	}

	/* Free unused clusters */
	for (b = 0;
	     cluster_ratio > 1 && b < ext2fs_blocks_count(rfs->new_fs->super);
	     b += cluster_ratio)
		if (ext2fs_test_block_bitmap2(old_map, b))
			ext2fs_unmark_block_bitmap2(rfs->new_fs->block_map, b);
out:
	if (old_map)
		ext2fs_free_block_bitmap(old_map);
	if (new_map)
		ext2fs_free_block_bitmap(new_map);
	return retval;
}

/* Zero out the high bits of extent fields */
static errcode_t zero_high_bits_in_extents(ext2_filsys fs, ext2_ino_t ino,
				 struct ext2_inode *inode)
{
	ext2_extent_handle_t	handle;
	struct ext2fs_extent	extent;
	int			op = EXT2_EXTENT_ROOT;
	errcode_t		errcode;

	if (!(inode->i_flags & EXT4_EXTENTS_FL))
		return 0;

	errcode = ext2fs_extent_open(fs, ino, &handle);
	if (errcode)
		return errcode;

	while (1) {
		errcode = ext2fs_extent_get(handle, op, &extent);
		if (errcode)
			break;

		op = EXT2_EXTENT_NEXT_SIB;

		if (extent.e_pblk > (1ULL << 32)) {
			extent.e_pblk &= (1ULL << 32) - 1;
			errcode = ext2fs_extent_replace(handle, 0, &extent);
			if (errcode)
				break;
		}
	}

	/* Ok if we run off the end */
	if (errcode == EXT2_ET_EXTENT_NO_NEXT)
		errcode = 0;
	ext2fs_extent_free(handle);
	return errcode;
}

/* Zero out the high bits of inodes. */
static errcode_t zero_high_bits_in_inodes(ext2_resize_t rfs)
{
	ext2_filsys	fs = rfs->old_fs;
	int length = EXT2_INODE_SIZE(fs->super);
	struct ext2_inode *inode = NULL;
	ext2_inode_scan	scan = NULL;
	errcode_t	retval;
	ext2_ino_t	ino;

	if (!(rfs->flags & (RESIZE_DISABLE_64BIT | RESIZE_ENABLE_64BIT)))
		return 0;

	if (fs->super->s_creator_os == EXT2_OS_HURD)
		return 0;

	retval = ext2fs_open_inode_scan(fs, 0, &scan);
	if (retval)
		return retval;

	retval = ext2fs_get_mem(length, &inode);
	if (retval)
		goto out;

	do {
		retval = ext2fs_get_next_inode_full(scan, &ino, inode, length);
		if (retval)
			goto out;
		if (!ino)
			break;
		if (!ext2fs_test_inode_bitmap2(fs->inode_map, ino))
			continue;

		/*
		 * Here's how we deal with high block number fields:
		 *
		 *  - i_size_high has been been written out with i_size_lo
		 *    since the ext2 days, so no conversion is needed.
		 *
		 *  - i_blocks_hi is guarded by both the huge_file feature and
		 *    inode flags and has always been written out with
		 *    i_blocks_lo if the feature is set.  The field is only
		 *    ever read if both feature and inode flag are set, so
		 *    we don't need to zero it now.
		 *
		 *  - i_file_acl_high can be uninitialized, so zero it if
		 *    it isn't already.
		 */
		if (inode->osd2.linux2.l_i_file_acl_high) {
			inode->osd2.linux2.l_i_file_acl_high = 0;
			retval = ext2fs_write_inode_full(fs, ino, inode,
							 length);
			if (retval)
				goto out;
		}

		retval = zero_high_bits_in_extents(fs, ino, inode);
		if (retval)
			goto out;
	} while (ino);

out:
	if (inode)
		ext2fs_free_mem(&inode);
	if (scan)
		ext2fs_close_inode_scan(scan);
	return retval;
}

/*
 * Clean up the bitmaps for uninitialized bitmaps
 */
static void fix_uninit_block_bitmaps(ext2_filsys fs)
{
	blk64_t		blk, lblk;
	dgrp_t		g;
	unsigned int	i;

	if (!ext2fs_has_group_desc_csum(fs))
		return;

	for (g=0; g < fs->group_desc_count; g++) {
		if (!(ext2fs_bg_flags_test(fs, g, EXT2_BG_BLOCK_UNINIT)))
			continue;

		blk = ext2fs_group_first_block2(fs, g);
		lblk = ext2fs_group_last_block2(fs, g);
		ext2fs_unmark_block_bitmap_range2(fs->block_map, blk,
						  lblk - blk + 1);

		ext2fs_reserve_super_and_bgd(fs, g, fs->block_map);
		ext2fs_mark_block_bitmap2(fs->block_map,
					  ext2fs_block_bitmap_loc(fs, g));
		ext2fs_mark_block_bitmap2(fs->block_map,
					  ext2fs_inode_bitmap_loc(fs, g));
		for (i = 0, blk = ext2fs_inode_table_loc(fs, g);
		     i < fs->inode_blocks_per_group;
		     i++, blk++)
			ext2fs_mark_block_bitmap2(fs->block_map, blk);
	}
}

/* --------------------------------------------------------------------
 *
 * Resize processing, phase 1.
 *
 * In this phase we adjust the in-memory superblock information, and
 * initialize any new parts of the inode table.  The new parts of the
 * inode table are created in virgin disk space, so we can abort here
 * without any side effects.
 * --------------------------------------------------------------------
 */

/*
 * If the group descriptor's bitmap and inode table blocks are valid,
 * release them in the new filesystem data structure, and mark them as
 * reserved so the old inode table blocks don't get overwritten.
 */
static errcode_t free_gdp_blocks(ext2_filsys fs,
				 ext2fs_block_bitmap reserve_blocks,
				 ext2_filsys old_fs,
				 dgrp_t group)
{
	blk64_t		blk;
	unsigned int	j;
	dgrp_t		i;
	ext2fs_block_bitmap bg_map = NULL;
	errcode_t	retval = 0;
	dgrp_t		count = old_fs->group_desc_count - fs->group_desc_count;

	/* If bigalloc, don't free metadata living in the same cluster */
	if (EXT2FS_CLUSTER_RATIO(fs) > 1) {
		retval = ext2fs_allocate_block_bitmap(fs, "bgdata", &bg_map);
		if (retval)
			goto out;

		retval = mark_table_blocks(fs, bg_map);
		if (retval)
			goto out;
	}

	for (i = group; i < group + count; i++) {
		blk = ext2fs_block_bitmap_loc(old_fs, i);
		if (blk &&
		    (blk < ext2fs_blocks_count(fs->super)) &&
		    !(bg_map && ext2fs_test_block_bitmap2(bg_map, blk))) {
			ext2fs_block_alloc_stats2(fs, blk, -1);
			ext2fs_mark_block_bitmap2(reserve_blocks, blk);
		}

		blk = ext2fs_inode_bitmap_loc(old_fs, i);
		if (blk &&
		    (blk < ext2fs_blocks_count(fs->super)) &&
		    !(bg_map && ext2fs_test_block_bitmap2(bg_map, blk))) {
			ext2fs_block_alloc_stats2(fs, blk, -1);
			ext2fs_mark_block_bitmap2(reserve_blocks, blk);
		}

		blk = ext2fs_inode_table_loc(old_fs, i);
		for (j = 0;
		     j < fs->inode_blocks_per_group; j++, blk++) {
			if (blk >= ext2fs_blocks_count(fs->super) ||
			    (bg_map && ext2fs_test_block_bitmap2(bg_map, blk)))
				continue;
			ext2fs_block_alloc_stats2(fs, blk, -1);
			ext2fs_mark_block_bitmap2(reserve_blocks, blk);
		}
	}

out:
	if (bg_map)
		ext2fs_free_block_bitmap(bg_map);
	return retval;
}

/*
 * This routine is shared by the online and offline resize routines.
 * All of the information which is adjusted in memory is done here.
 */
errcode_t adjust_fs_info(ext2_filsys fs, ext2_filsys old_fs,
			 ext2fs_block_bitmap reserve_blocks, blk64_t new_size)
{
	errcode_t	retval;
	blk64_t		overhead = 0;
	blk64_t		rem;
	blk64_t		blk, group_block;
	blk64_t		real_end;
	blk64_t		old_numblocks, numblocks, adjblocks;
	unsigned long	i, j, old_desc_blocks;
	unsigned int	meta_bg, meta_bg_size;
	int		has_super, csum_flag, has_bg;
	unsigned long long new_inodes;	/* u64 to check for overflow */
	double		percent;

	ext2fs_blocks_count_set(fs->super, new_size);
	fs->super->s_overhead_clusters = 0;

retry:
	fs->group_desc_count = ext2fs_div64_ceil(ext2fs_blocks_count(fs->super) -
				       fs->super->s_first_data_block,
				       EXT2_BLOCKS_PER_GROUP(fs->super));
	if (fs->group_desc_count == 0)
		return EXT2_ET_TOOSMALL;
	fs->desc_blocks = ext2fs_div_ceil(fs->group_desc_count,
					  EXT2_DESC_PER_BLOCK(fs->super));

	/*
	 * Overhead is the number of bookkeeping blocks per group.  It
	 * includes the superblock backup, the group descriptor
	 * backups, the inode bitmap, the block bitmap, and the inode
	 * table.
	 */
	overhead = (int) (2 + fs->inode_blocks_per_group);

	has_bg = 0;
	if (ext2fs_has_feature_sparse_super2(fs->super)) {
		/*
		 * We have to do this manually since
		 * super->s_backup_bgs hasn't been set up yet.
		 */
		if (fs->group_desc_count == 2)
			has_bg = fs->super->s_backup_bgs[0] != 0;
		else
			has_bg = fs->super->s_backup_bgs[1] != 0;
	} else
		has_bg = ext2fs_bg_has_super(fs, fs->group_desc_count - 1);
	if (has_bg)
		overhead += 1 + fs->desc_blocks +
			fs->super->s_reserved_gdt_blocks;

	/*
	 * See if the last group is big enough to support the
	 * necessary data structures.  If not, we need to get rid of
	 * it.
	 */
	rem = (ext2fs_blocks_count(fs->super) - fs->super->s_first_data_block) %
		fs->super->s_blocks_per_group;
	if ((fs->group_desc_count == 1) && rem && (rem < overhead))
		return EXT2_ET_TOOSMALL;
	if ((fs->group_desc_count > 1) && rem && (rem < overhead+50)) {
		ext2fs_blocks_count_set(fs->super,
					ext2fs_blocks_count(fs->super) - rem);
		goto retry;
	}
	/*
	 * Adjust the number of inodes
	 */
	new_inodes =(unsigned long long) fs->super->s_inodes_per_group * fs->group_desc_count;
	if (new_inodes > ~0U) {
		new_inodes = (unsigned long long) fs->super->s_inodes_per_group * (fs->group_desc_count - 1);
		if (new_inodes <= ~0U) {
			unsigned long long new_blocks =
		((unsigned long long) fs->super->s_blocks_per_group *
		 (fs->group_desc_count - 1)) + fs->super->s_first_data_block;

			ext2fs_blocks_count_set(fs->super, new_blocks);
			goto retry;
		}
		fprintf(stderr, _("inodes (%llu) must be less than %u\n"),
			(unsigned long long) new_inodes, ~0U);
		return EXT2_ET_TOO_MANY_INODES;
	}
	fs->super->s_inodes_count = fs->super->s_inodes_per_group *
		fs->group_desc_count;

	/*
	 * Adjust the number of free blocks
	 */
	blk = ext2fs_blocks_count(old_fs->super);
	if (blk > ext2fs_blocks_count(fs->super))
		ext2fs_free_blocks_count_set(fs->super, 
			ext2fs_free_blocks_count(fs->super) -
			(blk - ext2fs_blocks_count(fs->super)));
	else
		ext2fs_free_blocks_count_set(fs->super, 
			ext2fs_free_blocks_count(fs->super) +
			(ext2fs_blocks_count(fs->super) - blk));

	/*
	 * Adjust the number of reserved blocks
	 */
	percent = (ext2fs_r_blocks_count(old_fs->super) * 100.0) /
		ext2fs_blocks_count(old_fs->super);
	ext2fs_r_blocks_count_set(fs->super,
				  (percent * ext2fs_blocks_count(fs->super) /
				   100.0));

	/*
	 * Adjust the bitmaps for size
	 */
	retval = ext2fs_resize_inode_bitmap2(fs->super->s_inodes_count,
					    fs->super->s_inodes_count,
					    fs->inode_map);
	if (retval) goto errout;

	real_end = EXT2_GROUPS_TO_CLUSTERS(fs->super, fs->group_desc_count) -
		1 + EXT2FS_B2C(fs, fs->super->s_first_data_block);
	retval = ext2fs_resize_block_bitmap2(new_size - 1,
					     real_end, fs->block_map);
	if (retval) goto errout;

	/*
	 * If we are growing the file system, also grow the size of
	 * the reserve_blocks bitmap
	 */
	if (reserve_blocks && new_size > ext2fs_blocks_count(old_fs->super)) {
		retval = ext2fs_resize_block_bitmap2(new_size - 1,
						     real_end, reserve_blocks);
		if (retval) goto errout;
	}

	/*
	 * Reallocate the group descriptors as necessary.
	 */
	if (EXT2_DESC_SIZE(old_fs->super) == EXT2_DESC_SIZE(fs->super) &&
	    old_fs->desc_blocks != fs->desc_blocks) {
		retval = ext2fs_resize_mem(old_fs->desc_blocks *
					   fs->blocksize,
					   fs->desc_blocks * fs->blocksize,
					   &fs->group_desc);
		if (retval)
			goto errout;
		if (fs->desc_blocks > old_fs->desc_blocks)
			memset((char *) fs->group_desc +
			       (old_fs->desc_blocks * fs->blocksize), 0,
			       (fs->desc_blocks - old_fs->desc_blocks) *
			       fs->blocksize);
	}

	/*
	 * If the resize_inode feature is set, and we are changing the
	 * number of descriptor blocks, then adjust
	 * s_reserved_gdt_blocks if possible to avoid needing to move
	 * the inode table either now or in the future.
	 *
	 * Note: If we're converting to 64bit mode, we did this earlier.
	 */
	if (EXT2_DESC_SIZE(old_fs->super) == EXT2_DESC_SIZE(fs->super))
		adjust_reserved_gdt_blocks(old_fs, fs);

	if (ext2fs_has_feature_meta_bg(fs->super) &&
	    (fs->super->s_first_meta_bg > fs->desc_blocks)) {
		ext2fs_clear_feature_meta_bg(fs->super);
		fs->super->s_first_meta_bg = 0;
	}

	/*
	 * Update the location of the backup superblocks if the
	 * sparse_super2 feature is enabled.
	 */
	if (ext2fs_has_feature_sparse_super2(fs->super)) {
		dgrp_t last_bg = fs->group_desc_count - 1;
		dgrp_t old_last_bg = old_fs->group_desc_count - 1;

		if (last_bg > old_last_bg) {
			if (old_fs->group_desc_count == 1)
				fs->super->s_backup_bgs[0] = 1;
			if ((old_fs->group_desc_count < 3 &&
			     fs->group_desc_count > 2) ||
			    fs->super->s_backup_bgs[1])
				fs->super->s_backup_bgs[1] = last_bg;
		} else if (last_bg < old_last_bg) {
			if (fs->super->s_backup_bgs[0] > last_bg)
				fs->super->s_backup_bgs[0] = 0;
			if (fs->super->s_backup_bgs[1] > last_bg)
				fs->super->s_backup_bgs[1] = 0;
			if (last_bg > 1 &&
			    old_fs->super->s_backup_bgs[1] == old_last_bg)
				fs->super->s_backup_bgs[1] = last_bg;
		}
	}

	/*
	 * If we are shrinking the number of block groups, we're done
	 * and can exit now.
	 */
	if (old_fs->group_desc_count > fs->group_desc_count) {
		/*
		 * Check the block groups that we are chopping off
		 * and free any blocks associated with their metadata
		 */
		retval = free_gdp_blocks(fs, reserve_blocks, old_fs,
					 fs->group_desc_count);
		goto errout;
	}

	/*
	 * Fix the count of the last (old) block group
	 */
	old_numblocks = (ext2fs_blocks_count(old_fs->super) -
			 old_fs->super->s_first_data_block) %
				 old_fs->super->s_blocks_per_group;
	if (!old_numblocks)
		old_numblocks = old_fs->super->s_blocks_per_group;
	if (old_fs->group_desc_count == fs->group_desc_count) {
		numblocks = (ext2fs_blocks_count(fs->super) -
			     fs->super->s_first_data_block) %
			fs->super->s_blocks_per_group;
		if (!numblocks)
			numblocks = fs->super->s_blocks_per_group;
	} else
		numblocks = fs->super->s_blocks_per_group;
	i = old_fs->group_desc_count - 1;
	ext2fs_bg_free_blocks_count_set(fs, i, ext2fs_bg_free_blocks_count(fs, i) + (numblocks - old_numblocks));
	ext2fs_group_desc_csum_set(fs, i);

	/*
	 * If the number of block groups is staying the same, we're
	 * done and can exit now.  (If the number block groups is
	 * shrinking, we had exited earlier.)
	 */
	if (old_fs->group_desc_count >= fs->group_desc_count) {
		retval = 0;
		goto errout;
	}

	/*
	 * Initialize the new block group descriptors
	 */
	group_block = ext2fs_group_first_block2(fs,
						old_fs->group_desc_count);
	csum_flag = ext2fs_has_group_desc_csum(fs);
	if (getenv("RESIZE2FS_FORCE_LAZY_ITABLE_INIT") ||
	    (!getenv("RESIZE2FS_FORCE_ITABLE_INIT") &&
	     access("/sys/fs/ext4/features/lazy_itable_init", F_OK) == 0))
		lazy_itable_init = 1;
	if (ext2fs_has_feature_meta_bg(fs->super))
		old_desc_blocks = fs->super->s_first_meta_bg;
	else
		old_desc_blocks = fs->desc_blocks +
			fs->super->s_reserved_gdt_blocks;

	/*
	 * If we changed the number of block_group descriptor blocks,
	 * we need to make sure they are all marked as reserved in the
	 * filesystem's block allocation map.
	 */
	for (i = 0; i < old_fs->group_desc_count; i++)
		ext2fs_reserve_super_and_bgd(fs, i, fs->block_map);

	for (i = old_fs->group_desc_count;
	     i < fs->group_desc_count; i++) {
		memset(ext2fs_group_desc(fs, fs->group_desc, i), 0,
		       sizeof(struct ext2_group_desc));
		adjblocks = 0;

		ext2fs_bg_flags_zap(fs, i);
		if (csum_flag) {
			ext2fs_bg_flags_set(fs, i, EXT2_BG_INODE_UNINIT);
			if (!lazy_itable_init)
				ext2fs_bg_flags_set(fs, i,
						    EXT2_BG_INODE_ZEROED);
			ext2fs_bg_itable_unused_set(fs, i,
					fs->super->s_inodes_per_group);
		}

		numblocks = ext2fs_group_blocks_count(fs, i);
		if ((i < fs->group_desc_count - 1) && csum_flag)
			ext2fs_bg_flags_set(fs, i, EXT2_BG_BLOCK_UNINIT);

		has_super = ext2fs_bg_has_super(fs, i);
		if (has_super) {
			ext2fs_block_alloc_stats2(fs, group_block, +1);
			adjblocks++;
		}
		meta_bg_size = EXT2_DESC_PER_BLOCK(fs->super);
		meta_bg = i / meta_bg_size;
		if (!ext2fs_has_feature_meta_bg(fs->super) ||
		    (meta_bg < fs->super->s_first_meta_bg)) {
			if (has_super) {
				for (j=0; j < old_desc_blocks; j++)
					ext2fs_block_alloc_stats2(fs,
						 group_block + 1 + j, +1);
				adjblocks += old_desc_blocks;
			}
		} else {
			if (has_super)
				has_super = 1;
			if (((i % meta_bg_size) == 0) ||
			    ((i % meta_bg_size) == 1) ||
			    ((i % meta_bg_size) == (meta_bg_size-1)))
				ext2fs_block_alloc_stats2(fs,
						 group_block + has_super, +1);
		}

		adjblocks += 2 + fs->inode_blocks_per_group;

		numblocks -= adjblocks;
		ext2fs_free_blocks_count_set(fs->super,
			     ext2fs_free_blocks_count(fs->super) - adjblocks);
		fs->super->s_free_inodes_count +=
			fs->super->s_inodes_per_group;
		ext2fs_bg_free_blocks_count_set(fs, i, numblocks);
		ext2fs_bg_free_inodes_count_set(fs, i,
						fs->super->s_inodes_per_group);
		ext2fs_bg_used_dirs_count_set(fs, i, 0);
		ext2fs_group_desc_csum_set(fs, i);

		retval = ext2fs_allocate_group_table(fs, i, 0);
		if (retval) goto errout;

		group_block += fs->super->s_blocks_per_group;
	}
	retval = 0;

	/*
	 * Mark all of the metadata blocks as reserved so they won't
	 * get allocated by the call to ext2fs_allocate_group_table()
	 * in blocks_to_move(), where we allocate new blocks to
	 * replace those allocation bitmap and inode table blocks
	 * which have to get relocated to make space for an increased
	 * number of the block group descriptors.
	 */
	if (reserve_blocks)
		mark_table_blocks(fs, reserve_blocks);

errout:
	return (retval);
}

/*
 * Replicate the first part of adjust_fs_info to determine what the
 * new size of the file system should be.  This allows resize2fs to
 * exit early if we aren't going to make any changes to the file
 * system.
 */
void adjust_new_size(ext2_filsys fs, blk64_t *sizep)
{
	blk64_t		size, rem, overhead = 0;
	unsigned long	desc_blocks;
	dgrp_t		group_desc_count;
	int		has_bg;
	unsigned long long new_inodes;	/* u64 to check for overflow */

	size = *sizep;
retry:
	group_desc_count = ext2fs_div64_ceil(size -
					     fs->super->s_first_data_block,
					     EXT2_BLOCKS_PER_GROUP(fs->super));
	if (group_desc_count == 0)
		return;
	desc_blocks = ext2fs_div_ceil(group_desc_count,
				      EXT2_DESC_PER_BLOCK(fs->super));

	/*
	 * Overhead is the number of bookkeeping blocks per group.  It
	 * includes the superblock backup, the group descriptor
	 * backups, the inode bitmap, the block bitmap, and the inode
	 * table.
	 */
	overhead = (int) (2 + fs->inode_blocks_per_group);

	has_bg = 0;
	if (ext2fs_has_feature_sparse_super2(fs->super)) {
		/*
		 * We have to do this manually since
		 * super->s_backup_bgs hasn't been set up yet.
		 */
		if (group_desc_count == 2)
			has_bg = fs->super->s_backup_bgs[0] != 0;
		else
			has_bg = fs->super->s_backup_bgs[1] != 0;
	} else
		has_bg = ext2fs_bg_has_super(fs, group_desc_count - 1);
	if (has_bg)
		overhead += 1 + desc_blocks +
			fs->super->s_reserved_gdt_blocks;

	/*
	 * See if the last group is big enough to support the
	 * necessary data structures.  If not, we need to get rid of
	 * it.
	 */
	rem = (size - fs->super->s_first_data_block) %
		fs->super->s_blocks_per_group;
	if ((group_desc_count == 1) && rem && (rem < overhead))
		return;
	if ((group_desc_count > 1) && rem && (rem < overhead+50)) {
		size -= rem;
		goto retry;
	}

	/*
	 * If we need to reduce the size by no more than a block
	 * group to avoid overrunning the max inode limit, do it.
	 */
	new_inodes =(unsigned long long) fs->super->s_inodes_per_group * group_desc_count;
	if (new_inodes > ~0U) {
		new_inodes = (unsigned long long) fs->super->s_inodes_per_group * (group_desc_count - 1);
		if (new_inodes > ~0U)
			return;
		size = ((unsigned long long) fs->super->s_blocks_per_group *
			(group_desc_count - 1)) + fs->super->s_first_data_block;

		goto retry;
	}
	*sizep = size;
}

/*
 * This routine adjusts the superblock and other data structures, both
 * in disk as well as in memory...
 */
static errcode_t adjust_superblock(ext2_resize_t rfs, blk64_t new_size)
{
	ext2_filsys	fs = rfs->new_fs;
	int		adj = 0;
	errcode_t	retval;
	unsigned long	i;
	unsigned long	max_group;

	ext2fs_mark_super_dirty(fs);
	ext2fs_mark_bb_dirty(fs);
	ext2fs_mark_ib_dirty(fs);

	retval = ext2fs_allocate_block_bitmap(fs, _("reserved blocks"),
					      &rfs->reserve_blocks);
	if (retval)
		return retval;

	retval = adjust_fs_info(fs, rfs->old_fs, rfs->reserve_blocks, new_size);
	if (retval)
		goto errout;

	/*
	 * Check to make sure there are enough inodes
	 */
	if ((rfs->old_fs->super->s_inodes_count -
	     rfs->old_fs->super->s_free_inodes_count) >
	    rfs->new_fs->super->s_inodes_count) {
		retval = ENOSPC;
		goto errout;
	}

	/*
	 * If we are shrinking the number block groups, we're done and
	 * can exit now.
	 */
	if (rfs->old_fs->group_desc_count > fs->group_desc_count) {
		retval = 0;
		goto errout;
	}

	/*
	 * If the number of block groups is staying the same, we're
	 * done and can exit now.  (If the number block groups is
	 * shrinking, we had exited earlier.)
	 */
	if (rfs->old_fs->group_desc_count >= fs->group_desc_count) {
		retval = 0;
		goto errout;
	}

	/*
	 * If we are using uninit_bg (aka GDT_CSUM) and the kernel
	 * supports lazy inode initialization, we can skip
	 * initializing the inode table.
	 */
	if (lazy_itable_init && ext2fs_has_group_desc_csum(fs)) {
		retval = 0;
		goto errout;
	}

	/*
	 * Initialize the inode table
	 */
	retval = ext2fs_get_array(fs->blocksize, fs->inode_blocks_per_group,
				&rfs->itable_buf);
	if (retval)
		goto errout;

	memset(rfs->itable_buf, 0, fs->blocksize * fs->inode_blocks_per_group);
	adj = rfs->old_fs->group_desc_count;
	max_group = fs->group_desc_count - adj;
	if (rfs->progress) {
		retval = rfs->progress(rfs, E2_RSZ_EXTEND_ITABLE_PASS,
				       0, max_group);
		if (retval)
			goto errout;
	}
	for (i = rfs->old_fs->group_desc_count;
	     i < fs->group_desc_count; i++) {
		/*
		 * Write out the new inode table
		 */
		retval = ext2fs_zero_blocks2(fs, ext2fs_inode_table_loc(fs, i),
					     fs->inode_blocks_per_group, NULL,
					     NULL);
		if (retval)
			goto errout;

		io_channel_flush(fs->io);
		if (rfs->progress) {
			retval = rfs->progress(rfs, E2_RSZ_EXTEND_ITABLE_PASS,
					       i - adj + 1, max_group);
			if (retval)
				goto errout;
		}
	}
	io_channel_flush(fs->io);
	retval = 0;

errout:
	return retval;
}

/* --------------------------------------------------------------------
 *
 * Resize processing, phase 2.
 *
 * In this phase we adjust determine which blocks need to be moved, in
 * blocks_to_move().  We then copy the blocks to their ultimate new
 * destinations using block_mover().  Since we are copying blocks to
 * their new locations, again during this pass we can abort without
 * any problems.
 * --------------------------------------------------------------------
 */

/*
 * This helper function creates a block bitmap with all of the
 * filesystem meta-data blocks.
 */
static errcode_t mark_table_blocks(ext2_filsys fs,
				   ext2fs_block_bitmap bmap)
{
	dgrp_t			i;
	blk64_t			blk;

	for (i = 0; i < fs->group_desc_count; i++) {
		ext2fs_reserve_super_and_bgd(fs, i, bmap);

		/*
		 * Mark the blocks used for the inode table
		 */
		blk = ext2fs_inode_table_loc(fs, i);
		if (blk)
			ext2fs_mark_block_bitmap_range2(bmap, blk,
						fs->inode_blocks_per_group);

		/*
		 * Mark block used for the block bitmap
		 */
		blk = ext2fs_block_bitmap_loc(fs, i);
		if (blk)
			ext2fs_mark_block_bitmap2(bmap, blk);

		/*
		 * Mark block used for the inode bitmap
		 */
		blk = ext2fs_inode_bitmap_loc(fs, i);
		if (blk)
			ext2fs_mark_block_bitmap2(bmap, blk);
	}
	/* Reserve the MMP block */
	if (ext2fs_has_feature_mmp(fs->super) &&
	    fs->super->s_mmp_block > fs->super->s_first_data_block &&
	    fs->super->s_mmp_block < ext2fs_blocks_count(fs->super))
		ext2fs_mark_block_bitmap2(bmap, fs->super->s_mmp_block);
	return 0;
}

/*
 * This function checks to see if a particular block (either a
 * superblock or a block group descriptor) overlaps with an inode or
 * block bitmap block, or with the inode table.
 */
static void mark_fs_metablock(ext2_resize_t rfs,
			      ext2fs_block_bitmap meta_bmap,
			      int group, blk64_t blk)
{
	ext2_filsys fs = rfs->new_fs;

	ext2fs_mark_block_bitmap2(rfs->reserve_blocks, blk);
	ext2fs_block_alloc_stats2(fs, blk, +1);

	/*
	 * Check to see if we overlap with the inode or block bitmap,
	 * or the inode tables.  If not, and the block is in use, then
	 * mark it as a block to be moved.
	 */
	if (is_block_bm(fs, group, blk)) {
		ext2fs_block_bitmap_loc_set(fs, group, 0);
		rfs->needed_blocks++;
		return;
	}
	if (is_inode_bm(fs, group, blk)) {
		ext2fs_inode_bitmap_loc_set(fs, group, 0);
		rfs->needed_blocks++;
		return;
	}
	if (is_inode_tb(fs, group, blk)) {
		ext2fs_inode_table_loc_set(fs, group, 0);
		rfs->needed_blocks++;
		return;
	}
	if (ext2fs_has_feature_flex_bg(fs->super)) {
		dgrp_t i;

		for (i = 0; i < rfs->old_fs->group_desc_count; i++) {
			if (is_block_bm(fs, i, blk)) {
				ext2fs_block_bitmap_loc_set(fs, i, 0);
				rfs->needed_blocks++;
				return;
			}
			if (is_inode_bm(fs, i, blk)) {
				ext2fs_inode_bitmap_loc_set(fs, i, 0);
				rfs->needed_blocks++;
				return;
			}
			if (is_inode_tb(fs, i, blk)) {
				ext2fs_inode_table_loc_set(fs, i, 0);
				rfs->needed_blocks++;
				return;
			}
		}
	}

	if (ext2fs_has_group_desc_csum(fs) &&
	    (ext2fs_bg_flags_test(fs, group, EXT2_BG_BLOCK_UNINIT))) {
		/*
		 * If the block bitmap is uninitialized, which means
		 * nothing other than standard metadata in use.
		 */
		return;
	} else if (blk < ext2fs_blocks_count(rfs->old_fs->super) &&
		   ext2fs_test_block_bitmap2(rfs->old_fs->block_map, blk) &&
		   !ext2fs_test_block_bitmap2(meta_bmap, blk)) {
		ext2fs_mark_block_bitmap2(rfs->move_blocks, blk);
		rfs->needed_blocks++;
	}
}


/*
 * This routine marks and unmarks reserved blocks in the new block
 * bitmap.  It also determines which blocks need to be moved and
 * places this information into the move_blocks bitmap.
 */
static errcode_t blocks_to_move(ext2_resize_t rfs)
{
	unsigned int	j;
	int		has_super;
	dgrp_t		i, max_groups, g;
	blk64_t		blk, group_blk;
	blk64_t		old_blocks, new_blocks, group_end, cluster_freed;
	blk64_t		new_size;
	unsigned int	meta_bg, meta_bg_size;
	errcode_t	retval;
	ext2_filsys 	fs, old_fs;
	ext2fs_block_bitmap	meta_bmap, new_meta_bmap = NULL;
	int		flex_bg;

	fs = rfs->new_fs;
	old_fs = rfs->old_fs;
	if (ext2fs_blocks_count(old_fs->super) > ext2fs_blocks_count(fs->super))
		fs = rfs->old_fs;

	retval = ext2fs_allocate_block_bitmap(fs, _("blocks to be moved"),
					      &rfs->move_blocks);
	if (retval)
		return retval;

	retval = ext2fs_allocate_block_bitmap(fs, _("meta-data blocks"),
					      &meta_bmap);
	if (retval)
		return retval;

	retval = mark_table_blocks(old_fs, meta_bmap);
	if (retval)
		return retval;

	fs = rfs->new_fs;

	/*
	 * If we're shrinking the filesystem, we need to move any
	 * group's metadata blocks (either allocation bitmaps or the
	 * inode table) which are beyond the end of the new
	 * filesystem.
	 */
	new_size = ext2fs_blocks_count(fs->super);
	if (new_size < ext2fs_blocks_count(old_fs->super)) {
		for (g = 0; g < fs->group_desc_count; g++) {
			int realloc = 0;
			/*
			 * ext2fs_allocate_group_table will re-allocate any
			 * metadata blocks whose location is set to zero.
			 */
			if (ext2fs_block_bitmap_loc(fs, g) >= new_size) {
				ext2fs_block_bitmap_loc_set(fs, g, 0);
				realloc = 1;
			}
			if (ext2fs_inode_bitmap_loc(fs, g) >= new_size) {
				ext2fs_inode_bitmap_loc_set(fs, g, 0);
				realloc = 1;
			}
			if ((ext2fs_inode_table_loc(fs, g) +
			     fs->inode_blocks_per_group) > new_size) {
				ext2fs_inode_table_loc_set(fs, g, 0);
				realloc = 1;
			}

			if (realloc) {
				retval = ext2fs_allocate_group_table(fs, g, 0);
				if (retval)
					return retval;
			}
		}
	}

	/*
	 * If we're shrinking the filesystem, we need to move all of
	 * the blocks that don't fit any more
	 */
	for (blk = ext2fs_blocks_count(fs->super);
	     blk < ext2fs_blocks_count(old_fs->super); blk++) {
		g = ext2fs_group_of_blk2(fs, blk);
		if (ext2fs_has_group_desc_csum(fs) &&
		    ext2fs_bg_flags_test(old_fs, g, EXT2_BG_BLOCK_UNINIT)) {
			/*
			 * The block bitmap is uninitialized, so skip
			 * to the next block group.
			 */
			blk = ext2fs_group_first_block2(fs, g+1) - 1;
			continue;
		}
		if (ext2fs_test_block_bitmap2(old_fs->block_map, blk) &&
		    !ext2fs_test_block_bitmap2(meta_bmap, blk)) {
			ext2fs_mark_block_bitmap2(rfs->move_blocks, blk);
			rfs->needed_blocks++;
		}
		ext2fs_mark_block_bitmap2(rfs->reserve_blocks, blk);
	}

	if (ext2fs_has_feature_meta_bg(old_fs->super))
		old_blocks = old_fs->super->s_first_meta_bg;
	else
		old_blocks = old_fs->desc_blocks +
			old_fs->super->s_reserved_gdt_blocks;
	if (ext2fs_has_feature_meta_bg(fs->super))
		new_blocks = fs->super->s_first_meta_bg;
	else
		new_blocks = fs->desc_blocks + fs->super->s_reserved_gdt_blocks;

	retval = reserve_sparse_super2_last_group(rfs, meta_bmap);
	if (retval)
		goto errout;

	if (EXT2_DESC_SIZE(rfs->old_fs->super) ==
	    EXT2_DESC_SIZE(rfs->new_fs->super) &&
	    old_blocks == new_blocks) {
		retval = 0;
		goto errout;
	}

	max_groups = fs->group_desc_count;
	if (max_groups > old_fs->group_desc_count)
		max_groups = old_fs->group_desc_count;
	group_blk = old_fs->super->s_first_data_block;
	/*
	 * If we're reducing the number of descriptor blocks, this
	 * makes life easy.  :-)   We just have to mark some extra
	 * blocks as free.
	 */
	if (old_blocks > new_blocks) {
		if (EXT2FS_CLUSTER_RATIO(fs) > 1) {
			retval = ext2fs_allocate_block_bitmap(fs,
							_("new meta blocks"),
							&new_meta_bmap);
			if (retval)
				goto errout;

			retval = mark_table_blocks(fs, new_meta_bmap);
			if (retval)
				goto errout;
		}

		for (i = 0; i < max_groups; i++) {
			if (!ext2fs_bg_has_super(old_fs, i)) {
				group_blk += fs->super->s_blocks_per_group;
				continue;
			}
			group_end = group_blk + 1 + old_blocks;
			for (blk = group_blk + 1 + new_blocks;
			     blk < group_end;) {
				if (new_meta_bmap == NULL ||
				    !ext2fs_test_block_bitmap2(new_meta_bmap,
							       blk)) {
					cluster_freed =
						EXT2FS_CLUSTER_RATIO(fs) -
						(blk &
						 EXT2FS_CLUSTER_MASK(fs));
					if (cluster_freed > group_end - blk)
						cluster_freed = group_end - blk;
					ext2fs_block_alloc_stats2(fs, blk, -1);
					blk += EXT2FS_CLUSTER_RATIO(fs);
					rfs->needed_blocks -= cluster_freed;
					continue;
				}
				rfs->needed_blocks--;
				blk++;
			}
			group_blk += fs->super->s_blocks_per_group;
		}
		retval = 0;
		goto errout;
	}
	/*
	 * If we're increasing the number of descriptor blocks, life
	 * gets interesting....
	 */
	meta_bg_size = EXT2_DESC_PER_BLOCK(fs->super);
	flex_bg = ext2fs_has_feature_flex_bg(fs->super);
	/* first reserve all of the existing fs meta blocks */
	for (i = 0; i < max_groups; i++) {
		has_super = ext2fs_bg_has_super(fs, i);
		if (has_super)
			mark_fs_metablock(rfs, meta_bmap, i, group_blk);

		meta_bg = i / meta_bg_size;
		if (!ext2fs_has_feature_meta_bg(fs->super) ||
		    (meta_bg < fs->super->s_first_meta_bg)) {
			if (has_super) {
				for (blk = group_blk+1;
				     blk < group_blk + 1 + new_blocks; blk++)
					mark_fs_metablock(rfs, meta_bmap,
							  i, blk);
			}
		} else {
			if (has_super)
				has_super = 1;
			if (((i % meta_bg_size) == 0) ||
			    ((i % meta_bg_size) == 1) ||
			    ((i % meta_bg_size) == (meta_bg_size-1)))
				mark_fs_metablock(rfs, meta_bmap, i,
						  group_blk + has_super);
		}

		/*
		 * Reserve the existing meta blocks that we know
		 * aren't to be moved.
		 *
		 * For flex_bg file systems, in order to avoid
		 * overwriting fs metadata (especially inode table
		 * blocks) belonging to a different block group when
		 * we are relocating the inode tables, we need to
		 * reserve all existing fs metadata blocks.
		 */
		if (ext2fs_block_bitmap_loc(fs, i))
			ext2fs_mark_block_bitmap2(rfs->reserve_blocks,
				 ext2fs_block_bitmap_loc(fs, i));
		else if (flex_bg && i < old_fs->group_desc_count)
			ext2fs_mark_block_bitmap2(rfs->reserve_blocks,
				 ext2fs_block_bitmap_loc(old_fs, i));

		if (ext2fs_inode_bitmap_loc(fs, i))
			ext2fs_mark_block_bitmap2(rfs->reserve_blocks,
				 ext2fs_inode_bitmap_loc(fs, i));
		else if (flex_bg && i < old_fs->group_desc_count)
			ext2fs_mark_block_bitmap2(rfs->reserve_blocks,
				 ext2fs_inode_bitmap_loc(old_fs, i));

		if (ext2fs_inode_table_loc(fs, i))
			ext2fs_mark_block_bitmap_range2(rfs->reserve_blocks,
					ext2fs_inode_table_loc(fs, i),
					fs->inode_blocks_per_group);
		else if (flex_bg && i < old_fs->group_desc_count)
			ext2fs_mark_block_bitmap_range2(rfs->reserve_blocks,
					ext2fs_inode_table_loc(old_fs, i),
					old_fs->inode_blocks_per_group);

		group_blk += rfs->new_fs->super->s_blocks_per_group;
	}

	/* Allocate the missing data structures */
	for (i = 0; i < max_groups; i++) {
		if (ext2fs_inode_table_loc(fs, i) &&
		    ext2fs_inode_bitmap_loc(fs, i) &&
		    ext2fs_block_bitmap_loc(fs, i))
			continue;

		retval = ext2fs_allocate_group_table(fs, i,
						     rfs->reserve_blocks);
		if (retval)
			goto errout;

		/*
		 * For those structures that have changed, we need to
		 * do bookkeeping.
		 */
		if (ext2fs_block_bitmap_loc(old_fs, i) !=
		    (blk = ext2fs_block_bitmap_loc(fs, i))) {
			ext2fs_block_alloc_stats2(fs, blk, +1);
			if (blk < ext2fs_blocks_count(old_fs->super) &&
			    ext2fs_test_block_bitmap2(old_fs->block_map, blk) &&
			    !ext2fs_test_block_bitmap2(meta_bmap, blk))
				ext2fs_mark_block_bitmap2(rfs->move_blocks,
							 blk);
		}
		if (ext2fs_inode_bitmap_loc(old_fs, i) !=
		    (blk = ext2fs_inode_bitmap_loc(fs, i))) {
			ext2fs_block_alloc_stats2(fs, blk, +1);
			if (blk < ext2fs_blocks_count(old_fs->super) &&
			    ext2fs_test_block_bitmap2(old_fs->block_map, blk) &&
			    !ext2fs_test_block_bitmap2(meta_bmap, blk))
				ext2fs_mark_block_bitmap2(rfs->move_blocks,
							 blk);
		}

		/*
		 * The inode table, if we need to relocate it, is
		 * handled specially.  We have to reserve the blocks
		 * for both the old and the new inode table, since we
		 * can't have the inode table be destroyed during the
		 * block relocation phase.
		 */
		if (ext2fs_inode_table_loc(fs, i) == ext2fs_inode_table_loc(old_fs, i))
			continue;	/* inode table not moved */

		rfs->needed_blocks += fs->inode_blocks_per_group;

		/*
		 * Mark the new inode table as in use in the new block
		 * allocation bitmap, and move any blocks that might
		 * be necessary.
		 */
		for (blk = ext2fs_inode_table_loc(fs, i), j=0;
		     j < fs->inode_blocks_per_group ; j++, blk++) {
			ext2fs_block_alloc_stats2(fs, blk, +1);
			if (blk < ext2fs_blocks_count(old_fs->super) &&
			    ext2fs_test_block_bitmap2(old_fs->block_map, blk) &&
			    !ext2fs_test_block_bitmap2(meta_bmap, blk))
				ext2fs_mark_block_bitmap2(rfs->move_blocks,
							 blk);
		}

		/*
		 * Make sure the old inode table is reserved in the
		 * block reservation bitmap.
		 */
		for (blk = ext2fs_inode_table_loc(rfs->old_fs, i), j=0;
		     j < fs->inode_blocks_per_group ; j++, blk++)
			ext2fs_mark_block_bitmap2(rfs->reserve_blocks, blk);
	}
	retval = 0;

errout:
	if (new_meta_bmap)
		ext2fs_free_block_bitmap(new_meta_bmap);
	if (meta_bmap)
		ext2fs_free_block_bitmap(meta_bmap);

	return retval;
}

/*
 * This helper function tries to allocate a new block.  We try to
 * avoid hitting the original group descriptor blocks at least at
 * first, since we want to make it possible to recover from a badly
 * aborted resize operation as much as possible.
 *
 * In the future, I may further modify this routine to balance out
 * where we get the new blocks across the various block groups.
 * Ideally we would allocate blocks that corresponded with the block
 * group of the containing inode, and keep contiguous blocks
 * together.  However, this very difficult to do efficiently, since we
 * don't have the necessary information up front.
 */

#define AVOID_OLD	1
#define DESPERATION	2

static void init_block_alloc(ext2_resize_t rfs)
{
	rfs->alloc_state = AVOID_OLD;
	rfs->new_blk = rfs->new_fs->super->s_first_data_block;
#if 0
	/* HACK for testing */
	if (ext2fs_blocks_count(rfs->new_fs->super) >
	    ext2fs_blocks_count(rfs->old_fs->super))
		rfs->new_blk = ext2fs_blocks_count(rfs->old_fs->super);
#endif
}

static blk64_t get_new_block(ext2_resize_t rfs)
{
	ext2_filsys	fs = rfs->new_fs;

	while (1) {
		if (rfs->new_blk >= ext2fs_blocks_count(fs->super)) {
			if (rfs->alloc_state == DESPERATION)
				return 0;

#ifdef RESIZE2FS_DEBUG
			if (rfs->flags & RESIZE_DEBUG_BMOVE)
				printf("Going into desperation mode "
				       "for block allocations\n");
#endif
			rfs->alloc_state = DESPERATION;
			rfs->new_blk = fs->super->s_first_data_block;
			continue;
		}
		if (ext2fs_test_block_bitmap2(fs->block_map, rfs->new_blk) ||
		    ext2fs_test_block_bitmap2(rfs->reserve_blocks,
					     rfs->new_blk) ||
		    ((rfs->alloc_state == AVOID_OLD) &&
		     (rfs->new_blk < ext2fs_blocks_count(rfs->old_fs->super)) &&
		     ext2fs_test_block_bitmap2(rfs->old_fs->block_map,
					      rfs->new_blk))) {
			rfs->new_blk++;
			continue;
		}
		return rfs->new_blk;
	}
}

static errcode_t resize2fs_get_alloc_block(ext2_filsys fs,
					   blk64_t goal EXT2FS_ATTR((unused)),
					   blk64_t *ret)
{
	ext2_resize_t rfs = (ext2_resize_t) fs->priv_data;
	blk64_t blk;
	int group;

	blk = get_new_block(rfs);
	if (!blk)
		return ENOSPC;

#ifdef RESIZE2FS_DEBUG
	if (rfs->flags & 0xF)
		printf("get_alloc_block allocating %llu\n",
		       (unsigned long long) blk);
#endif

	ext2fs_mark_block_bitmap2(rfs->old_fs->block_map, blk);
	ext2fs_mark_block_bitmap2(rfs->new_fs->block_map, blk);

	group = ext2fs_group_of_blk2(rfs->old_fs, blk);
	ext2fs_clear_block_uninit(rfs->old_fs, group);
	group = ext2fs_group_of_blk2(rfs->new_fs, blk);
	ext2fs_clear_block_uninit(rfs->new_fs, group);

	*ret = (blk64_t) blk;
	return 0;
}

static errcode_t block_mover(ext2_resize_t rfs)
{
	blk64_t			blk, old_blk, new_blk;
	ext2_filsys		fs = rfs->new_fs;
	ext2_filsys		old_fs = rfs->old_fs;
	errcode_t		retval;
	__u64			c, size;
	int			to_move, moved;
	ext2_badblocks_list	badblock_list = 0;
	int			bb_modified = 0;

	fs->get_alloc_block = resize2fs_get_alloc_block;
	old_fs->get_alloc_block = resize2fs_get_alloc_block;

	retval = ext2fs_read_bb_inode(old_fs, &badblock_list);
	if (retval)
		return retval;

	new_blk = fs->super->s_first_data_block;
	if (!rfs->itable_buf) {
		retval = ext2fs_get_array(fs->blocksize,
					fs->inode_blocks_per_group,
					&rfs->itable_buf);
		if (retval)
			goto errout;
	}
	retval = ext2fs_create_extent_table(&rfs->bmap, 0);
	if (retval)
		goto errout;

	/*
	 * The first step is to figure out where all of the blocks
	 * will go.
	 */
	to_move = moved = 0;
	init_block_alloc(rfs);
	for (blk = B2C(old_fs->super->s_first_data_block);
	     blk < ext2fs_blocks_count(old_fs->super);
	     blk += EXT2FS_CLUSTER_RATIO(fs)) {
		if (!ext2fs_test_block_bitmap2(old_fs->block_map, blk))
			continue;
		if (!ext2fs_test_block_bitmap2(rfs->move_blocks, blk))
			continue;
		if (ext2fs_badblocks_list_test(badblock_list, blk)) {
			ext2fs_badblocks_list_del(badblock_list, blk);
			bb_modified++;
			continue;
		}

		new_blk = get_new_block(rfs);
		if (!new_blk) {
			retval = ENOSPC;
			goto errout;
		}
		ext2fs_block_alloc_stats2(fs, new_blk, +1);
		ext2fs_add_extent_entry(rfs->bmap, B2C(blk), B2C(new_blk));
		to_move++;
	}

	if (to_move == 0) {
		if (rfs->bmap) {
			ext2fs_free_extent_table(rfs->bmap);
			rfs->bmap = 0;
		}
		retval = 0;
		goto errout;
	}

	/*
	 * Step two is to actually move the blocks
	 */
	retval =  ext2fs_iterate_extent(rfs->bmap, 0, 0, 0);
	if (retval) goto errout;

	if (rfs->progress) {
		retval = (rfs->progress)(rfs, E2_RSZ_BLOCK_RELOC_PASS,
					 0, to_move);
		if (retval)
			goto errout;
	}
	while (1) {
		retval = ext2fs_iterate_extent(rfs->bmap, &old_blk, &new_blk, &size);
		if (retval) goto errout;
		if (!size)
			break;
		old_blk = C2B(old_blk);
		new_blk = C2B(new_blk);
		size = C2B(size);
#ifdef RESIZE2FS_DEBUG
		if (rfs->flags & RESIZE_DEBUG_BMOVE)
			printf("Moving %llu blocks %llu->%llu\n",
			       (unsigned long long) size,
			       (unsigned long long) old_blk,
			       (unsigned long long) new_blk);
#endif
		do {
			c = size;
			if (c > fs->inode_blocks_per_group)
				c = fs->inode_blocks_per_group;
			retval = io_channel_read_blk64(fs->io, old_blk, c,
						       rfs->itable_buf);
			if (retval) goto errout;
			retval = io_channel_write_blk64(fs->io, new_blk, c,
							rfs->itable_buf);
			if (retval) goto errout;
			size -= c;
			new_blk += c;
			old_blk += c;
			moved += c;
			if (rfs->progress) {
				retval = (rfs->progress)(rfs,
						E2_RSZ_BLOCK_RELOC_PASS,
						moved, to_move);
				if (retval)
					goto errout;
			}
		} while (size > 0);
	}

	io_channel_flush(fs->io);

errout:
	if (badblock_list) {
		if (!retval && bb_modified)
			retval = ext2fs_update_bb_inode(old_fs,
							badblock_list);
		ext2fs_badblocks_list_free(badblock_list);
	}
	return retval;
}


/* --------------------------------------------------------------------
 *
 * Resize processing, phase 3
 *
 * --------------------------------------------------------------------
 */


/*
 * The extent translation table is stored in clusters so we need to
 * take special care when mapping a source block number to its
 * destination block number.
 */
static __u64 extent_translate(ext2_filsys fs, ext2_extent extent, __u64 old_loc)
{
	__u64 new_block = C2B(ext2fs_extent_translate(extent, B2C(old_loc)));

	if (new_block != 0)
		new_block += old_loc & (EXT2FS_CLUSTER_RATIO(fs) - 1);
	return new_block;
}

struct process_block_struct {
	ext2_resize_t 		rfs;
	ext2_ino_t		ino;
	ext2_ino_t		old_ino;
	struct ext2_inode *	inode;
	errcode_t		error;
	int			is_dir;
	int			changed;
	int			has_extents;
};

static int process_block(ext2_filsys fs, blk64_t	*block_nr,
			 e2_blkcnt_t blockcnt,
			 blk64_t ref_block EXT2FS_ATTR((unused)),
			 int ref_offset EXT2FS_ATTR((unused)), void *priv_data)
{
	struct process_block_struct *pb;
	errcode_t	retval;
	blk64_t		block, new_block;
	int		ret = 0;

	pb = (struct process_block_struct *) priv_data;
	block = *block_nr;
	if (pb->rfs->bmap) {
		new_block = extent_translate(fs, pb->rfs->bmap, block);
		if (new_block) {
			*block_nr = new_block;
			ret |= BLOCK_CHANGED;
			pb->changed = 1;
#ifdef RESIZE2FS_DEBUG
			if (pb->rfs->flags & RESIZE_DEBUG_BMOVE)
				printf("ino=%u, blockcnt=%lld, %llu->%llu\n",
				       pb->old_ino, (long long) blockcnt,
				       (unsigned long long) block,
				       (unsigned long long) new_block);
#endif
			block = new_block;
		}
	}

	if (pb->is_dir) {
		retval = ext2fs_add_dir_block2(fs->dblist, pb->ino,
					       block, (int) blockcnt);
		if (retval) {
			pb->error = retval;
			ret |= BLOCK_ABORT;
		}
	}
	return ret;
}

/*
 * Progress callback
 */
static errcode_t progress_callback(ext2_filsys fs,
				   ext2_inode_scan scan EXT2FS_ATTR((unused)),
				   dgrp_t group, void * priv_data)
{
	ext2_resize_t rfs = (ext2_resize_t) priv_data;
	errcode_t		retval;

	/*
	 * This check is to protect against old ext2 libraries.  It
	 * shouldn't be needed against new libraries.
	 */
	if ((group+1) == 0)
		return 0;

	if (rfs->progress) {
		io_channel_flush(fs->io);
		retval = (rfs->progress)(rfs, E2_RSZ_INODE_SCAN_PASS,
					 group+1, fs->group_desc_count);
		if (retval)
			return retval;
	}

	return 0;
}

static errcode_t migrate_ea_block(ext2_resize_t rfs, ext2_ino_t ino,
				  struct ext2_inode *inode, int *changed)
{
	char *buf = NULL;
	blk64_t new_block;
	errcode_t err = 0;

	/* No EA block or no remapping?  Quit early. */
	if (ext2fs_file_acl_block(rfs->old_fs, inode) == 0 || !rfs->bmap)
		return 0;
	new_block = extent_translate(rfs->old_fs, rfs->bmap,
		ext2fs_file_acl_block(rfs->old_fs, inode));
	if (new_block == 0)
		return 0;

	/* Set the new ACL block */
	ext2fs_file_acl_block_set(rfs->old_fs, inode, new_block);

	/* Update checksum */
	if (ext2fs_has_feature_metadata_csum(rfs->new_fs->super)) {
		err = ext2fs_get_mem(rfs->old_fs->blocksize, &buf);
		if (err)
			return err;
		rfs->old_fs->flags |= EXT2_FLAG_IGNORE_CSUM_ERRORS;
		err = ext2fs_read_ext_attr3(rfs->old_fs, new_block, buf, ino);
		rfs->old_fs->flags &= ~EXT2_FLAG_IGNORE_CSUM_ERRORS;
		if (err)
			goto out;
		err = ext2fs_write_ext_attr3(rfs->old_fs, new_block, buf, ino);
		if (err)
			goto out;
	}
	*changed = 1;

out:
	ext2fs_free_mem(&buf);
	return err;
}

static void quiet_com_err_proc(const char *whoami EXT2FS_ATTR((unused)),
			       errcode_t code EXT2FS_ATTR((unused)),
			       const char *fmt EXT2FS_ATTR((unused)),
			       va_list args EXT2FS_ATTR((unused)))
{
}

static int fix_ea_entries(ext2_extent imap, struct ext2_ext_attr_entry *entry,
			  struct ext2_ext_attr_entry *end, ext2_ino_t last_ino)
{
	int modified = 0;
	ext2_ino_t new_ino;

	while (entry < end && !EXT2_EXT_IS_LAST_ENTRY(entry)) {
		if (entry->e_value_inum > last_ino) {
			new_ino = ext2fs_extent_translate(imap,
							  entry->e_value_inum);
			entry->e_value_inum = new_ino;
			modified = 1;
		}
		entry = EXT2_EXT_ATTR_NEXT(entry);
	}
	return modified;
}

static int fix_ea_ibody_entries(ext2_extent imap,
				struct ext2_inode_large *inode, int inode_size,
				ext2_ino_t last_ino)
{
	struct ext2_ext_attr_entry *start, *end;
	__u32 *ea_magic;

	if (inode->i_extra_isize == 0)
		return 0;

	ea_magic = (__u32 *)((char *)inode + EXT2_GOOD_OLD_INODE_SIZE +
				inode->i_extra_isize);
	if (*ea_magic != EXT2_EXT_ATTR_MAGIC)
		return 0;

	start = (struct ext2_ext_attr_entry *)(ea_magic + 1);
	end = (struct ext2_ext_attr_entry *)((char *)inode + inode_size);

	return fix_ea_entries(imap, start, end, last_ino);
}

static int fix_ea_block_entries(ext2_extent imap, char *block_buf,
				unsigned int blocksize, ext2_ino_t last_ino)
{
	struct ext2_ext_attr_header *header;
	struct ext2_ext_attr_entry *start, *end;

	header = (struct ext2_ext_attr_header *)block_buf;
	start = (struct ext2_ext_attr_entry *)(header+1);
	end = (struct ext2_ext_attr_entry *)(block_buf + blocksize);

	return fix_ea_entries(imap, start, end, last_ino);
}

/* A simple LRU cache to check recently processed blocks. */
struct blk_cache {
	int cursor;
	blk64_t blks[4];
};

#define BLK_IN_CACHE(b,c) ((b) == (c).blks[0] || (b) == (c).blks[1] || \
			   (b) == (c).blks[2] || (b) == (c).blks[3])
#define BLK_ADD_CACHE(b,c) { 			\
	(c).blks[(c).cursor] = (b);		\
	(c).cursor = ((c).cursor + 1) % 4;	\
}

static errcode_t fix_ea_inode_refs(ext2_resize_t rfs, struct ext2_inode *inode,
				   char *block_buf, ext2_ino_t last_ino)
{
	ext2_filsys	fs = rfs->new_fs;
	ext2_inode_scan	scan = NULL;
	ext2_ino_t	ino;
	int		inode_size = EXT2_INODE_SIZE(fs->super);
	blk64_t		blk;
	int		modified;
	struct blk_cache blk_cache;
	struct ext2_ext_attr_header *header;
	errcode_t		retval;

	memset(&blk_cache, 0, sizeof(blk_cache));

	header = (struct ext2_ext_attr_header *)block_buf;

	retval = ext2fs_open_inode_scan(fs, 0, &scan);
	if (retval)
		goto out;

	while (1) {
		retval = ext2fs_get_next_inode_full(scan, &ino, inode,
						    inode_size);
		if (retval)
			goto out;
		if (!ino)
			break;

		if (inode->i_links_count == 0 && ino != EXT2_RESIZE_INO)
			continue; /* inode not in use */

		if (inode_size != EXT2_GOOD_OLD_INODE_SIZE) {
			modified = fix_ea_ibody_entries(rfs->imap,
					(struct ext2_inode_large *)inode,
					inode_size, last_ino);
			if (modified) {
				retval = ext2fs_write_inode_full(fs, ino, inode,
								 inode_size);
				if (retval)
					goto out;
			}
		}

		blk = ext2fs_file_acl_block(fs, inode);
		if (blk && !BLK_IN_CACHE(blk, blk_cache)) {
			retval = ext2fs_read_ext_attr3(fs, blk, block_buf, ino);
			if (retval)
				goto out;

			modified = fix_ea_block_entries(rfs->imap, block_buf,
							fs->blocksize,
							last_ino);
			if (modified) {
				retval = ext2fs_write_ext_attr3(fs, blk,
								block_buf, ino);
				if (retval)
					goto out;
				/*
				 * If refcount is greater than 1, we might see
				 * the same block referenced by other inodes
				 * later.
				 */
				if (header->h_refcount > 1)
					BLK_ADD_CACHE(blk, blk_cache);
			}
		}
	}
	retval = 0;
out:
	if (scan)
		ext2fs_close_inode_scan(scan);
	return retval;

}
static errcode_t inode_scan_and_fix(ext2_resize_t rfs)
{
	struct process_block_struct	pb;
	ext2_ino_t		ino, new_inode;
	struct ext2_inode 	*inode = NULL;
	ext2_inode_scan 	scan = NULL;
	errcode_t		retval;
	char			*block_buf = 0;
	ext2_ino_t		start_to_move;
	int			inode_size;
	int			update_ea_inode_refs = 0;

	if ((rfs->old_fs->group_desc_count <=
	     rfs->new_fs->group_desc_count) &&
	    !rfs->bmap)
		return 0;

	set_com_err_hook(quiet_com_err_proc);

	retval = ext2fs_open_inode_scan(rfs->old_fs, 0, &scan);
	if (retval) goto errout;

	retval = ext2fs_init_dblist(rfs->old_fs, 0);
	if (retval) goto errout;
	retval = ext2fs_get_array(rfs->old_fs->blocksize, 3, &block_buf);
	if (retval) goto errout;

	start_to_move = (rfs->new_fs->group_desc_count *
			 rfs->new_fs->super->s_inodes_per_group);

	if (rfs->progress) {
		retval = (rfs->progress)(rfs, E2_RSZ_INODE_SCAN_PASS,
					 0, rfs->old_fs->group_desc_count);
		if (retval)
			goto errout;
	}
	ext2fs_set_inode_callback(scan, progress_callback, (void *) rfs);
	pb.rfs = rfs;
	pb.inode = inode;
	pb.error = 0;
	new_inode = EXT2_FIRST_INODE(rfs->new_fs->super);
	inode_size = EXT2_INODE_SIZE(rfs->new_fs->super);
	inode = malloc(inode_size);
	if (!inode) {
		retval = ENOMEM;
		goto errout;
	}
	/*
	 * First, copy all of the inodes that need to be moved
	 * elsewhere in the inode table
	 */
	while (1) {
		retval = ext2fs_get_next_inode_full(scan, &ino, inode, inode_size);
		if (retval) goto errout;
		if (!ino)
			break;

		if (inode->i_links_count == 0 && ino != EXT2_RESIZE_INO)
			continue; /* inode not in use */

		pb.is_dir = LINUX_S_ISDIR(inode->i_mode);
		pb.changed = 0;

		/* Remap EA block */
		retval = migrate_ea_block(rfs, ino, inode, &pb.changed);
		if (retval)
			goto errout;

		new_inode = ino;
		if (ino <= start_to_move)
			goto remap_blocks; /* Don't need to move inode. */

		/*
		 * Find a new inode.  Now that extents and directory blocks
		 * are tied to the inode number through the checksum, we must
		 * set up the new inode before we start rewriting blocks.
		 */
		retval = ext2fs_new_inode(rfs->new_fs, 0, 0, 0, &new_inode);
		if (retval)
			goto errout;

		ext2fs_inode_alloc_stats2(rfs->new_fs, new_inode, +1,
					  pb.is_dir);
		/*
		 * i_ctime field in xattr inodes contain a portion of the ref
		 * count, do not overwrite.
		 */
		if (inode->i_flags & EXT4_EA_INODE_FL)
			update_ea_inode_refs = 1;
		else
			inode->i_ctime = rfs->old_fs->now ?
				rfs->old_fs->now : time(0);

		retval = ext2fs_write_inode_full(rfs->old_fs, new_inode,
						inode, inode_size);
		if (retval)
			goto errout;
		pb.changed = 0;

#ifdef RESIZE2FS_DEBUG
		if (rfs->flags & RESIZE_DEBUG_INODEMAP)
			printf("Inode moved %u->%u\n", ino, new_inode);
#endif
		if (!rfs->imap) {
			retval = ext2fs_create_extent_table(&rfs->imap, 0);
			if (retval)
				goto errout;
		}
		ext2fs_add_extent_entry(rfs->imap, ino, new_inode);

remap_blocks:
		if (pb.changed)
			retval = ext2fs_write_inode_full(rfs->old_fs,
							 new_inode,
							 inode, inode_size);
		if (retval)
			goto errout;

		/*
		 * Update inodes to point to new blocks; schedule directory
		 * blocks for inode remapping.  Need to write out dir blocks
		 * with new inode numbers if we have metadata_csum enabled.
		 */
		rfs->old_fs->flags |= EXT2_FLAG_IGNORE_CSUM_ERRORS;
		if (ext2fs_inode_has_valid_blocks2(rfs->old_fs, inode) &&
		    (rfs->bmap || pb.is_dir)) {
			pb.ino = new_inode;
			pb.old_ino = ino;
			pb.has_extents = inode->i_flags & EXT4_EXTENTS_FL;
			retval = ext2fs_block_iterate3(rfs->old_fs,
						       new_inode, 0, block_buf,
						       process_block, &pb);
			if (retval)
				goto errout;
			if (pb.error) {
				retval = pb.error;
				goto errout;
			}
		} else if ((inode->i_flags & EXT4_INLINE_DATA_FL) &&
			   (rfs->bmap || pb.is_dir)) {
			/* inline data dir; update it too */
			retval = ext2fs_add_dir_block2(rfs->old_fs->dblist,
						       new_inode, 0, 0);
			if (retval)
				goto errout;
		}

		/* Fix up extent block checksums with the new inode number */
		if (ext2fs_has_feature_metadata_csum(rfs->old_fs->super) &&
		    (inode->i_flags & EXT4_EXTENTS_FL)) {
			retval = ext2fs_fix_extents_checksums(rfs->old_fs,
							      new_inode, NULL);
			if (retval)
				goto errout;
		}
	}

	if (update_ea_inode_refs &&
	    ext2fs_has_feature_ea_inode(rfs->new_fs->super)) {
		retval = fix_ea_inode_refs(rfs, inode, block_buf,
					   start_to_move);
		if (retval)
			goto errout;
	}
	io_channel_flush(rfs->old_fs->io);

errout:
	reset_com_err_hook();
	rfs->old_fs->flags &= ~EXT2_FLAG_IGNORE_CSUM_ERRORS;
	if (rfs->bmap) {
		ext2fs_free_extent_table(rfs->bmap);
		rfs->bmap = 0;
	}
	if (scan)
		ext2fs_close_inode_scan(scan);
	if (block_buf)
		ext2fs_free_mem(&block_buf);
	free(inode);
	return retval;
}

/* --------------------------------------------------------------------
 *
 * Resize processing, phase 4.
 *
 * --------------------------------------------------------------------
 */

struct istruct {
	ext2_resize_t rfs;
	errcode_t	err;
	unsigned int	max_dirs;
	unsigned int	num;
};

static int check_and_change_inodes(ext2_ino_t dir,
				   int entry EXT2FS_ATTR((unused)),
				   struct ext2_dir_entry *dirent, int offset,
				   int	blocksize EXT2FS_ATTR((unused)),
				   char *buf EXT2FS_ATTR((unused)),
				   void *priv_data)
{
	struct istruct *is = (struct istruct *) priv_data;
	struct ext2_inode 	inode;
	ext2_ino_t		new_inode;
	errcode_t		retval;
	int			ret = 0;

	if (is->rfs->progress && offset == 0) {
		io_channel_flush(is->rfs->old_fs->io);
		is->err = (is->rfs->progress)(is->rfs,
					      E2_RSZ_INODE_REF_UPD_PASS,
					      ++is->num, is->max_dirs);
		if (is->err)
			return DIRENT_ABORT;
	}

	/*
	 * If we have checksums enabled and the inode wasn't present in the
	 * old fs, then we must rewrite all dir blocks with new checksums.
	 */
	if (ext2fs_has_feature_metadata_csum(is->rfs->old_fs->super) &&
	    !ext2fs_test_inode_bitmap2(is->rfs->old_fs->inode_map, dir))
		ret |= DIRENT_CHANGED;

	if (!dirent->inode)
		return ret;

	new_inode = ext2fs_extent_translate(is->rfs->imap, dirent->inode);

	if (!new_inode)
		return ret;
#ifdef RESIZE2FS_DEBUG
	if (is->rfs->flags & RESIZE_DEBUG_INODEMAP)
		printf("Inode translate (dir=%u, name=%.*s, %u->%u)\n",
		       dir, ext2fs_dirent_name_len(dirent), dirent->name,
		       dirent->inode, new_inode);
#endif

	dirent->inode = new_inode;

	/* Update the directory mtime and ctime */
	retval = ext2fs_read_inode(is->rfs->old_fs, dir, &inode);
	if (retval == 0) {
		inode.i_mtime = inode.i_ctime = is->rfs->old_fs->now ?
			is->rfs->old_fs->now : time(0);
		is->err = ext2fs_write_inode(is->rfs->old_fs, dir, &inode);
		if (is->err)
			return ret | DIRENT_ABORT;
	}

	return ret | DIRENT_CHANGED;
}

static errcode_t inode_ref_fix(ext2_resize_t rfs)
{
	errcode_t		retval;
	struct istruct 		is;

	if (!rfs->imap)
		return 0;

	/*
	 * Now, we iterate over all of the directories to update the
	 * inode references
	 */
	is.num = 0;
	is.max_dirs = ext2fs_dblist_count2(rfs->old_fs->dblist);
	is.rfs = rfs;
	is.err = 0;

	if (rfs->progress) {
		retval = (rfs->progress)(rfs, E2_RSZ_INODE_REF_UPD_PASS,
					 0, is.max_dirs);
		if (retval)
			goto errout;
	}

	rfs->old_fs->flags |= EXT2_FLAG_IGNORE_CSUM_ERRORS;
	retval = ext2fs_dblist_dir_iterate(rfs->old_fs->dblist,
					   DIRENT_FLAG_INCLUDE_EMPTY, 0,
					   check_and_change_inodes, &is);
	rfs->old_fs->flags &= ~EXT2_FLAG_IGNORE_CSUM_ERRORS;
	if (retval)
		goto errout;
	if (is.err) {
		retval = is.err;
		goto errout;
	}

	if (rfs->progress && (is.num < is.max_dirs))
		(rfs->progress)(rfs, E2_RSZ_INODE_REF_UPD_PASS,
				is.max_dirs, is.max_dirs);

errout:
	ext2fs_free_extent_table(rfs->imap);
	rfs->imap = 0;
	return retval;
}


/* --------------------------------------------------------------------
 *
 * Resize processing, phase 5.
 *
 * In this phase we actually move the inode table around, and then
 * update the summary statistics.  This is scary, since aborting here
 * will potentially scramble the filesystem.  (We are moving the
 * inode tables around in place, and so the potential for lost data,
 * or at the very least scrambling the mapping between filenames and
 * inode numbers is very high in case of a power failure here.)
 * --------------------------------------------------------------------
 */


/*
 * A very scary routine --- this one moves the inode table around!!!
 *
 * After this you have to use the rfs->new_fs file handle to read and
 * write inodes.
 */
static errcode_t move_itables(ext2_resize_t rfs)
{
	int		n, num, size;
	long long	diff;
	dgrp_t		i, max_groups;
	ext2_filsys	fs = rfs->new_fs;
	char		*cp;
	blk64_t		old_blk, new_blk, blk, cluster_freed;
	errcode_t	retval;
	int		to_move, moved;
	unsigned int	j;
	ext2fs_block_bitmap	new_bmap = NULL;

	max_groups = fs->group_desc_count;
	if (max_groups > rfs->old_fs->group_desc_count)
		max_groups = rfs->old_fs->group_desc_count;

	size = fs->blocksize * fs->inode_blocks_per_group;
	if (!rfs->itable_buf) {
		retval = ext2fs_get_mem(size, &rfs->itable_buf);
		if (retval)
			return retval;
	}

	if (EXT2FS_CLUSTER_RATIO(fs) > 1) {
		retval = ext2fs_allocate_block_bitmap(fs, _("new meta blocks"),
						      &new_bmap);
		if (retval)
			return retval;

		retval = mark_table_blocks(fs, new_bmap);
		if (retval)
			goto errout;
	}

	/*
	 * Figure out how many inode tables we need to move
	 */
	to_move = moved = 0;
	for (i=0; i < max_groups; i++)
		if (ext2fs_inode_table_loc(rfs->old_fs, i) !=
		    ext2fs_inode_table_loc(fs, i))
			to_move++;

	if (to_move == 0) {
		retval = 0;
		goto errout;
	}

	if (rfs->progress) {
		retval = rfs->progress(rfs, E2_RSZ_MOVE_ITABLE_PASS,
				       0, to_move);
		if (retval)
			goto errout;
	}

	rfs->old_fs->flags |= EXT2_FLAG_MASTER_SB_ONLY;

	for (i=0; i < max_groups; i++) {
		old_blk = ext2fs_inode_table_loc(rfs->old_fs, i);
		new_blk = ext2fs_inode_table_loc(fs, i);
		diff = new_blk - old_blk;

#ifdef RESIZE2FS_DEBUG
		if (rfs->flags & RESIZE_DEBUG_ITABLEMOVE)
			printf("Itable move group %d block %llu->%llu (diff %lld)\n",
			       i, (unsigned long long) old_blk,
			       (unsigned long long) new_blk, diff);
#endif

		if (!diff)
			continue;
		if (diff < 0)
			diff = 0;

		retval = io_channel_read_blk64(fs->io, old_blk,
					       fs->inode_blocks_per_group,
					       rfs->itable_buf);
		if (retval)
			goto errout;
		/*
		 * The end of the inode table segment often contains
		 * all zeros, and we're often only moving the inode
		 * table down a block or two.  If so, we can optimize
		 * things by not rewriting blocks that we know to be zero
		 * already.
		 */
		for (cp = rfs->itable_buf+size-1, n=0; n < size; n++, cp--)
			if (*cp)
				break;
		n = n >> EXT2_BLOCK_SIZE_BITS(fs->super);
#ifdef RESIZE2FS_DEBUG
		if (rfs->flags & RESIZE_DEBUG_ITABLEMOVE)
			printf("%d blocks of zeros...\n", n);
#endif
		num = fs->inode_blocks_per_group;
		if (n > diff)
			num -= n;

		retval = io_channel_write_blk64(fs->io, new_blk,
						num, rfs->itable_buf);
		if (retval) {
			(void) io_channel_write_blk64(fs->io, old_blk,
						      num, rfs->itable_buf);
			goto errout;
		}
		if (n > diff) {
			retval = io_channel_write_blk64(fs->io,
			      old_blk + fs->inode_blocks_per_group,
			      diff, (rfs->itable_buf +
				     (fs->inode_blocks_per_group - diff) *
				     fs->blocksize));
			if (retval)
				goto errout;
		}

		for (blk = ext2fs_inode_table_loc(rfs->old_fs, i), j=0;
		     j < fs->inode_blocks_per_group;) {
			if (new_bmap == NULL ||
			    !ext2fs_test_block_bitmap2(new_bmap, blk)) {
				ext2fs_block_alloc_stats2(fs, blk, -1);
				cluster_freed = EXT2FS_CLUSTER_RATIO(fs) -
						(blk & EXT2FS_CLUSTER_MASK(fs));
				blk += cluster_freed;
				j += cluster_freed;
				continue;
			}
			blk++;
			j++;
		}

		ext2fs_inode_table_loc_set(rfs->old_fs, i, new_blk);
		ext2fs_group_desc_csum_set(rfs->old_fs, i);
		ext2fs_mark_super_dirty(rfs->old_fs);
		ext2fs_flush(rfs->old_fs);

		if (rfs->progress) {
			retval = rfs->progress(rfs, E2_RSZ_MOVE_ITABLE_PASS,
					       ++moved, to_move);
			if (retval)
				goto errout;
		}
	}
	mark_table_blocks(fs, fs->block_map);
	ext2fs_flush(fs);
#ifdef RESIZE2FS_DEBUG
	if (rfs->flags & RESIZE_DEBUG_ITABLEMOVE)
		printf("Inode table move finished.\n");
#endif
	retval = 0;

errout:
	if (new_bmap)
		ext2fs_free_block_bitmap(new_bmap);
	return retval;
}

/*
 * This function is used when expanding a file system.  It frees the
 * superblock and block group descriptor blocks from the block group
 * which is no longer the last block group.
 */
static errcode_t clear_sparse_super2_last_group(ext2_resize_t rfs)
{
	ext2_filsys	fs = rfs->new_fs;
	ext2_filsys	old_fs = rfs->old_fs;
	errcode_t	retval;
	dgrp_t		old_last_bg = rfs->old_fs->group_desc_count - 1;
	dgrp_t		last_bg = fs->group_desc_count - 1;
	blk64_t		sb, old_desc;
	blk_t		num;

	if (!ext2fs_has_feature_sparse_super2(fs->super))
		return 0;

	if (last_bg <= old_last_bg)
		return 0;

	if (fs->super->s_backup_bgs[0] == old_fs->super->s_backup_bgs[0] &&
	    fs->super->s_backup_bgs[1] == old_fs->super->s_backup_bgs[1])
		return 0;

	if (old_fs->super->s_backup_bgs[0] != old_last_bg &&
	    old_fs->super->s_backup_bgs[1] != old_last_bg)
		return 0;

	if (fs->super->s_backup_bgs[0] == old_last_bg ||
	    fs->super->s_backup_bgs[1] == old_last_bg)
		return 0;

	if (old_last_bg == 0)
		return 0;

	retval = ext2fs_super_and_bgd_loc2(rfs->old_fs, old_last_bg,
					   &sb, &old_desc, NULL, &num);
	if (retval)
		return retval;

	if (sb)
		ext2fs_unmark_block_bitmap2(fs->block_map, sb);
	if (old_desc)
		ext2fs_unmark_block_bitmap_range2(fs->block_map, old_desc, num);
	return 0;
}

/*
 * This function is used when shrinking a file system.  We need to
 * utilize blocks from what will be the new last block group for the
 * backup superblock and block group descriptor blocks.
 * Unfortunately, those blocks may be used by other files or fs
 * metadata blocks.  We need to mark them as being in use.
 */
static errcode_t reserve_sparse_super2_last_group(ext2_resize_t rfs,
						 ext2fs_block_bitmap meta_bmap)
{
	ext2_filsys	fs = rfs->new_fs;
	ext2_filsys	old_fs = rfs->old_fs;
	errcode_t	retval;
	dgrp_t		old_last_bg = rfs->old_fs->group_desc_count - 1;
	dgrp_t		last_bg = fs->group_desc_count - 1;
	dgrp_t		g;
	blk64_t		blk, sb, old_desc;
	blk_t		i, num;
	int		realloc = 0;

	if (!ext2fs_has_feature_sparse_super2(fs->super))
		return 0;

	if (last_bg >= old_last_bg)
		return 0;

	if (fs->super->s_backup_bgs[0] == old_fs->super->s_backup_bgs[0] &&
	    fs->super->s_backup_bgs[1] == old_fs->super->s_backup_bgs[1])
		return 0;

	if (fs->super->s_backup_bgs[0] != last_bg &&
	    fs->super->s_backup_bgs[1] != last_bg)
		return 0;

	if (old_fs->super->s_backup_bgs[0] == last_bg ||
	    old_fs->super->s_backup_bgs[1] == last_bg)
		return 0;

	retval = ext2fs_super_and_bgd_loc2(rfs->new_fs, last_bg,
					   &sb, &old_desc, NULL, &num);
	if (retval)
		return retval;

	if (last_bg && !sb) {
		fputs(_("Should never happen!  No sb in last super_sparse bg?\n"),
		      stderr);
		exit(1);
	}
	if (old_desc && old_desc != sb+1) {
		fputs(_("Should never happen!  Unexpected old_desc in "
			"super_sparse bg?\n"),
		      stderr);
		exit(1);
	}
	num = (old_desc) ? num : 1;

	/* Reserve the backup blocks */
	ext2fs_mark_block_bitmap_range2(fs->block_map, sb, num);

	for (g = 0; g < fs->group_desc_count; g++) {
		blk64_t mb;

		mb = ext2fs_block_bitmap_loc(fs, g);
		if ((mb >= sb) && (mb < sb + num)) {
			ext2fs_block_bitmap_loc_set(fs, g, 0);
			realloc = 1;
		}
		mb = ext2fs_inode_bitmap_loc(fs, g);
		if ((mb >= sb) && (mb < sb + num)) {
			ext2fs_inode_bitmap_loc_set(fs, g, 0);
			realloc = 1;
		}
		mb = ext2fs_inode_table_loc(fs, g);
		if ((mb < sb + num) &&
		    (sb < mb + fs->inode_blocks_per_group)) {
			ext2fs_inode_table_loc_set(fs, g, 0);
			realloc = 1;
		}
		if (realloc) {
			retval = ext2fs_allocate_group_table(fs, g, 0);
			if (retval)
				return retval;
		}
	}

	for (blk = sb, i = 0; i < num; blk++, i++) {
		if (ext2fs_test_block_bitmap2(old_fs->block_map, blk) &&
		    !ext2fs_test_block_bitmap2(meta_bmap, blk)) {
			ext2fs_mark_block_bitmap2(rfs->move_blocks, blk);
			rfs->needed_blocks++;
		}
		ext2fs_mark_block_bitmap2(rfs->reserve_blocks, blk);
	}
	return 0;
}

/*
 * Fix the resize inode
 */
static errcode_t fix_resize_inode(ext2_filsys fs)
{
	struct ext2_inode	inode;
	errcode_t		retval;

	if (!ext2fs_has_feature_resize_inode(fs->super))
		return 0;

	retval = ext2fs_read_inode(fs, EXT2_RESIZE_INO, &inode);
	if (retval) goto errout;

	ext2fs_iblk_set(fs, &inode, 1);

	retval = ext2fs_write_inode(fs, EXT2_RESIZE_INO, &inode);
	if (retval) goto errout;

	if (!inode.i_block[EXT2_DIND_BLOCK]) {
		/*
		 * Avoid zeroing out block #0; that's rude.  This
		 * should never happen anyway since the filesystem
		 * should be fsck'ed and we assume it is consistent.
		 */
		fprintf(stderr, "%s",
			_("Should never happen: resize inode corrupt!\n"));
		exit(1);
	}

	retval = ext2fs_zero_blocks2(fs, inode.i_block[EXT2_DIND_BLOCK], 1,
				     NULL, NULL);
	if (retval)
		goto errout;

	retval = ext2fs_create_resize_inode(fs);
	if (retval)
		goto errout;

errout:
	return retval;
}

struct process_orphan_block_data {
	char 		*buf;
	errcode_t	errcode;
	ext2_ino_t	ino;
	__u32		generation;
};

static int process_orphan_block(ext2_filsys fs,
			       blk64_t	*block_nr,
			       e2_blkcnt_t blockcnt EXT2FS_ATTR((unused)),
			       blk64_t	ref_blk EXT2FS_ATTR((unused)),
			       int	ref_offset EXT2FS_ATTR((unused)),
			       void *priv_data)
{
	struct process_orphan_block_data *pd = priv_data;
	struct ext4_orphan_block_tail *tail;
	blk64_t			blk = *block_nr;
	__le32			new_crc;

	pd->errcode = io_channel_read_blk64(fs->io, blk, 1, pd->buf);
	if (pd->errcode)
		return BLOCK_ABORT;
	tail = ext2fs_orphan_block_tail(fs, pd->buf);
	new_crc = ext2fs_cpu_to_le32(ext2fs_do_orphan_file_block_csum(fs,
			pd->ino, pd->generation, blk, pd->buf));
	if (new_crc == tail->ob_checksum)
		return 0;
	tail->ob_checksum = new_crc;
	pd->errcode = io_channel_write_blk64(fs->io, blk, 1, pd->buf);
	if (pd->errcode)
		return BLOCK_ABORT;
	return 0;
}

/*
 * Fix the checksums in orphan_file inode
 */
static errcode_t fix_orphan_file_inode(ext2_filsys fs)
{
	struct process_orphan_block_data pd;
	struct ext2_inode	inode;
	errcode_t		retval;
	ext2_ino_t		orphan_inum;
	char			*orphan_buf;

	if (!ext2fs_has_feature_orphan_file(fs->super) ||
	    !ext2fs_has_feature_metadata_csum(fs->super))
		return 0;

	orphan_inum = fs->super->s_orphan_file_inum;
	retval = ext2fs_read_inode(fs, orphan_inum, &inode);
	if (retval)
		return retval;
	orphan_buf = malloc(fs->blocksize * 4);
	if (!orphan_buf)
		return ENOMEM;

	pd.errcode = 0;
	pd.buf = orphan_buf + 3 * fs->blocksize;
	pd.ino = orphan_inum;
	pd.generation = inode.i_generation;

	retval = ext2fs_block_iterate3(fs, fs->super->s_orphan_file_inum,
				       BLOCK_FLAG_DATA_ONLY,
				       orphan_buf, process_orphan_block, &pd);
	free(orphan_buf);
	return (retval ? retval : pd.errcode);
}

/*
 * Finally, recalculate the summary information
 */
static errcode_t resize2fs_calculate_summary_stats(ext2_filsys fs)
{
	errcode_t	retval;
	blk64_t		b, blk = fs->super->s_first_data_block;
	ext2_ino_t	ino;
	unsigned int	n, max, group, count;
	blk64_t		total_clusters_free = 0;
	int		total_inodes_free = 0;
	int		group_free = 0;
	int		uninit = 0;
	char		*bitmap_buf;

	/*
	 * First calculate the block statistics
	 */
	bitmap_buf = malloc(fs->blocksize);
	if (!bitmap_buf)
		return ENOMEM;
	for (group = 0; group < fs->group_desc_count; group++) {
		retval = ext2fs_get_block_bitmap_range2(fs->block_map,
			B2C(blk), fs->super->s_clusters_per_group, bitmap_buf);
		if (retval) {
			free(bitmap_buf);
			return retval;
		}
		max = ext2fs_group_blocks_count(fs, group) >>
		       fs->cluster_ratio_bits;
		if ((group == fs->group_desc_count - 1) && (max & 7)) {
			n = 0;
			for (b = (fs->super->s_first_data_block +
				  ((blk64_t) fs->super->s_blocks_per_group *
				   group));
			     b < ext2fs_blocks_count(fs->super);
			     b += EXT2FS_CLUSTER_RATIO(fs)) {
				if (ext2fs_test_block_bitmap2(fs->block_map, b))
					n++;
			}
		} else {
			n = ext2fs_bitcount(bitmap_buf, (max + 7) / 8);
		}
		group_free = max - n;
		total_clusters_free += group_free;
		ext2fs_bg_free_blocks_count_set(fs, group, group_free);
		ext2fs_group_desc_csum_set(fs, group);
		blk += fs->super->s_blocks_per_group;
	}
	free(bitmap_buf);
	ext2fs_free_blocks_count_set(fs->super, C2B(total_clusters_free));

	/*
	 * Next, calculate the inode statistics
	 */
	group_free = 0;
	count = 0;
	group = 0;

	/* Protect loop from wrap-around if s_inodes_count maxed */
	uninit = ext2fs_bg_flags_test(fs, group, EXT2_BG_INODE_UNINIT);
	for (ino = 1; ino <= fs->super->s_inodes_count && ino > 0; ino++) {
		if (uninit ||
		    !ext2fs_fast_test_inode_bitmap2(fs->inode_map, ino)) {
			group_free++;
			total_inodes_free++;
		}
		count++;
		if ((count == fs->super->s_inodes_per_group) ||
		    (ino == fs->super->s_inodes_count)) {
			ext2fs_bg_free_inodes_count_set(fs, group, group_free);
			ext2fs_group_desc_csum_set(fs, group);
			group++;
			if (group >= fs->group_desc_count)
				break;
			count = 0;
			group_free = 0;
			uninit = ext2fs_bg_flags_test(fs, group, EXT2_BG_INODE_UNINIT);
		}
	}
	fs->super->s_free_inodes_count = total_inodes_free;
	ext2fs_mark_super_dirty(fs);
	return 0;
}

/*
 *  Journal may have been relocated; update the backup journal blocks
 *  in the superblock.
 */
static errcode_t fix_sb_journal_backup(ext2_filsys fs)
{
	errcode_t	  retval;
	struct ext2_inode inode;

	if (!ext2fs_has_feature_journal(fs->super))
		return 0;

	/* External journal? Nothing to do. */
	if (fs->super->s_journal_dev && !fs->super->s_journal_inum)
		return 0;

	retval = ext2fs_read_inode(fs, fs->super->s_journal_inum, &inode);
	if (retval)
		return retval;
	memcpy(fs->super->s_jnl_blocks, inode.i_block, EXT2_N_BLOCKS*4);
	fs->super->s_jnl_blocks[15] = inode.i_size_high;
	fs->super->s_jnl_blocks[16] = inode.i_size;
	fs->super->s_jnl_backup_type = EXT3_JNL_BACKUP_BLOCKS;
	ext2fs_mark_super_dirty(fs);
	return 0;
}

static int calc_group_overhead(ext2_filsys fs, blk64_t grp,
			       int old_desc_blocks)
{
	blk64_t	super_blk, old_desc_blk, new_desc_blk;
	int overhead;

	/* inode table blocks plus allocation bitmaps */
	overhead = fs->inode_blocks_per_group + 2;

	ext2fs_super_and_bgd_loc2(fs, grp, &super_blk,
				  &old_desc_blk, &new_desc_blk, 0);
	if ((grp == 0) || super_blk)
		overhead++;
	if (old_desc_blk)
		overhead += old_desc_blocks;
	else if (new_desc_blk)
		overhead++;
	return overhead;
}


/*
 * calculate the minimum number of blocks the given fs can be resized to
 */
blk64_t calculate_minimum_resize_size(ext2_filsys fs, int flags)
{
	ext2_ino_t inode_count;
	dgrp_t groups, flex_groups;
	blk64_t blks_needed, data_blocks;
	blk64_t grp, data_needed, last_start;
	blk64_t overhead = 0;
	int old_desc_blocks;
	unsigned flexbg_size = 1U << fs->super->s_log_groups_per_flex;

	/*
	 * first figure out how many group descriptors we need to
	 * handle the number of inodes we have
	 */
	inode_count = fs->super->s_inodes_count -
		fs->super->s_free_inodes_count;
	blks_needed = ext2fs_div_ceil(inode_count,
				      fs->super->s_inodes_per_group) *
		(blk64_t) EXT2_BLOCKS_PER_GROUP(fs->super);
	groups = ext2fs_div64_ceil(blks_needed,
				   EXT2_BLOCKS_PER_GROUP(fs->super));
#ifdef RESIZE2FS_DEBUG
	if (flags & RESIZE_DEBUG_MIN_CALC)
		printf("fs has %d inodes, %d groups required.\n",
		       inode_count, groups);
#endif

	/*
	 * number of old-style block group descriptor blocks
	 */
	if (ext2fs_has_feature_meta_bg(fs->super))
		old_desc_blocks = fs->super->s_first_meta_bg;
	else
		old_desc_blocks = fs->desc_blocks +
			fs->super->s_reserved_gdt_blocks;

	/* calculate how many blocks are needed for data */
	data_needed = ext2fs_blocks_count(fs->super);
	for (grp = 0; grp < fs->group_desc_count; grp++) {
		__u32 n = ext2fs_bg_free_blocks_count(fs, grp);

		if (n > EXT2_BLOCKS_PER_GROUP(fs->super))
			n = EXT2_BLOCKS_PER_GROUP(fs->super);
		n += calc_group_overhead(fs, grp, old_desc_blocks);
		if (data_needed < n) {
			if (flags & RESIZE_DEBUG_MIN_CALC)
				printf("file system appears inconsistent?!?\n");
			return ext2fs_blocks_count(fs->super);
		}
		data_needed -= n;
	}
#ifdef RESIZE2FS_DEBUG
	if (flags & RESIZE_DEBUG_MIN_CALC)
		printf("fs requires %llu data blocks.\n",
		       (unsigned long long) data_needed);
#endif

	/*
	 * For ext4 we need to allow for up to a flex_bg worth of
	 * inode tables of slack space so the resize operation can be
	 * guaranteed to finish.
	 */
	flex_groups = groups;
	if (ext2fs_has_feature_flex_bg(fs->super)) {
		dgrp_t remainder = groups & (flexbg_size - 1);

		flex_groups += flexbg_size - remainder;
		if (flex_groups > fs->group_desc_count)
			flex_groups = fs->group_desc_count;
	}

	/*
	 * figure out how many data blocks we have given the number of groups
	 * we need for our inodes
	 */
	data_blocks = EXT2_GROUPS_TO_BLOCKS(fs->super, groups);
	last_start = 0;
	for (grp = 0; grp < flex_groups; grp++) {
		overhead = calc_group_overhead(fs, grp, old_desc_blocks);

		/*
		 * we want to keep track of how much data we can store in
		 * the groups leading up to the last group so we can determine
		 * how big the last group needs to be
		 */
		if (grp < (groups - 1))
			last_start += EXT2_BLOCKS_PER_GROUP(fs->super) -
				overhead;

		if (data_blocks > overhead)
			data_blocks -= overhead;
		else
			data_blocks = 0;
	}
#ifdef RESIZE2FS_DEBUG
	if (flags & RESIZE_DEBUG_MIN_CALC)
		printf("With %d group(s), we have %llu blocks available.\n",
		       groups, (unsigned long long) data_blocks);
#endif

	/*
	 * if we need more group descriptors in order to accommodate our data
	 * then we need to add them here
	 */
	blks_needed = data_needed;
	while (blks_needed > data_blocks) {
		blk64_t remainder = blks_needed - data_blocks;
		dgrp_t extra_grps;

		/* figure out how many more groups we need for the data */
		extra_grps = ext2fs_div64_ceil(remainder,
					       EXT2_BLOCKS_PER_GROUP(fs->super));

		data_blocks += EXT2_GROUPS_TO_BLOCKS(fs->super, extra_grps);

		/* ok we have to account for the last group */
		overhead = calc_group_overhead(fs, groups-1, old_desc_blocks);
		last_start += EXT2_BLOCKS_PER_GROUP(fs->super) - overhead;

		grp = flex_groups;
		groups += extra_grps;
		if (!ext2fs_has_feature_flex_bg(fs->super))
			flex_groups = groups;
		else if (groups > flex_groups) {
			dgrp_t r = groups & (flexbg_size - 1);

			flex_groups = groups + flexbg_size - r;
			if (flex_groups > fs->group_desc_count)
				flex_groups = fs->group_desc_count;
		}

		for (; grp < flex_groups; grp++) {
			overhead = calc_group_overhead(fs, grp,
						       old_desc_blocks);

			/*
			 * again, we need to see how much data we cram into
			 * all of the groups leading up to the last group
			 */
			if (grp < groups - 1)
				last_start += EXT2_BLOCKS_PER_GROUP(fs->super)
					- overhead;

			data_blocks -= overhead;
		}

#ifdef RESIZE2FS_DEBUG
		if (flags & RESIZE_DEBUG_MIN_CALC)
			printf("Added %d extra group(s), "
			       "blks_needed %llu, data_blocks %llu, "
			       "last_start %llu\n", extra_grps,
			       (unsigned long long) blks_needed,
			       (unsigned long long) data_blocks,
			       (unsigned long long) last_start);
#endif
	}

	/* now for the fun voodoo */
	grp = groups - 1;
	if (ext2fs_has_feature_flex_bg(fs->super) &&
	    (grp & ~(flexbg_size - 1)) == 0)
		grp = grp & ~(flexbg_size - 1);
	overhead = 0;
	for (; grp < flex_groups; grp++)
		overhead += calc_group_overhead(fs, grp, old_desc_blocks);

#ifdef RESIZE2FS_DEBUG
	if (flags & RESIZE_DEBUG_MIN_CALC)
		printf("Last group's overhead is %llu\n",
		       (unsigned long long) overhead);
#endif

	/*
	 * if this is the case then the last group is going to have data in it
	 * so we need to adjust the size of the last group accordingly
	 */
	if (last_start < blks_needed) {
		blk64_t remainder = blks_needed - last_start;

#ifdef RESIZE2FS_DEBUG
		if (flags & RESIZE_DEBUG_MIN_CALC)
			printf("Need %llu data blocks in last group\n",
			       (unsigned long long) remainder);
#endif
		/*
		 * 50 is a magic number that mkfs/resize uses to see if its
		 * even worth making/resizing the fs.  basically you need to
		 * have at least 50 blocks in addition to the blocks needed
		 * for the metadata in the last group
		 */
		if (remainder > 50)
			overhead += remainder;
		else
			overhead += 50;
	} else
		overhead += 50;

	overhead += fs->super->s_first_data_block;
#ifdef RESIZE2FS_DEBUG
	if (flags & RESIZE_DEBUG_MIN_CALC)
		printf("Final size of last group is %llu\n",
		       (unsigned long long) overhead);
#endif

	/* Add extra slack for bigalloc file systems */
	if (EXT2FS_CLUSTER_RATIO(fs) > 1)
		overhead += EXT2FS_CLUSTER_RATIO(fs) * 2;

	/*
	 * since our last group doesn't have to be BLOCKS_PER_GROUP
	 * large, we only do groups-1, and then add the number of
	 * blocks needed to handle the group descriptor metadata+data
	 * that we need
	 */
	blks_needed = EXT2_GROUPS_TO_BLOCKS(fs->super, groups - 1);
	blks_needed += overhead;

	/*
	 * Make sure blks_needed covers the end of the inode table in
	 * the last block group.
	 */
	overhead = ext2fs_inode_table_loc(fs, groups-1) +
		fs->inode_blocks_per_group;
	if (blks_needed < overhead)
		blks_needed = overhead;

#ifdef RESIZE2FS_DEBUG
	if (flags & RESIZE_DEBUG_MIN_CALC)
		printf("Estimated blocks needed: %llu\n",
		       (unsigned long long) blks_needed);
#endif

	/*
	 * If at this point we've already added up more "needed" than
	 * the current size, just return current size as minimum.
	 */
	if (blks_needed >= ext2fs_blocks_count(fs->super))
		return ext2fs_blocks_count(fs->super);
	/*
	 * We need to reserve a few extra blocks if extents are
	 * enabled, in case we need to grow the extent tree.  The more
	 * we shrink the file system, the more space we need.
	 *
	 * The absolute worst case is every single data block is in
	 * the part of the file system that needs to be evacuated,
	 * with each data block needs to be in its own extent, and
	 * with each inode needing at least one extent block.
	 */
	if (ext2fs_has_feature_extents(fs->super)) {
		blk64_t safe_margin = (ext2fs_blocks_count(fs->super) -
				       blks_needed)/500;
		unsigned int exts_per_blk = (fs->blocksize /
					     sizeof(struct ext3_extent)) - 1;
		blk64_t worst_case = ((data_needed + exts_per_blk - 1) /
				      exts_per_blk);

		if (worst_case < inode_count)
			worst_case = inode_count;

		if (safe_margin > worst_case)
			safe_margin = worst_case;

#ifdef RESIZE2FS_DEBUG
		if (flags & RESIZE_DEBUG_MIN_CALC)
			printf("Extents safety margin: %llu\n",
			       (unsigned long long) safe_margin);
#endif
		blks_needed += safe_margin;
	}

	return blks_needed;
}
