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
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#ifdef HAVE_SETJMP_H
#include <setjmp.h>
#endif

#ifdef HAVE_LINUX_FS_H
#include <linux/fs.h>
#endif
#include <linux/ext2_fs.h>

#include "ext2fs/ext2fs.h"

/* Everything is STDC, these days */
#define NOARGS void

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
 * The last ext2fs revision level that this version of e2fsck is able to
 * support
 */
#define E2FSCK_CURRENT_REV	1

/*
 * The directory information structure; stores directory information
 * collected in earlier passes, to avoid disk i/o in fetching the
 * directory information.
 */
struct dir_info {
	ino_t			ino;	/* Inode number */
	ino_t			dotdot;	/* Parent according to '..' */
	ino_t			parent; /* Parent according to treewalk */
};

#ifdef RESOURCE_TRACK
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
#endif

/*
 * E2fsck options
 */
#define E2F_OPT_READONLY	0x0001
#define E2F_OPT_PREEN		0x0002
#define E2F_OPT_YES		0x0004
#define E2F_OPT_NO		0x0008
#define E2F_OPT_TIME		0x0010
#define E2F_OPT_TIME2		0x0020
#define E2F_OPT_CHECKBLOCKS	0x0040
#define E2F_OPT_DEBUG		0x0080

/*
 * E2fsck flags
 */
#define E2F_FLAG_ABORT		0x0001 /* Abort signaled */
#define E2F_FLAG_CANCEL		0x0002 /* Cancel signaled */
#define E2F_FLAG_RESTART	0x0004 /* Restart signaled */
#define E2F_FLAG_SIGNAL_MASK	0x000F

#define E2F_FLAG_SETJMP_OK	0x0010 /* Setjmp valid for abort */

/*
 * Defines for indicating the e2fsck pass number
 */
#define E2F_PASS_1	1
#define E2F_PASS_2	2
#define E2F_PASS_3	3
#define E2F_PASS_4	4
#define E2F_PASS_5	5
#define E2F_PASS_1B	6

/*
 * This is the global e2fsck structure.
 */
typedef struct e2fsck_struct *e2fsck_t;

struct e2fsck_struct {
	ext2_filsys fs;
	const char *program_name;
	const char *filesystem_name;
	const char *device_name;
	int	flags;		/* E2fsck internal flags */
	int	options;
	blk_t	use_superblock;	/* sb requested by user */
	blk_t	superblock;	/* sb used to open fs */
	blk_t	num_blocks;	/* Total number of blocks */

#ifdef HAVE_SETJMP_H
	jmp_buf	abort_loc;
#endif
	unsigned long abort_code;

	void (*progress)(e2fsck_t ctx, int pass, unsigned long cur,
			 unsigned long max);

	ext2fs_inode_bitmap inode_used_map; /* Inodes which are in use */
	ext2fs_inode_bitmap inode_bad_map; /* Inodes which are bad somehow */
	ext2fs_inode_bitmap inode_dir_map; /* Inodes which are directories */
	ext2fs_inode_bitmap inode_bb_map; /* Inodes which are in bad blocks */

	ext2fs_block_bitmap block_found_map; /* Blocks which are in use */
	ext2fs_block_bitmap block_dup_map; /* Blks referenced more than once */
	ext2fs_block_bitmap block_illegal_map; /* Meta-data blocks */

	/*
	 * Inode count arrays
	 */
	ext2_icount_t	inode_count;
	ext2_icount_t inode_link_info;

	/*
	 * Array of flags indicating whether an inode bitmap, block
	 * bitmap, or inode table is invalid
	 */
	int *invalid_inode_bitmap_flag;
	int *invalid_block_bitmap_flag;
	int *invalid_inode_table_flag;
	int invalid_bitmaps;	/* There are invalid bitmaps/itable */

	/*
	 * Block buffer
	 */
	char *block_buf;

	/*
	 * For pass1_check_directory and pass1_get_blocks
	 */
	ino_t stashed_ino;
	struct ext2_inode *stashed_inode;

	/*
	 * Directory information
	 */
	int		dir_info_count;
	int		dir_info_size;
	struct dir_info	*dir_info;

	/*
	 * Tuning parameters
	 */
	int process_inode_size;
	int inode_buffer_blocks;

#ifdef RESOURCE_TRACK
	/*
	 * For timing purposes
	 */
	struct resource_track	global_rtrack;
#endif

	/* File counts */
	int fs_directory_count;
	int fs_regular_count;
	int fs_blockdev_count;
	int fs_chardev_count;
	int fs_links_count;
	int fs_symlinks_count;
	int fs_fast_symlinks_count;
	int fs_fifo_count;
	int fs_total_count;
	int fs_badblocks_count;
	int fs_sockets_count;
	int fs_ind_count;
	int fs_dind_count;
	int fs_tind_count;
	int fs_fragmented;
};


/*
 * Procedure declarations
 */

extern void e2fsck_pass1(e2fsck_t ctx);
extern void e2fsck_pass1_dupblocks(e2fsck_t ctx, char *block_buf);
extern void e2fsck_pass2(e2fsck_t ctx);
extern void e2fsck_pass3(e2fsck_t ctx);
extern void e2fsck_pass4(e2fsck_t ctx);
extern void e2fsck_pass5(e2fsck_t ctx);

/* e2fsck.c */
extern errcode_t e2fsck_allocate_context(e2fsck_t *ret);
extern errcode_t e2fsck_reset_context(e2fsck_t ctx);
extern void e2fsck_free_context(e2fsck_t ctx);
extern int e2fsck_run(e2fsck_t ctx);


/* pass1.c */
extern errcode_t pass1_check_directory(ext2_filsys fs, ino_t ino);
extern errcode_t pass1_get_blocks(ext2_filsys fs, ino_t ino, blk_t *blocks);
extern errcode_t pass1_read_inode(ext2_filsys fs, ino_t ino,
				  struct ext2_inode *inode);
extern errcode_t pass1_write_inode(ext2_filsys fs, ino_t ino,
				   struct ext2_inode *inode);
extern int e2fsck_pass1_check_device_inode(struct ext2_inode *inode);

/* badblock.c */
extern void read_bad_blocks_file(e2fsck_t ctx, const char *bad_blocks_file,
				 int replace_bad_blocks);
extern void test_disk(e2fsck_t ctx);

/* dirinfo.c */
extern void e2fsck_add_dir_info(e2fsck_t ctx, ino_t ino, ino_t parent);
extern struct dir_info *e2fsck_get_dir_info(e2fsck_t ctx, ino_t ino);
extern void e2fsck_free_dir_info(e2fsck_t ctx);
extern int e2fsck_get_num_dirs(e2fsck_t ctx);
extern int e2fsck_get_num_dirinfo(e2fsck_t ctx);
extern struct dir_info *e2fsck_dir_info_iter(e2fsck_t ctx, int *control);

/* ehandler.c */
extern const char *ehandler_operation(const char *op);
extern void ehandler_init(io_channel channel);

/* super.c */
void check_super_block(e2fsck_t ctx);

/* swapfs.c */
void swap_filesys(e2fsck_t ctx);

/* util.c */
extern void *e2fsck_allocate_memory(e2fsck_t ctx, unsigned int size,
				    const char *description);
extern int ask(e2fsck_t ctx, const char * string, int def);
extern int ask_yn(const char * string, int def);
extern void fatal_error(e2fsck_t ctx, const char * fmt_string);
extern void e2fsck_read_bitmaps(e2fsck_t ctx);
extern void e2fsck_write_bitmaps(e2fsck_t ctx);
extern void preenhalt(e2fsck_t ctx);
#ifdef RESOURCE_TRACK
extern void print_resource_track(const char *desc,
				 struct resource_track *track);
extern void init_resource_track(struct resource_track *track);
#endif
extern int inode_has_valid_blocks(struct ext2_inode *inode);
extern void e2fsck_read_inode(e2fsck_t ctx, unsigned long ino,
			      struct ext2_inode * inode, const char * proc);
extern void e2fsck_write_inode(e2fsck_t ctx, unsigned long ino,
			       struct ext2_inode * inode, const char * proc);
#ifdef MTRACE
extern void mtrace_print(char *mesg);
#endif
extern blk_t get_backup_sb(ext2_filsys fs);

/*
 * pass3.c
 */
extern int e2fsck_reconnect_file(e2fsck_t ctx, ino_t inode);
