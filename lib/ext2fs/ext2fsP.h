/*
 * ext2fsP.h --- private header file for ext2 library
 *
 * Copyright (C) 1997 Theodore Ts'o.
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 */

#include "ext2fs.h"

/*
 * Badblocks list
 */
struct ext2_struct_u32_list {
	int	magic;
	int	num;
	int	size;
	__u32	*list;
	int	badblocks_flags;
};

struct ext2_struct_u32_iterate {
	int			magic;
	ext2_u32_list		bb;
	int			ptr;
};


/*
 * Directory block iterator definition
 */
struct ext2_struct_dblist {
	int			magic;
	ext2_filsys		fs;
	ext2_ino_t		size;
	ext2_ino_t		count;
	int			sorted;
	struct ext2_db_entry *	list;
};

/*
 * For directory iterators
 */
struct dir_context {
	ext2_ino_t		dir;
	int		flags;
	char		*buf;
	int (*func)(ext2_ino_t	dir,
		    int	entry,
		    struct ext2_dir_entry *dirent,
		    int	offset,
		    int	blocksize,
		    char	*buf,
		    void	*priv_data);
	void		*priv_data;
	errcode_t	errcode;
};

/*
 * Inode cache structure
 */
struct ext2_inode_cache {
	void *				buffer;
	blk_t				buffer_blk;
	int				cache_last;
	int				cache_size;
	int				refcount;
	struct ext2_inode_cache_ent	*cache;
};

struct ext2_inode_cache_ent {
	ext2_ino_t		ino;
	struct ext2_inode	inode;
};

/* Function prototypes */

extern int ext2fs_process_dir_block(ext2_filsys  	fs,
				    blk_t		*blocknr,
				    e2_blkcnt_t		blockcnt,
				    blk_t		ref_block,
				    int			ref_offset,
				    void		*priv_data);


/*
 * 64-bit bitmap support
 */

#define EXT2FS_BMAP64_BITARRAY	1

extern errcode_t ext2fs_alloc_generic_bmap(ext2_filsys fs, errcode_t magic,
					   int type, __u64 start, __u64 end,
					   __u64 real_end,
					   const char * description,
					   ext2fs_generic_bitmap *bmap);

extern void ext2fs_free_generic_bmap(ext2fs_generic_bitmap bmap);

extern errcode_t ext2fs_copy_generic_bmap(ext2fs_generic_bitmap src,
					  ext2fs_generic_bitmap *dest);

extern errcode_t ext2fs_resize_generic_bmap(ext2fs_generic_bitmap bmap,
					    __u64 new_end,
					    __u64 new_real_end);
extern errcode_t ext2fs_fudge_generic_bmap_end(ext2fs_generic_bitmap bitmap,
					       errcode_t neq,
					       __u64 end, __u64 *oend);
extern int ext2fs_mark_generic_bmap(ext2fs_generic_bitmap bitmap,
				    __u64 arg);
extern int ext2fs_unmark_generic_bmap(ext2fs_generic_bitmap bitmap,
				      __u64 arg);
extern int ext2fs_test_generic_bmap(ext2fs_generic_bitmap bitmap,
				    __u64 arg);
extern errcode_t ext2fs_set_generic_bmap_range(ext2fs_generic_bitmap bitmap,
					       __u64 start, unsigned int num,
					       void *in);
extern errcode_t ext2fs_get_generic_bmap_range(ext2fs_generic_bitmap bitmap,
					       __u64 start, unsigned int num,
					       void *out);
extern int ext2fs_mem_is_zero(const char *mem, size_t len);
