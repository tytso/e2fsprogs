/*
 * Compatibility header file for e2fsck which should be included
 * instead of linux/jfs.h
 *
 * Copyright (C) 2000 Stephen C. Tweedie
 *
 * This file may be redistributed under the terms of the
 * GNU General Public License version 2 or at your discretion
 * any later version.
 */

/*
 * Pull in the definition of the e2fsck context structure
 */
#include "e2fsck.h"

struct buffer_head {
	char	 b_data[8192];
	e2fsck_t b_ctx;
	io_channel b_io;
	int	 b_size;
	blk_t	 b_blocknr;
	int	 b_dirty;
	int	 b_uptodate;
	int	 b_err;
};

struct inode {
	e2fsck_t	i_ctx;
	ext2_ino_t	i_ino;
	struct ext2_inode i_ext2;
};

typedef e2fsck_t kdev_t;

/*
 * Kernel compatibility functions are defined in journal.c
 */
int bmap(struct inode *inode, int block);
struct buffer_head *getblk(e2fsck_t ctx, blk_t blocknr, int blocksize);
void ll_rw_block(int rw, int dummy, struct buffer_head *bh[]);
void mark_buffer_dirty(struct buffer_head *bh, int dummy);
void mark_buffer_uptodate(struct buffer_head *bh, int val);
void brelse(struct buffer_head *bh);
int buffer_uptodate(struct buffer_head *bh);
void wait_on_buffer(struct buffer_head *bh);
#define fsync_dev(dev) do {} while(0)
#define buffer_req(bh) 1
#define do_readahead(journal, start) do {} while(0)
	
extern e2fsck_t e2fsck_global_ctx;  /* Try your very best not to use this! */

typedef struct {
	int	object_length;
} kmem_cache_t;

#define kmem_cache_alloc(cache,flags) malloc((cache)->object_length)
#define kmem_cache_free(cache,obj) free(obj)
#define kmem_cache_create(name,len,a,b,c,d) do_cache_create(len)
#define kmalloc(len,flags) malloc(len)
#define kfree(p) free(p)

/*
 * We use the standard libext2fs portability tricks for inline
 * functions.  
 */
extern kmem_cache_t * do_cache_create(int len);
	
#if (defined(E2FSCK_INCLUDE_INLINE_FUNCS) || !defined(NO_INLINE_FUNCS))
#ifdef E2FSCK_INCLUDE_INLINE_FUNCS
#define _INLINE_ extern
#else
#ifdef __GNUC__
#define _INLINE_ extern __inline__
#else				/* For Watcom C */
#define _INLINE_ extern inline
#endif
#endif

_INLINE_ kmem_cache_t * do_cache_create(int len)
{
	kmem_cache_t *new_cache;
	new_cache = malloc(sizeof(*new_cache));
	if (new_cache)
		new_cache->object_length = len;
	return new_cache;
}

#undef _INLINE_
#endif

/*
 * Now pull in the real linux/jfs.h definitions.
 */
#include <linux/jfs.h>
