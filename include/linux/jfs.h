/*
 * linux/include/linux/jfs.h
 * 
 * Written by Stephen C. Tweedie <sct@redhat.com>
 *
 * Copyright 1998-2000 Red Hat, Inc --- All Rights Reserved
 *
 * This file is part of the Linux kernel and is made available under
 * the terms of the GNU General Public License, version 2, or at your
 * option, any later version, incorporated herein by reference.
 *
 * Definitions for transaction data structures for the buffer cache
 * filesystem journaling support.
 */

#ifndef _LINUX_JFS_H
#define _LINUX_JFS_H

/* Allow this file to be included directly into e2fsprogs */
#ifndef __KERNEL__
#include "jfs_compat.h"
#endif

/*
 * Debug code enabled by default for kernel builds
 */
#ifdef __KERNEL__
#define JFS_DEBUG
#endif

extern int journal_enable_debug;

#ifdef JFS_DEBUG
#define jfs_debug(n, f, a...)						\
	do {								\
		if ((n) <= journal_enable_debug) {			\
			printk (KERN_DEBUG "JFS DEBUG: (%s, %d): %s: ",	\
				__FILE__, __LINE__, __FUNCTION__);	\
		  	printk (f, ## a);				\
		}							\
	} while (0)
#else
#define jfs_debug(f, a...)	/**/
#endif

#define JFS_MIN_JOURNAL_BLOCKS 1024

/*
 * Internal structures used by the logging mechanism:
 */

#define JFS_MAGIC_NUMBER 0xc03b3998U /* The first 4 bytes of /dev/random! */


/*
 * On-disk structures
 */

/* 
 * Descriptor block types:
 */

#define JFS_DESCRIPTOR_BLOCK	1
#define JFS_COMMIT_BLOCK	2
#define JFS_SUPERBLOCK_V1	3
#define JFS_SUPERBLOCK_V2	4
#define JFS_REVOKE_BLOCK	5

/*
 * Standard header for all descriptor blocks:
 */
typedef struct journal_header_s
{
	__u32		h_magic;
	__u32		h_blocktype;
	__u32		h_sequence;
} journal_header_t;


/* 
 * The block tag: used to describe a single buffer in the journal 
 */
typedef struct journal_block_tag_s
{
	__u32		t_blocknr;	/* The on-disk block number */
	__u32		t_flags;	/* See below */
} journal_block_tag_t;

/* 
 * The revoke descriptor: used on disk to describe a series of blocks to
 * be revoked from the log 
 */
typedef struct journal_revoke_header_s
{
	journal_header_t r_header;
	int		 r_count;	/* Count of bytes used in the block */
} journal_revoke_header_t;


/* Definitions for the journal tag flags word: */
#define JFS_FLAG_ESCAPE		1	/* on-disk block is escaped */
#define JFS_FLAG_SAME_UUID	2	/* block has same uuid as previous */
#define JFS_FLAG_DELETED	4	/* block deleted by this transaction */
#define JFS_FLAG_LAST_TAG	8	/* last tag in this descriptor block */


/*
 * The journal superblock.  All fields are in big-endian byte order.
 */
typedef struct journal_superblock_s
{
/* 0x0000 */
	journal_header_t s_header;

/* 0x000C */
	/* Static information describing the journal */
	__u32	s_blocksize;		/* journal device blocksize */
	__u32	s_maxlen;		/* total blocks in journal file */
	__u32	s_first;		/* first block of log information */
	
/* 0x0018 */
	/* Dynamic information describing the current state of the log */
	__u32	s_sequence;		/* first commit ID expected in log */
	__u32	s_start;		/* blocknr of start of log */

/* 0x0020 */
	/* Error value, as set by journal_abort(). */
	__s32	s_errno;

/* 0x0024 */
	/* Remaining fields are only valid in a version-2 superblock */
	__u32	s_feature_compat; 	/* compatible feature set */
	__u32	s_feature_incompat; 	/* incompatible feature set */
	__u32	s_feature_ro_compat; 	/* readonly-compatible feature set */
/* 0x0030 */
	__u8	s_uuid[16];		/* 128-bit uuid for journal */

/* 0x0040 */
	__u32	s_nr_users;		/* Nr of filesystems sharing log */
	
	__u32	s_dynsuper;		/* Blocknr of dynamic superblock copy*/
	
/* 0x0048 */
	__u32	s_max_transaction;	/* Limit of journal blocks per trans.*/
	__u32	s_max_trans_data;	/* Limit of data blocks per trans. */

/* 0x0050 */
	__u32	s_padding[44];

/* 0x0100 */
	__u8	s_users[16*48];		/* ids of all fs'es sharing the log */
/* 0x0400 */
} journal_superblock_t;

#define JFS_HAS_COMPAT_FEATURE(j,mask)					\
	((j)->j_format_version >= 2 &&					\
	 ((j)->j_superblock->s_feature_compat & cpu_to_be32((mask))))
#define JFS_HAS_RO_COMPAT_FEATURE(j,mask)				\
	((j)->j_format_version >= 2 &&					\
	 ((j)->j_superblock->s_feature_ro_compat & cpu_to_be32((mask))))
#define JFS_HAS_INCOMPAT_FEATURE(j,mask)				\
	((j)->j_format_version >= 2 &&					\
	 ((j)->j_superblock->s_feature_incompat & cpu_to_be32((mask))))

#define JFS_FEATURE_INCOMPAT_REVOKE	0x00000001

/* Features known to this kernel version: */
#define JFS_KNOWN_COMPAT_FEATURES	0
#define JFS_KNOWN_ROCOMPAT_FEATURES	0
#define JFS_KNOWN_INCOMPAT_FEATURES	JFS_FEATURE_INCOMPAT_REVOKE

#ifdef __KERNEL__

#include <linux/fs.h>


#define J_ASSERT(assert)						\
	do { if (!(assert)) {						\
		printk (KERN_EMERG					\
			"Assertion failure in %s() at %s line %d: "	\
			"\"%s\"\n",					\
			__FUNCTION__, __FILE__, __LINE__, # assert);	\
		* ((char *) 0) = 0;					\
	} } while (0)


struct jfs_revoke_table_s;

/* The handle_t type represents a single atomic update being performed
 * by some process.  All filesystem modifications made by the process go
 * through this handle.  Recursive operations (such as quota operations)
 * are gathered into a single update.
 *
 * The buffer credits field is used to account for journaled buffers
 * being modified by the running process.  To ensure that there is
 * enough log space for all outstanding operations, we need to limit the
 * number of outstanding buffers possible at any time.  When the
 * operation completes, any buffer credits not used are credited back to
 * the transaction, so that at all times we know how many buffers the
 * outstanding updates on a transaction might possibly touch. */

struct handle_s 
{
	/* Which compound transaction is this update a part of? */
	transaction_t	      * h_transaction;

	/* Number of remaining buffers we are allowed to dirty: */
	int			h_buffer_credits;

	/* Reference count on this handle */
	int			h_ref;

	/* Flags */
	unsigned int		h_sync	: 1;	/* sync-on-close */
	unsigned int		h_jdata	: 1;	/* force data journaling */
};


/* The transaction_t type is the guts of the journaling mechanism.  It
 * tracks a compound transaction through its various states:
 *
 * RUNNING:	accepting new updates
 * LOCKED:	Updates still running but we don't accept new ones
 * RUNDOWN:	Updates are tidying up but have finished requesting
 *		new buffers to modify (state not used for now)
 * FLUSH:       All updates complete, but we are still writing to disk
 * COMMIT:      All data on disk, writing commit record
 * FINISHED:	We still have to keep the transaction for checkpointing.
 *
 * The transaction keeps track of all of the buffers modified by a
 * running transaction, and all of the buffers committed but not yet
 * flushed to home for finished transactions.
 */

struct transaction_s 
{
	/* Pointer to the journal for this transaction. */
	journal_t *		t_journal;
	
	/* Sequence number for this transaction */
	tid_t			t_tid;
	
	/* Transaction's current state */
	enum {
		T_RUNNING,
		T_LOCKED,
		T_RUNDOWN,
		T_FLUSH,
		T_COMMIT,
		T_FINISHED 
	}			t_state;

	/* Where in the log does this transaction's commit start? */
	unsigned long		t_log_start;
	
	/* Doubly-linked circular list of all inodes owned by this
           transaction */
	struct inode *		t_ilist;
	
	/* Number of buffers on the t_buffers list */
	int			t_nr_buffers;
	
	/* Doubly-linked circular list of all buffers reserved but not
           yet modified by this transaction */
	struct buffer_head *	t_reserved_list;
	
	/* Doubly-linked circular list of all metadata buffers owned by this
           transaction */
	struct buffer_head *	t_buffers;
	
	/* Doubly-linked circular list of all data buffers still to be
           flushed before this transaction can be committed */
	struct buffer_head *	t_datalist;
	
	/* Doubly-linked circular list of all forget buffers (superceded
           buffers which we can un-checkpoint once this transaction
           commits) */
	struct buffer_head *	t_forget;
	
	/* Doubly-linked circular list of all buffers still to be
           flushed before this transaction can be checkpointed */
	struct buffer_head *	t_checkpoint_list;
	
	/* Doubly-linked circular list of temporary buffers currently
           undergoing IO in the log */
	struct buffer_head *	t_iobuf_list;
	
	/* Doubly-linked circular list of metadata buffers being
           shadowed by log IO.  The IO buffers on the iobuf list and the
           shadow buffers on this list match each other one for one at
           all times. */
	struct buffer_head *	t_shadow_list;
	
	/* Doubly-linked circular list of control buffers being written
           to the log. */
	struct buffer_head *	t_log_list;
	
	/* Number of outstanding updates running on this transaction */
	int			t_updates;

	/* Number of buffers reserved for use by all handles in this
	 * transaction handle but not yet modified. */
	int			t_outstanding_credits;
	
	/* Forward and backward links for the circular list of all
	 * transactions awaiting checkpoint */
	transaction_t		*t_cpnext, *t_cpprev;

	/* When will the transaction expire (become due for commit), in
	 * jiffies ? */
	unsigned long		t_expires;
};


/* The journal_t maintains all of the journaling state information for a
 * single filesystem.  It is linked to from the fs superblock structure.
 * 
 * We use the journal_t to keep track of all outstanding transaction
 * activity on the filesystem, and to manage the state of the log
 * writing process. */

struct journal_s
{
	/* General journaling state flags */
	unsigned long		j_flags;

	/* Is there an outstanding uncleared error on the journal (from
	 * a prior abort)? */
	int			j_errno;
	
	/* The superblock buffer */
	struct buffer_head *	j_sb_buffer;
	journal_superblock_t *	j_superblock;

	/* Version of the superblock format */
	int			j_format_version;

	/* Number of processes waiting to create a barrier lock */
	int			j_barrier_count;
	
	/* The barrier lock itself */
	struct semaphore	j_barrier;
	
	/* Transactions: The current running transaction... */
	transaction_t *		j_running_transaction;
	
	/* ... the transaction we are pushing to disk ... */
	transaction_t *		j_committing_transaction;
	
	/* ... and a linked circular list of all transactions waiting
	 * for checkpointing. */
	transaction_t *		j_checkpoint_transactions;

	/* Wait queue for locking of the journal structure.  */
	struct wait_queue *	j_wait_lock;

	/* Wait queue for waiting for a locked transaction to start
           committing, or for a barrier lock to be released */
	struct wait_queue *	j_wait_transaction_locked;
	
	/* Wait queue for waiting for checkpointing to complete */
	struct wait_queue *	j_wait_logspace;
	
	/* Wait queue for waiting for commit to complete */
	struct wait_queue *	j_wait_done_commit;
	
	/* Wait queue to trigger checkpointing */
	struct wait_queue *	j_wait_checkpoint;
	
	/* Wait queue to trigger commit */
	struct wait_queue *	j_wait_commit;
	
	/* Wait queue to wait for updates to complete */
	struct wait_queue *	j_wait_updates;

	/* Semaphore for locking against concurrent checkpoints */
	struct semaphore 	j_checkpoint_sem;
		
	/* Journal running state: */
	/* The lock flag is *NEVER* touched from interrupts. */
	unsigned int		j_locked : 1;

	/* Journal head: identifies the first unused block in the journal. */
	unsigned long		j_head;
	
	/* Journal tail: identifies the oldest still-used block in the
	 * journal. */
	unsigned long		j_tail;

	/* Journal free: how many free blocks are there in the journal? */
	unsigned long		j_free;

	/* Journal start and end: the block numbers of the first usable
	 * block and one beyond the last usable block in the journal. */
	unsigned long		j_first, j_last;

	/* Device, blocksize and starting block offset for the location
	 * where we store the journal. */
	kdev_t			j_dev;
	int			j_blocksize;
	unsigned int		j_blk_offset;

	/* Total maximum capacity of the journal region on disk. */
	unsigned int		j_maxlen;

	/* Optional inode where we store the journal.  If present, all
	 * journal block numbers are mapped into this inode via
	 * bmap(). */
	struct inode *		j_inode;

	/* Sequence number of the oldest transaction in the log */
	tid_t			j_tail_sequence;
	/* Sequence number of the next transaction to grant */
	tid_t			j_transaction_sequence;
	/* Sequence number of the most recently committed transaction */
	tid_t			j_commit_sequence;
	/* Sequence number of the most recent transaction wanting commit */
	tid_t			j_commit_request;

	/* Journal uuid: identifies the object (filesystem, LVM volume
	 * etc) backed by this journal.  This will eventually be
	 * replaced by an array of uuids, allowing us to index multiple
	 * devices within a single journal and to perform atomic updates
	 * across them.  */

	__u8			j_uuid[16];

	/* Pointer to the current commit thread for this journal */
	struct task_struct *	j_task;

	/* Maximum number of metadata buffers to allow in a single
	 * compound commit transaction */
	int			j_max_transaction_buffers;

	/* What is the maximum transaction lifetime before we begin a
	 * commit? */
	unsigned long		j_commit_interval;

	/* The timer used to wakeup the commit thread: */
	struct timer_list *	j_commit_timer;
	int			j_commit_timer_active;

	/* The revoke table: maintains the list of revoked blocks in the
           current transaction. */
	struct jfs_revoke_table_s *j_revoke;
};

/* 
 * Journal flag definitions 
 */
#define JFS_UNMOUNT	0x001	/* Journal thread is being destroyed */
#define JFS_SYNC	0x002	/* Perform synchronous transaction commits */
#define JFS_ABORT	0x004	/* Journaling has been aborted for errors. */
#define JFS_ACK_ERR	0x008	/* The errno in the sb has been acked */
#define JFS_FLUSHED	0x010	/* The journal superblock has been flushed */
#define JFS_LOADED	0x020	/* The journal superblock has been loaded */

/* 
 * Journaling internal variables/parameters 
 */

extern int journal_flush_nr_buffers;


/* 
 * Function declarations for the journaling transaction and buffer
 * management
 */

/* Filing buffers */
extern void journal_unfile_buffer(struct buffer_head *);
extern void journal_refile_buffer(struct buffer_head *);
extern void journal_file_buffer(struct buffer_head *, transaction_t *, int);
extern void journal_clean_data_list(transaction_t *transaction);

/* Log buffer allocation */
extern struct buffer_head * journal_get_descriptor_buffer(journal_t *);
extern unsigned long journal_next_log_block(journal_t *);

/* Commit management */
extern void journal_commit_transaction(journal_t *);

/* Checkpoint list management */
extern void journal_remove_checkpoint(struct buffer_head *);
extern void journal_insert_checkpoint(struct buffer_head *, transaction_t *);

/* Buffer IO */
extern int 
journal_write_metadata_buffer(transaction_t	  *transaction,
			      struct buffer_head  *bh_in,
			      struct buffer_head **bh_out,
			      int		   blocknr);

/* Create and destroy transactions */
extern transaction_t *	get_transaction (journal_t *);
extern void		put_transaction (transaction_t *);

/* Notify state transitions (called by the log writer thread): */
extern int		set_transaction_state (transaction_t *, int);


/* Transaction locking */
extern void		__wait_on_journal (journal_t *);

/* Journal locking.  In 2.2, we assume that the kernel lock is already
 * held. */
static inline void lock_journal (journal_t * journal)
{
#ifdef __SMP__
	J_ASSERT(current->lock_depth >= 0);
#endif
	if (journal->j_locked)
		__wait_on_journal(journal);
	journal->j_locked = 1;
}

static inline int try_lock_journal (journal_t * journal)
{
	if (journal->j_locked)
		return 1;
	journal->j_locked = 1;
	return 0;
}

static inline void unlock_journal (journal_t * journal)
{
	J_ASSERT (journal->j_locked);
	journal->j_locked = 0;
	wake_up(&journal->j_wait_lock);
}

/* This function is gross, but unfortunately we need it as long as
 * existing filesystems want to guard against races by testing
 * bh->b_count.  @@@ Remove this?  We no longer abuse b_count so badly!
 */

static inline int journal_is_buffer_shared(struct buffer_head *bh)
{
	int count = bh->b_count;
	J_ASSERT (count >= 1);
	return (count > 1);
}

/* The journaling code user interface:
 *
 * Create and destroy handles
 * Register buffer modifications against the current transaction. 
 */

extern handle_t *journal_start (journal_t *, int nblocks);
extern int	 journal_restart (handle_t *, int nblocks);
extern int	 journal_extend (handle_t *, int nblocks);
extern int	 journal_get_write_access (handle_t *, struct buffer_head *);
extern int	 journal_get_create_access (handle_t *, struct buffer_head *);
extern int	 journal_get_undo_access (handle_t *, struct buffer_head *);
extern int	 journal_dirty_data (handle_t *, struct buffer_head *);
extern int	 journal_dirty_metadata (handle_t *, struct buffer_head *);
extern void	 journal_release_buffer (handle_t *, struct buffer_head *);
extern void	 journal_forget (handle_t *, struct buffer_head *);
extern void	 journal_sync_buffer (struct buffer_head *);
extern int	 journal_stop (handle_t *);
extern int	 journal_flush (journal_t *);

extern void	 journal_lock_updates (journal_t *);
extern void	 journal_unlock_updates (journal_t *);

extern journal_t * journal_init_dev   (kdev_t, int start, int len, int bsize);
extern journal_t * journal_init_inode (struct inode *);
extern int	   journal_update_format (journal_t *);
extern int	   journal_check_used_features 
		   (journal_t *, unsigned long, unsigned long, unsigned long);
extern int	   journal_check_available_features 
		   (journal_t *, unsigned long, unsigned long, unsigned long);
extern int	   journal_set_features 
		   (journal_t *, unsigned long, unsigned long, unsigned long);
extern int	   journal_create     (journal_t *);
extern int	   journal_load       (journal_t *);
extern void	   journal_release    (journal_t *);
extern int	   journal_wipe       (journal_t *, int);
extern int	   journal_skip_recovery (journal_t *);
extern void	   journal_update_superblock (journal_t *, int);
extern void	   __journal_abort      (journal_t *);
extern void	   journal_abort      (journal_t *, int);
extern int	   journal_errno      (journal_t *);
extern void	   journal_ack_err    (journal_t *);
extern int	   journal_clear_err  (journal_t *);

/* Primary revoke support */
#define JOURNAL_REVOKE_DEFAULT_HASH 256
extern int	   journal_revoke (handle_t *, unsigned long, struct buffer_head *);
extern void	   journal_cancel_revoke(handle_t *, struct buffer_head *);
extern void	   journal_write_revoke_records(journal_t *, transaction_t *);

/* The log thread user interface:
 *
 * Request space in the current transaction, and force transaction commit
 * transitions on demand.
 */

extern int	log_space_left (journal_t *); /* Called with journal locked */
extern void	log_start_commit (journal_t *, transaction_t *);
extern void	log_wait_commit (journal_t *, tid_t);
extern int	log_do_checkpoint (journal_t *, int);

extern void	log_wait_for_space(journal_t *, int nblocks);
extern void	journal_drop_transaction(journal_t *, transaction_t *);
extern int	cleanup_journal_tail(journal_t *);


/* Debugging code only: */

#define jfs_ENOSYS() \
do {								      \
	printk (KERN_ERR "JFS unimplemented function " __FUNCTION__); \
	current->state = TASK_UNINTERRUPTIBLE;			      \
	schedule();						      \
} while (1)

/*
 * is_journal_abort
 *
 * Simple test wrapper function to test the JFS_ABORT state flag.  This
 * bit, when set, indicates that we have had a fatal error somewhere,
 * either inside the journaling layer or indicated to us by the client
 * (eg. ext3), and that we and should not commit any further
 * transactions.  
 */

static inline int is_journal_abort(journal_t *journal)
{
	return journal->j_flags & JFS_ABORT;
}


extern inline void mark_buffer_jdirty(struct buffer_head * bh)
{
	if (!test_and_set_bit(BH_JDirty, &bh->b_state))
		set_writetime(bh, 0);
}

/* Not all architectures define BUG() */
#ifndef BUG
 #define BUG() do { \
        printk("kernel BUG at %s:%d!\n", __FILE__, __LINE__); \
	* ((char *) 0) = 0; \
 } while (0)
#endif /* BUG */

#endif /* __KERNEL__   */

/* Function prototypes, used by both user- and kernel- space */

/* recovery.c */
extern int	   journal_recover    (journal_t *);

/* revoke.c */
	/* Primary recovery support */
extern int	   journal_init_revoke(journal_t *, int);
extern void	   journal_destroy_revoke(journal_t *);

	/* Recovery revoke support */
extern int	   journal_set_revoke(journal_t *, unsigned long, tid_t);
extern int	   journal_test_revoke(journal_t *, unsigned long, tid_t);
extern void	   journal_clear_revoke(journal_t *);


/* Comparison functions for transaction IDs: perform comparisons using
 * modulo arithmetic so that they work over sequence number wraps. */

static inline int tid_gt(tid_t x, tid_t y)
{
	int difference = (x - y);
	return (difference > 0);
}

static inline int tid_geq(tid_t x, tid_t y)
{
	int difference = (x - y);
	return (difference >= 0);
}

#endif /* _LINUX_JFS_H */
