/*
 * pass1.c -- pass #1 of e2fsck: sequential scan of the inode table
 * 
 * Copyright (C) 1993, 1994 Theodore Ts'o.  This file may be
 * redistributed under the terms of the GNU Public License.
 * 
 * Pass 1 of e2fsck iterates over all the inodes in the filesystems,
 * and applies the following tests to each inode:
 *
 * 	- The mode field of the inode must be legal.
 * 	- The size and block count fields of the inode are correct.
 * 	- A data block must not be used by another inode
 *
 * Pass 1 also gathers the collects the following information:
 *
 * 	- A bitmap of which inodes are in use.		(inode_used_map)
 * 	- A bitmap of which inodes are directories.	(inode_dir_map)
 * 	- A bitmap of which inodes have bad fields.	(inode_bad_map)
 * 	- A bitmap of which blocks are in use.		(block_found_map)
 * 	- A bitmap of which blocks are in use by two inodes	(block_dup_map)
 * 	- The data blocks of the directory inodes.	(dir_map)
 *
 * Pass 1 is designed to stash away enough information so that the
 * other passes should not need to read in the inode information
 * during the normal course of a filesystem check.  (Althogh if an
 * inconsistency is detected, other passes may need to read in an
 * inode to fix it.)
 *
 * Note that pass 1B will be invoked if there are any duplicate blocks
 * found.
 */

#include <time.h>
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif

#include <et/com_err.h>
#include "e2fsck.h"

#ifdef NO_INLINE_FUNCS
#define _INLINE_
#else
#define _INLINE_ inline
#endif

/* Files counts */
int fs_directory_count = 0;
int fs_regular_count = 0;
int fs_blockdev_count = 0;
int fs_chardev_count = 0;
int fs_links_count = 0;
int fs_symlinks_count = 0;
int fs_fast_symlinks_count = 0;
int fs_fifo_count = 0;
int fs_total_count = 0;
int fs_badblocks_count = 0;
int fs_sockets_count = 0;
int fs_ind_count = 0;
int fs_dind_count = 0;
int fs_tind_count = 0;
int fs_fragmented = 0;

ext2fs_inode_bitmap inode_used_map = 0;	/* Inodes which are in use */
ext2fs_inode_bitmap inode_bad_map = 0;	/* Inodes which are bad in some way */
ext2fs_inode_bitmap inode_dir_map = 0;	/* Inodes which are directories */

ext2fs_block_bitmap block_found_map = 0;
ext2fs_block_bitmap block_dup_map = 0;
ext2fs_block_bitmap block_illegal_map = 0;

static int fix_link_count = -1;

unsigned short * inode_link_info = NULL;

static int process_block(ext2_filsys fs, blk_t	*blocknr,
			 int	blockcnt, void	*private);
static int process_bad_block(ext2_filsys fs, blk_t *block_nr,
			     int blockcnt, void *private);
static void check_blocks(ext2_filsys fs, ino_t ino, struct ext2_inode *inode,
			 char *block_buf);
static void mark_table_blocks(ext2_filsys fs);
static errcode_t pass1_check_directory(ext2_filsys fs, ino_t ino);
static errcode_t pass1_get_blocks(ext2_filsys fs, ino_t ino, blk_t *blocks);
static void alloc_bad_map(ext2_filsys fs);
static void handle_fs_bad_blocks(ext2_filsys fs);
static void process_inodes(ext2_filsys fs, char *block_buf);
static int process_inode_cmp(const void *a, const void *b);
static int dir_block_cmp(const void *a, const void *b);
static errcode_t scan_callback(ext2_filsys fs, ext2_inode_scan scan,
				  dgrp_t group, void * private);
static char *describe_illegal_block(ext2_filsys fs, blk_t block);

struct process_block_struct {
	ino_t	ino;
	int	is_dir:1, clear:1, suppress:1, fragmented:1;
	int	num_blocks;
	int	last_block;
	int	num_illegal_blocks;
	int	fix;
	blk_t	previous_block;
	struct ext2_inode *inode;
};

struct process_inode_block {
	ino_t	ino;
	struct ext2_inode inode;
};

/*
 * For pass1_check_directory and pass1_get_blocks
 */
ino_t stashed_ino;
struct ext2_inode *stashed_inode;

/*
 * For the inodes to process list.
 */
static struct process_inode_block *inodes_to_process;
static int process_inode_count;
int process_inode_size = 256;

/*
 * For the directory blocks list.
 */
struct dir_block_struct *dir_blocks = 0;
int	dir_block_count = 0;
int	dir_block_size = 0;

/*
 * Free all memory allocated by pass1 in preparation for restarting
 * things.
 */
static void unwind_pass1(ext2_filsys fs)
{
	ext2fs_free_inode_bitmap(inode_used_map);	inode_used_map = 0;
	ext2fs_free_inode_bitmap(inode_dir_map);	inode_dir_map = 0;
	ext2fs_free_block_bitmap(block_found_map);	block_found_map = 0;
	free(inode_link_info);	inode_link_info = 0;
	free(inodes_to_process);inodes_to_process = 0;
	free(dir_blocks);	dir_blocks = 0;
	dir_block_size = 0;
	if (block_dup_map) {
		ext2fs_free_block_bitmap(block_dup_map); block_dup_map = 0;
	}

	/* Clear statistic counters */
	fs_directory_count = 0;
	fs_regular_count = 0;
	fs_blockdev_count = 0;
	fs_chardev_count = 0;
	fs_links_count = 0;
	fs_symlinks_count = 0;
	fs_fast_symlinks_count = 0;
	fs_fifo_count = 0;
	fs_total_count = 0;
	fs_badblocks_count = 0;
	fs_sockets_count = 0;
	fs_ind_count = 0;
	fs_dind_count = 0;
	fs_tind_count = 0;
	fs_fragmented = 0;
}

void pass1(ext2_filsys fs)
{
	ino_t	ino;
	struct ext2_inode inode;
	ext2_inode_scan	scan;
	char		*block_buf;
	errcode_t	retval;
	struct resource_track	rtrack;
	
	init_resource_track(&rtrack);
	
	if (!preen)
		printf("Pass 1: Checking inodes, blocks, and sizes\n");

#ifdef MTRACE
	mtrace_print("Pass 1");
#endif

	/*
	 * Allocate bitmaps structures
	 */
	retval = ext2fs_allocate_inode_bitmap(fs, "in-use inode map",
					      &inode_used_map);
	if (retval) {
		com_err("ext2fs_allocate_inode_bitmap", retval,
			"while allocating inode_used_map");
		fatal_error(0);
	}
	retval = ext2fs_allocate_inode_bitmap(fs, "directory inode map",
					      &inode_dir_map);
	if (retval) {
		com_err("ext2fs_allocate_inode_bitmap", retval,
			"while allocating inode_dir_map");
		fatal_error(0);
	}
	retval = ext2fs_allocate_block_bitmap(fs, "in-use block map",
					      &block_found_map);
	if (retval) {
		com_err("ext2fs_allocate_block_bitmap", retval,
			"while allocating block_found_map");
		fatal_error(0);
	}
	retval = ext2fs_allocate_block_bitmap(fs, "illegal block map",
					      &block_illegal_map);
	if (retval) {
		com_err("ext2fs_allocate_block_bitmap", retval,
			"while allocating block_illegal_map");
		fatal_error(0);
	}
	inode_link_info = allocate_memory((fs->super->s_inodes_count + 1) *
					  sizeof(unsigned short),
					  "inode link count array");
	inodes_to_process = allocate_memory(process_inode_size *
					    sizeof(struct process_inode_block),
					    "array of inodes to process");
	process_inode_count = 0;

	dir_block_size = get_num_dirs(fs) * 4;
	dir_block_count = 0;
	dir_blocks = allocate_memory(sizeof(struct dir_block_struct) *
				     dir_block_size,
				     "directory block information");

	mark_table_blocks(fs);
	block_buf = allocate_memory(fs->blocksize * 3, "block interate buffer");
	fs->get_blocks = pass1_get_blocks;
	fs->check_directory = pass1_check_directory;
	ehandler_operation("doing inode scan");
	retval = ext2fs_open_inode_scan(fs, inode_buffer_blocks, &scan);
	if (retval) {
		com_err(program_name, retval, "while opening inode scan");
		fatal_error(0);
	}
	retval = ext2fs_get_next_inode(scan, &ino, &inode);
	if (retval) {
		com_err(program_name, retval, "while starting inode scan");
		fatal_error(0);
	}
	stashed_inode = &inode;
	ext2fs_set_inode_callback(scan, scan_callback, block_buf);
	while (ino) {
		stashed_ino = ino;
		inode_link_info[ino] = inode.i_links_count;
		if (ino == EXT2_BAD_INO) {
			struct process_block_struct pb;
			
			pb.ino = EXT2_BAD_INO;
			pb.num_blocks = pb.last_block = 0;
			pb.num_illegal_blocks = 0;
			pb.suppress = pb.clear = pb.is_dir = 0;
			pb.fragmented = 0;
			pb.fix = -1;
			pb.inode = &inode;
			retval = ext2fs_block_iterate(fs, ino, 0, block_buf,
						      process_bad_block, &pb);
			if (retval)
				com_err(program_name, retval, "while calling e2fsc_block_interate in pass 1");

			ext2fs_mark_inode_bitmap(inode_used_map, ino);
			goto next;
		}
		if (ino == EXT2_ROOT_INO) {
			/*
			 * Make sure the root inode is a directory; if
			 * not, offer to clear it.  It will be
			 * regnerated in pass #3.
			 */
			if (!LINUX_S_ISDIR(inode.i_mode)) {
				printf("Root inode is not a directory.  ");
				preenhalt(fs);
				if (ask("Clear", 1)) {
					inode.i_dtime = time(0);
					inode.i_links_count = 0;
					inode_link_info[ino] = 0;
					e2fsck_write_inode(fs, ino, &inode,
							   "pass1");
				} else 
					ext2fs_unmark_valid(fs);
			}
			/*
			 * If dtime is set, offer to clear it.  mke2fs
			 * version 0.2b created filesystems with the
			 * dtime field set for the root and lost+found
			 * directories.  We won't worry about
			 * /lost+found, since that can be regenerated
			 * easily.  But we will fix the root directory
			 * as a special case.
			 */
			if (inode.i_dtime && inode.i_links_count) {
				if (ask("Root inode has dtime set "
					"(probably due to old mke2fs).  Fix",
					1)) {
					inode.i_dtime = 0;
					e2fsck_write_inode(fs, ino, &inode,
							   "pass1");
					printf("Note: /lost+found will "
					       "probably be deleted as well, "
					       "due to the mke2fs bug.\n"
					       "Be sure to run mklost+found "
					       "to recreate it after e2fsck "
					       "finishes.\n\n");
				} else
					ext2fs_unmark_valid(fs);
			}
		}
		if ((ino != EXT2_ROOT_INO) && (ino < EXT2_FIRST_INO)) {
			ext2fs_mark_inode_bitmap(inode_used_map, ino);
			if (inode.i_mode != 0) {
				printf("Reserved inode %lu has bad mode.  ", ino);
				if (ask("Clear", 1)) {
					inode.i_mode = 0;
					e2fsck_write_inode(fs, ino, &inode,
							   "pass1");
				} else 
					ext2fs_unmark_valid(fs);
			}
			check_blocks(fs, ino, &inode, block_buf);
			goto next;
		}
		/*
		 * This code assumes that deleted inodes have
		 * i_links_count set to 0.  
		 */
		if (!inode.i_links_count) {
			if (!inode.i_dtime && inode.i_mode) {
				printf("Deleted inode %lu has zero dtime.\n",
				       ino);
				if (ask("Set dtime", 1)) {
					inode.i_dtime = time(0);
					e2fsck_write_inode(fs, ino, &inode,
							   "pass1");
				} else
					ext2fs_unmark_valid(fs);
			}
			goto next;
		}
		/*
		 * 0.3c ext2fs code didn't clear i_links_count for
		 * deleted files.  Oops.
		 * 
		 * In the future, when the new ext2fs behavior is the
		 * norm, we may want to handle the case of a non-zero
		 * i_links_count and non-zero dtime by clearing dtime
		 * and assuming the inode is in use, instead of
		 * assuming the inode is not in use.
		 */
		if (inode.i_dtime) {
			if (fix_link_count == -1) {
				printf("\nDeleted inode detected with non-zero link count.\n");
				printf("This is probably due to old ext2fs kernel code.  \n");
				fix_link_count = ask("Fix inode(s)", 1);
			}
			printf("Inode %lu is deleted w/ non-zero link_count.  %s\n",
			       ino, clear_msg[fix_link_count]);
			if (fix_link_count) {
				inode.i_links_count = 0;
				inode_link_info[ino] = 0;
				e2fsck_write_inode(fs, ino, &inode, "pass1");
			} else
				ext2fs_unmark_valid(fs);
			goto next;
		}
		
		ext2fs_mark_inode_bitmap(inode_used_map, ino);
		if (inode.i_faddr
#if HAVE_EXT2_FRAGS
		    || inode.i_frag || inode.i_fsize
#endif
		    || inode.i_file_acl || inode.i_dir_acl) {
			if (!inode_bad_map)
				alloc_bad_map(fs);
			ext2fs_mark_inode_bitmap(inode_bad_map, ino);
		}
		
		if (LINUX_S_ISDIR(inode.i_mode)) {
			ext2fs_mark_inode_bitmap(inode_dir_map, ino);
			add_dir_info(fs, ino, 0, &inode);
			fs_directory_count++;
		} else if (LINUX_S_ISREG (inode.i_mode))
			fs_regular_count++;
		else if (LINUX_S_ISCHR (inode.i_mode))
			fs_chardev_count++;
		else if (LINUX_S_ISBLK (inode.i_mode))
			fs_blockdev_count++;
		else if (LINUX_S_ISLNK (inode.i_mode)) {
			fs_symlinks_count++;
			if (!inode.i_blocks)
				fs_fast_symlinks_count++;
		}
		else if (LINUX_S_ISFIFO (inode.i_mode))
			fs_fifo_count++;
		else if (LINUX_S_ISSOCK (inode.i_mode))
		        fs_sockets_count++;
		else {
			if (!inode_bad_map)
				alloc_bad_map(fs);
			ext2fs_mark_inode_bitmap(inode_bad_map, ino);
		}
		if (inode.i_block[EXT2_IND_BLOCK])
			fs_ind_count++;
		if (inode.i_block[EXT2_DIND_BLOCK])
			fs_dind_count++;
		if (inode.i_block[EXT2_TIND_BLOCK])
			fs_tind_count++;
		if (inode.i_block[EXT2_IND_BLOCK] ||
		    inode.i_block[EXT2_DIND_BLOCK] ||
		    inode.i_block[EXT2_TIND_BLOCK]) {
			inodes_to_process[process_inode_count].ino = ino;
			inodes_to_process[process_inode_count].inode = inode;
			process_inode_count++;
		} else
			check_blocks(fs, ino, &inode, block_buf);

		if (process_inode_count >= process_inode_size)
			process_inodes(fs, block_buf);
	next:
		retval = ext2fs_get_next_inode(scan, &ino, &inode);
		if (retval) {
			com_err(program_name, retval,
				"while doing inode scan");
			fatal_error(0);
		}
	}
	process_inodes(fs, block_buf);
	ext2fs_close_inode_scan(scan);
	ehandler_operation(0);

	qsort(dir_blocks, dir_block_count, sizeof(struct dir_block_struct),
	      dir_block_cmp);

	if (invalid_bitmaps)
		handle_fs_bad_blocks(fs);

	if (restart_e2fsck) {
		unwind_pass1(fs);
		goto endit;
	}

	if (block_dup_map) {
		if (preen) {
			printf("Duplicate or bad blocks in use!\n");
			preenhalt(fs);
		}
		pass1_dupblocks(fs, block_buf);
	}
	fs->get_blocks = 0;
	fs->check_directory = 0;
	free(inodes_to_process);
endit:
	free(block_buf);
	ext2fs_free_block_bitmap(block_illegal_map);
	block_illegal_map = 0;
	
	if (tflag > 1) {
		printf("Pass 1: ");
		print_resource_track(&rtrack);
	}
}

/*
 * When the inode_scan routines call this callback at the end of the
 * glock group, call process_inodes.
 */
static errcode_t scan_callback(ext2_filsys fs, ext2_inode_scan scan,
			       dgrp_t group, void * private)
{
	process_inodes(fs, (char *) private);
	return 0;
}

/*
 * Process the inodes in the "inodes to process" list.
 */
static void process_inodes(ext2_filsys fs, char *block_buf)
{
	int			i;
	struct ext2_inode	*old_stashed_inode;
	ino_t			ino;
	const char		*old_operation;
	char			buf[80];

#if 0
	printf("begin process_inodes: ");
#endif
	old_operation = ehandler_operation(0);
	old_stashed_inode = stashed_inode;
	qsort(inodes_to_process, process_inode_count,
		      sizeof(struct process_inode_block), process_inode_cmp);
	for (i=0; i < process_inode_count; i++) {
		stashed_inode = &inodes_to_process[i].inode;
		ino = inodes_to_process[i].ino;
		stashed_ino = ino;
#if 0
		printf("%u ", ino);
#endif
		sprintf(buf, "reading indirect blocks of inode %lu", ino);
		ehandler_operation(buf);
		check_blocks(fs, ino, stashed_inode, block_buf);
		
	}
	stashed_inode = old_stashed_inode;
	process_inode_count = 0;
#if 0
	printf("end process inodes\n");
#endif
	ehandler_operation(old_operation);
}

static int process_inode_cmp(const void *a, const void *b)
{
	const struct process_inode_block *ib_a =
		(const struct process_inode_block *) a;
	const struct process_inode_block *ib_b =
		(const struct process_inode_block *) b;

	return (ib_a->inode.i_block[EXT2_IND_BLOCK] -
		ib_b->inode.i_block[EXT2_IND_BLOCK]);
}

static int dir_block_cmp(const void *a, const void *b)
{
	const struct dir_block_struct *db_a =
		(const struct dir_block_struct *) a;
	const struct dir_block_struct *db_b =
		(const struct dir_block_struct *) b;

	if (db_a->blk != db_b->blk)
		return (db_a->blk - db_b->blk);
	
	if (db_a->ino != db_b->ino)
		return (db_a->ino - db_b->ino);

	return (db_a->blockcnt - db_b->blockcnt);
}

/*
 * This procedure will allocate the inode bad map table
 */
static void alloc_bad_map(ext2_filsys fs)
{
	errcode_t	retval;
	
	retval = ext2fs_allocate_inode_bitmap(fs, "bad inode map",
					      &inode_bad_map);
	if (retval) {
		com_err("ext2fs_allocate_inode_bitmap", retval,
			"while allocating inode_bad_map");
		fatal_error(0);
	}
}

/*
 * Marks a block as in use, setting the dup_map if it's been set
 * already.  Called by process_block and process_bad_block.
 *
 * WARNING: Assumes checks have already been done to make sure block
 * is valid.  This is true in both process_block and process_bad_block.
 */
static _INLINE_ void mark_block_used(ext2_filsys fs, blk_t block)
{
	errcode_t	retval;
	
	if (ext2fs_fast_test_block_bitmap(block_found_map, block)) {
		if (!block_dup_map) {
			retval = ext2fs_allocate_block_bitmap(fs,
			      "multiply claimed block map", &block_dup_map);
			if (retval) {
				com_err("ext2fs_allocate_block_bitmap", retval,
					"while allocating block_dup_map");
				fatal_error(0);
			}
		}
		ext2fs_fast_mark_block_bitmap(block_dup_map, block);
	} else {
		ext2fs_fast_mark_block_bitmap(block_found_map, block);
	}
}

/*
 * This subroutine is called on each inode to account for all of the
 * blocks used by that inode.
 */
static void check_blocks(ext2_filsys fs, ino_t ino, struct ext2_inode *inode,
			 char *block_buf)
{
	struct process_block_struct pb;
	errcode_t	retval;
	
	if (!inode_has_valid_blocks(inode))
		return;
	
	pb.ino = ino;
	pb.num_blocks = pb.last_block = 0;
	pb.num_illegal_blocks = 0;
	pb.suppress = pb.clear = 0;
	pb.fragmented = 0;
	pb.previous_block = 0;
	pb.is_dir = LINUX_S_ISDIR(inode->i_mode);
	pb.fix = -1;
	pb.inode = inode;
	retval = ext2fs_block_iterate(fs, ino,
				      pb.is_dir ? BLOCK_FLAG_HOLE : 0,
				      block_buf, process_block, &pb);
	if (retval)
		com_err(program_name, retval,
			"while calling ext2fs_block_iterate in check_blocks");

	if (pb.fragmented && pb.num_blocks < fs->super->s_blocks_per_group)
		fs_fragmented++;

	if (pb.clear) {
		e2fsck_read_inode(fs, ino, inode, "check_blocks");
		if (retval) {
			com_err("check_blocks", retval,
				"while reading to be cleared inode %d", ino);
			fatal_error(0);
		}
		inode->i_links_count = 0;
		inode_link_info[ino] = 0;
		inode->i_dtime = time(0);
		e2fsck_write_inode(fs, ino, inode, "check_blocks");
		ext2fs_unmark_inode_bitmap(inode_dir_map, ino);
		ext2fs_unmark_inode_bitmap(inode_used_map, ino);
		/*
		 * The inode was probably partially accounted for
		 * before processing was aborted, so we need to
		 * restart the pass 1 scan.
		 */
		restart_e2fsck++;
		return;
	}

	if (pb.fix > 0)
		e2fsck_read_inode(fs, ino, inode, "check_blocks");

	pb.num_blocks *= (fs->blocksize / 512);
#if 0
	printf("inode %u, i_size = %lu, last_block = %lu, i_blocks=%lu, num_blocks = %lu\n",
	       ino, inode->i_size, pb.last_block, inode->i_blocks,
	       pb.num_blocks);
#endif
	if (!pb.num_blocks && pb.is_dir) {
		printf("Inode %lu is a zero length directory.  ", ino);
		if (ask("Clear", 1)) {
			inode->i_links_count = 0;
			inode_link_info[ino] = 0;
			inode->i_dtime = time(0);
			e2fsck_write_inode(fs, ino, inode, "check_blocks");
			ext2fs_unmark_inode_bitmap(inode_dir_map, ino);
			ext2fs_unmark_inode_bitmap(inode_used_map, ino);
			fs_directory_count--;
			pb.is_dir = 0;
		} else
			ext2fs_unmark_valid(fs);
	}
	if ((pb.is_dir && (inode->i_size != (pb.last_block + 1) * fs->blocksize)) ||
	    (inode->i_size < pb.last_block * fs->blocksize)) {
		printf ("%s %lu, incorrect size, %u (counted = %u). ",
			pb.is_dir ? "Directory" : "Inode", ino, inode->i_size,
			(pb.last_block+1) * fs->blocksize);
		if (ask ("Set size to counted", 1)) {
			inode->i_size = (pb.last_block+1) * fs->blocksize;
			e2fsck_write_inode(fs, ino, inode, "check_blocks");
		} else
			ext2fs_unmark_valid(fs);
	}
	if (pb.num_blocks != inode->i_blocks) {
		printf ("Inode %lu, i_blocks wrong %u (counted=%u).  ",
			ino, inode->i_blocks, pb.num_blocks);
		if (ask ("Set i_blocks to counted", 1)) {
			inode->i_blocks = pb.num_blocks;
			e2fsck_write_inode(fs, ino, inode, "check_blocks");
		} else
				ext2fs_unmark_valid(fs);
	}
}

/*
 * Helper function called by process block when an illegal block is
 * found.  It returns a description about why the block is illegal
 */
static char *describe_illegal_block(ext2_filsys fs, blk_t block)
{
	blk_t	super;
	int	i;
	static char	problem[80];

	super = fs->super->s_first_data_block;
	strcpy(problem, "PROGRAMMING ERROR: Unknown reason for illegal block");
	if (block < super) {
		sprintf(problem, "< FIRSTBLOCK (%u)", super);
		return(problem);
	} else if (block >= fs->super->s_blocks_count) {
		sprintf(problem, "> BLOCKS (%u)", fs->super->s_blocks_count);
		return(problem);
	}
	for (i = 0; i < fs->group_desc_count; i++) {
		if (block == super) {
			sprintf(problem, "is the superblock in group %d", i);
			break;
		}
		if (block > super &&
		    block <= (super + fs->desc_blocks)) {
			sprintf(problem, "is in the group descriptors "
				"of group %d", i);
			break;
		}
		if (block == fs->group_desc[i].bg_block_bitmap) {
			sprintf(problem, "is the block bitmap of group %d", i);
			break;
		}
		if (block == fs->group_desc[i].bg_inode_bitmap) {
			sprintf(problem, "is the inode bitmap of group %d", i);
			break;
		}
		if (block >= fs->group_desc[i].bg_inode_table &&
		    (block < fs->group_desc[i].bg_inode_table
		     + fs->inode_blocks_per_group)) {
			sprintf(problem, "is in the inode table of group %d",
				i);
			break;
		}
		super += fs->super->s_blocks_per_group;
	}
	return(problem);
}

/*
 * This is a helper function for check_blocks().
 */
int process_block(ext2_filsys fs,
		  blk_t	*block_nr,
		  int blockcnt,
		  void *private)
{
	struct process_block_struct *p;
	char	*problem;
	blk_t	blk = *block_nr;
	int	ret_code = 0;

	p = (struct process_block_struct *) private;

	if (blk == 0) {
		if (p->is_dir == 0) {
			printf("process_block() called with blk == 0, "
			       "inode %lu???", p->ino);
			return 0;
		}
		if (blockcnt < 0)
			return 0;
		if (blockcnt * fs->blocksize < p->inode->i_size) {
			printf("Hole found in directory inode %lu!  "
			       "(blkcnt=%d)\n", p->ino, blockcnt);
			goto mark_dir;
		}
		return 0;
	}

#if 0
	printf("Process_block, inode %lu, block %u, #%d\n", p->ino, blk,
	       blockcnt);
#endif
	
	/*
	 * Simplistic fragmentation check.  We merely require that the
	 * file be contiguous.  (Which can never be true for really
	 * big files that are greater than a block group.)
	 */
	if (p->previous_block) {
		if (p->previous_block+1 != blk)
			p->fragmented = 1;
	}
	p->previous_block = blk;
	
	
	if (blk < fs->super->s_first_data_block ||
	    blk >= fs->super->s_blocks_count ||
	    ext2fs_test_block_bitmap(block_illegal_map, blk)) {
		problem = describe_illegal_block(fs, blk);
		if (preen) {
			printf("Block %u of inode %lu %s\n", blk, p->ino,
			       problem);
			preenhalt(fs);
		}
		if (p->fix == -1) {
			printf("Remove illegal block(s) in inode %lu", p->ino);
			p->fix = ask("", 1);
		}
		p->num_illegal_blocks++;
		if (!p->suppress && (p->num_illegal_blocks % 20) == 0) {
			printf("Too many illegal blocks in inode %lu.\n",
			       p->ino);
			if (ask("Clear inode", 1)) {
				p->clear = 1;
				return BLOCK_ABORT;
			}
			if (ask("Supress messages", 0)) {
				p->suppress = 1;
			}
		}
		if (!p->suppress)
			printf("Block #%d (%u) %s.  %s\n", blockcnt, blk,
			       problem, clear_msg[p->fix]);
		if (p->fix) {
			blk = *block_nr = 0;
			ret_code = BLOCK_CHANGED;
			goto mark_dir;
		} else {
			ext2fs_unmark_valid(fs);
			return 0;
		}
	}

	mark_block_used(fs, blk);
	p->num_blocks++;
	if (blockcnt < 0)
		return 0;
	
	p->last_block = blockcnt;
mark_dir:
	if (p->is_dir) {
		if (dir_block_count >= dir_block_size) {
			dir_block_size += 100;
			dir_blocks = realloc(dir_blocks,
					     dir_block_size *
					     sizeof(struct dir_block_struct));
			if (dir_blocks == 0)
				fatal_error("Not enough memory to "
					    "realloc dir_blocks");
		}

		dir_blocks[dir_block_count].blk = blk;
		dir_blocks[dir_block_count].ino = p->ino;
		dir_blocks[dir_block_count].blockcnt = blockcnt;
		dir_block_count++;
	}
	return ret_code;
}

static void bad_block_indirect(ext2_filsys fs, blk_t blk)
{
	printf("Bad block %u used as bad block indirect block?!?\n", blk);
	preenhalt(fs);
	printf("\nThis inconsistency can not be fixed with "
	       "e2fsck; to fix it, use\n"
	       """dumpe2fs -b"" to dump out the bad block "
	       "list and ""e2fsck -L filename""\n"
	       "to read it back in again.\n");
	if (ask("Continue", 0))
		return;
	fatal_error(0);
}

static int bad_primary_block(ext2_filsys fs, blk_t *block_nr)
{
	printf("\nIf the block is really bad, the filesystem can not be "
	       "fixed.\n");
	preenhalt(fs);
	printf("You can clear the this block from the bad block list\n");
	printf("and hope that block is really OK, but there are no "
	       "guarantees.\n\n");
	if (ask("Clear (and hope for the best)", 1)) {
		*block_nr = 0;
		return 1;
	}
	ext2fs_unmark_valid(fs);
	return 0;
}

int process_bad_block(ext2_filsys fs,
		      blk_t *block_nr,
		      int blockcnt,
		      void *private)
{
	struct process_block_struct *p;
	blk_t		blk = *block_nr;
	int		first_block;
	int		i;
	
	if (!blk)
		return 0;
	p = (struct process_block_struct *) private;

	if ((blk < fs->super->s_first_data_block) ||
	    (blk >= fs->super->s_blocks_count)) {
		if (preen) {
			printf("Illegal block %u in bad block inode\n", blk);
			preenhalt(fs);
		}
		if (p->fix == -1)
			p->fix = ask("Remove illegal block(s) in bad block inode", 1);
		printf("Illegal block %u in bad block inode.  %s\n", blk,
		       clear_msg[p->fix]);
		if (p->fix) {
			*block_nr = 0;
			return BLOCK_CHANGED;
		} else {
			ext2fs_unmark_valid(fs);
			return 0;
		}
	}

	if (blockcnt < 0) {
		if (ext2fs_test_block_bitmap(block_found_map, blk))
			bad_block_indirect(fs, blk);
		else
			mark_block_used(fs, blk);
		return 0;
	}
#if 0 
	printf ("DEBUG: Marking %u as bad.\n", blk);
#endif
	fs_badblocks_count++;
	/*
	 * If the block is not used, then mark it as used and return.
	 * If it is already marked as found, this must mean that
	 * there's an overlap between the filesystem table blocks
	 * (bitmaps and inode table) and the bad block list.
	 */
	if (!ext2fs_test_block_bitmap(block_found_map, blk)) {
		ext2fs_mark_block_bitmap(block_found_map, blk);
		return 0;
	}
	/*
	 * Try to find the where the filesystem block was used...
	 */
	first_block = fs->super->s_first_data_block;
	
	for (i = 0; i < fs->group_desc_count; i++ ) {
		if (blk == first_block) {
			if (i == 0) {
				printf("The primary superblock (%u) is "
				       "on the bad block list.\n", blk);
				if (bad_primary_block(fs, block_nr))
					return BLOCK_CHANGED;
				return 0;
			}
			if (!preen)
				printf("Warning: Group %d's superblock "
				       "(%u) is bad.\n", i, blk);
			return 0;
		}
		if ((blk > first_block) &&
		    (blk <= first_block + fs->desc_blocks)) {
			if (i == 0) {
				printf("Block %u in the primary group "
				       "descriptors is on the bad block "
				       "list\n", blk);
				if (bad_primary_block(fs, block_nr))
					return BLOCK_CHANGED;
				return 0;
			}
			if (!preen)
				printf("Warning: Group %d's copy of the "
				       "group descriptors has a bad "
				       "block (%u).\n", i, blk);
			return 0;
		}
		if (blk == fs->group_desc[i].bg_block_bitmap) {
			printf("Group %d's block bitmap (%u) is bad.  ",
			       i, blk);
			if (ask("Relocate", 1)) {
				invalid_block_bitmap[i]++;
				invalid_bitmaps++;
			} else
				ext2fs_unmark_valid(fs);
			return 0;
		}
		if (blk == fs->group_desc[i].bg_inode_bitmap) {
			printf("Group %d's inode bitmap (%u) is bad.  ",
			       i, blk);
			if (ask("Relocate", 1)) {
				invalid_inode_bitmap[i]++;
				invalid_bitmaps++;
			} else
				ext2fs_unmark_valid(fs);
			return 0;
		}
		if ((blk >= fs->group_desc[i].bg_inode_table) &&
		    (blk < (fs->group_desc[i].bg_inode_table +
			    fs->inode_blocks_per_group))) {
			printf("WARNING: Severe data loss possible!!!!\n");
			printf("Bad block %u in group %d's inode table.  ",
			       blk, i);
			if (ask("Relocate", 1)) {
				invalid_inode_table[i]++;
				invalid_bitmaps++;
			} else
				ext2fs_unmark_valid(fs);
			return 0;
		}
		first_block += fs->super->s_blocks_per_group;
	}
	/*
	 * If we've gotten to this point, then the only
	 * possibility is that the bad block inode meta data
	 * is using a bad block.
	 */
	if ((blk == p->inode->i_block[EXT2_IND_BLOCK]) ||
	    p->inode->i_block[EXT2_DIND_BLOCK]) {
		bad_block_indirect(fs, blk);
		return 0;
	}
	
	printf("Programming error?  block #%u claimed for no reason "
	       "in process_bad_block.\n", blk);
	return 0;
}

static void new_table_block(ext2_filsys fs, blk_t first_block, int group, 
			    const char *name, int num, blk_t *new_block)
{
	errcode_t	retval;
	blk_t		old_block = *new_block;
	int		i;
	char		*buf;
	
	retval = ext2fs_get_free_blocks(fs, first_block,
			first_block + fs->super->s_blocks_per_group,
					num, block_found_map, new_block);
	if (retval) {
		printf("Could not allocate %d block(s) for %s: %s\n",
		       num, name, error_message(retval));
		ext2fs_unmark_valid(fs);
		return;
	}
	buf = malloc(fs->blocksize);
	if (!buf) {
		printf("Could not allocate block buffer for relocating %s\n",
		       name);
		ext2fs_unmark_valid(fs);
		return;
	}
	ext2fs_mark_super_dirty(fs);
	printf("Relocating group %d's %s ", group, name);
	if (old_block)
		printf("from %u ", old_block);
	printf("to %u...\n", *new_block);
	for (i = 0; i < num; i++) {
		ext2fs_mark_block_bitmap(block_found_map, (*new_block)+i);
		if (old_block) {
			retval = io_channel_read_blk(fs->io, old_block + i,
						     1, buf);
			if (retval)
				printf("Warning: could not read block %u "
				       "of %s: %s\n",
				       old_block + i, name,
				       error_message(retval));
		} else
			memset(buf, 0, fs->blocksize);

		retval = io_channel_write_blk(fs->io, (*new_block) + i,
					      1, buf);
		if (retval)
			printf("Warning: could not write block %u for %s: %s\n",
			       (*new_block) + i, name, error_message(retval));
	}
	free(buf);
}

/*
 * This routine gets called at the end of pass 1 if bad blocks are
 * detected in the superblock, group descriptors, inode_bitmaps, or
 * block bitmaps.  At this point, all of the blocks have been mapped
 * out, so we can try to allocate new block(s) to replace the bad
 * blocks.
 */
static void handle_fs_bad_blocks(ext2_filsys fs)
{
	int		i;
	int		first_block = fs->super->s_first_data_block;

	for (i = 0; i < fs->group_desc_count; i++) {
		if (invalid_block_bitmap[i]) {
			new_table_block(fs, first_block, i, "block bitmap", 1, 
					&fs->group_desc[i].bg_block_bitmap);
		}
		if (invalid_inode_bitmap[i]) {
			new_table_block(fs, first_block, i, "inode bitmap", 1, 
					&fs->group_desc[i].bg_inode_bitmap);
		}
		if (invalid_inode_table[i]) {
			new_table_block(fs, first_block, i, "inode table",
					fs->inode_blocks_per_group, 
					&fs->group_desc[i].bg_inode_table);
			restart_e2fsck++;
		}
		first_block += fs->super->s_blocks_per_group;
	}
	invalid_bitmaps = 0;
}

/*
 * This routine marks all blocks which are used by the superblock,
 * group descriptors, inode bitmaps, and block bitmaps.
 */
static void mark_table_blocks(ext2_filsys fs)
{
	blk_t	block, b;
	int	i,j;
	
	block = fs->super->s_first_data_block;
	for (i = 0; i < fs->group_desc_count; i++) {
		/*
		 * Mark block used for the block bitmap 
		 */
		if (fs->group_desc[i].bg_block_bitmap) {
			if (ext2fs_test_block_bitmap(block_found_map,
				     fs->group_desc[i].bg_block_bitmap)) {
				printf("Group %i's block bitmap at %u "
				       "conflicts with some other fs block.\n",
				       i, fs->group_desc[i].bg_block_bitmap);
				preenhalt(fs);
				if (ask("Relocate", 1)) {
					invalid_block_bitmap[i]++;
					invalid_bitmaps++;
				} else {
					ext2fs_unmark_valid(fs);
				}
			} else {
			    ext2fs_mark_block_bitmap(block_found_map,
				     fs->group_desc[i].bg_block_bitmap);
			    ext2fs_mark_block_bitmap(block_illegal_map,
				     fs->group_desc[i].bg_block_bitmap);
		    }
			
		}
		/*
		 * Mark block used for the inode bitmap 
		 */
		if (fs->group_desc[i].bg_inode_bitmap) {
			if (ext2fs_test_block_bitmap(block_found_map,
				     fs->group_desc[i].bg_inode_bitmap)) {
				printf("Group %i's inode bitmap at %u "
				       "conflicts with some other fs block.\n",
				       i, fs->group_desc[i].bg_inode_bitmap);
				preenhalt(fs);
				if (ask("Relocate", 1)) {
					invalid_inode_bitmap[i]++;
					invalid_bitmaps++;
				} else {
					ext2fs_unmark_valid(fs);
				}
			} else {
			    ext2fs_mark_block_bitmap(block_found_map,
				     fs->group_desc[i].bg_inode_bitmap);
			    ext2fs_mark_block_bitmap(block_illegal_map,
				     fs->group_desc[i].bg_inode_bitmap);
			}
		}
		    
		/*
		 * Mark the blocks used for the inode table
		 */
		if (fs->group_desc[i].bg_inode_table) {
			for (j = 0, b = fs->group_desc[i].bg_inode_table;
			     j < fs->inode_blocks_per_group;
			     j++, b++) {
				if (ext2fs_test_block_bitmap(block_found_map,
							     b)) {
					printf("Group %i's inode table at %u "
					       "conflicts with some other "
					       "fs block.\n",
					       i, b);
					preenhalt(fs);
					if (ask("Relocate", 1)) {
						invalid_inode_table[i]++;
						invalid_bitmaps++;
					} else {
						ext2fs_unmark_valid(fs);
					}
				} else {
				    ext2fs_mark_block_bitmap(block_found_map,
							     b);
				    ext2fs_mark_block_bitmap(block_illegal_map,
							     b);
				    }
			}
		}
			    
		/*
		 * Mark this group's copy of the superblock
		 */
		ext2fs_mark_block_bitmap(block_found_map, block);
		ext2fs_mark_block_bitmap(block_illegal_map, block);
		
		/*
		 * Mark this group's copy of the descriptors
		 */
		for (j = 0; j < fs->desc_blocks; j++) {
			ext2fs_mark_block_bitmap(block_found_map,
						 block + j + 1);
			ext2fs_mark_block_bitmap(block_illegal_map,
						 block + j + 1);
		}
		block += fs->super->s_blocks_per_group;
	}
}
	
/*
 * This subroutines short circuits ext2fs_get_blocks and
 * ext2fs_check_directory; we use them since we already have the inode
 * structure, so there's no point in letting the ext2fs library read
 * the inode again.
 */
static errcode_t pass1_get_blocks(ext2_filsys fs, ino_t ino, blk_t *blocks)
{
	int	i;
	
	if (ino == stashed_ino) {
		for (i=0; i < EXT2_N_BLOCKS; i++)
			blocks[i] = stashed_inode->i_block[i];
		return 0;
	}
	printf("INTERNAL ERROR: pass1_get_blocks: unexpected inode #%lu\n",
	       ino);
	printf("\t(was expecting %lu)\n", stashed_ino);
	exit(FSCK_ERROR);
}

static errcode_t pass1_check_directory(ext2_filsys fs, ino_t ino)
{
	if (ino == stashed_ino) {
		if (!LINUX_S_ISDIR(stashed_inode->i_mode))
			return ENOTDIR;
		return 0;
	}
	printf("INTERNAL ERROR: pass1_check_directory: unexpected inode #%lu\n",
	       ino);
	printf("\t(was expecting %lu)\n", stashed_ino);
	exit(FSCK_ERROR);
}
