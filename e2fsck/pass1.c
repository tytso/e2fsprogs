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

#include <et/com_err.h>
#include "e2fsck.h"

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

char * inode_used_map = 0;	/* Inodes which are in use */
char * inode_bad_map = 0;	/* Inodes which are bad in some way */
char * inode_dir_map = 0;	/* Inodes which are directories */

char * block_found_map = 0;
char * block_dup_map = 0;
static char * bad_fs_block_map = 0;

static int fix_link_count = -1;

unsigned short * inode_link_info = NULL;

static int process_block(ext2_filsys fs, blk_t	*blocknr,
			 int	blockcnt, void	*private);
static int process_bad_block(ext2_filsys fs, blk_t *block_nr,
			     int blockcnt, void *private);
static int process_fs_bad_block(ext2_filsys fs, blk_t *block_nr,
				int blockcnt, void *private);
static void check_blocks(ext2_filsys fs, ino_t ino, struct ext2_inode *inode,
			 char *block_buf);
static void mark_table_blocks(ext2_filsys fs);
static errcode_t pass1_check_directory(ext2_filsys fs, ino_t ino);
static errcode_t pass1_get_blocks(ext2_filsys fs, ino_t ino, blk_t *blocks);
static void alloc_bad_map(ext2_filsys fs);
static void handle_fs_bad_blocks(ext2_filsys fs, char *block_buf);
static void process_inodes(ext2_filsys fs, char *block_buf);
static int process_inode_cmp(const void *a, const void *b);
static int dir_block_cmp(const void *a, const void *b);

struct process_block_struct {
	ino_t	ino;
	int	is_dir;
	int	num_blocks;
	int	last_block;
	int	fix;
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
int process_inode_size = 128;

/*
 * For the directory blocks list.
 */
struct dir_block_struct *dir_blocks = 0;
int	dir_block_count = 0;
int	dir_block_size = 0;

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
	retval = ext2fs_allocate_inode_bitmap(fs, &inode_used_map);
	if (retval) {
		com_err("ext2fs_allocate_inode_bitmap", retval,
			"while allocating inode_used_map");
		fatal_error(0);
	}
	retval = ext2fs_allocate_inode_bitmap(fs, &inode_dir_map);
	if (retval) {
		com_err("ext2fs_allocate_inode_bitmap", retval,
			"while allocating inode_dir_map");
		fatal_error(0);
	}
	retval = ext2fs_allocate_block_bitmap(fs, &block_found_map);
	if (retval) {
		com_err("ext2fs_allocate_block_bitmap", retval,
			"while allocating block_found_map");
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
	while (ino) {
		stashed_ino = ino;
		inode_link_info[ino] = inode.i_links_count;
		if (ino == EXT2_BAD_INO) {
			struct process_block_struct pb;
			
			pb.ino = EXT2_BAD_INO;
			pb.num_blocks = pb.last_block = 0;
			pb.is_dir = 0;
			pb.fix = -1;
			retval = ext2fs_block_iterate(fs, ino, 0, block_buf,
						      process_bad_block, &pb);
			if (retval)
				com_err(program_name, retval, "while calling e2fsc_block_interate in pass 1");

			ext2fs_mark_inode_bitmap(fs, inode_used_map, ino);
			goto next;
		}
		if (ino == EXT2_ROOT_INO) {
			/*
			 * Make sure the root inode is a directory; if
			 * not, offer to clear it.  It will be
			 * regnerated in pass #3.
			 */
			if (!S_ISDIR(inode.i_mode)) {
				printf("Root inode is not a directory.  ");
				preenhalt();
				if (ask("Clear", 1)) {
					inode.i_dtime = time(0);
					inode.i_links_count = 0;
					ext2fs_write_inode(fs, ino, &inode);
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
					ext2fs_write_inode(fs, ino, &inode);
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
			ext2fs_mark_inode_bitmap(fs, inode_used_map, ino);
			check_blocks(fs, ino, &inode, block_buf);
			goto next;
		}
		/*
		 * This code assumes that deleted inodes have
		 * i_links_count set to 0.  
		 */
		if (!inode.i_links_count) {
			if (!inode.i_dtime && inode.i_mode) {
				printf("Deleted inode %ld has zero dtime.\n",
				       ino);
				if (ask("Set dtime", 1)) {
					inode.i_dtime = time(0);
					ext2fs_write_inode(fs, ino, &inode);
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
			printf("Inode %ld is deleted w/ non-zero link_count.  %s\n",
			       ino, clear_msg[fix_link_count]);
			if (fix_link_count) {
				inode.i_links_count = 0;
				ext2fs_write_inode(fs, ino, &inode);
			} else
				ext2fs_unmark_valid(fs);
			goto next;
		}
		
		ext2fs_mark_inode_bitmap(fs, inode_used_map, ino);
		if (inode.i_faddr || inode.i_frag || inode.i_fsize ||
		    inode.i_file_acl || inode.i_dir_acl) {
			if (!inode_bad_map)
				alloc_bad_map(fs);
			ext2fs_mark_inode_bitmap(fs, inode_bad_map, ino);
		}
		
		if (S_ISDIR(inode.i_mode)) {
			ext2fs_mark_inode_bitmap(fs, inode_dir_map, ino);
			add_dir_info(fs, ino, 0, &inode);
			fs_directory_count++;
		} else if (S_ISREG (inode.i_mode))
			fs_regular_count++;
		else if (S_ISCHR (inode.i_mode))
			fs_chardev_count++;
		else if (S_ISBLK (inode.i_mode))
			fs_blockdev_count++;
		else if (S_ISLNK (inode.i_mode)) {
			fs_symlinks_count++;
			if (!inode.i_blocks)
				fs_fast_symlinks_count++;
		}
		else if (S_ISFIFO (inode.i_mode))
			fs_fifo_count++;
		else if (S_ISSOCK (inode.i_mode))
		        fs_sockets_count++;
		else {
			if (!inode_bad_map)
				alloc_bad_map(fs);
			ext2fs_mark_inode_bitmap(fs, inode_bad_map, ino);
		}
		if (inode.i_block[EXT2_IND_BLOCK] ||
		    inode.i_block[EXT2_DIND_BLOCK] ||
		    inode.i_block[EXT2_TIND_BLOCK]) {
			inodes_to_process[process_inode_count].ino = ino;
			inodes_to_process[process_inode_count].inode = inode;
			process_inode_count++;
		} else 
			check_blocks(fs, ino, &inode, block_buf);
		inode_link_info[ino] = inode.i_links_count;

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

	if (block_dup_map) {
		if (preen) {
			printf("Duplicate or bad blocks in use!\n");
			preenhalt();
		}
		pass1_dupblocks(fs, block_buf);
	}
	fs->get_blocks = 0;
	fs->check_directory = 0;
	free(inodes_to_process);
	if (bad_fs_block_map) {
		handle_fs_bad_blocks(fs, block_buf);
		free(bad_fs_block_map);
	}
	free(block_buf);

	if (tflag > 1) {
		printf("Pass 1: ");
		print_resource_track(&rtrack);
	}
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
	printf("process_inodes: ");
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
		printf("%d ", ino);
#endif
		sprintf(buf, "reading indirect blocks of inode %ld", ino);
		ehandler_operation(buf);
		check_blocks(fs, ino, stashed_inode, block_buf);
		
	}
	stashed_inode = old_stashed_inode;
	process_inode_count = 0;
#if 0
	printf("\n");
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

	return (db_a->blk - db_b->blk);
}

/*
 * This procedure will allocate the inode bad map table
 */
static void alloc_bad_map(ext2_filsys fs)
{
	errcode_t	retval;
	
	retval = ext2fs_allocate_inode_bitmap(fs, &inode_bad_map);
	if (retval) {
		com_err("ext2fs_allocate_inode_bitmap", retval,
			"while allocating inode_bad_map");
		fatal_error(0);
	}
}

/*
 * Marks a block as in use, setting the dup_map if it's been set
 * already.  Called by process_block and process_bad_block.
 */
static void mark_block_used(ext2_filsys fs, blk_t block)
{
	errcode_t	retval;
	
	if (ext2fs_test_block_bitmap(fs, block_found_map, block)) {
		if (!block_dup_map) {
			retval = ext2fs_allocate_block_bitmap(fs,
							      &block_dup_map);
			if (retval) {
				com_err("ext2fs_allocate_block_bitmap", retval,
					"while allocating block_dup_map");
				fatal_error(0);
			}
		}
		ext2fs_mark_block_bitmap(fs, block_dup_map, block);
	} else {
		ext2fs_mark_block_bitmap(fs, block_found_map, block);
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
	pb.is_dir = S_ISDIR(inode->i_mode);
	pb.fix = -1;
	retval = ext2fs_block_iterate(fs, ino, 0, block_buf,
				      process_block, &pb);
	if (retval)
		com_err(program_name, retval,
			"while calling ext2fs_block_iterate in check_blocks");

	pb.num_blocks *= (fs->blocksize / 512);
#if 0
	printf("inode %d, i_size = %d, last_block = %d, i_blocks=%d, num_blocks = %d\n",
	       ino, inode->i_size, pb.last_block, inode->i_blocks,
	       pb.num_blocks);
#endif
	if (!pb.num_blocks && pb.is_dir) {
		printf("Inode %ld is a zero length directory.  ", ino);
		if (ask("Clear", 1)) {
			inode->i_links_count = 0;
			inode->i_dtime = time(0);
			ext2fs_write_inode(fs, ino, inode);
			ext2fs_unmark_inode_bitmap(fs, inode_dir_map, ino);
			ext2fs_unmark_inode_bitmap(fs, inode_used_map, ino);
			fs_directory_count--;
		} else
			ext2fs_unmark_valid(fs);
	}
	if (inode->i_size < pb.last_block * fs->blocksize) {
		printf ("Inode %ld, incorrect size, %ld (counted = %d). ",
			ino, inode->i_size,
			(pb.last_block+1) * fs->blocksize);
		if (ask ("Set size to counted", 1)) {
			inode->i_size = (pb.last_block+1) * fs->blocksize;
			ext2fs_write_inode(fs, ino, inode);
		} else
			ext2fs_unmark_valid(fs);
	}
	if (pb.num_blocks != inode->i_blocks) {
		printf ("Inode %ld, i_blocks wrong %ld (counted=%d) .",
			ino, inode->i_blocks, pb.num_blocks);
		if (ask ("Set i_blocks to counted", 1)) {
			inode->i_blocks = pb.num_blocks;
			ext2fs_write_inode(fs, ino, inode);
		} else
				ext2fs_unmark_valid(fs);
	}
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
	int	group;
	int	illegal_block = 0;
	char	problem[80];
	blk_t	firstblock;
	blk_t	blk = *block_nr;

	if (!blk)
		return 0;
	p = (struct process_block_struct *) private;

#if 0
	printf("Process_block, inode %d, block %d, #%d\n", p->ino, blk,
	       blockcnt);
#endif	
	
	p->num_blocks++;
	if (blockcnt > 0)
		p->last_block = blockcnt;

	firstblock = fs->super->s_first_data_block;
	group = (blk - firstblock) / fs->super->s_blocks_per_group;
	if (blk < firstblock) {
		sprintf(problem, "< FIRSTBLOCK (%ld)", firstblock);
		illegal_block++;
	} else if (blk >= fs->super->s_blocks_count) {
		sprintf(problem, "> BLOCKS (%ld)", fs->super->s_blocks_count);
		illegal_block++;
	} else if (blk == fs->group_desc[group].bg_block_bitmap) {
		sprintf(problem, "is the block bitmap of group %d", group);
		illegal_block++;
	} else if (blk == fs->group_desc[group].bg_inode_bitmap) {
		sprintf(problem, "is the inode bitmap of group %d", group);
		illegal_block++;
	} else if (blk >= fs->group_desc[group].bg_inode_table &&
		   blk < fs->group_desc[group].bg_inode_table + fs->inode_blocks_per_group) {
		sprintf(problem, "is in the inode table of group %d", group);
		illegal_block++;
	}
	if (illegal_block) {
		if (preen) {
			printf("Block %ld of inode %ld %s\n", blk, p->ino,
			       problem);
			preenhalt();
		}
		if (p->fix == -1) {
			printf("Remove illegal block(s) in inode %ld", p->ino);
			p->fix = ask("", 1);
		}
		printf("Block #%d (%ld) %s.  %s\n", blockcnt, blk, problem,
		       clear_msg[p->fix]);
		if (p->fix) {
			*block_nr = 0;
			return BLOCK_CHANGED;
		} else {
			ext2fs_unmark_valid(fs);
			return 0;
		}
	}

	mark_block_used(fs, blk);
	
	if (p->is_dir && (blockcnt >= 0)) {
		if (dir_block_count >= dir_block_size) {
			dir_block_size += 100;
			dir_blocks = realloc(dir_blocks,
					     dir_block_size *
					     sizeof(struct dir_block_struct));
		}

		dir_blocks[dir_block_count].blk = blk;
		dir_blocks[dir_block_count].ino = p->ino;
		dir_blocks[dir_block_count].blockcnt = blockcnt;
		dir_block_count++;
	}
	
#if 0
	printf("process block, inode %d, block #%d is %d\n",
	       p->ino, blockcnt, blk);
#endif
	
	return 0;
}

int process_bad_block(ext2_filsys fs,
		      blk_t *block_nr,
		      int blockcnt,
		      void *private)
{
	struct process_block_struct *p;
	errcode_t	retval;
	blk_t		blk = *block_nr;
	
	if (!blk)
		return 0;
	p = (struct process_block_struct *) private;

	if ((blk < fs->super->s_first_data_block) ||
	    (blk >= fs->super->s_blocks_count)) {
		if (preen) {
			printf("Illegal block %ld in bad block inode\n", blk);
			preenhalt();
		}
		if (p->fix == -1)
			p->fix = ask("Remove illegal block(s) in bad block inode", 1);
		printf("Illegal block %ld in bad block inode.  %s\n", blk,
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
		mark_block_used(fs, blk);
		return 0;
	}
#if 0 
	printf ("DEBUG: Marking %d as bad.\n", blk);
#endif
	fs_badblocks_count++;
	/*
	 * If the block is not used, then mark it as used and return.
	 * If it is already marked as found, this must mean that
	 * there's an overlap between the filesystem table blocks
	 * (bitmaps and inode table) and the bad block list.
	 */
	if (!ext2fs_test_block_bitmap(fs, block_found_map, blk)) {
		ext2fs_mark_block_bitmap(fs, block_found_map, blk);
		return 0;
	}
	if (!bad_fs_block_map) {
		retval = ext2fs_allocate_inode_bitmap(fs, &bad_fs_block_map);
		if (retval) {
			com_err("ext2fs_allocate_block_bitmap", retval,
				"while allocating bad_fs_block_map");
		fatal_error(0);
		}
	}
	ext2fs_mark_block_bitmap(fs, bad_fs_block_map, blk);
	return 0;
}

/*
 * This routine gets called at the end of pass 1 if bad blocks are
 * detected in the superblock, group descriptors, inode_bitmaps, or
 * block bitmaps.  At this point, all of the blocks have been mapped
 * out, so we can try to allocate new block(s) to replace the bad
 * blocks.
 */
static void handle_fs_bad_blocks(ext2_filsys fs, char *block_buf)
{
	errcode_t	retval;
	
	printf("Warning: Bad block(s) found in filesystem-reserved blocks.\n");
	
	retval = ext2fs_block_iterate(fs, EXT2_BAD_INO, 0, block_buf,
				      process_fs_bad_block, 0);
}

static void new_table_block(ext2_filsys fs, blk_t first_block,
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
	for (i = 0; i < num; i++) {
		ext2fs_mark_block_bitmap(fs, block_found_map, (*new_block)+i);
		retval = io_channel_read_blk(fs->io, old_block + i,
					     1, buf);
		if (retval)
			printf("Warning: could not read block %ld of %s: %s\n",
			       old_block + i, name, error_message(retval));
		retval = io_channel_write_blk(fs->io, (*new_block) + i,
					      1, buf);
		if (retval)
			printf("Warning: could not write block %ld for %s: %s\n",
			       (*new_block) + i, name, error_message(retval));
		/*
		 * If this particular block is not marked as bad, then
		 * clear its bit in the block_found map.  Otherwise,
		 * leave it set, since it is included in the bad
		 * blocks inode.
		 */
		if (!ext2fs_test_block_bitmap(fs, bad_fs_block_map,
					      old_block + i))
			ext2fs_unmark_block_bitmap(fs, block_found_map,
						   old_block + i);
		/*
		 * Clear the bitmap since this block has now been moved.
		 */
		ext2fs_unmark_block_bitmap(fs, bad_fs_block_map,
					   old_block + i);
	}
	free(buf);
}

/*
 * Helper function for handle_fs_bad_blocks()
 */
static int process_fs_bad_block(ext2_filsys fs, blk_t *block_nr,
			     int blockcnt, void *private)
{
	int	i;
	blk_t	block = *block_nr;
	int	first_block = fs->super->s_first_data_block;

	/*
	 * If this block isn't one that is marked as a bad block in
	 * the filesystem tables, return
	 */
	if (!ext2fs_test_block_bitmap(fs, bad_fs_block_map, block))
		return 0;

	for (i = 0; i < fs->group_desc_count; i++) {
		if (block == first_block)
			printf("Bad block %ld in group %d's superblock.\n",
			       block, i);
		if (block == fs->group_desc[i].bg_block_bitmap) {
			printf("Bad block %ld in group %d's block bitmap.  ",
			       block, i);
			if (ask("Relocate", 1)) {
				new_table_block(fs, first_block,
						"block bitmap", 1, 
					&fs->group_desc[i].bg_block_bitmap);
			} else
				ext2fs_unmark_valid(fs);
		}
		if (block == fs->group_desc[i].bg_inode_bitmap) {
			printf("Bad block %ld in group %d's inode bitmap.  ",
			       block, i);
			if (ask("Relocate", 1)) {
				new_table_block(fs, first_block,
						"inode bitmap", 1, 
					&fs->group_desc[i].bg_inode_bitmap);
			} else
				ext2fs_unmark_valid(fs);
		}
		if ((block >= fs->group_desc[i].bg_inode_table) &&
		    (block < (fs->group_desc[i].bg_inode_table +
			      fs->inode_blocks_per_group))) {
			printf("WARNING: Severe data loss possible!!!!\n");
			printf("Bad block %ld in group %d's inode table.  ",
			       block, i);
			if (ask("Relocate", 1)) {
				new_table_block(fs, first_block,
						"inode table",
						fs->inode_blocks_per_group, 
					&fs->group_desc[i].bg_inode_table);
			} else
				ext2fs_unmark_valid(fs);
		}
		if ((block > first_block) &&
		    (block <= first_block + fs->desc_blocks))
			printf("Bad block %ld in group %d's copy of the descriptors.\n",
			       block, i);
		first_block += fs->super->s_blocks_per_group;
	}
	return 0;
}

/*
 * This routine marks all blocks which are used by the superblock,
 * group descriptors, inode bitmaps, and block bitmaps.
 */
static void mark_table_blocks(ext2_filsys fs)
{
	blk_t	block;
	int	i,j;
	
	block = fs->super->s_first_data_block;
	for (i = 0; i < fs->group_desc_count; i++) {
		/*
		 * Mark block used for the block bitmap 
		 */
		ext2fs_mark_block_bitmap(fs, block_found_map,
					 fs->group_desc[i].bg_block_bitmap);
		/*
		 * Mark block used for the inode bitmap 
		 */
		ext2fs_mark_block_bitmap(fs, block_found_map,
					 fs->group_desc[i].bg_inode_bitmap);
		/*
		 * Mark the blocks used for the inode table
		 */
		for (j = 0; j < fs->inode_blocks_per_group; j++)
			ext2fs_mark_block_bitmap(fs, block_found_map,
						 fs->group_desc[i].bg_inode_table + j);
		/*
		 * Mark this group's copy of the superblock
		 */
		ext2fs_mark_block_bitmap(fs, block_found_map, block);
		
		/*
		 * Mark this group's copy of the descriptors
		 */
		for (j = 0; j < fs->desc_blocks; j++)
			ext2fs_mark_block_bitmap(fs, block_found_map,
						 block + j + 1);
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
	printf("INTERNAL ERROR: pass1_get_blocks: unexpected inode #%ld\n",
	       ino);
	printf("\t(was expecting %ld)\n", stashed_ino);
	exit(FSCK_ERROR);
}

static errcode_t pass1_check_directory(ext2_filsys fs, ino_t ino)
{
	if (ino == stashed_ino) {
		if (!S_ISDIR(stashed_inode->i_mode))
			return ENOTDIR;
		return 0;
	}
	printf("INTERNAL ERROR: pass1_check_directory: unexpected inode #%ld\n",
	       ino);
	printf("\t(was expecting %ld)\n", stashed_ino);
	exit(FSCK_ERROR);
}
