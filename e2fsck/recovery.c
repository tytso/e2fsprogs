/*
 * linux/fs/recovery.c
 * 
 * Written by Stephen C. Tweedie <sct@redhat.com>, 1999
 *
 * Copyright 1999 Red Hat Software --- All Rights Reserved
 *
 * This file is part of the Linux kernel and is made available under
 * the terms of the GNU General Public License, version 2, or at your
 * option, any later version, incorporated herein by reference.
 *
 * Journal recovery routines for the generic filesystem journaling code;
 * part of the ext2fs journaling system.  
 */

#ifndef __KERNEL__
#include "jfs.h"
#else
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/jfs.h>
#include <linux/errno.h>
#include <linux/malloc.h>
#include <linux/locks.h>
#include <linux/buffer.h>


/* Release readahead buffers after use */
static void brelse_array(struct buffer_head *b[], int n)
{
	while (--n >= 0)
		brelse (b[n]);
}


/*
 * When reading from the journal, we are going through the block device
 * layer directly and so there is no readahead being done for us.  We
 * need to implement any readahead ourselves if we want it to happen at
 * all.  Recovery is basically one long sequential read, so make sure we
 * do the IO in reasonably large chunks.
 *
 * This is not so critical that we need to be enormously clever about
 * the readahead size, though.  128K is a purely arbitrary, good-enough
 * fixed value.
 */

static int do_readahead(journal_t *journal, unsigned int start)
{
	int err;
	unsigned int max, nbufs, next, blocknr;
	struct buffer_head *bh;
	
	#define MAXBUF 8
	struct buffer_head * bufs[MAXBUF];
	
	/* Do up to 128K of readahead */
	max = start + (128 * 1024 / journal->j_blocksize);
	if (max > journal->j_maxlen)
		max = journal->j_maxlen;

	/* Do the readahead itself.  We'll submit MAXBUF buffer_heads at
	 * a time to the block device IO layer. */
	
	nbufs = 0;
	
	for (next = start; next < max; next++) {
		blocknr = next;
		if (journal->j_inode)
			blocknr = bmap(journal->j_inode, next);
		if (!blocknr) {
			printk (KERN_ERR "JFS: bad block at offset %u\n",
				next);
			err = -EIO;
			goto failed;
		}
		
		bh = getblk(journal->j_dev, blocknr, journal->j_blocksize);
		if (!bh) {
			printk(KERN_ERR "JFS: readahead getblk failed\n");
			err = -ENOMEM;
			goto failed;
		}

		if (!buffer_uptodate(bh) && !buffer_locked(bh)) {
			bufs[nbufs++] = bh;
			if (nbufs == MAXBUF) {
				ll_rw_block(READ, nbufs, bufs);
				brelse_array(bufs, nbufs);
				nbufs = 0;
			}
		} else
			brelse(bh);
	}

	if (nbufs)
		ll_rw_block(READ, nbufs, bufs);
	err = 0;
	
failed:	
	if (nbufs) 
		brelse_array(bufs, nbufs);
	return err;
}
#endif

/*
 * Read a block from the journal
 */

static int jread(struct buffer_head **bhp, journal_t *journal, 
		 unsigned int offset)
{
	unsigned int blocknr;
	struct buffer_head *bh;

	*bhp = NULL;

	if (offset >= journal->j_maxlen)
		return -EINVAL;
			
	blocknr = offset;
	if (journal->j_inode)
		blocknr = bmap(journal->j_inode, offset);
	
	if (!blocknr) {
		printk (KERN_ERR "JFS: bad block at offset %u\n",
			offset);
		return -EIO;
	}
	
	bh = getblk(journal->j_dev, blocknr, journal->j_blocksize);
	if (!bh)
		return -ENOMEM;
	
	if (!buffer_uptodate(bh)) {
		/* If this is a brand new buffer, start readahead.
                   Otherwise, we assume we are already reading it.  */
		if (!buffer_req(bh))
			do_readahead(journal, offset);
		wait_on_buffer(bh);
	}
	
	if (!buffer_uptodate(bh)) {
		printk (KERN_ERR "JFS: Failed to read block at offset %u\n",
			offset);
		brelse(bh);
		return -EIO;
	}
		
	*bhp = bh;
	return 0;
}


/*
 * Count the number of in-use tags in a journal descriptor block.
 */

int count_tags(struct buffer_head *bh, int size)
{
	char *			tagp;
	journal_block_tag_t *	tag;
	int			nr = 0;
	
	tagp = &bh->b_data[sizeof(journal_header_t)];
	
	while ((tagp - bh->b_data + sizeof(journal_block_tag_t)) <= size) {
		tag = (journal_block_tag_t *) tagp;
		
		nr++;
		tagp += sizeof(journal_block_tag_t);
		if (!(tag->t_flags & htonl(JFS_FLAG_SAME_UUID)))
			tagp += 16;

		if (tag->t_flags & htonl(JFS_FLAG_LAST_TAG))
			break;
	}
	
	return nr;
}


/* Make sure we wrap around the log correctly! */
#define wrap(journal, var)						\
do {									\
	if (var >= (journal)->j_last)					\
		var -= ((journal)->j_last - (journal)->j_first);	\
} while (0)

/*
 * journal_recover
 *
 * The primary function for recovering the log contents when mounting a
 * journaled device.  
 */

int journal_recover(journal_t *journal)
{
	unsigned int		first_commit_ID, next_commit_ID;
	unsigned long		next_log_block;
	unsigned long		transaction_start;
	int			err, success = 0;
	journal_superblock_t *	jsb;
	journal_header_t * 	tmp;
	struct buffer_head *	bh;

	/* Precompute the maximum metadata descriptors in a descriptor block */
	int			MAX_BLOCKS_PER_DESC;
	MAX_BLOCKS_PER_DESC = ((journal->j_blocksize-sizeof(journal_header_t))
			       / sizeof(journal_block_tag_t));

	/* 
	 * First thing is to establish what we expect to find in the log
	 * (in terms of transaction IDs), and where (in terms of log
	 * block offsets): query the superblock.  
	 */

	jsb = journal->j_superblock;
	next_commit_ID = ntohl(jsb->s_sequence);
	next_log_block = ntohl(jsb->s_start);

	first_commit_ID = next_commit_ID;
	
	/* 
	 * The journal superblock's s_start field (the current log head)
	 * is always zero if, and only if, the journal was cleanly
	 * unmounted.  
	 */

	if (!jsb->s_start) {
		jfs_debug(1, "No recovery required, last transaction %d\n",
			  ntohl(jsb->s_sequence));
		journal->j_transaction_sequence = ++next_commit_ID;
		return 0;
	}
	
	jfs_debug(1, "Starting recovery\n");
	
	/*
	 * Now we walk through the log, transaction by transaction,
	 * making sure that each transaction has a commit block in the
	 * expected place.  Each complete transaction gets replayed back
	 * into the main filesystem. 
	 */

	while (1) { 
		jfs_debug(2, "Looking for commit ID %u at %lu/%lu\n",
			  next_commit_ID, next_log_block, journal->j_last);
 		transaction_start = next_log_block;

		while (next_log_block < journal->j_last) {
			/* Skip over each chunk of the transaction
			 * looking either the next descriptor block or
			 * the final commit record. */

			jfs_debug(3, "JFS: checking block %ld\n", 
				  next_log_block);
			err = jread(&bh, journal, next_log_block);
			if (err)
				goto failed;
			
			/* What kind of buffer is it? 
			 * 
			 * If it is a descriptor block, work out the
			 * expected location of the next and skip to it.
			 *
			 * If it is the right commit block, end the
			 * search and start recovering the transaction.
			 *
			 * Any non-control block, or an unexpected
			 * control block is interpreted as old data from
			 * a previous wrap of the log: stop recovery at
			 * this point.  
			 */
		
			tmp = (journal_header_t *) bh->b_data;
			
			if (tmp->h_magic == htonl(JFS_MAGIC_NUMBER)) {
				int blocktype = ntohl(tmp->h_blocktype);
				jfs_debug(3, "Found magic %d\n", blocktype);
				
				if (blocktype == JFS_DESCRIPTOR_BLOCK) {
					/* Work out where the next descriptor
					 * should be. */
					next_log_block++;
					next_log_block += count_tags(bh, journal->j_blocksize);
					wrap(journal, next_log_block);
					brelse(bh);
					continue;
				} else if (blocktype == JFS_COMMIT_BLOCK) {
					unsigned int sequence = tmp->h_sequence;
					brelse(bh);
					if (sequence == htonl(next_commit_ID))
						goto commit;
					jfs_debug(2, "found sequence %d, "
						  "expected %d.\n",
						  ntohl(sequence),
						  next_commit_ID);
					goto finished;
				}
			}

			/* We didn't recognise it?  OK, we've gone off
			 * the tail of the log in that case. */
			brelse(bh);
			break;
		}

		goto finished;
		
	commit:
		jfs_debug(2, "Found transaction %d\n", next_commit_ID);

		/* OK, we have a transaction to commit.  Rewind to the
		 * start of it, gather up all of the buffers in each
		 * transaction segment, and replay the segments one by
		 * one. */

		next_log_block = transaction_start;
		
		while (1) {
			int			flags;
			char *			tagp;
			journal_block_tag_t *	tag;
			struct buffer_head *	obh;
			struct buffer_head *	nbh;
			
			err = jread(&bh, journal, next_log_block++);
			wrap(journal, next_log_block);
			if (err)
				goto failed;

			tmp = (journal_header_t *) bh->b_data;
			/* should never happen - we just checked above - AED */
			J_ASSERT(tmp->h_magic == htonl(JFS_MAGIC_NUMBER));

			/* If it is the commit block, then we are all done! */
			if (tmp->h_blocktype == htonl(JFS_COMMIT_BLOCK)) {
				brelse(bh);
				break;
			}
			
			/* A descriptor block: we can now write all of
			 * the data blocks.  Yay, useful work is finally
			 * getting done here! */

			tagp = &bh->b_data[sizeof(journal_header_t)];
			
			while ((tagp - bh->b_data +sizeof(journal_block_tag_t))
			       <= journal->j_blocksize) {
				tag = (journal_block_tag_t *) tagp;
				flags = ntohl(tag->t_flags);
				
				err = jread(&obh, journal, next_log_block++);
				wrap(journal, next_log_block);
				if (err) {
					/* Recover what we can, but
					 * report failure at the end. */
					success = err;
					printk (KERN_ERR 
						"JFS: IO error recovering "
						"block %ld in log\n",
						next_log_block-1);
				} else {
					/* can never happen if jread OK - AED */
					J_ASSERT(obh != NULL);

					/* And find a buffer for the new data
					 * being restored */
					nbh = getblk(journal->j_dev, 
						     ntohl(tag->t_blocknr),
						     journal->j_blocksize);
					if (nbh == NULL) {
						printk(KERN_ERR 
						       "JFS: Out of memory "
						       "during recovery.\n");
						err = -ENOMEM;
						brelse(bh);
						brelse(obh);
						goto failed;
					}

					memcpy(nbh->b_data, obh->b_data, 
					       journal->j_blocksize);
					if (flags & JFS_FLAG_ESCAPE) {
						* ((unsigned int *) bh->b_data) = htonl(JFS_MAGIC_NUMBER);
					}
					
					mark_buffer_dirty(nbh, 1);
					/* ll_rw_block(WRITE, 1, &nbh); */
					brelse(obh);
					brelse(nbh);
				}
				
				tagp += sizeof(journal_block_tag_t);
				if (!(flags & JFS_FLAG_SAME_UUID))
					tagp += 16;

				if (flags & JFS_FLAG_LAST_TAG)
					break;

			} /* end of tag loop */

			brelse(bh);
			
		} /* end of descriptor block loop */
			
		/* We have now replayed that entire transaction: start
		 * looking for the next transaction. */
		next_commit_ID++;
	}
		
 finished:
	err = success;
	fsync_dev(journal->j_dev);

 failed:
	
	/* Restart the log at the next transaction ID, thus invalidating
	 * any existing commit records in the log. */
	jfs_debug(0, "JFS: recovery, exit status %d, "
		  "recovered transactions %u to %u\n", 
		  err, first_commit_ID, next_commit_ID);
	journal->j_transaction_sequence = ++next_commit_ID;

	return err;
}
