/*
 * banalysis.c --- Analyze a filesystem by block 
 *
 * Copyright (C) 1997 Theodore Ts'o.  This file may be redistributed
 * under the terms of the GNU Public License.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/time.h>

#include <linux/ext2_fs.h>

#include "ext2fs/ext2fs.h"

#include "ext2fs/brel.h"
#include "banalysis.h"

struct process_block_struct {
	struct ext2_block_analyzer_funcs *funcs;
	struct ext2_inode_context *ctx;
	void *private;
};

static int process_block(ext2_filsys fs, blk_t	*block_nr,
			 int blockcnt, blk_t ref_block,
			 int ref_offset, void *private)
{
	struct process_block_struct *pb = private;
	blk_t	new_block;
	struct ext2_block_relocate_entry ent;

	if (ref_block == 0)
		ref_offset = blockcnt;

	new_block = pb->funcs->block_analyze(fs, *block_nr, ref_block,
					     ref_offset, pb->ctx, pb->private);
	if (new_block) {
		ent.new = new_block;
		ent.offset = ref_offset;
		if (ref_block) {
			ent.owner.block_ref = ref_block;
			ent.flags = 0;
		} else {
			ent.owner.inode_ref = pb->ctx->ino;
			ent.flags = RELOCATE_INODE_REF;
		}
		ext2fs_brel_put(pb->ctx->brel, *block_nr, &ent);
	}
	return 0;
}

errcode_t ext2_block_analyze(ext2_filsys fs,
			     struct ext2_block_analyzer_funcs *funcs,
			     ext2_brel block_relocation_table,
			     void *private)
{
	ino_t	ino;
	struct ext2_inode inode;
	errcode_t	retval;
	struct process_block_struct pb;
	struct ext2_inode_context ctx;
	ext2_inode_scan	scan;
	char		*block_buf;
	
	retval = ext2fs_open_inode_scan(fs, 0, &scan);
	if (retval)
		return retval;

	pb.funcs = funcs;
	pb.private = private;
	pb.ctx = &ctx;
	
	block_buf = malloc(fs->blocksize * 3);
	if (!block_buf)
		return ENOMEM;

	retval = ext2fs_get_next_inode(scan, &ino, &inode);
	if (retval)
		return retval;
	ctx.ctx = private;
	ctx.brel = block_relocation_table;
	while (ino) {
		if ((inode.i_links_count == 0) ||
		    !ext2fs_inode_has_valid_blocks(&inode))
			goto next;
		
		ctx.ino = ino;
		ctx.inode = &inode;
		ctx.error = 0;

		if (funcs->pre_analyze &&
		    !(*funcs->pre_analyze)(fs, &ctx, private))
			goto next;

		retval = ext2fs_block_iterate2(fs, ino, 0, block_buf,
					      process_block, &pb);
		if (retval)
			return retval;

		if (funcs->post_analyze) 
			(funcs->post_analyze)(fs, &ctx, private);

	next:
		retval = ext2fs_get_next_inode(scan, &ino, &inode);
		if (retval == EXT2_ET_BAD_BLOCK_IN_INODE_TABLE)
			goto next;
	}
	return 0;
}

