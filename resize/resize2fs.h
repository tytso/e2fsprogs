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
 * For the extent map
 */
typedef struct _ext2_extent *ext2_extent;

/*
 * For the simple progress meter
 */
typedef struct ext2_sim_progress *ext2_sim_progmeter;

/*
 * Flags for the resizer; most are debugging flags only
 */
#define RESIZE_DEBUG_IO			0x0001
#define RESIZE_DEBUG_BMOVE		0x0002
#define RESIZE_DEBUG_INODEMAP		0x0004
#define RESIZE_DEBUG_ITABLEMOVE		0x0008

#define RESIZE_PERCENT_COMPLETE		0x0100
#define RESIZE_VERBOSE			0x0200

/*
 * The core state structure for the ext2 resizer
 */

struct ext2_resize_struct {
	ext2_filsys	old_fs;
	ext2_filsys	new_fs;
	ext2_brel	block_relocate;
	ext2fs_block_bitmap reserve_blocks;
	ext2fs_block_bitmap move_blocks;
	int		needed_blocks;
	int		flags;
	char		*itable_buf;
};

typedef struct ext2_resize_struct *ext2_resize_t;

/* prototypes */
extern errcode_t resize_fs(ext2_filsys fs, blk_t new_size, int flags);
extern errcode_t ext2fs_inode_move(ext2_resize_t rfs);
extern errcode_t ext2fs_block_move(ext2_resize_t rfs);

/* extent.c */
extern errcode_t ext2fs_create_extent_table(ext2_extent *ret_extent,
					    int size);
extern void ext2fs_free_extent_table(ext2_extent extent);
extern errcode_t ext2fs_add_extent_entry(ext2_extent extent,
					 __u32 old, __u32 new);
extern __u32 ext2fs_extent_translate(ext2_extent extent, __u32 old);
extern void ext2fs_extent_dump(ext2_extent extent, FILE *out);
extern errcode_t ext2fs_iterate_extent(ext2_extent extent, __u32 *old,
				       __u32 *new, int *size);

/* sim_progress.c */
extern errcode_t ext2fs_progress_init(ext2_sim_progmeter *ret_prog,
				      const char *label,
				      int labelwidth, int barwidth,
				      __u32 maxdone, int flags);
extern void ext2fs_progress_update(ext2_sim_progmeter prog,
					__u32 current);
extern void ext2fs_progress_close(ext2_sim_progmeter prog);


