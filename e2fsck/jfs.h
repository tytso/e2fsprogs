/*
 * linux/include/linux/jfs.h
 * 
 * Written by Stephen C. Tweedie <sct@redhat.com>, 1998
 *
 * Copyright 1998 Red Hat corp --- All Rights Reserved
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

#ifndef __KERNEL__
#include "jfs_compat.h"
#endif

/*
 * Debug code
 */

/* #define JFS_DEBUG */

#ifdef JFS_DEBUG
extern int jfs_enable_debug;

#define jfs_debug(n, f, a...)						\
	do {								\
		if ((n) <= jfs_enable_debug) {				\
			printk (KERN_DEBUG "JFS DEBUG: (%s, %d): %s: ",	\
				__FILE__, __LINE__, __FUNCTION__);	\
		  	printk (f, ## a);			\
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
#define JFS_SUPERBLOCK		3

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

/* Definitions for the journal tag flags word: */
#define JFS_FLAG_ESCAPE		1	/* on-disk block is escaped */
#define JFS_FLAG_SAME_UUID	2	/* block has same uuid as previous */
#define JFS_FLAG_DELETED	4	/* block deleted by this transaction */
#define JFS_FLAG_LAST_TAG	8	/* last tag in this descriptor block */


/*
 * The journal superblock
 */
typedef struct journal_superblock_s
{
	journal_header_t s_header;

	/* Static information describing the journal */
	__u32		s_blocksize;	/* journal device blocksize */
	__u32		s_maxlen;	/* total blocks in journal file */
	__u32		s_first;	/* first block of log information */
	
	/* Dynamic information describing the current state of the log */
	__u32		s_sequence;	/* first commit ID expected in log */
	__u32		s_start;	/* blocknr of start of log */
	
} journal_superblock_t;

#ifdef __KERNEL__

#include <asm/semaphone.h>
#include <linux/fs.h>


#define J_ASSERT(assert)						\
	do { if (!(assert)) {						\
		printk (KERN_CRIT					\
			"Assertion failure in %s() at %s line %d: "	\
			"\"%s\"\n",					\
			__FUNCTION__, __FILE__, __LINE__, # assert);	\
		* ((char *) 0) = 0;					\
	} } while (0)



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
	
	/* Wait queue to wait for updates to complete */
	struct wait_queue *	t_wait;

	/* Forward and backward links for the circular list of all
	 * transactions awaiting checkpoint */
	transaction_t		*t_cpnext, *t_cpprev;

	/* When will the transaction expire (become due for commit), in
	 * jiffies ? */
	unsigned long		t_expires;
};
#endif /* __KERNEL__ */


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
	
	/* The superblock buffer */
	struct buffer_head *	j_sb_buffer;
	journal_superblock_t *	j_superblock;
	
#ifdef __KERNEL__
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
           committing */
	struct wait_queue *	j_wait_transaction_locked;
	
	/* Wait queue for waiting for checkpointing to complete */
	struct wait_queue *	j_wait_logspace;
	
	/* Wait queue for waiting for commit to complete */
	struct wait_queue *	j_wait_done_commit;
	
	/* Wait queue to trigger checkpointing */
	struct wait_queue *	j_wait_checkpoint;
	
	/* Wait queue to trigger commit */
	struct wait_queue *	j_wait_commit;
	
	/* Semaphore for locking against concurrent checkpoints */
	struct semaphore 	j_checkpoint_sem;
		
	/* Journal running state: */
	/* The lock flag is *NEVER* touched from interrupts. */
	unsigned int		j_locked : 1;

	/* Pointer to the current commit thread for this journal */
	struct task_struct *	j_task;

	/* The timer used to wakeup the commit thread: */
	struct timer_list *	j_commit_timer;
	int			j_commit_timer_active;
#endif

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

	/* Maximum number of metadata buffers to allow in a single
	 * compound commit transaction */
	int			j_max_transaction_buffers;

	/* What is the maximum transaction lifetime before we begin a
	 * commit? */
	unsigned long		j_commit_interval;

};

#ifdef __KERNEL__

/* 
 * Journal flag definitions 
 */
#define JFS_UNMOUNT	1	/* Journal thread is being destroyed */
#define JFS_SYNC	2	/* Perform synchronous transaction commits */

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


/*
 * Transaction locking
 *
 * We need to lock the journal during transaction state changes so that
 * nobody ever tries to take a handle on the running transaction while
 * we are in the middle of moving it to the commit phase.
 *
 * Note that the locking is completely interrupt unsafe.  We never touch
 * journal structures from interrupts.
 */

static inline void __wait_on_journal (journal_t * journal)
{
	while (journal->j_locked)
		sleep_on (&journal->j_wait_lock);
}


/* Journal locking.  In 2.2, we assume that the kernel lock is already
 * held. */
static inline void lock_journal (journal_t * journal)
{
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

/* Debugging code only: */

#define jfs_ENOSYS() \
do {								      \
	printk (KERN_ERR "JFS unimplemented function " __FUNCTION__); \
	current->state = TASK_UNINTERRUPTIBLE;			      \
	schedule();						      \
} while (1)

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

extern journal_t * journal_init_dev   (kdev_t, int start, int len, int bsize);
extern journal_t * journal_init_inode (struct inode *);
extern int	   journal_create     (journal_t *);
extern int	   journal_load       (journal_t *);
extern void	   journal_release    (journal_t *);
extern void	   journal_update_superblock (journal_t *, int);
#endif /* __KERNEL__   */
extern int	   journal_recover    (journal_t *);

#endif /* _LINUX_JFS_H */
