/*
 * closefs.c --- close an ext2 filesystem
 * 
 * Copyright (C) 1993, 1994, 1995, 1996 Theodore Ts'o.
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 */

#include <stdio.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <time.h>
#include <string.h>

#include "ext2_fs.h"
#include "ext2fsP.h"

static int test_root(int a, int b)
{
	if (a == 0)
		return 1;
	while (1) {
		if (a == 1)
			return 1;
		if (a % b)
			return 0;
		a = a / b;
	}
}

int ext2fs_bg_has_super(ext2_filsys fs, int group_block)
{
	if (!(fs->super->s_feature_ro_compat &
	      EXT2_FEATURE_RO_COMPAT_SPARSE_SUPER))
		return 1;

	if (test_root(group_block, 3) || (test_root(group_block, 5)) ||
	    test_root(group_block, 7))
		return 1;
	
	return 0;
}

/*
 * This function forces out the primary superblock.  We need to only
 * write out those fields which we have changed, since if the
 * filesystem is mounted, it may have changed some of the other
 * fields.
 *
 * It takes as input a superblock which has already been byte swapped
 * (if necessary).
 *
 */
static errcode_t write_primary_superblock(ext2_filsys fs,
					  struct ext2_super_block *super)
{
	__u16		*old_super, *new_super;
	int		check_idx, write_idx, size;
	errcode_t	retval;

	if (!fs->io->manager->write_byte || !fs->orig_super) {
		io_channel_set_blksize(fs->io, SUPERBLOCK_OFFSET);
		retval = io_channel_write_blk(fs->io, 1, -SUPERBLOCK_SIZE,
					      super);
		io_channel_set_blksize(fs->io, fs->blocksize);
		return retval;
	}

	old_super = (__u16 *) fs->orig_super;
	new_super = (__u16 *) super;

	for (check_idx = 0; check_idx < SUPERBLOCK_SIZE/2; check_idx++) {
		if (old_super[check_idx] == new_super[check_idx])
			continue;
		write_idx = check_idx;
		for (check_idx++; check_idx < SUPERBLOCK_SIZE/2; check_idx++)
			if (old_super[check_idx] == new_super[check_idx])
				break;
		size = 2 * (check_idx - write_idx);
#if 0
		printf("Writing %d bytes starting at %d\n",
		       size, write_idx*2);
#endif
		retval = io_channel_write_byte(fs->io,
			       SUPERBLOCK_OFFSET + (2 * write_idx), size,
					       new_super + write_idx);
		if (retval)
			return retval;
	}
	memcpy(fs->orig_super, super, SUPERBLOCK_SIZE);
	return 0;
}


/*
 * Updates the revision to EXT2_DYNAMIC_REV
 */
void ext2fs_update_dynamic_rev(ext2_filsys fs)
{
	struct ext2_super_block *sb = fs->super;

	if (sb->s_rev_level > EXT2_GOOD_OLD_REV)
		return;

	sb->s_rev_level = EXT2_DYNAMIC_REV;
	sb->s_first_ino = EXT2_GOOD_OLD_FIRST_INO;
	sb->s_inode_size = EXT2_GOOD_OLD_INODE_SIZE;
	/* s_uuid is handled by e2fsck already */
	/* other fields should be left alone */
}

/*
 * This writes out the block group descriptors the original,
 * old-fashioned way.
 */
static errcode_t write_bgdesc(ext2_filsys fs, dgrp_t group, blk_t group_block,
			      struct ext2_group_desc *group_shadow)
{
	errcode_t	retval;
	char		*group_ptr = (char *) group_shadow;
	int		j, old_desc_blocks, mod;
	int		has_super = ext2fs_bg_has_super(fs, group);
	dgrp_t		meta_bg_size, meta_bg;

	meta_bg_size = (fs->blocksize / sizeof (struct ext2_group_desc));
	meta_bg = group / meta_bg_size;
	if (fs->super->s_feature_incompat & EXT2_FEATURE_INCOMPAT_META_BG)
		old_desc_blocks = fs->super->s_first_meta_bg;
	else
		old_desc_blocks = fs->desc_blocks;
	if (!(fs->super->s_feature_incompat & EXT2_FEATURE_INCOMPAT_META_BG) ||
	    (meta_bg < fs->super->s_first_meta_bg)) {
		if (!has_super ||
		    ((fs->flags & EXT2_FLAG_MASTER_SB_ONLY) && (group != 0)))
			return 0;
		for (j=0; j < old_desc_blocks; j++) {
			retval = io_channel_write_blk(fs->io,
						      group_block+1+j, 1,
						      group_ptr);
			if (retval)
				return retval;
			group_ptr += fs->blocksize;
		}
	} else {
		if (has_super)
			group_block++;
		mod = group % meta_bg_size;
		if ((mod == 0) || (mod == 1) || (mod == (meta_bg_size-1))) {
			if (mod && (fs->flags & EXT2_FLAG_MASTER_SB_ONLY))
				return 0;
			return io_channel_write_blk(fs->io, group_block,
				1, group_ptr + (meta_bg*fs->blocksize));
		}
	}
	return 0;
}


static errcode_t write_backup_super(ext2_filsys fs, dgrp_t group,
				    blk_t group_block,
				    struct ext2_super_block *super_shadow)
{
	dgrp_t	sgrp = group;
	
	if (sgrp > ((1 << 16) - 1))
		sgrp = (1 << 16) - 1;
#ifdef EXT2FS_ENABLE_SWAPFS
	if (fs->flags & EXT2_FLAG_SWAP_BYTES)
		super_shadow->s_block_group_nr = ext2fs_swab16(sgrp);
	else
#endif
		fs->super->s_block_group_nr = sgrp;

	return io_channel_write_blk(fs->io, group_block, -SUPERBLOCK_SIZE, 
				    super_shadow);
}


errcode_t ext2fs_flush(ext2_filsys fs)
{
	dgrp_t		i,j;
	blk_t		group_block;
	errcode_t	retval;
	unsigned long	fs_state;
	struct ext2_super_block *super_shadow = 0;
	struct ext2_group_desc *group_shadow = 0;
	struct ext2_group_desc *s, *t;
	
	EXT2_CHECK_MAGIC(fs, EXT2_ET_MAGIC_EXT2FS_FILSYS);

	fs_state = fs->super->s_state;

	fs->super->s_wtime = time(NULL);
	fs->super->s_block_group_nr = 0;
#ifdef EXT2FS_ENABLE_SWAPFS
	if (fs->flags & EXT2_FLAG_SWAP_BYTES) {
		retval = EXT2_ET_NO_MEMORY;
		retval = ext2fs_get_mem(SUPERBLOCK_SIZE,
					(void **) &super_shadow);
		if (retval)
			goto errout;
		retval = ext2fs_get_mem((size_t)(fs->blocksize *
						 fs->desc_blocks),
					(void **) &group_shadow);
		if (retval)
			goto errout;
		memset(group_shadow, 0, (size_t) fs->blocksize *
		       fs->desc_blocks);

		/* swap the superblock */
		*super_shadow = *fs->super;
		ext2fs_swap_super(super_shadow);

		/* swap the group descriptors */
		for (j=0, s=fs->group_desc, t=group_shadow;
		     j < fs->group_desc_count; j++, t++, s++) {
			*t = *s;
			ext2fs_swap_group_desc(t);
		}
	} else {
		super_shadow = fs->super;
		group_shadow = fs->group_desc;
	}
#else
	super_shadow = fs->super;
	group_shadow = fs->group_desc;
#endif
	
	/*
	 * Write out master superblock.  This has to be done
	 * separately, since it is located at a fixed location
	 * (SUPERBLOCK_OFFSET).
	 */
	retval = write_primary_superblock(fs, super_shadow);
	if (retval)
		goto errout;

	/*
	 * If this is an external journal device, don't write out the
	 * block group descriptors or any of the backup superblocks
	 */
	if (fs->super->s_feature_incompat &
	    EXT3_FEATURE_INCOMPAT_JOURNAL_DEV) {
		retval = 0;
		goto errout;
	}

	/*
	 * Set the state of the FS to be non-valid.  (The state has
	 * already been backed up earlier, and will be restored when
	 * we exit.)
	 */
	fs->super->s_state &= ~EXT2_VALID_FS;
#ifdef EXT2FS_ENABLE_SWAPFS
	if (fs->flags & EXT2_FLAG_SWAP_BYTES) {
		*super_shadow = *fs->super;
		ext2fs_swap_super(super_shadow);
	}
#endif

	/*
	 * Write out the master group descriptors, and the backup
	 * superblocks and group descriptors.
	 */
	group_block = fs->super->s_first_data_block;
	for (i = 0; i < fs->group_desc_count; i++) {
		if (!(fs->flags & EXT2_FLAG_MASTER_SB_ONLY) &&
		    i && ext2fs_bg_has_super(fs, i)) {
			retval = write_backup_super(fs, i, group_block,
						    super_shadow);
			if (retval)
				goto errout;
		}
		if (!(fs->flags & EXT2_FLAG_SUPER_ONLY)) {
			if ((retval = write_bgdesc(fs, i, group_block, 
						   group_shadow)))
				goto errout;
		}
		group_block += EXT2_BLOCKS_PER_GROUP(fs->super);
	}
	fs->super->s_block_group_nr = 0;

	/*
	 * If the write_bitmaps() function is present, call it to
	 * flush the bitmaps.  This is done this way so that a simple
	 * program that doesn't mess with the bitmaps doesn't need to
	 * drag in the bitmaps.c code.
	 */
	if (fs->write_bitmaps) {
		retval = fs->write_bitmaps(fs);
		if (retval)
			goto errout;
	}

	fs->flags &= ~EXT2_FLAG_DIRTY;

	/*
	 * Flush the blocks out to disk
	 */
	retval = io_channel_flush(fs->io);
errout:
	fs->super->s_state = fs_state;
	if (fs->flags & EXT2_FLAG_SWAP_BYTES) {
		if (super_shadow)
			ext2fs_free_mem((void **) &super_shadow);
		if (group_shadow)
			ext2fs_free_mem((void **) &group_shadow);
	}
	return retval;
}

errcode_t ext2fs_close(ext2_filsys fs)
{
	errcode_t	retval;
	
	EXT2_CHECK_MAGIC(fs, EXT2_ET_MAGIC_EXT2FS_FILSYS);

	if (fs->flags & EXT2_FLAG_DIRTY) {
		retval = ext2fs_flush(fs);
		if (retval)
			return retval;
	}
	if (fs->write_bitmaps) {
		retval = fs->write_bitmaps(fs);
		if (retval)
			return retval;
	}
	ext2fs_free(fs);
	return 0;
}

