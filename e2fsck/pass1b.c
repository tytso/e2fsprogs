/*
 * pass1b.c --- Pass #1b of e2fsck
 *
 * This file contains pass1B, pass1C, and pass1D of e2fsck.  They are
 * only invoked if pass 1 discovered blocks which are in use by more
 * than one inode.
 * 
 * Pass1B scans the data blocks of all the inodes again, generating a
 * complete list of duplicate blocks and which inodes have claimed
 * them.
 *
 * Pass1C does a tree-traversal of the filesystem, to determine the
 * parent directories of these inodes.  This step is necessary so that
 * e2fsck can print out the pathnames of affected inodes.
 *
 * Pass1D is a reconciliation pass.  For each inode with duplicate
 * blocks, the user is prompted if s/he would like to clone the file
 * (so that the file gets a fresh copy of the duplicated blocks) or
 * simply to delete the file.
 * 
 * Copyright (C) 1993, 1994 Theodore Ts'o.  This file may be
 * redistributed under the terms of the GNU Public License.
 * 
 */

#include <time.h>

#include <et/com_err.h>
#include "e2fsck.h"

/*
 * This is structure is allocated for each time that a block is
 * claimed by more than one file.  So if a particular block is claimed
 * by 3 files, then three copies of this structure will be allocated,
 * one for each conflict.
 *
 * The linked list structure is as follows:
 *
 * dup_blk -->  block #34  --> block #35  --> block #47
 * 		inode #12      inode #14      inode #17
 * 		num_bad = 3    num_bad = 2    num_bad = 2
 * 		  |              |               |
 * 		  V              V               V
 * 		block #34      block #35      block #47
 * 		inode #14      inode #15      inode #23
 * 		  |
 * 		  V
 * 		block #34
 * 		inode #15
 *
 * The num_bad field indicates how many inodes are sharing a
 * particular block, and is only stored in the first element of the
 * linked list for a particular block.  As the block conflicts are
 * resolved, num_bad is decremented; when it reaches 1, then we no
 * longer need to worry about that block.
 */
struct dup_block {
	blk_t		block;		/* Block number */
	ino_t		ino;		/* Inode number */
	int		num_bad;
	/* Pointer to next dup record with different block */
	struct dup_block *next_block;
	/* Pointer to next dup record with different inode */
	struct dup_block *next_inode;
};

/*
 * This structure stores information about a particular inode which
 * is sharing blocks with other inodes.  This information is collected
 * to display to the user, so that the user knows what files he or she
 * is dealing with, when trying to decide how to resolve the conflict
 * of multiply-claimed blocks.
 */
struct dup_inode {
	ino_t		ino;
	time_t		mtime;
	char		*pathname;
	int		num_dupblocks;
	int		flags;
	struct dup_inode	*next;
};

#define DUP_INODE_DONT_FREE_PATHNAME	0x1

static int process_pass1b_block(ext2_filsys fs, blk_t	*blocknr,
				int	blockcnt, void	*private);
static void delete_file(ext2_filsys fs, struct dup_inode *dp,
			char *block_buf);
static int clone_file(ext2_filsys fs, struct dup_inode *dp, char* block_buf);
static void pass1b(ext2_filsys fs, char *block_buf);
static void pass1c(ext2_filsys fs, char *block_buf);
static void pass1d(ext2_filsys fs, char *block_buf);

static struct dup_block *dup_blk = 0;
static struct dup_inode *dup_ino = 0;
static int dup_inode_count = 0;

/*
 * For pass1_check_directory and pass1_get_blocks
 */
extern ino_t stashed_ino;
extern struct ext2_inode *stashed_inode;

static char *inode_dup_map;

/*
 * Main procedure for handling duplicate blocks
 */
void pass1_dupblocks(ext2_filsys fs, char *block_buf)
{
	errcode_t		retval;
	struct dup_block	*p, *q, *next_p, *next_q;
	struct dup_inode	*r, *next_r;
	
	retval = ext2fs_allocate_inode_bitmap(fs, &inode_dup_map);
	if (retval) {
		com_err("ext2fs_allocate_inode_bitmap", retval,
			"while allocating inode_dup_map");
		fatal_error(0);
	}
	
	pass1b(fs, block_buf);
	pass1c(fs, block_buf);
	pass1d(fs, block_buf);

	/*
	 * Time to free all of the accumulated data structures that we
	 * don't need anymore.
	 */
	free(inode_dup_map);   	inode_dup_map = 0;
	free(block_dup_map);    block_dup_map = 0;
	for (p = dup_blk; p; p = next_p) {
		next_p = p->next_block;
		for (q = p; q; q = next_q) {
			next_q = q->next_inode;
			free(q);
		}
	}
	for (r = dup_ino; r; r = next_r) {
		next_r = r->next;
		if (r->pathname && !(r->flags & DUP_INODE_DONT_FREE_PATHNAME))
			free(r->pathname);
		free(r);
	}
}

/*
 * Scan the inodes looking for inodes that contain duplicate blocks.
 */
struct process_block_struct {
	ino_t	ino;
	int	dup_blocks;
};

void pass1b(ext2_filsys fs, char *block_buf)
{
	ino_t	ino;
	struct ext2_inode inode;
	ext2_inode_scan	scan;
	errcode_t	retval;
	struct process_block_struct pb;
	struct dup_inode *dp;
	
	printf("Duplicate blocks found... invoking duplicate block passes.\n");
	printf("Pass 1B: Rescan for duplicate/bad blocks\n");
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
		if ((ino != EXT2_BAD_INO) &&
		    (!ext2fs_test_inode_bitmap(fs, inode_used_map, ino) ||
		     !inode_has_valid_blocks(&inode)))
			goto next;

		pb.ino = ino;
		pb.dup_blocks = 0;
		retval = ext2fs_block_iterate(fs, ino, 0, block_buf,
					      process_pass1b_block, &pb);
		if (pb.dup_blocks) {
			if (ino != EXT2_BAD_INO)
				printf("\n");
			dp = allocate_memory(sizeof(struct dup_inode),
					     "duplicate inode record");
			dp->ino = ino;
			dp->mtime = inode.i_mtime;
			dp->num_dupblocks = pb.dup_blocks;
			dp->pathname = 0;
			dp->flags = 0;
			dp->next = dup_ino;
			dup_ino = dp;
			if (ino != EXT2_BAD_INO)
				dup_inode_count++;
		}
		if (retval)
			com_err(program_name, retval,
				"while calling ext2fs_block_iterate in pass1b");
		
	next:
		retval = ext2fs_get_next_inode(scan, &ino, &inode);
		if (retval) {
			com_err(program_name, retval,
				"while doing inode scan");
			fatal_error(0);
		}
	}
	ext2fs_close_inode_scan(scan);
	fs->get_blocks = 0;
	fs->check_directory = 0;
}

int process_pass1b_block(ext2_filsys fs,
			 blk_t	*block_nr,
			 int blockcnt,
			 void *private)
{
	struct process_block_struct *p;
	struct dup_block *dp, *q, *r;
	int i;

	if (!*block_nr)
		return 0;
	p = (struct process_block_struct *) private;
	
	if (ext2fs_test_block_bitmap(fs, block_dup_map, *block_nr)) {
		/* OK, this is a duplicate block */
		if (p->ino != EXT2_BAD_INO) {
			if (!p->dup_blocks)
				printf("Duplicate/bad block(s) in inode %ld:",
				       p->ino);
			printf(" %ld", *block_nr);
		}
		p->dup_blocks++;
		ext2fs_mark_block_bitmap(fs, block_dup_map, *block_nr);
		ext2fs_mark_inode_bitmap(fs, inode_dup_map, p->ino);
		dp = allocate_memory(sizeof(struct dup_block),
				      "duplicate block record");
		dp->block = *block_nr;
		dp->ino = p->ino;
		dp->num_bad = 0;
		q = dup_blk;
		while (q) {
			if (q->block == *block_nr)
				break;
			q = q->next_block;
		}
		if (q) {
			dp->next_inode = q->next_inode;
			q->next_inode = dp;
		} else {
			dp->next_block = dup_blk;
			dup_blk = dp;
		}
	}
	/*
	 * Set the num_bad field
	 */
	for (q = dup_blk; q; q = q->next_block) {
		i = 0;
		for (r = q; r; r = r->next_inode)
			i++;
		q->num_bad = i;
	}
	return 0;
}

/*
 * Used by pass1c to name the "special" inodes.  They are declared as
 * writeable strings to prevent const problems.
 */
#define num_special_inodes	7
char special_inode_name[num_special_inodes][40] =
{
	"<The NULL inode>",			/* 0 */
	"<The bad blocks inode>", 		/* 1 */
	"/",					/* 2 */
	"<The ACL index inode>",		/* 3 */
	"<The ACL data inode>",			/* 4 */
	"<The boot loader inode>",		/* 5 */
	"<The undelete directory inode>"	/* 6 */
};

/*
 * Pass 1c: Scan directories for inodes with duplicate blocks.  This
 * is used so that we can print pathnames when prompting the user for
 * what to do.
 */
struct process_dir_struct {
	ext2_filsys	fs;
	ino_t		dir_ino;
	int		count;
};

void pass1c(ext2_filsys fs, char *block_buf)
{
	int	i;
	struct dup_inode	*p;
	errcode_t	retval;
	char	buf[80];
	int	inodes_left = dup_inode_count;
	int	offset, entry;
	struct ext2_dir_entry *dirent;

	printf("Pass 1C: Scan directories for inodes with dup blocks.\n");

	/*
	 * First check to see if any of the inodes with dup blocks is
	 * the bad block inode or the root inode; handle them as
	 * special cases.
	 */
	for (p = dup_ino; p; p = p->next) {
		if (p->ino < num_special_inodes) {
			p->pathname = special_inode_name[p->ino];
			p->flags |= DUP_INODE_DONT_FREE_PATHNAME;
			inodes_left--;
		}
	}

	/*
	 * Search through all directories to translate inodes to names
	 * (by searching for the containing directory for that inode.)
	 */
	for (i=0; inodes_left && i < dir_block_count; i++) {
		retval = io_channel_read_blk(fs->io, dir_blocks[i].blk,
					     1, block_buf);
		entry = offset = 0;
		while (offset < fs->blocksize) {
			entry++;
			dirent = (struct ext2_dir_entry *)
				(block_buf + offset);
			if (!dirent->inode ||
			    ((dir_blocks[i].blockcnt == 0) && (entry <= 2)))
				goto next;

			if (!ext2fs_test_inode_bitmap(fs, inode_dup_map,
						      dirent->inode))
				goto next;

			for (p = dup_ino; p; p = p->next) {
				if (p->ino == dirent->inode)
					break;
			}

			if (!p || p->pathname)
				goto next;
			
			(void) ext2fs_get_pathname(fs, dir_blocks[i].ino,
						   p->ino, &p->pathname);
			inodes_left--;
			
		next:
			if (dirent->rec_len < 8)
				break;
			offset += dirent->rec_len;
		}
	}


	/*
	 * If we can't get a name, then put in a generic one.
	 */
	for (p = dup_ino; p; p = p->next) {
		if (!p->pathname) {
			sprintf(buf, "<Unknown inode #%ld>", p->ino);
			p->pathname = malloc(strlen(buf)+1);
			if (!p->pathname) {
				fprintf(stderr,	"pass1c: couldn't malloc "
					"generic pathname\n");
				fatal_error(0);
			}
			strcpy(p->pathname, buf);
		}
	}
}	

static void pass1d(ext2_filsys fs, char *block_buf)
{
	struct dup_inode	*p, *s;
	struct dup_block	*q, *r;
	ino_t	*shared;
	int	shared_len;
	int	i;
	errcode_t	retval;
	char	*time_str;
	int	file_ok;
	
	printf("Pass 1D: Reconciling duplicate blocks\n");
	read_bitmaps(fs);

	printf("(There are %d inodes containing duplicate/bad blocks.)\n\n",
	       dup_inode_count);
	shared = allocate_memory(sizeof(ino_t) * dup_inode_count,
				 "Shared inode list");
	for (p = dup_ino; p; p = p->next) {
		shared_len = 0;
		file_ok = 1;
		if (p->ino == EXT2_BAD_INO)
			continue;

		/*
		 * Search through the duplicate records to see which
		 * inodes share blocks with this one
		 */
		for (q = dup_blk; q; q = q->next_block) {
			/*
			 * See if this block is used by this inode.
			 * If it isn't, continue.
			 */
			for (r = q; r; r = r->next_inode)
				if (r->ino == p->ino)
					break;
			if (!r)
				continue;
			if (q->num_bad > 1)
				file_ok = 0;
			/*
			 * Add all inodes used by this block to the
			 * shared[] --- which is a unique list, so
			 * if an inode is already in shared[], don't
			 * add it again.
			 */
			for (r = q; r; r = r->next_inode) {
				if (r->ino == p->ino)
					continue;
				for (i = 0; i < shared_len; i++)
					if (shared[i] == r->ino)
						break;
				if (i == shared_len) {
					shared[shared_len++] = r->ino;
				}
			}
		}
		time_str = ctime(&p->mtime);
		time_str[24] = 0;
		printf("File %s (inode #%ld, mod time %s) \n",
		       p->pathname, p->ino, time_str);
		printf("  has %d duplicate blocks, shared with %d file%s:\n",
		       p->num_dupblocks, shared_len,
		       (shared_len>1) ? "s" : "");
		for (i = 0; i < shared_len; i++) {
			for (s = dup_ino; s; s = s->next)
				if (s->ino == shared[i])
					break;
			if (!s)
				continue;
			time_str = ctime(&s->mtime);
			time_str[24] = 0;
			printf("\t%s (inode #%ld, mod time %s)\n",
			       s->pathname, s->ino, time_str);
		}
		if (file_ok) {
			printf("Duplicated blocks already reassigned or cloned.\n\n");
			continue;
		}
			
		if (ask("Clone duplicate/bad blocks", 1)) {
			retval = clone_file(fs, p, block_buf);
			if (retval)
				printf("Couldn't clone file: %s\n",
				       error_message(retval));
			else {
				printf("\n");
				continue;
			}
		}
		if (ask("Delete file", 1))
			delete_file(fs, p, block_buf);
		else
			ext2fs_unmark_valid(fs);
		printf("\n");
	}
}

static int delete_file_block(ext2_filsys fs,
			     blk_t	*block_nr,
			     int blockcnt,
			     void *private)
{
	struct dup_block *p;

	if (!*block_nr)
		return 0;

	if (ext2fs_test_block_bitmap(fs, block_dup_map, *block_nr)) {
		for (p = dup_blk; p; p = p->next_block)
			if (p->block == *block_nr)
				break;
		if (p) {
			p->num_bad--;
			if (p->num_bad == 1)
				ext2fs_unmark_block_bitmap(fs, block_dup_map,
							   *block_nr);
		} else
			com_err("delete_file_block", 0,
				"internal error; can't find dup_blk for %d\n",
				*block_nr);
	} else {
		ext2fs_unmark_block_bitmap(fs, block_found_map, *block_nr);
		ext2fs_unmark_block_bitmap(fs, fs->block_map, *block_nr);
	}
		
	return 0;
}
		
static void delete_file(ext2_filsys fs, struct dup_inode *dp, char* block_buf)
{
	errcode_t	retval;
	struct process_block_struct pb;
	struct ext2_inode	inode;

	pb.ino = dp->ino;
	pb.dup_blocks = dp->num_dupblocks;
	
	retval = ext2fs_block_iterate(fs, dp->ino, 0, block_buf,
				      delete_file_block, &pb);
	if (retval)
		com_err("delete_file", retval,
			"while calling ext2fs_block_iterate for inode %d",
			dp->ino);
	ext2fs_unmark_inode_bitmap(fs, inode_used_map, dp->ino);
	ext2fs_unmark_inode_bitmap(fs, inode_dir_map, dp->ino);
	if (inode_bad_map)
		ext2fs_unmark_inode_bitmap(fs, inode_bad_map, dp->ino);
	ext2fs_unmark_inode_bitmap(fs, fs->inode_map, dp->ino);
	ext2fs_mark_ib_dirty(fs);
	ext2fs_mark_bb_dirty(fs);
	retval = ext2fs_read_inode(fs, dp->ino, &inode);
	if (retval) {
		com_err("delete_file", retval, "while reading inode %d",
			dp->ino);
		return;
	}
	inode.i_links_count = 0;
	inode.i_dtime = time(0);
	retval = ext2fs_write_inode(fs, dp->ino, &inode);
	if (retval) {
		com_err("delete_file", retval, "while writing inode %d",
			dp->ino);
		return;
	}
}

struct clone_struct {
	errcode_t	errcode;
	char	*buf;
};

static int clone_file_block(ext2_filsys fs,
			    blk_t	*block_nr,
			    int blockcnt,
			    void *private)
{
	struct dup_block *p;
	blk_t	new_block;
	errcode_t	retval;
	struct clone_struct *cs = (struct clone_struct *) private;

	if (!*block_nr)
		return 0;

	if (ext2fs_test_block_bitmap(fs, block_dup_map, *block_nr)) {
		for (p = dup_blk; p; p = p->next_block)
			if (p->block == *block_nr)
				break;
		if (p) {
			retval = ext2fs_new_block(fs, 0, block_found_map,
						  &new_block);
			if (retval) {
				cs->errcode = retval;
				return BLOCK_ABORT;
			}
			retval = io_channel_read_blk(fs->io, *block_nr, 1,
						     cs->buf);
			if (retval) {
				cs->errcode = retval;
				return BLOCK_ABORT;
			}
			retval = io_channel_write_blk(fs->io, new_block, 1,
						      cs->buf);
			if (retval) {
				cs->errcode = retval;
				return BLOCK_ABORT;
			}
			p->num_bad--;
			if (p->num_bad == 1)
				ext2fs_unmark_block_bitmap(fs, block_dup_map,
							   *block_nr);
			*block_nr = new_block;
			ext2fs_mark_block_bitmap(fs, block_found_map,
						 new_block);
			ext2fs_mark_block_bitmap(fs, fs->block_map, new_block);
			return BLOCK_CHANGED;
		} else
			com_err("clone_file_block", 0,
				"internal error; can't find dup_blk for %d\n",
				*block_nr);
	}
	return 0;
}
		
static int clone_file(ext2_filsys fs, struct dup_inode *dp, char* block_buf)
{
	errcode_t	retval;
	struct clone_struct cs;

	cs.errcode = 0;
	cs.buf = malloc(fs->blocksize);
	if (!cs.buf)
		return ENOMEM;
	
	retval = ext2fs_block_iterate(fs, dp->ino, 0, block_buf,
				      clone_file_block, &cs);
	ext2fs_mark_bb_dirty(fs);
	free(cs.buf);
	if (retval) {
		com_err("clone_file", retval,
			"while calling ext2fs_block_iterate for inode %d",
			dp->ino);
		return retval;
	}
	if (cs.errcode) {
		com_err("clone_file", retval,
			"returned from clone_file_block");
		return retval;
	}
	return 0;
}


	

	
