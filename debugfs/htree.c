/*
 * htree.c --- hash tree routines
 * 
 * Copyright (C) 2002 Theodore Ts'o.  This file may be redistributed
 * under the terms of the GNU Public License.
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <time.h>
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#include <sys/types.h>
#ifdef HAVE_GETOPT_H
#include <getopt.h>
#else 
extern int optind;
extern char *optarg;
#endif
#ifdef HAVE_OPTRESET
extern int optreset;		/* defined by BSD, but not others */
#endif

#include "debugfs.h"

static FILE *pager;

static unsigned dx_hack_hash (const char *name, int len)
{
	__u32 hash0 = 0x12a3fe2d, hash1 = 0x37abe8f9;
	while (len--) {
		__u32 hash = hash1 + (hash0 ^ (*name++ * 7152373));
		
		if (hash & 0x80000000) hash -= 0x7fffffff;
		hash1 = hash0;
		hash0 = hash;
	}
	return hash0;
}

#define dx_hash(s,n) (dx_hack_hash(s,n) << 1)

static void htree_dump_leaf_node(ext2_filsys fs, ext2_ino_t ino,
				 struct ext2_inode *inode,
				 blk_t blk, char *buf)
{
	errcode_t	errcode;
	struct ext2_dir_entry *dirent;
	int		thislen, col = 0, offset = 0;
	char		name[EXT2_NAME_LEN];
	char		tmp[EXT2_NAME_LEN + 16];
	blk_t		pblk;
	
	errcode = ext2fs_bmap(fs, ino, inode, buf, 0, blk, &pblk);
	if (errcode) {
		com_err("htree_dump_leaf_node", errcode,
			"while mapping logical block %d\n", blk);
		return;
	}

	errcode = io_channel_read_blk(current_fs->io, pblk, 1, buf);
	if (errcode) {
		com_err("htree_dump_leaf_node", errcode,
			"while 	reading block %d\n", blk);
		return;
	}

	while (offset < fs->blocksize) {
		dirent = (struct ext2_dir_entry *) (buf + offset);
		if (((offset + dirent->rec_len) > fs->blocksize) ||
		    (dirent->rec_len < 8) ||
		    ((dirent->rec_len % 4) != 0) ||
		    (((dirent->name_len & 0xFF)+8) > dirent->rec_len)) {
			fprintf(pager, "Corrupted directory block (%d)!\n", blk);
			break;
		}
		thislen = ((dirent->name_len & 0xFF) < EXT2_NAME_LEN) ?
			(dirent->name_len & 0xFF) : EXT2_NAME_LEN;
		strncpy(name, dirent->name, thislen);
		name[thislen] = '\0';
		sprintf(tmp, "%u 0x%08x (%d) %s   ", dirent->inode,
		       dx_hash(name, thislen), dirent->rec_len, name);
		thislen = strlen(tmp);
		if (col + thislen > 80) {
			fprintf(pager, "\n");
			col = 0;
		}
		fprintf(pager, "%s", tmp);
		col += thislen;
		offset += dirent->rec_len;
	}
	fprintf(pager, "\n");
}


static void htree_dump_int_block(ext2_filsys fs, ext2_ino_t ino,
				 struct ext2_inode *inode,
				 blk_t blk, char *buf, int level);


static void htree_dump_int_node(ext2_filsys fs, ext2_ino_t ino,
				struct ext2_inode *inode,
				struct ext2_dx_entry *ent, 
				char *buf, int level)
{
	struct		ext2_dx_countlimit *limit;
	int		i;

	limit = (struct ext2_dx_countlimit *) ent;

	fprintf(pager, "Number of entries (count): %d\n", limit->count);
	fprintf(pager, "Number of entries (limit): %d\n", limit->limit);

	for (i=0; i < limit->count; i++)
		fprintf(pager, "Entry #%d: Hash 0x%08x, block %d\n", i,
		       i ? ent[i].hash : 0, ent[i].block);

	fprintf(pager, "\n");

	for (i=0; i < limit->count; i++) {
		fprintf(pager, "Entry #%d: Hash 0x%08x, block %d\n", i,
		       i ? ent[i].hash : 0, ent[i].block);
		if (level)
			htree_dump_int_block(fs, ino, inode,
					     ent[i].block, buf, level-1);
		else
			htree_dump_leaf_node(fs, ino, inode,
					     ent[i].block, buf);
	}

	fprintf(pager, "---------------------\n");
}

static void htree_dump_int_block(ext2_filsys fs, ext2_ino_t ino,
				 struct ext2_inode *inode,
				 blk_t blk, char *buf, int level)
{
	char		*cbuf;
	errcode_t	errcode;
	blk_t		pblk;

	cbuf = malloc(fs->blocksize);
	if (!cbuf) {
		fprintf(pager, "Couldn't allocate child block.\n");
		return;
	}
	
	errcode = ext2fs_bmap(fs, ino, inode, buf, 0, blk, &pblk);
	if (errcode) {
		com_err("htree_dump_int_block", errcode,
			"while mapping logical block %d\n", blk);
		return;
	}

	errcode = io_channel_read_blk(current_fs->io, pblk, 1, buf);
	if (errcode) {
		com_err("htree_dump_int_block", errcode,
			"while 	reading block %d\n", blk);
		return;
	}

	htree_dump_int_node(fs, ino, inode, (struct ext2_dx_entry *) (buf+8),
			    cbuf, level);
	free(cbuf);
}



void do_htree_dump(int argc, char *argv[])
{
	ext2_ino_t	ino;
	struct ext2_inode inode;
	int		retval;
	int		i, c;
	int		flags;
	int		long_opt;
	void		*buf = NULL;
	struct 		ext2_dx_root_info  *root;
	struct 		ext2_dx_entry *ent;
	struct		ext2_dx_countlimit *limit;
	errcode_t	errcode;

	if (check_fs_open(argv[0]))
		return;

	pager = open_pager();

	optind = 0;
#ifdef HAVE_OPTRESET
	optreset = 1;		/* Makes BSD getopt happy */
#endif
	while ((c = getopt (argc, argv, "l")) != EOF) {
		switch (c) {
		case 'l':
			long_opt++;
			break;
		}
	}

	if (argc > optind+1) {
		com_err(0, 0, "Usage: htree_dump [-l] file");
		return;
	}

	if (argc == optind)
		ino = cwd;
	else
		ino = string_to_inode(argv[optind]);
	if (!ino)
		return;

	if (debugfs_read_inode(ino, &inode, argv[1]))
		return;

	if (!LINUX_S_ISDIR(inode.i_mode)) {
		com_err(argv[0], 0, "Not a directory");
		return;
	}
	
	if ((inode.i_flags & EXT2_BTREE_FL) == 0) {
		com_err(argv[0], 0, "Not a hash-indexed directory");
		return;
	}

	buf = malloc(2*current_fs->blocksize);
	if (!buf) {
		com_err(argv[0], 0, "Couldn't allocate htree buffer");
		return;
	}

	errcode = io_channel_read_blk(current_fs->io, inode.i_block[0], 
				      1, buf);
	if (errcode) {
		com_err(argv[0], errcode, "Error reading root node");
		goto errout;
	}

	root = (struct ext2_dx_root_info *) (buf + 24);

	fprintf(pager, "Root node dump:\n");
	fprintf(pager, "\t Reserved zero: %d\n", root->reserved_zero);
	fprintf(pager, "\t Hash Version: %d\n", root->hash_version);
	fprintf(pager, "\t Info length: %d\n", root->info_length);
	fprintf(pager, "\t Indirect levels: %d\n", root->indirect_levels);
	fprintf(pager, "\t Flags: %d\n", root->unused_flags);

	ent = (struct ext2_dx_entry *) (buf + 24 + root->info_length);
	limit = (struct ext2_dx_countlimit *) ent;

	htree_dump_int_node(current_fs, ino, &inode, ent,
			    buf + current_fs->blocksize,
			    root->indirect_levels);

errout:
	if (buf)
		free(buf);
	close_pager(pager);
}

/*
 * This function prints the hash of a given file.
 */
void do_dx_hash(int argc, char *argv[])
{
	if (argc != 2) {
		com_err(argv[0], 0, "usage: dx_hash filename");
		return;
	}
	printf("Hash of %s is 0x%0x\n", argv[1],
	       dx_hash(argv[1], strlen(argv[1])));
}

/*
 * Search for particular directory entry (useful for debugging very
 * large hash tree directories that have lost some blocks from the
 * btree index).
 */
struct process_block_struct {
	char	*search_name;
	char	*buf;
	int	len;
};

static int search_dir_block(ext2_filsys fs, blk_t *blocknr,
			    e2_blkcnt_t blockcnt, blk_t ref_blk, 
			    int ref_offset, void *priv_data);

void do_dirsearch(int argc, char *argv[])
{
	ext2_ino_t	inode;
	int		retval;
	int		c;
	int		flags;
	struct process_block_struct pb;
	
	if (check_fs_open(argv[0]))
		return;

	if (argc != 3) {
		com_err(0, 0, "Usage: dirsearch dir filename");
		return;
	}

	inode = string_to_inode(argv[1]);
	if (!inode)
		return;

	pb.buf = malloc(current_fs->blocksize);
	if (!pb.buf) {
		com_err("dirsearch", 0, "Couldn't allocate buffer");
		return;
	}
	pb.search_name = argv[2];
	pb.len = strlen(pb.search_name);
	
	ext2fs_block_iterate2(current_fs, inode, 0, 0, search_dir_block, &pb);

	free(pb.buf);
}


static int search_dir_block(ext2_filsys fs, blk_t *blocknr,
			    e2_blkcnt_t blockcnt, blk_t ref_blk, 
			    int ref_offset, void *priv_data)
{
	struct process_block_struct *p;
	struct ext2_dir_entry *dirent;
	errcode_t	       	errcode;
	int			offset = 0;

	if (blockcnt < 0)
		return 0;

	p = (struct process_block_struct *) priv_data;

	errcode = io_channel_read_blk(current_fs->io, *blocknr, 1, p->buf);
	if (errcode) {
		com_err("search_dir_block", errcode,
			"while reading block %lu", *blocknr);
		return BLOCK_ABORT;
	}

	while (offset < fs->blocksize) {
		dirent = (struct ext2_dir_entry *) (p->buf + offset);

		if (dirent->inode &&
		    p->len == (dirent->name_len & 0xFF) && 
		    strncmp(p->search_name, dirent->name,
			    p->len) == 0) {
			printf("Entry found at logical block %lld, "
			       "phys %d, offset %d\n", blockcnt,
			       *blocknr, offset);
			printf("offset %d\n", offset);
			return BLOCK_ABORT;
		}
		offset += dirent->rec_len;
	}
	return 0;
}

