/*
 * resize2fs.h --- ext2 resizer header file
 *
 * Copyright (C) 1997 Theodore Ts'o
 * 
 * %Begin-Header%
 * All rights reserved.
 * %End-Header%
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/time.h>

#ifdef HAVE_LINUX_FS_H
#include <linux/fs.h>
#endif
#include <linux/ext2_fs.h>

#include "ext2fs/ext2fs.h"
#include "ext2fs/brel.h"
#include "banalysis.h"

#ifdef __STDC__
#define NOARGS void
#else
#define NOARGS
#define const
#endif

/*
 * The core state structure for the ext2 resizer
 */

struct ext2_resize_struct {
	ext2_filsys	old_fs;
	ext2_filsys	new_fs;
	ext2_brel	block_relocate;
	ext2fs_block_bitmap reserve_blocks;
	int		needed_blocks;
};

typedef struct ext2_resize_struct *ext2_resize_t;

/* prototypes */
extern errcode_t resize_fs(ext2_filsys fs, blk_t new_size);
