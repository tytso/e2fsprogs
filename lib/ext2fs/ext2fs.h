/*
 * ext2fs.h --- ext2fs
 * 
 * Copyright (C) 1993 Theodore Ts'o.  This file may be redistributed
 * under the terms of the GNU Public License.
 */

/*
 * Where the master copy of the superblock is located, and how big
 * superblocks are supposed to be.  We define SUPERBLOCK_SIZE because
 * the size of the superblock structure is not necessarily trustworthy
 * (some versions have the padding set up so that the superblock is
 * 1032 bytes long).
 */
#define SUPERBLOCK_OFFSET	1024
#define SUPERBLOCK_SIZE 	1024

typedef unsigned long	blk_t;
typedef unsigned int	dgrp_t;

#include "et/com_err.h"
#include "ext2fs/io.h"
#include "ext2fs/ext2_err.h"

/*
 * Flags for the ext2_filsys structure
 */

#define EXT2_FLAG_RW		0x01
#define EXT2_FLAG_CHANGED	0x02
#define EXT2_FLAG_DIRTY		0x04
#define EXT2_FLAG_VALID		0x08
#define EXT2_FLAG_IB_DIRTY	0x10
#define EXT2_FLAG_BB_DIRTY	0x20

typedef struct struct_ext2_filsys *ext2_filsys;

struct struct_ext2_filsys {
	io_channel			io;
	int				flags;
	char *				device_name;
	struct ext2_super_block	* 	super;
	int				blocksize;
	int				fragsize;
	unsigned long			group_desc_count;
	unsigned long			desc_blocks;
	struct ext2_group_desc *	group_desc;
	int				inode_blocks_per_group;
	char *				inode_map;
	char *				block_map;
	errcode_t (*get_blocks)(ext2_filsys fs, ino_t ino, blk_t *blocks);
	errcode_t (*check_directory)(ext2_filsys fs, ino_t ino);
	errcode_t (*write_bitmaps)(ext2_filsys fs);

	/*
	 * Not used by ext2fs library; reserved for the use of the
	 * calling application.
	 */
	void *				private; 
};

/*
 * badblocks list definitions
 */

typedef struct struct_badblocks_list *badblocks_list;

struct struct_badblocks_list {
	int	num;
	int	size;
	blk_t	*list;
	int	badblocks_flags;
};

#define BADBLOCKS_FLAG_DIRTY	1

typedef struct struct_badblocks_iterate *badblocks_iterate;

struct struct_badblocks_iterate {
	badblocks_list	bb;
	int		ptr;
};
	
#include "ext2fs/bitops.h"

/*
 * Return flags for the block iterator functions
 */
#define BLOCK_CHANGED	1
#define BLOCK_ABORT	2
#define BLOCK_ERROR	4

/*
 * Block interate flags
 */
#define BLOCK_FLAG_APPEND	1
#define BLOCK_FLAG_DEPTH_TRAVERSE	2

/*
 * Return flags for the directory iterator functions
 */
#define DIRENT_CHANGED	1
#define DIRENT_ABORT	2
#define DIRENT_ERROR	3

/*
 * Directory iterator flags
 */

#define DIRENT_FLAG_INCLUDE_EMPTY	1

/*
 * Inode scan definitions
 */
struct ext2_struct_inode_scan {
	ext2_filsys		fs;
	ino_t			current_inode;
	blk_t			current_block;
	dgrp_t			current_group;
	int			inodes_left, blocks_left, groups_left;
	int			inode_buffer_blocks;
	char *			inode_buffer;
	struct ext2_inode *	inode_scan_ptr;
};

typedef struct ext2_struct_inode_scan *ext2_inode_scan;

/*
 * function prototypes
 */

/* alloc.c */
extern errcode_t ext2fs_new_inode(ext2_filsys fs, ino_t dir, int mode,
				  char *map, ino_t *ret);
extern errcode_t ext2fs_new_block(ext2_filsys fs, blk_t goal,
				  char *map, blk_t *ret);
extern errcode_t ext2fs_get_free_blocks(ext2_filsys fs, blk_t start,
					blk_t finish, int num, char *map,
					blk_t *ret);

/* badblocks.c */
extern errcode_t badblocks_list_create(badblocks_list *ret, int size);
extern void badblocks_list_free(badblocks_list bb);
extern errcode_t badblocks_list_add(badblocks_list bb, blk_t blk);
extern int badblocks_list_test(badblocks_list bb, blk_t blk);
extern errcode_t badblocks_list_iterate_begin(badblocks_list bb,
					      badblocks_iterate *ret);
extern int badblocks_list_iterate(badblocks_iterate iter, blk_t *blk);
extern void badblocks_list_iterate_end(badblocks_iterate iter);

/* bb_inode.c */
extern errcode_t ext2fs_update_bb_inode(ext2_filsys fs,
					badblocks_list bb_list);

/* bitmaps.c */
extern errcode_t ext2fs_write_inode_bitmap(ext2_filsys fs);
extern errcode_t ext2fs_write_block_bitmap (ext2_filsys fs);
extern errcode_t ext2fs_read_inode_bitmap (ext2_filsys fs);
extern errcode_t ext2fs_read_block_bitmap(ext2_filsys fs);
extern errcode_t ext2fs_allocate_inode_bitmap(ext2_filsys fs, char **ret);
extern errcode_t ext2fs_allocate_block_bitmap(ext2_filsys fs, char **ret);
extern errcode_t ext2fs_read_bitmaps(ext2_filsys fs);
extern errcode_t ext2fs_write_bitmaps(ext2_filsys fs);

/* block.c */
extern errcode_t ext2fs_block_iterate(ext2_filsys fs,
				      ino_t	ino,
				      int	flags,
				      char *block_buf,
				      int (*func)(ext2_filsys fs,
						  blk_t	*blocknr,
						  int	blockcnt,
						  void	*private),
				      void *private);

/* closefs.c */
extern errcode_t ext2fs_close(ext2_filsys fs);
extern errcode_t ext2fs_flush(ext2_filsys fs);

/* expanddir.c */
extern errcode_t ext2fs_expand_dir(ext2_filsys fs, ino_t dir);

/* freefs.c */
extern void ext2fs_free(ext2_filsys fs);

/* initialize.c */
extern errcode_t ext2fs_initialize(const char *name, int flags,
				   struct ext2_super_block *param,
				   io_manager manager, ext2_filsys *ret_fs);

/* inode.c */
extern errcode_t ext2fs_open_inode_scan(ext2_filsys fs, int buffer_blocks,
				  ext2_inode_scan *ret_scan);
extern void ext2fs_close_inode_scan(ext2_inode_scan scan);
extern errcode_t ext2fs_get_next_inode(ext2_inode_scan scan, ino_t *ino,
			       struct ext2_inode *inode);
extern errcode_t ext2fs_read_inode (ext2_filsys fs, unsigned long ino,
			    struct ext2_inode * inode);
extern errcode_t ext2fs_write_inode(ext2_filsys fs, unsigned long ino,
			    struct ext2_inode * inode);
extern errcode_t ext2fs_get_blocks(ext2_filsys fs, ino_t ino, blk_t *blocks);
extern errcode_t ext2fs_check_directory(ext2_filsys fs, ino_t ino);

/* namei.c */
extern errcode_t ext2fs_dir_iterate(ext2_filsys fs, 
			      ino_t dir,
			      int flags,
			      char *block_buf,
			      int (*func)(struct ext2_dir_entry *dirent,
					  int	offset,
					  int	blocksize,
					  char	*buf,
					  void	*private),
			      void *private);
extern errcode_t ext2fs_lookup(ext2_filsys fs, ino_t dir, const char *name,
			 int namelen, char *buf, ino_t *inode);
extern errcode_t ext2fs_namei(ext2_filsys fs, ino_t root, ino_t cwd,
			const char *name, ino_t *inode);

/* newdir.c */
extern errcode_t ext2fs_new_dir_block(ext2_filsys fs, ino_t dir_ino,
				ino_t parent_ino, char **block);

/* mkdir.c */
extern errcode_t ext2fs_mkdir(ext2_filsys fs, ino_t parent, ino_t inum,
			      const char *name);

/* openfs.c */
extern errcode_t ext2fs_open(const char *name, int flags, int superblock,
			     int block_size, io_manager manager,
			     ext2_filsys *ret_fs);
extern errcode_t ext2fs_check_desc(ext2_filsys fs);

/* get_pathname.c */
extern errcode_t ext2fs_get_pathname(ext2_filsys fs, ino_t dir, ino_t ino,
			       char **name);

/* link.c */
errcode_t ext2fs_link(ext2_filsys fs, ino_t dir, const char *name,
		      ino_t ino, int flags);
errcode_t ext2fs_unlink(ext2_filsys fs, ino_t dir, const char *name,
			ino_t ino, int flags);

/* read_bb.c */
extern errcode_t ext2fs_read_bb_inode(ext2_filsys fs, badblocks_list *bb_list);

/* read_bb_file.c */
extern errcode_t ext2fs_read_bb_FILE(ext2_filsys fs, FILE *f, 
				     badblocks_list *bb_list,
				     void (*invalid)(ext2_filsys fs,
						     blk_t blk));

/* inline functions */
extern void ext2fs_mark_super_dirty(ext2_filsys fs);
extern void ext2fs_mark_changed(ext2_filsys fs);
extern int ext2fs_test_changed(ext2_filsys fs);
extern void ext2fs_mark_valid(ext2_filsys fs);
extern void ext2fs_unmark_valid(ext2_filsys fs);
extern int ext2fs_test_valid(ext2_filsys fs);
extern void ext2fs_mark_ib_dirty(ext2_filsys fs);
extern void ext2fs_mark_bb_dirty(ext2_filsys fs);
extern int ext2fs_test_ib_dirty(ext2_filsys fs);
extern int ext2fs_test_bb_dirty(ext2_filsys fs);
extern int ext2fs_group_of_blk(ext2_filsys fs, blk_t blk);
extern int ext2fs_group_of_ino(ext2_filsys fs, ino_t ino);

/*
 * The actual inlined functions definitions themselves...
 *
 * If NO_INLINE_FUNCS is defined, then we won't try to do inline
 * functions at all!
 */
#if (defined(INCLUDE_INLINE_FUNCS) || !defined(NO_INLINE_FUNCS))
#ifdef INCLUDE_INLINE_FUNCS
#define _INLINE_ extern
#else
#define _INLINE_ extern __inline__
#endif

/*
 * Mark a filesystem superblock as dirty
 */
_INLINE_ void ext2fs_mark_super_dirty(ext2_filsys fs)
{
	fs->flags |= EXT2_FLAG_DIRTY | EXT2_FLAG_CHANGED;
}

/*
 * Mark a filesystem as changed
 */
_INLINE_ void ext2fs_mark_changed(ext2_filsys fs)
{
	fs->flags |= EXT2_FLAG_CHANGED;
}

/*
 * Check to see if a filesystem has changed
 */
_INLINE_ int ext2fs_test_changed(ext2_filsys fs)
{
	return (fs->flags & EXT2_FLAG_CHANGED);
}

/*
 * Mark a filesystem as valid
 */
_INLINE_ void ext2fs_mark_valid(ext2_filsys fs)
{
	fs->flags |= EXT2_FLAG_VALID;
}

/*
 * Mark a filesystem as NOT valid
 */
_INLINE_ void ext2fs_unmark_valid(ext2_filsys fs)
{
	fs->flags &= ~EXT2_FLAG_VALID;
}

/*
 * Check to see if a filesystem is valid
 */
_INLINE_ int ext2fs_test_valid(ext2_filsys fs)
{
	return (fs->flags & EXT2_FLAG_VALID);
}

/*
 * Mark the inode bitmap as dirty
 */
_INLINE_ void ext2fs_mark_ib_dirty(ext2_filsys fs)
{
	fs->flags |= EXT2_FLAG_IB_DIRTY | EXT2_FLAG_CHANGED;
}

/*
 * Mark the block bitmap as dirty
 */
_INLINE_ void ext2fs_mark_bb_dirty(ext2_filsys fs)
{
	fs->flags |= EXT2_FLAG_BB_DIRTY | EXT2_FLAG_CHANGED;
}

/*
 * Check to see if a filesystem's inode bitmap is dirty
 */
_INLINE_ int ext2fs_test_ib_dirty(ext2_filsys fs)
{
	return (fs->flags & EXT2_FLAG_IB_DIRTY);
}

/*
 * Check to see if a filesystem's block bitmap is dirty
 */
_INLINE_ int ext2fs_test_bb_dirty(ext2_filsys fs)
{
	return (fs->flags & EXT2_FLAG_BB_DIRTY);
}

/*
 * Return the group # of a block
 */
_INLINE_ int ext2fs_group_of_blk(ext2_filsys fs, blk_t blk)
{
	return (blk - fs->super->s_first_data_block) /
		fs->super->s_blocks_per_group;
}

/*
 * Return the group # of an inode number
 */
_INLINE_ int ext2fs_group_of_ino(ext2_filsys fs, ino_t ino)
{
	return (ino - 1) / fs->super->s_inodes_per_group;
}
#undef _INLINE_
#endif

