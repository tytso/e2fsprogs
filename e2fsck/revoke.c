/*
 * linux/fs/revoke.c
 * 
 * Written by Stephen C. Tweedie <sct@redhat.com>, 2000
 *
 * Copyright 2000 Red Hat corp --- All Rights Reserved
 *
 * This file is part of the Linux kernel and is made available under
 * the terms of the GNU General Public License, version 2, or at your
 * option, any later version, incorporated herein by reference.
 *
 * Journal revoke routines for the generic filesystem journaling code;
 * part of the ext2fs journaling system.
 *
 * Revoke is the mechanism used to prevent old log records for deleted
 * metadata from being replayed on top of newer data using the same
 * blocks.  The revoke mechanism is used in two separate places:
 * 
 * + Commit: during commit we write the entire list of the current
 *   transaction's revoked blocks to the journal
 * 
 * + Recovery: during recovery we record the transaction ID of all
 *   revoked blocks.  If there are multiple revoke records in the log
 *   for a single block, only the last one counts, and if there is a log
 *   entry for a block beyond the last revoke, then that log entry still
 *   gets replayed.
 *
 * We can get interactions between revokes and new log data within a
 * single transaction:
 *
 * Block is revoked and then journaled:
 *   The desired end result is the journaling of the new block, so we 
 *   cancel the revoke before the transaction commits.
 *
 * Block is journaled and then revoked:
 *   The revoke must take precedence over the write of the block, so we
 *   need either to cancel the journal entry or to write the revoke
 *   later in the log than the log block.  In this case, we choose the
 *   latter: journaling a block cancels any revoke record for that block
 *   in the current transaction, so any revoke for that block in the
 *   transaction must have happened after the block was journaled and so
 *   the revoke must take precedence.
 *
 * Block is revoked and then written as data: 
 *   The data write is allowed to succeed, but the revoke is _not_
 *   cancelled.  We still need to prevent old log records from
 *   overwriting the new data.  We don't even need to clear the revoke
 *   bit here.
 *
 * Revoke information on buffers is a tri-state value:
 *
 * RevokeValid clear:	no cached revoke status, need to look it up
 * RevokeValid set, Revoke clear:
 *			buffer has not been revoked, and cancel_revoke
 *			need do nothing.
 * RevokeValid set, Revoke set:
 * buffer has been revoked.  
 */

#ifndef __KERNEL__
#include "jfs_user.h"
#else
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/jfs.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/locks.h>
#include <linux/buffer.h>
#include <linux/list.h>
#endif

static kmem_cache_t *revoke_record_cache;
static kmem_cache_t *revoke_table_cache;

/* Each revoke record represents one single revoked block.  During
   journal replay, this involves recording the transaction ID of the
   last transaction to revoke this block. */

struct jfs_revoke_record_s 
{
	struct list_head  hash;
	tid_t		  sequence;	/* Used for recovery only */
	unsigned long	  blocknr;	
};


/* The revoke table is just a simple hash table of revoke records. */
struct jfs_revoke_table_s
{
	/* It is conceivable that we might want a larger hash table
	 * for recovery.  Must be a power of two. */
	int		  hash_size; 
	int		  hash_shift; 
	struct list_head *hash_table;
};


#ifdef __KERNEL__
static void write_one_revoke_record(journal_t *, transaction_t *,
				    struct buffer_head **, int *,
				    struct jfs_revoke_record_s *);
static void flush_descriptor(journal_t *, struct buffer_head *, int);
#endif

/* Utility functions to maintain the revoke table */

/* Borrowed from buffer.c: this is a tried and tested block hash function */
static inline int hash(journal_t *journal, unsigned long block)
{
	struct jfs_revoke_table_s *table = journal->j_revoke;
	int hash_shift = table->hash_shift;
	
	return ((block << (hash_shift - 6)) ^
		(block >> 13) ^
		(block << (hash_shift - 12))) & (table->hash_size - 1);
}

static int insert_revoke_hash(journal_t *journal,
			      unsigned long blocknr, tid_t seq)
{
	struct list_head *hash_list;
	struct jfs_revoke_record_s *record;
	
	record = kmem_cache_alloc(revoke_record_cache, GFP_KERNEL);
	if (!record)
		return -ENOMEM;

	record->sequence = seq;
	record->blocknr = blocknr;
	hash_list = &journal->j_revoke->hash_table[hash(journal, blocknr)];
	list_add(&record->hash, hash_list);
	return 0;
}

/* Find a revoke record in the journal's hash table. */

static struct jfs_revoke_record_s *find_revoke_record(journal_t *journal,
						      unsigned long blocknr)
{
	struct list_head *hash_list;
	struct jfs_revoke_record_s *record;
	
	hash_list = &journal->j_revoke->hash_table[hash(journal, blocknr)];

	record = (struct jfs_revoke_record_s *) hash_list->next;
	while (&(record->hash) != hash_list) {
		if (record->blocknr == blocknr)
			return record;
		record = (struct jfs_revoke_record_s *) record->hash.next;
	}
	return NULL;
}



/* Initialise the revoke table for a given journal to a given size. */

int journal_init_revoke(journal_t *journal, int hash_size)
{
	int shift, tmp;
	
	J_ASSERT (journal->j_revoke == NULL);
	
	if (!revoke_record_cache)
		revoke_record_cache = 
			kmem_cache_create ("revoke_record",
					   sizeof(struct jfs_revoke_record_s),
					   0, SLAB_HWCACHE_ALIGN, NULL, NULL);
	
	if (!revoke_table_cache)
		revoke_table_cache = 
			kmem_cache_create ("revoke_table",
					   sizeof(struct jfs_revoke_table_s),
					   0, 0, NULL, NULL);

	if (!revoke_record_cache || !revoke_table_cache)
		return -ENOMEM;
	
	journal->j_revoke = kmem_cache_alloc(revoke_table_cache, GFP_KERNEL);
	if (!journal->j_revoke)
		return -ENOMEM;
	
	/* Check that the hash_size is a power of two */
	J_ASSERT ((hash_size & (hash_size-1)) == 0);

	journal->j_revoke->hash_size = hash_size;

	shift = 0;
	tmp = hash_size;
	while((tmp >>= 1UL) != 0UL)
		shift++;
	journal->j_revoke->hash_shift = shift;

	journal->j_revoke->hash_table =
		kmalloc(hash_size * sizeof(struct list_head), GFP_KERNEL);
	if (!journal->j_revoke->hash_table) {
		kmem_cache_free(revoke_table_cache, journal->j_revoke);
		journal->j_revoke = NULL;
		return -ENOMEM;
	}
	
	for (tmp = 0; tmp < hash_size; tmp++)
		INIT_LIST_HEAD(&journal->j_revoke->hash_table[tmp]);
	
	return 0;
}

/* Destoy a journal's revoke table.  The table must already be empty! */

void journal_destroy_revoke(journal_t *journal)
{
	struct jfs_revoke_table_s *table;
	struct list_head *hash_list;
	int i;
	
	table = journal->j_revoke;
	if (!table)
		return;
	
	for (i=0; i<table->hash_size; i++) {
		hash_list = &table->hash_table[i];
		J_ASSERT (list_empty(hash_list));
	}
	
	kfree(table->hash_table);
	kmem_cache_free(revoke_table_cache, table);
	journal->j_revoke = NULL;
}


#ifdef __KERNEL__

/* 
 * journal_revoke: revoke a given buffer_head from the journal.  This
 * prevents the block from being replayed during recovery if we take a
 * crash after this current transaction commits.  Any subsequent
 * metadata writes of the buffer in this transaction cancel the
 * revoke.  
 *
 * Note that this call may block --- it is up to the caller to make
 * sure that there are no further calls to journal_write_metadata
 * before the revoke is complete.  In ext3, this implies calling the
 * revoke before clearing the block bitmap when we are deleting
 * metadata. 
 *
 * Revoke performs a journal_forget on any buffer_head passed in as a
 * parameter, but does _not_ forget the buffer_head if the bh was only
 * found implicitly. 
 *
 * Revoke must observe the same synchronisation rules as bforget: it
 * must not discard the buffer once it has blocked.
 */

int journal_revoke(handle_t *handle, unsigned long blocknr, 
		   struct buffer_head *bh_in)
{
	struct buffer_head *bh;
	journal_t *journal;
	kdev_t dev;
	int err;

	journal = handle->h_transaction->t_journal;
	if (!journal_set_features(journal, 0, 0, JFS_FEATURE_INCOMPAT_REVOKE)){
		J_ASSERT (!"Cannot set revoke feature!");
		return -EINVAL;
	}
	
	dev = journal->j_dev;
	bh = bh_in;

	if (!bh)
		bh = get_hash_table(dev, blocknr, journal->j_blocksize);

	/* We really ought not ever to revoke twice in a row without
           first having the revoke cancelled: it's illegal to free a
           block twice without allocating it in between! */
	if (bh) {
		J_ASSERT (!test_and_set_bit(BH_Revoked, &bh->b_state));
		set_bit(BH_RevokeValid, &bh->b_state);
		if (bh_in)
			journal_forget(handle, bh_in);
		else
			brelse(bh);
	}

	lock_journal(journal);
	err = insert_revoke_hash(journal, blocknr, 
				 handle->h_transaction->t_tid);
	unlock_journal(journal);
	
	return err;
}


/*
 * Cancel an outstanding revoke.  For use only internally by the
 * journaling code (called from journal_get_write_access).
 *
 * We trust the BH_Revoked bit on the buffer if the buffer is already
 * being journaled: if there is no revoke pending on the buffer, then we
 * don't do anything here.
 *
 * This would break if it were possible for a buffer to be revoked and
 * discarded, and then reallocated within the same transaction.  In such
 * a case we would have lost the revoked bit, but when we arrived here
 * the second time we would still have a pending revoke to cancel.  So,
 * do not trust the Revoked bit on buffers unless RevokeValid is also
 * set.
 *
 * The caller must have the journal locked.
 * */

void journal_cancel_revoke(handle_t *handle, struct buffer_head *bh)
{
	struct jfs_revoke_record_s *record;
	journal_t *journal = handle->h_transaction->t_journal;
	int need_cancel;
	
	J_ASSERT (journal->j_locked);
	
	/* Is the existing Revoke bit valid?  If so, we trust it, and
	 * only perform the full cancel if the revoke bit is set.  If
	 * not, we can't trust the revoke bit, and we need to do the
	 * full search for a revoke record. */
	if (test_and_set_bit(BH_RevokeValid, &bh->b_state))
		need_cancel = (test_and_clear_bit(BH_Revoked, &bh->b_state));
	else {
		need_cancel = 1;
		clear_bit(BH_Revoked, &bh->b_state);
	}
	
	if (need_cancel) {
		record = find_revoke_record(journal, bh->b_blocknr);
		if (record) {
			list_del(&record->hash);
			kmem_cache_free(revoke_record_cache, record);
		}
	}
}


/*
 * Write revoke records to the journal for all entries in the current
 * revoke hash, deleting the entries as we go.
 *
 * Called with the journal lock held.
 */

void journal_write_revoke_records(journal_t *journal, 
				  transaction_t *transaction)
{
	struct buffer_head *descriptor;
	struct jfs_revoke_record_s *record;
	struct jfs_revoke_table_s *revoke;
	struct list_head *hash_list;
	int i, offset, count;
	
	descriptor = NULL; 
	offset = 0;
	count = 0;
	revoke = journal->j_revoke;
	
	for (i = 0; i < revoke->hash_size; i++) {
		hash_list = &revoke->hash_table[i];

		while (!list_empty(hash_list)) {
			record = (struct jfs_revoke_record_s *) 
				hash_list->next;
			write_one_revoke_record(journal, transaction,
						&descriptor, &offset, 
						record);
			count++;
			list_del(&record->hash);
			kmem_cache_free(revoke_record_cache, record);
		}
	}
	if (descriptor) 
		flush_descriptor(journal, descriptor, offset);
	jfs_debug(1, "Wrote %d revoke records\n", count);
}

/* 
 * Write out one revoke record.  We need to create a new descriptor
 * block if the old one is full or if we have not already created one.  
 */

static void write_one_revoke_record(journal_t *journal, 
				    transaction_t *transaction,
				    struct buffer_head **descriptorp, 
				    int *offsetp,
				    struct jfs_revoke_record_s *record)
{
	struct buffer_head *descriptor;
	int offset;
	journal_header_t *header;
	
	/* If we are already aborting, this all becomes a noop.  We
           still need to go round the loop in
           journal_write_revoke_records in order to free all of the
           revoke records: only the IO to the journal is omitted. */
	if (is_journal_abort(journal))
		return;

	descriptor = *descriptorp;
	offset = *offsetp;
	
	/* Make sure we have a descriptor with space left for the record */
	if (descriptor) {
		if (offset == journal->j_blocksize) {
			flush_descriptor(journal, descriptor, offset);
			descriptor = NULL;
		}
	}
	
	if (!descriptor) {
		descriptor = journal_get_descriptor_buffer(journal);
		header = (journal_header_t *) &descriptor->b_data[0];
		header->h_magic     = htonl(JFS_MAGIC_NUMBER);
		header->h_blocktype = htonl(JFS_REVOKE_BLOCK);
		header->h_sequence  = htonl(transaction->t_tid);

		/* Record it so that we can wait for IO completion later */
		journal_file_buffer(descriptor, transaction, BJ_LogCtl);
		
		offset = sizeof(journal_revoke_header_t);
		*descriptorp = descriptor;
	}
	
	* ((unsigned int *)(&descriptor->b_data[offset])) = 
		htonl(record->blocknr);
	offset += 4;
	*offsetp = offset;
}

/* 
 * Flush a revoke descriptor out to the journal.  If we are aborting,
 * this is a noop; otherwise we are generating a buffer which needs to
 * be waited for during commit, so it has to go onto the appropriate
 * journal buffer list.
 */

static void flush_descriptor(journal_t *journal, 
			     struct buffer_head *descriptor, 
			     int offset)
{
	journal_revoke_header_t *header;
	
	if (is_journal_abort(journal)) {
		brelse(descriptor);
		return;
	}
	
	header = (journal_revoke_header_t *) descriptor->b_data;
	header->r_count = htonl(offset);
	set_bit(BH_JWrite, &descriptor->b_state);
	ll_rw_block (WRITE, 1, &descriptor);
}

#endif

/* 
 * Revoke support for recovery.
 *
 * Recovery needs to be able to:
 *
 *  record all revoke records, including the tid of the latest instance
 *  of each revoke in the journal
 *
 *  check whether a given block in a given transaction should be replayed
 *  (ie. has not been revoked by a revoke record in that or a subsequent
 *  transaction)
 * 
 *  empty the revoke table after recovery.
 */

/*
 * First, setting revoke records.  We create a new revoke record for
 * every block ever revoked in the log as we scan it for recovery, and
 * we update the existing records if we find multiple revokes for a
 * single block. 
 */

int journal_set_revoke(journal_t *journal, 
		       unsigned long blocknr, 
		       tid_t sequence)
{
	struct jfs_revoke_record_s *record;
	
	record = find_revoke_record(journal, blocknr);
	if (record) {
		/* If we have multiple occurences, only record the
		 * latest sequence number in the hashed record */
		if (tid_gt(sequence, record->sequence))
			record->sequence = sequence;
		return 0;
	} 
	return insert_revoke_hash(journal, blocknr, sequence);
}

/* 
 * Test revoke records.  For a given block referenced in the log, has
 * that block been revoked?  A revoke record with a given transaction
 * sequence number revokes all blocks in that transaction and earlier
 * ones, but later transactions still need replayed.
 */

int journal_test_revoke(journal_t *journal, 
			unsigned long blocknr,
			tid_t sequence)
{
	struct jfs_revoke_record_s *record;
	
	record = find_revoke_record(journal, blocknr);
	if (!record)
		return 0;
	if (tid_gt(sequence, record->sequence))
		return 0;
	return 1;
}

/*
 * Finally, once recovery is over, we need to clear the revoke table so
 * that it can be reused by the running filesystem.
 */

void journal_clear_revoke(journal_t *journal)
{
	int i;
	struct list_head *hash_list;
	struct jfs_revoke_record_s *record;
	struct jfs_revoke_table_s *revoke;
	
	revoke = journal->j_revoke;
	
	for (i = 0; i < revoke->hash_size; i++) {
		hash_list = &revoke->hash_table[i];
		while (!list_empty(hash_list)) {
			record = (struct jfs_revoke_record_s*) hash_list->next;
			list_del(&record->hash);
			kmem_cache_free(revoke_record_cache, record);
		}
	}
}

