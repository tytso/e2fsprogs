
#ifndef _JFS_COMPAT_H
#define _JFS_COMPAT_H

#include "e2fsck.h"
#include <errno.h>

#define printk printf
#define KERN_ERR ""
#define KERN_DEBUG ""

#define READ 0
#define WRITE 1

typedef int tid_t;
typedef e2fsck_t kdev_t;
typedef struct journal_s journal_t;

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

struct journal_s
{
	unsigned long		j_flags;
	int			j_errno;
	struct buffer_head *	j_sb_buffer;
	struct journal_superblock_s *j_superblock;
	unsigned long		j_head;
	unsigned long		j_tail;
	unsigned long		j_free;
	unsigned long		j_first, j_last;
	kdev_t			j_dev;
	int			j_blocksize;
	unsigned int		j_blk_offset;
	unsigned int		j_maxlen;
	struct inode *		j_inode;
	tid_t			j_tail_sequence;
	tid_t			j_transaction_sequence;
	__u8			j_uuid[16];
};

int bmap(struct inode *inode, int block);
struct buffer_head *getblk(e2fsck_t ctx, blk_t blocknr, int blocksize);
void ll_rw_block(int rw, int dummy, struct buffer_head *bh);
void mark_buffer_dirty(struct buffer_head *bh, int dummy);
void brelse(struct buffer_head *bh);
int buffer_uptodate(struct buffer_head *bh);
void wait_on_buffer(struct buffer_head *bh);
#define fsync_dev(dev) do {} while(0)
#define buffer_req(bh) 1
#define do_readahead(journal, start) do {} while(0)
	
extern e2fsck_t e2fsck_global_ctx;  /* Try your very best not to use this! */

#define J_ASSERT(assert)						\
	do { if (!(assert)) {						\
		printf ("Assertion failure in %s() at %s line %d: "	\
			"\"%s\"\n",					\
			__FUNCTION__, __FILE__, __LINE__, # assert);	\
		fatal_error(e2fsck_global_ctx, 0);			\
	} } while (0)

#endif /* _JFS_COMPAT_H */
