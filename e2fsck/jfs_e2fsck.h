#ifndef _JFS_E2FSCK_H
#define _JFS_E2FSCK_H

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
	e2fsck_t i_ctx;
	ino_t	 i_ino;
	struct ext2_inode i_ext2;
};

typedef e2fsck_t kdev_t;

int bmap(struct inode *inode, int block);
struct buffer_head *getblk(e2fsck_t ctx, blk_t blocknr, int blocksize);
void ll_rw_block(int rw, int dummy, struct buffer_head *bh[]);
void mark_buffer_dirty(struct buffer_head *bh, int dummy);
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

static inline kmem_cache_t * do_cache_create(int len)
{
	kmem_cache_t *new_cache;
	new_cache = malloc(sizeof(*new_cache));
	if (new_cache)
		new_cache->object_length = len;
	return new_cache;
}

#endif /* _JFS_E2FSCK_H */

