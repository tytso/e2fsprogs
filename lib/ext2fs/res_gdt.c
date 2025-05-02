/*
 * res_gdt.c --- reserve blocks for growing the group descriptor table
 *               during online resizing.
 *
 * Copyright (C) 2002 Andreas Dilger
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Library
 * General Public License, version 2.
 * %End-Header%
 */

#include "config.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "ext2_fs.h"
#include "ext2fsP.h"

/*
 * Iterate through the groups which hold BACKUP superblock/GDT copies in an
 * ext3 filesystem.  The counters should be initialized to 1, 5, and 7 before
 * calling this for the first time.  In a sparse_super filesystem it will be
 * the sequence of powers of 3, 5, and 7: 1, 3, 5, 7, 9, 25, 27, 49, 81, ...
 * For a non-sparse filesystem it will be every group: 1, 2, 3, 4, ...
 * For a sparse_super2 filesystem there are two backups in specific groups.
 */
dgrp_t ext2fs_list_backups(ext2_filsys fs, dgrp_t *three,
			   dgrp_t *five, dgrp_t *seven)
{
	dgrp_t *min = three;
	unsigned long long mult = 3;
	dgrp_t ret;

	if (fs && ext2fs_has_feature_sparse_super2(fs->super)) {
		if (*min == 1) {
			*min += 1;
			if (fs->super->s_backup_bgs[0])
				return fs->super->s_backup_bgs[0];
		}
		if (*min == 2) {
			*min += 1;
			if (fs->super->s_backup_bgs[1])
				return fs->super->s_backup_bgs[1];
		}

		return fs->group_desc_count;
	}

	if (fs && !ext2fs_has_feature_sparse_super(fs->super)) {
		ret = *min;
		*min += 1;

		return ret;
	}

	if (*five < *min) {
		min = five;
		mult = 5;
	}
	if (*seven < *min) {
		min = seven;
		mult = 7;
	}

	ret = *min;
	mult *= *min;
	if (mult > (dgrp_t)-1)
		*min = (dgrp_t)-1;
	else
		*min = mult;

	return ret;
}

/*
 * This code assumes that the reserved blocks have already been marked in-use
 * during ext2fs_initialize(), so that they are not allocated for other
 * uses before we can add them to the resize inode (which has to come
 * after the creation of the inode table).
 */
errcode_t ext2fs_create_resize_inode(ext2_filsys fs)
{
	errcode_t		retval, retval2;
	struct ext2_super_block	*sb;
	struct ext2_inode	inode;
	__u32			*dindir_buf, *gdt_buf;
	unsigned long long	apb, inode_size;
	/* FIXME-64 - can't deal with extents */
	blk_t			dindir_blk, rsv_off, gdt_off, gdt_blk;
	int			dindir_dirty = 0, inode_dirty = 0, sb_blk = 0;

	EXT2_CHECK_MAGIC(fs, EXT2_ET_MAGIC_EXT2FS_FILSYS);

	sb = fs->super;

	retval = ext2fs_get_array(2, fs->blocksize, &dindir_buf);
	if (retval)
		return retval;
	gdt_buf = (__u32 *)((char *)dindir_buf + fs->blocksize);

	retval = ext2fs_read_inode(fs, EXT2_RESIZE_INO, &inode);
	if (retval)
		goto out_free;

	/*
	 * File systems with a blocksize of 1024 and bigalloc have
	 * sb->s_first_data_block of 0; yet the superblock is still at
	 * block #1.  We compensate for it here.
	 */
	sb_blk = sb->s_first_data_block;
	if (fs->blocksize == 1024 && sb_blk == 0)
		sb_blk = 1;

	/* Maximum possible file size (we only use double indirect blocks) */
	apb = EXT2_ADDR_PER_BLOCK(sb);
	if ((dindir_blk = inode.i_block[EXT2_DIND_BLOCK])) {
#ifdef RES_GDT_DEBUG
		printf("reading GDT dindir %u\n", dindir_blk);
#endif
		retval = ext2fs_read_ind_block(fs, dindir_blk, dindir_buf);
		if (retval)
			goto out_inode;
	} else {
		blk_t goal = sb_blk + fs->desc_blocks +
			sb->s_reserved_gdt_blocks + 2 +
			fs->inode_blocks_per_group;

		retval = ext2fs_alloc_block(fs, goal, 0, &dindir_blk);
		if (retval)
			goto out_free;
		inode.i_mode = LINUX_S_IFREG | 0600;
		inode.i_links_count = 1;
		inode.i_block[EXT2_DIND_BLOCK] = dindir_blk;
		ext2fs_iblk_set(fs, &inode, 1);
		memset(dindir_buf, 0, fs->blocksize);
#ifdef RES_GDT_DEBUG
		printf("allocated GDT dindir %u\n", dindir_blk);
#endif
		dindir_dirty = inode_dirty = 1;
		inode_size = apb*apb + apb + EXT2_NDIR_BLOCKS;
		inode_size *= fs->blocksize;
		retval = ext2fs_inode_size_set(fs, &inode, inode_size);
		if (retval)
			goto out_free;
		inode.i_ctime = ext2fsP_get_time(fs);
	}

	for (rsv_off = 0, gdt_off = fs->desc_blocks,
	     gdt_blk = sb_blk + 1 + fs->desc_blocks;
	     rsv_off < sb->s_reserved_gdt_blocks;
	     rsv_off++, gdt_off++, gdt_blk++) {
		dgrp_t three = 1, five = 5, seven = 7;
		dgrp_t grp, last = 0;
		int gdt_dirty = 0;

		gdt_off %= apb;
		if (!dindir_buf[gdt_off]) {
			/* FIXME XXX XXX
			blk_t new_blk;

			retval = ext2fs_new_block(fs, gdt_blk, 0, &new_blk);
			if (retval)
				goto out_free;
			if (new_blk != gdt_blk) {
				// XXX free block
				retval = -1; // XXX
			}
			*/
			gdt_dirty = dindir_dirty = inode_dirty = 1;
			memset(gdt_buf, 0, fs->blocksize);
			dindir_buf[gdt_off] = gdt_blk;
			ext2fs_iblk_add_blocks(fs, &inode, 1);
#ifdef RES_GDT_DEBUG
			printf("added primary GDT block %u at %u[%u]\n",
			       gdt_blk, dindir_blk, gdt_off);
#endif
		} else if (dindir_buf[gdt_off] == gdt_blk) {
#ifdef RES_GDT_DEBUG
			printf("reading primary GDT block %u\n", gdt_blk);
#endif
			retval = ext2fs_read_ind_block(fs, gdt_blk, gdt_buf);
			if (retval)
				goto out_dindir;
		} else {
#ifdef RES_GDT_DEBUG
			printf("bad primary GDT %u != %u at %u[%u]\n",
			       dindir_buf[gdt_off], gdt_blk,dindir_blk,gdt_off);
#endif
			retval = EXT2_ET_RESIZE_INODE_CORRUPT;
			goto out_dindir;
		}

		while ((grp = ext2fs_list_backups(fs, &three, &five, &seven)) <
		       fs->group_desc_count) {
			blk_t expect = gdt_blk + grp * sb->s_blocks_per_group;

			if (!gdt_buf[last]) {
#ifdef RES_GDT_DEBUG
				printf("added backup GDT %u grp %u@%u[%u]\n",
				       expect, grp, gdt_blk, last);
#endif
				gdt_buf[last] = expect;
				ext2fs_iblk_add_blocks(fs, &inode, 1);
				gdt_dirty = inode_dirty = 1;
			} else if (gdt_buf[last] != expect) {
#ifdef RES_GDT_DEBUG
				printf("bad backup GDT %u != %u at %u[%u]\n",
				       gdt_buf[last], expect, gdt_blk, last);
#endif
				retval = EXT2_ET_RESIZE_INODE_CORRUPT;
				goto out_dindir;
			}
			last++;
		}
		if (gdt_dirty) {
#ifdef RES_GDT_DEBUG
			printf("writing primary GDT block %u\n", gdt_blk);
#endif
			retval = ext2fs_write_ind_block(fs, gdt_blk, gdt_buf);
			if (retval)
				goto out_dindir;
		}
	}

out_dindir:
	if (dindir_dirty) {
		retval2 = ext2fs_write_ind_block(fs, dindir_blk, dindir_buf);
		if (!retval)
			retval = retval2;
	}
out_inode:
#ifdef RES_GDT_DEBUG
	printf("inode.i_blocks = %u, i_size = %lu\n", inode.i_blocks,
	       EXT2_I_SIZE(&inode));
#endif
	if (inode_dirty) {
		time_t now = ext2fsP_get_time(fs);

		ext2fs_inode_xtime_set(&inode, i_atime, now);
		ext2fs_inode_xtime_set(&inode, i_mtime, now);
		retval2 = ext2fs_write_new_inode(fs, EXT2_RESIZE_INO, &inode);
		if (!retval)
			retval = retval2;
	}
out_free:
	ext2fs_free_mem(&dindir_buf);
	return retval;
}

