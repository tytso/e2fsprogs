/*
 * e2fsck.h
 * 
 * Copyright (C) 1993, 1994 Theodore Ts'o.  This file may be
 * redistributed under the terms of the GNU Public License.
 * 
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>

#include <linux/fs.h>
#include <linux/ext2_fs.h>

#include "ext2fs/ext2fs.h"

#ifdef __STDC__
#define NOARGS void
#else
#define NOARGS
#define const
#endif

/*
 * Exit codes used by fsck-type programs
 */
#define FSCK_OK          0	/* No errors */
#define FSCK_NONDESTRUCT 1	/* File system errors corrected */
#define FSCK_REBOOT      2	/* System should be rebooted */
#define FSCK_UNCORRECTED 4	/* File system errors left uncorrected */
#define FSCK_ERROR       8	/* Operational error */
#define FSCK_USAGE       16	/* Usage or syntax error */
#define FSCK_LIBRARY     128	/* Shared library error */

/*
 * Inode count arrays
 */
extern unsigned short * inode_count;
extern unsigned short * inode_link_info;

/*
 * The directory information structure; stores directory information
 * collected in earlier passes, to avoid disk i/o in fetching the
 * directoryt information.
 */
struct dir_info {
	ino_t			ino;	/* Inode number */
	ino_t			dotdot;	/* Parent according to '..' */
	ino_t			parent; /* Parent according to treewalk */
};

struct dir_block_struct {
	ino_t	ino;
	blk_t	blk;
	int	blockcnt;
};

struct dir_block_struct *dir_blocks;
int	dir_block_count;
int	dir_block_size;

/*
 * This structure is used for keeping track of how much resources have
 * been used for a particular pass of e2fsck.
 */
struct resource_track {
	struct timeval time_start;
	struct timeval user_start;
	struct timeval system_start;
	void	*brk_start;
};

/*
 * Variables
 */
extern const char * program_name;
extern const char * device_name;

extern char * inode_used_map;	/* Inodes which are in use */
extern char * inode_bad_map;	/* Inodes which are bad in some way */
extern char * inode_dir_map;	/* Inodes which are directories */

extern char * block_found_map;	/* Blocks which are used by an inode */
extern char * block_dup_map;	/* Blocks which are used by more than once */

extern const char *fix_msg[2];	/* Fixed or ignored! */
extern const char *clear_msg[2]; /* Cleared or ignored! */

/* Command line options */
extern int nflag;
extern int yflag;
extern int tflag;
extern int preen;
extern int verbose;
extern int list;
extern int debug;
extern int force;

extern int rwflag;

extern int inode_buffer_blocks;
extern int process_inode_size;
extern int directory_blocks;

extern int no_bad_inode;
extern int no_lpf;
extern int lpf_corrupted;

/* Files counts */
extern int fs_directory_count;
extern int fs_regular_count;
extern int fs_blockdev_count;
extern int fs_chardev_count;
extern int fs_links_count;
extern int fs_symlinks_count;
extern int fs_fast_symlinks_count;
extern int fs_fifo_count;
extern int fs_total_count;
extern int fs_badblocks_count;
extern int fs_sockets_count;

extern struct resource_track	global_rtrack;

/*
 * Procedure declarations
 */

extern void pass1(ext2_filsys fs);
extern void pass1_dupblocks(ext2_filsys fs, char *block_buf);
extern void pass2(ext2_filsys fs);
extern void pass3(ext2_filsys fs);
extern void pass4(ext2_filsys fs);
extern void pass5(ext2_filsys fs);

/* badblock.c */
extern void read_bad_blocks_file(ext2_filsys fs, const char *bad_blocks_file,
				 int replace_bad_blocks);
extern void test_disk(ext2_filsys fs);

/* dirinfo.c */
extern void add_dir_info(ext2_filsys fs, ino_t ino, ino_t parent,
		       struct ext2_inode *inode);
extern struct dir_info *get_dir_info(ino_t ino);
extern void free_dir_info(ext2_filsys fs);
extern int get_num_dirs(ext2_filsys fs);

/* ehandler.c */
extern const char *ehandler_operation(const char *op);
extern void ehandler_init(io_channel channel);

/* util.c */
extern void *allocate_memory(int size, const char *description);
extern int ask(const char * string, int def);
extern int ask_yn(const char * string, int def);
extern void fatal_error (const char * fmt_string);
extern void read_bitmaps(ext2_filsys fs);
extern void write_bitmaps(ext2_filsys fs);
extern void preenhalt(NOARGS);
extern void print_resource_track(struct resource_track *track);
extern void init_resource_track(struct resource_track *track);
extern int inode_has_valid_blocks(struct ext2_inode *inode);
#ifdef MTRACE
extern void mtrace_print(char *mesg);
#endif

#define die(str)	fatal_error(str)

/*
 * pass3.c
 */
extern int reconnect_file(ext2_filsys fs, ino_t inode);
