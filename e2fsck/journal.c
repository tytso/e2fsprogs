/*
 * journal.c --- code for handling the "ext3" journal
 *
 * Copyright (C) 2000 Andreas Dilger
 * Copyright (C) 2000 Theodore Ts'o
 *
 * Parts of the code are based on fs/jfs/journal.c by Stephen C. Tweedie
 * Copyright (C) 1999 Red Hat Software
 *
 * This file may be redistributed under the terms of the
 * GNU General Public License version 2 or at your discretion
 * any later version.
 */

#ifdef HAVE_SYS_MOUNT_H
#include <sys/mount.h>
#define MNT_FL (MS_MGC_VAL | MS_RDONLY)
#endif
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

#define E2FSCK_INCLUDE_INLINE_FUNCS
#include "jfs_user.h"
#include "problem.h"
#include "uuid/uuid.h"

#ifdef JFS_DEBUG		/* Enabled by configure --enable-jfs-debug */
static int bh_count = 0;
int journal_enable_debug = 2;
#endif

/* Kernel compatibility functions for handling the journal.  These allow us
 * to use the recovery.c file virtually unchanged from the kernel, so we
 * don't have to do much to keep kernel and user recovery in sync.
 */
int bmap(struct inode *inode, int block)
{
	int retval;
	blk_t phys;

	retval = ext2fs_bmap(inode->i_ctx->fs, inode->i_ino, &inode->i_ext2,
			     NULL, 0, block, &phys);

	if (retval)
		com_err(inode->i_ctx->device_name, retval,
			_("bmap journal inode %ld, block %d\n"),
			inode->i_ino, block);

	return phys;
}

struct buffer_head *getblk(e2fsck_t ctx, blk_t blocknr, int blocksize)
{
	struct buffer_head *bh;

	bh = e2fsck_allocate_memory(ctx, sizeof(*bh), "block buffer");
	if (!bh)
		return NULL;

	jfs_debug(4, "getblk for block %lu (%d bytes)(total %d)\n",
		  (unsigned long) blocknr, blocksize, ++bh_count);

	bh->b_ctx = ctx;
	bh->b_size = blocksize;
	bh->b_blocknr = blocknr;

	return bh;
}

void ll_rw_block(int rw, int nr, struct buffer_head *bhp[])
{
	int retval;
	struct buffer_head *bh;

	for (; nr > 0; --nr) {
		bh = *bhp++;
		if (rw == READ && !bh->b_uptodate) {
			jfs_debug(3, "reading block %lu/%p\n", 
				  (unsigned long) bh->b_blocknr, (void *) bh);
			retval = io_channel_read_blk(bh->b_ctx->journal_io, 
						     bh->b_blocknr,
						     1, bh->b_data);
			if (retval) {
				com_err(bh->b_ctx->device_name, retval,
					"while reading block %ld\n", 
					bh->b_blocknr);
				bh->b_err = retval;
				continue;
			}
			bh->b_uptodate = 1;
		} else if (rw == WRITE && bh->b_dirty) {
			jfs_debug(3, "writing block %lu/%p\n", 
				  (unsigned long) bh->b_blocknr, (void *) bh);
			retval = io_channel_write_blk(bh->b_ctx->journal_io, 
						      bh->b_blocknr,
						      1, bh->b_data);
			if (retval) {
				com_err(bh->b_ctx->device_name, retval,
					"while writing block %ld\n", 
					bh->b_blocknr);
				bh->b_err = retval;
				continue;
			}
			bh->b_dirty = 0;
			bh->b_uptodate = 1;
		} else
			jfs_debug(3, "no-op %s for block %lu\n",
				  rw == READ ? "read" : "write", 
				  (unsigned long) bh->b_blocknr);
	}
}

void mark_buffer_dirty(struct buffer_head *bh, int dummy)
{
	bh->b_dirty = dummy | 1; /* use dummy to avoid unused variable */
}

static void mark_buffer_clean(struct buffer_head * bh)
{
	bh->b_dirty = 0;
}

void brelse(struct buffer_head *bh)
{
	if (bh->b_dirty)
		ll_rw_block(WRITE, 1, &bh);
	jfs_debug(3, "freeing block %lu/%p (total %d)\n",
		  (unsigned long) bh->b_blocknr, (void *) bh, --bh_count);
	ext2fs_free_mem((void **) &bh);
}

int buffer_uptodate(struct buffer_head *bh)
{
	return bh->b_uptodate;
}

void mark_buffer_uptodate(struct buffer_head *bh, int val)
{
	bh->b_uptodate = val;
}

void wait_on_buffer(struct buffer_head *bh)
{
	if (!bh->b_uptodate)
		ll_rw_block(READ, 1, &bh);
}


static void e2fsck_clear_recover(e2fsck_t ctx, int error)
{
	ctx->fs->super->s_feature_incompat &= ~EXT3_FEATURE_INCOMPAT_RECOVER;

	/* if we had an error doing journal recovery, we need a full fsck */
	if (error)
		ctx->fs->super->s_state &= ~EXT2_VALID_FS;
	ext2fs_mark_super_dirty(ctx->fs);
}

static errcode_t e2fsck_journal_init_inode(e2fsck_t ctx,
					   struct ext2_super_block *s,
					   journal_t **journal)
{
	struct inode *inode;
	struct buffer_head *bh;
	blk_t start;
	int retval;

	jfs_debug(1, "Using journal inode %u\n", s->s_journal_inum);
	*journal = e2fsck_allocate_memory(ctx, sizeof(journal_t), "journal");
	if (!*journal) {
		return EXT2_ET_NO_MEMORY;
	}

	inode = e2fsck_allocate_memory(ctx, sizeof(*inode), "journal inode");
	if (!inode) {
		retval = EXT2_ET_NO_MEMORY;
		goto exit_journal;
	}

	inode->i_ctx = ctx;
	inode->i_ino = s->s_journal_inum;
	retval = ext2fs_read_inode(ctx->fs, s->s_journal_inum, &inode->i_ext2);
	if (retval)
		goto exit_inode;

	(*journal)->j_dev = ctx;
	(*journal)->j_inode = inode;
	(*journal)->j_blocksize = ctx->fs->blocksize;
	(*journal)->j_maxlen = inode->i_ext2.i_size / (*journal)->j_blocksize;
	ctx->journal_io = ctx->fs->io;

	if (!inode->i_ext2.i_links_count ||
	    !LINUX_S_ISREG(inode->i_ext2.i_mode) ||
	    (*journal)->j_maxlen < JFS_MIN_JOURNAL_BLOCKS ||
	    (start = bmap(inode, 0)) == 0) {
		retval = EXT2_ET_BAD_INODE_NUM;
		goto exit_inode;
	}

	bh = getblk(ctx, start, (*journal)->j_blocksize);
	if (!bh) {
		retval = EXT2_ET_NO_MEMORY;
		goto exit_inode;
	}
	(*journal)->j_sb_buffer = bh;
	(*journal)->j_superblock = (journal_superblock_t *)bh->b_data;
	
	return 0;

exit_inode:
	ext2fs_free_mem((void **)&inode);
exit_journal:
	ext2fs_free_mem((void **)journal);

	return retval;
}

static errcode_t e2fsck_journal_init_dev(e2fsck_t ctx,
					 struct ext2_super_block *s,
					 journal_t **journal)
{
	struct buffer_head *bh;
	io_manager	io_ptr;
	blk_t		start;
	int		retval;
	int		blocksize = ctx->fs->blocksize;
	struct ext2_super_block jsuper;
	struct problem_context pctx;
	const char	*journal_name;

	clear_problem_context(&pctx);
	journal_name = ctx->journal_name;
	if (!journal_name)
		journal_name = ext2fs_find_block_device(s->s_journal_dev);

	if (!journal_name) {
		fix_problem(ctx, PR_0_CANT_FIND_JOURNAL, &pctx);
		return EXT2_ET_LOAD_EXT_JOURNAL;
	}

	jfs_debug(1, "Using journal file %s\n", journal_name);

#if 1
	io_ptr = unix_io_manager;
#else
	io_ptr = test_io_manager;
	test_io_backing_manager = unix_io_manager;
#endif
	retval = io_ptr->open(journal_name, IO_FLAG_RW, &ctx->journal_io);
	if (!ctx->journal_name)
		free((void *) journal_name);
	if (retval)
		return retval;

	io_channel_set_blksize(ctx->journal_io, blocksize);
	start = (blocksize == 1024) ? 1 : 0;
	bh = getblk(ctx, start, blocksize);
	if (!bh)
		return EXT2_ET_NO_MEMORY;
	ll_rw_block(READ, 1, &bh);
	if (bh->b_err)
		return bh->b_err;
	memcpy(&jsuper, start ? bh->b_data :  bh->b_data + 1024,
	       sizeof(jsuper));
	brelse(bh);
#ifdef EXT2FS_ENABLE_SWAPFS
	if (jsuper.s_magic == ext2fs_swab16(EXT2_SUPER_MAGIC)) 
		ext2fs_swap_super(&jsuper);
#endif
	if (jsuper.s_magic != EXT2_SUPER_MAGIC ||
	    !(jsuper.s_feature_incompat & EXT3_FEATURE_INCOMPAT_JOURNAL_DEV)) {
		fix_problem(ctx, PR_0_EXT_JOURNAL_BAD_SUPER, &pctx);
		return EXT2_ET_LOAD_EXT_JOURNAL;
	}
	/* Make sure the journal UUID is correct */
	if (memcmp(jsuper.s_uuid, ctx->fs->super->s_journal_uuid,
		   sizeof(jsuper.s_uuid))) {
		fix_problem(ctx, PR_0_JOURNAL_BAD_UUID, &pctx);
		return EXT2_ET_LOAD_EXT_JOURNAL;
	}
		
	*journal = e2fsck_allocate_memory(ctx, sizeof(journal_t), "journal");
	if (!*journal) {
		return EXT2_ET_NO_MEMORY;
	}

	(*journal)->j_dev = ctx;
	(*journal)->j_inode = NULL;
	(*journal)->j_blocksize = ctx->fs->blocksize;
	(*journal)->j_maxlen = jsuper.s_blocks_count;

	bh = getblk(ctx, start+1, (*journal)->j_blocksize);
	if (!bh) {
		retval = EXT2_ET_NO_MEMORY;
		goto errout;
	}
	(*journal)->j_sb_buffer = bh;
	(*journal)->j_superblock = (journal_superblock_t *)bh->b_data;
	
	return 0;

errout:
	ext2fs_free_mem((void **)journal);
	return retval;
}

static errcode_t e2fsck_get_journal(e2fsck_t ctx, journal_t **journal)
{
	struct ext2_super_block *sb = ctx->fs->super;

	if (uuid_is_null(sb->s_journal_uuid)) {
		if (!sb->s_journal_inum)
			return EXT2_ET_BAD_INODE_NUM;
		return e2fsck_journal_init_inode(ctx, sb, journal);
	} else {
		return e2fsck_journal_init_dev(ctx, sb, journal);
	}
}

static errcode_t e2fsck_journal_fix_bad_inode(e2fsck_t ctx,
					      struct problem_context *pctx)
{
	struct ext2_super_block *sb = ctx->fs->super;
	int recover = ctx->fs->super->s_feature_incompat &
		EXT3_FEATURE_INCOMPAT_RECOVER;
	int has_journal = ctx->fs->super->s_feature_compat &
		EXT3_FEATURE_COMPAT_HAS_JOURNAL;

	if (has_journal || sb->s_journal_inum) {
		/* The journal inode is bogus, remove and force full fsck */
		pctx->ino = sb->s_journal_inum;
		if (fix_problem(ctx, PR_0_JOURNAL_BAD_INODE, pctx)) {
			if (has_journal && sb->s_journal_inum)
				printf("*** ext3 journal has been deleted - "
				       "filesystem is now ext2 only ***\n\n");
			sb->s_feature_compat &= ~EXT3_FEATURE_COMPAT_HAS_JOURNAL;
			sb->s_journal_inum = 0;
			ctx->flags |= E2F_FLAG_JOURNAL_INODE; /* FIXME: todo */
			e2fsck_clear_recover(ctx, 1);
			return 0;
		}
		return EXT2_ET_BAD_INODE_NUM;
	} else if (recover) {
		if (fix_problem(ctx, PR_0_JOURNAL_RECOVER_SET, pctx)) {
			e2fsck_clear_recover(ctx, 1);
			return 0;
		}
		return EXT2_ET_UNSUPP_FEATURE;
	}
	return 0;
}

static errcode_t e2fsck_journal_load(journal_t *journal)
{
	e2fsck_t ctx = journal->j_dev;
	journal_superblock_t *jsb;
	struct buffer_head *jbh = journal->j_sb_buffer;
	struct problem_context pctx;

	clear_problem_context(&pctx);

	ll_rw_block(READ, 1, &jbh);
	if (jbh->b_err) {
		com_err(ctx->device_name, jbh->b_err,
			_("reading journal superblock\n"));
		return jbh->b_err;
	}

	jsb = journal->j_superblock;
	/* If we don't even have JFS_MAGIC, we probably have a wrong inode */
	if (jsb->s_header.h_magic != htonl(JFS_MAGIC_NUMBER))
		return e2fsck_journal_fix_bad_inode(ctx, &pctx);

	switch (ntohl(jsb->s_header.h_blocktype)) {
	case JFS_SUPERBLOCK_V1:
		journal->j_format_version = 1;
		break;
		
	case JFS_SUPERBLOCK_V2:
		journal->j_format_version = 2;
		if (ntohl(jsb->s_nr_users) > 1) {
			fix_problem(ctx, PR_0_JOURNAL_UNSUPP_MULTIFS, &pctx);
			return EXT2_ET_JOURNAL_UNSUPP_VERSION;
		}
		break;

	/*
	 * These should never appear in a journal super block, so if
	 * they do, the journal is badly corrupted.
	 */
	case JFS_DESCRIPTOR_BLOCK:
	case JFS_COMMIT_BLOCK:
	case JFS_REVOKE_BLOCK:
		return EXT2_ET_CORRUPT_SUPERBLOCK;
		
	/* If we don't understand the superblock major type, but there
	 * is a magic number, then it is likely to be a new format we
	 * just don't understand, so leave it alone. */
	default:
		return EXT2_ET_JOURNAL_UNSUPP_VERSION;
	}

	if (JFS_HAS_INCOMPAT_FEATURE(journal, ~JFS_KNOWN_INCOMPAT_FEATURES))
		return EXT2_ET_UNSUPP_FEATURE;
	
	if (JFS_HAS_RO_COMPAT_FEATURE(journal, ~JFS_KNOWN_ROCOMPAT_FEATURES))
		return EXT2_ET_RO_UNSUPP_FEATURE;

	/* We have now checked whether we know enough about the journal
	 * format to be able to proceed safely, so any other checks that
	 * fail we should attempt to recover from. */
	if (jsb->s_blocksize != htonl(journal->j_blocksize)) {
		com_err(ctx->program_name, EXT2_ET_CORRUPT_SUPERBLOCK,
			_("%s: no valid journal superblock found\n"),
			ctx->device_name);
		return EXT2_ET_CORRUPT_SUPERBLOCK;
	}

	if (ntohl(jsb->s_maxlen) < journal->j_maxlen)
		journal->j_maxlen = ntohl(jsb->s_maxlen);
	else if (ntohl(jsb->s_maxlen) > journal->j_maxlen) {
		com_err(ctx->program_name, EXT2_ET_CORRUPT_SUPERBLOCK,
			_("%s: journal too short\n"),
			ctx->device_name);
		return EXT2_ET_CORRUPT_SUPERBLOCK;
	}

	journal->j_tail_sequence = ntohl(jsb->s_sequence);
	journal->j_transaction_sequence = journal->j_tail_sequence;
	journal->j_tail = ntohl(jsb->s_start);
	journal->j_first = ntohl(jsb->s_first);
	journal->j_last = ntohl(jsb->s_maxlen);

	return 0;
}

static void e2fsck_journal_reset_super(e2fsck_t ctx, journal_superblock_t *jsb,
				       journal_t *journal)
{
	char *p;
	union {
		uuid_t uuid;
		__u32 val[4];
	} u;
	__u32 new_seq = 0;
	int i;

	/* Leave a valid existing V1 superblock signature alone.
	 * Anything unrecognisable we overwrite with a new V2
	 * signature. */
	
	if (jsb->s_header.h_magic != htonl(JFS_MAGIC_NUMBER) ||
	    jsb->s_header.h_blocktype != htonl(JFS_SUPERBLOCK_V1)) {
		jsb->s_header.h_magic = htonl(JFS_MAGIC_NUMBER);
		jsb->s_header.h_blocktype = htonl(JFS_SUPERBLOCK_V2);
	}

	/* Zero out everything else beyond the superblock header */
	
	p = ((char *) jsb) + sizeof(journal_header_t);
	memset (p, 0, ctx->fs->blocksize-sizeof(journal_header_t));

	jsb->s_blocksize = htonl(ctx->fs->blocksize);
	jsb->s_maxlen = htonl(journal->j_maxlen);
	jsb->s_first = htonl(1);

	/* Initialize the journal sequence number so that there is "no"
	 * chance we will find old "valid" transactions in the journal.
	 * This avoids the need to zero the whole journal (slow to do,
	 * and risky when we are just recovering the filesystem).
	 */
	uuid_generate(u.uuid);
	for (i = 0; i < 4; i ++)
		new_seq ^= u.val[i];
	jsb->s_sequence = htonl(new_seq);

	mark_buffer_dirty(journal->j_sb_buffer, 1);
	ll_rw_block(WRITE, 1, &journal->j_sb_buffer);
}

static errcode_t e2fsck_journal_fix_corrupt_super(e2fsck_t ctx,
						  journal_t *journal,
						  struct problem_context *pctx)
{
	struct ext2_super_block *sb = ctx->fs->super;
	int recover = ctx->fs->super->s_feature_incompat &
		EXT3_FEATURE_INCOMPAT_RECOVER;

	pctx->num = journal->j_inode->i_ino;

	if (sb->s_feature_compat & EXT3_FEATURE_COMPAT_HAS_JOURNAL) {
		if (fix_problem(ctx, PR_0_JOURNAL_BAD_SUPER, pctx)) {
			e2fsck_journal_reset_super(ctx, journal->j_superblock,
						   journal);
			journal->j_transaction_sequence = 1;
			e2fsck_clear_recover(ctx, recover);
			return 0;
		}
		return EXT2_ET_CORRUPT_SUPERBLOCK;
	} else if (e2fsck_journal_fix_bad_inode(ctx, pctx))
		return EXT2_ET_CORRUPT_SUPERBLOCK;

	return 0;
}

static void e2fsck_journal_release(e2fsck_t ctx, journal_t *journal,
				   int reset, int drop)
{
	journal_superblock_t *jsb;

	if (drop)
		mark_buffer_clean(journal->j_sb_buffer);
	else if (!(ctx->options & E2F_OPT_READONLY)) {
		jsb = journal->j_superblock;
		jsb->s_sequence = htonl(journal->j_transaction_sequence);
		if (reset)
			jsb->s_start = 0; /* this marks the journal as empty */
		mark_buffer_dirty(journal->j_sb_buffer, 1);
	}
	brelse(journal->j_sb_buffer);

	if (ctx->journal_io) {
		if (ctx->fs && ctx->fs->io != ctx->journal_io)
			io_channel_close(ctx->journal_io);
		ctx->journal_io = 0;
	}
	
	if (journal->j_inode)
		ext2fs_free_mem((void **)&journal->j_inode);
	ext2fs_free_mem((void **)&journal);
}

/*
 * This function makes sure that the superblock fields regarding the
 * journal are consistent.
 */
int e2fsck_check_ext3_journal(e2fsck_t ctx)
{
	struct ext2_super_block *sb = ctx->fs->super;
	journal_t *journal;
	int recover = ctx->fs->super->s_feature_incompat &
		EXT3_FEATURE_INCOMPAT_RECOVER;
	struct problem_context pctx;
	int reset = 0, force_fsck = 0;
	int retval;

	/* If we don't have any journal features, don't do anything more */
	if (!(sb->s_feature_compat & EXT3_FEATURE_COMPAT_HAS_JOURNAL) &&
	    !recover && sb->s_journal_inum == 0 && sb->s_journal_dev == 0 &&
	    uuid_is_null(sb->s_journal_uuid))
 		return 0;

	clear_problem_context(&pctx);
	pctx.num = sb->s_journal_inum;

	retval = e2fsck_get_journal(ctx, &journal);
	if (retval) {
		if (retval == EXT2_ET_BAD_INODE_NUM)
			return e2fsck_journal_fix_bad_inode(ctx, &pctx);
		return retval;
	}

	retval = e2fsck_journal_load(journal);
	if (retval) {
		if ((retval == EXT2_ET_CORRUPT_SUPERBLOCK) ||
		    ((retval == EXT2_ET_UNSUPP_FEATURE) &&
		    (!fix_problem(ctx, PR_0_JOURNAL_UNSUPP_INCOMPAT,
				  &pctx))) ||
		    ((retval == EXT2_ET_RO_UNSUPP_FEATURE) &&
		    (!fix_problem(ctx, PR_0_JOURNAL_UNSUPP_ROCOMPAT,
				  &pctx))) ||
		    ((retval == EXT2_ET_JOURNAL_UNSUPP_VERSION) &&
		    (!fix_problem(ctx, PR_0_JOURNAL_UNSUPP_VERSION, &pctx))))
			retval = e2fsck_journal_fix_corrupt_super(ctx, journal,
								  &pctx);
		e2fsck_journal_release(ctx, journal, 0, 1);
		return retval;
	}

	/*
	 * We want to make the flags consistent here.  We will not leave with
	 * needs_recovery set but has_journal clear.  We can't get in a loop
	 * with -y, -n, or -p, only if a user isn't making up their mind.
	 */
no_has_journal:
	if (!(sb->s_feature_compat & EXT3_FEATURE_COMPAT_HAS_JOURNAL)) {
		recover = sb->s_feature_incompat & EXT3_FEATURE_INCOMPAT_RECOVER;
		pctx.str = "inode";
		if (fix_problem(ctx, PR_0_JOURNAL_HAS_JOURNAL, &pctx)) {
			if (recover &&
			    !fix_problem(ctx, PR_0_JOURNAL_RECOVER_SET, &pctx))
				goto no_has_journal;
			/*
			 * Need a full fsck if we are releasing a
			 * journal stored on a reserved inode.
			 */
			force_fsck = recover ||
				(sb->s_journal_inum < EXT2_FIRST_INODE(sb));
			/* Clear all of the journal fields */
			sb->s_journal_inum = 0;
			sb->s_journal_dev = 0;
			memset(sb->s_journal_uuid, 0,
			       sizeof(sb->s_journal_uuid));
			e2fsck_clear_recover(ctx, force_fsck);
		} else if (!(ctx->options & E2F_OPT_READONLY)) {
			sb->s_feature_compat |= EXT3_FEATURE_COMPAT_HAS_JOURNAL;
			ext2fs_mark_super_dirty(ctx->fs);
		}
	}

	if (sb->s_feature_compat & EXT3_FEATURE_COMPAT_HAS_JOURNAL &&
	    !(sb->s_feature_incompat & EXT3_FEATURE_INCOMPAT_RECOVER) &&
	    journal->j_superblock->s_start != 0) {
		if (fix_problem(ctx, PR_0_JOURNAL_RESET_JOURNAL, &pctx)) {
			reset = 1;
			sb->s_state &= ~EXT2_VALID_FS;
			ext2fs_mark_super_dirty(ctx->fs);
		}
		/*
		 * If the user answers no to the above question, we
		 * ignore the fact that journal apparently has data;
		 * accidentally replaying over valid data would be far
		 * worse than skipping a questionable recovery.
		 * 
		 * XXX should we abort with a fatal error here?  What
		 * will the ext3 kernel code do if a filesystem with
		 * !NEEDS_RECOVERY but with a non-zero
		 * journal->j_superblock->s_start is mounted?
		 */
	}

	e2fsck_journal_release(ctx, journal, reset, 0);
	return retval;
}

static errcode_t recover_ext3_journal(e2fsck_t ctx)
{
	journal_t *journal;
	int retval;

	retval = e2fsck_get_journal(ctx, &journal);
	if (retval)
		return retval;

	retval = e2fsck_journal_load(journal);
	if (retval)
		goto errout;

	retval = journal_init_revoke(journal, 1024);
	if (retval)
		goto errout;
	
	retval = -journal_recover(journal);
	if (retval)
		goto errout;
	
	if (journal->j_superblock->s_errno) {
		ctx->fs->super->s_state |= EXT2_ERROR_FS;
		ext2fs_mark_super_dirty(ctx->fs);
		journal->j_superblock->s_errno = 0;
		mark_buffer_dirty(journal->j_sb_buffer, 1);
	}
		
errout:
	e2fsck_journal_release(ctx, journal, 1, 0);
	return retval;
}

int e2fsck_run_ext3_journal(e2fsck_t ctx)
{
	io_manager io_ptr = ctx->fs->io->manager;
	int blocksize = ctx->fs->blocksize;
	errcode_t	retval, recover_retval;

	printf(_("%s: recovering journal\n"), ctx->device_name);
	if (ctx->options & E2F_OPT_READONLY) {
		printf(_("%s: won't do journal recovery while read-only\n"),
		       ctx->device_name);
		return EXT2_ET_FILE_RO;
	}

	if (ctx->fs->flags & EXT2_FLAG_DIRTY)
		ext2fs_flush(ctx->fs);	/* Force out any modifications */

	recover_retval = recover_ext3_journal(ctx);
	
	/*
	 * Reload the filesystem context to get up-to-date data from disk
	 * because journal recovery will change the filesystem under us.
	 */
	ext2fs_close(ctx->fs);
	retval = ext2fs_open(ctx->filesystem_name, EXT2_FLAG_RW,
			     ctx->superblock, blocksize, io_ptr,
			     &ctx->fs);

	if (retval) {
		com_err(ctx->program_name, retval,
			_("while trying to re-open %s"),
			ctx->device_name);
		fatal_error(ctx, 0);
	}
	ctx->fs->priv_data = ctx;

	/* Set the superblock flags */
	e2fsck_clear_recover(ctx, recover_retval);
	return recover_retval;
}
