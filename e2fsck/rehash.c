/*
 * rehash.c --- rebuild hash tree directories
 * 
 * Copyright (C) 2002 Theodore Ts'o
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 * 
 * This algorithm is designed for simplicity of implementation and to
 * pack the directory as much as possible.  It however requires twice
 * as much memory as the size of the directory.  The maximum size
 * directory supported using a 4k blocksize is roughly a gigabyte, and
 * so there may very well be problems with machines that don't have
 * virtual memory, and obscenely large directories.
 *
 * An alternate algorithm which is much more disk intensive could be
 * written, and probably will need to be written in the future.  The
 * design goals of such an algorithm are: (a) use (roughly) constant
 * amounts of memory, no matter how large the directory, (b) the
 * directory must be safe at all times, even if e2fsck is interrupted
 * in the middle, (c) we must use minimal amounts of extra disk
 * blocks.  This pretty much requires an incremental approach, where
 * we are reading from one part of the directory, and inserting into
 * the front half.  So the algorithm will have to keep track of a
 * moving block boundary between the new tree and the old tree, and
 * files will need to be moved from the old directory and inserted
 * into the new tree.  If the new directory requires space which isn't
 * yet available, blocks from the beginning part of the old directory
 * may need to be moved to the end of the directory to make room for
 * the new tree:
 *
 *    --------------------------------------------------------
 *    |  new tree   |        | old tree                      |
 *    --------------------------------------------------------
 *                  ^ ptr    ^ptr
 *                tail new   head old
 * 
 * This is going to be a pain in the tuckus to implement, and will
 * require a lot more disk accesses.  So I'm going to skip it for now;
 * it's only really going to be an issue for really, really big
 * filesystems (when we reach the level of tens of millions of files
 * in a single directory).  It will probably be easier to simply
 * require that e2fsck use VM first.
 */

#include <errno.h>
#include "e2fsck.h"
#include "problem.h"

struct fill_dir_struct {
	char *buf;
	struct ext2_inode *inode;
	int err;
	e2fsck_t ctx;
	struct hash_entry *harray;
	int max_array, num_array;
	int dir_size;
	ino_t parent;
};

struct hash_entry {
	ext2_dirhash_t	hash;
	ext2_dirhash_t	minor_hash;
	struct ext2_dir_entry	*dir;
};

struct out_dir {
	int		num;
	int		max;
	char		*buf;
	ext2_dirhash_t	*hashes;
};

static int fill_dir_block(ext2_filsys fs,
			  blk_t	*block_nr,
			  e2_blkcnt_t blockcnt,
			  blk_t ref_block,
			  int ref_offset, 
			  void *priv_data)
{
	struct fill_dir_struct	*fd = (struct fill_dir_struct *) priv_data;
	struct hash_entry 	*new_array, *ent;
	struct ext2_dir_entry 	*dirent;
	char			*dir;
	int			offset, dir_offset;
	
	if (blockcnt < 0)
		return 0;

	offset = blockcnt * fs->blocksize;
	if (offset + fs->blocksize > fd->inode->i_size) {
		fd->err = EXT2_ET_DIR_CORRUPTED;
		return BLOCK_ABORT;
	}
	dir = (fd->buf+offset);
	if (HOLE_BLKADDR(*block_nr)) {
		memset(dir, 0, fs->blocksize);
		dirent = (struct ext2_dir_entry *) dir;
		dirent->rec_len = fs->blocksize;
	} else {
		fd->err = ext2fs_read_dir_block(fs, *block_nr, dir);
		if (fd->err)
			return BLOCK_ABORT;
	}
	/* While the directory block is "hot", index it. */
	dir_offset = 0;
	while (dir_offset < fs->blocksize) {
		dirent = (struct ext2_dir_entry *) (dir + dir_offset);
		if (((dir_offset + dirent->rec_len) > fs->blocksize) ||
		    (dirent->rec_len < 8) ||
		    ((dirent->rec_len % 4) != 0) ||
		    (((dirent->name_len & 0xFF)+8) > dirent->rec_len)) {
			fd->err = EXT2_ET_DIR_CORRUPTED;
			return BLOCK_ABORT;
		}
		if (dirent->inode == 0)
			goto next;
		if (((dirent->name_len&0xFF) == 1) && (dirent->name[0] == '.'))
			goto next;
		if (((dirent->name_len&0xFF) == 2) &&
		    (dirent->name[0] == '.') && (dirent->name[1] == '.')) {
			fd->parent = dirent->inode;
			goto next;
		}
		if (fd->num_array >= fd->max_array) {
			new_array = realloc(fd->harray,
			    sizeof(struct hash_entry) * (fd->max_array+500));
			if (!new_array) {
				fd->err = ENOMEM;
				return BLOCK_ABORT;
			}
			fd->harray = new_array;
			fd->max_array += 500;
		}
		ent = fd->harray + fd->num_array;
		ent->dir = dirent;
		fd->err = ext2fs_dirhash(fs->super->s_def_hash_version,
					 dirent->name,
					 dirent->name_len & 0xFF,
					 fs->super->s_hash_seed,
					 &ent->hash, &ent->minor_hash);
		if (fd->err)
			return BLOCK_ABORT;
		fd->num_array++;
		fd->dir_size += EXT2_DIR_REC_LEN(dirent->name_len & 0xFF);
	next:
		dir_offset += dirent->rec_len;
	}
	
	return 0;
}

/* Used for sorting the hash entry */
static EXT2_QSORT_TYPE hash_cmp(const void *a, const void *b)
{
	const struct hash_entry *he_a = (const struct hash_entry *) a;
	const struct hash_entry *he_b = (const struct hash_entry *) b;
	int	ret;
	
	if (he_a->hash > he_b->hash)
		ret = 1;
	else if (he_a->hash < he_b->hash)
		ret = -1;
	else {
		if (he_a->minor_hash > he_b->minor_hash)
			ret = 1;
		else if (he_a->minor_hash < he_b->minor_hash)
			ret = -1;
		else
			ret = 0;
	}
	return ret;
}

static errcode_t alloc_size_dir(ext2_filsys fs, struct out_dir *outdir, 
				int blocks)
{
	void			*new_mem;

	if (outdir->max) {
		new_mem = realloc(outdir->buf, blocks * fs->blocksize);
		if (!new_mem)
			return ENOMEM;
		outdir->buf = new_mem;
		new_mem = realloc(outdir->hashes,
				  blocks * sizeof(ext2_dirhash_t));
		if (!new_mem)
			return ENOMEM;
		outdir->hashes = new_mem;
	} else {
		outdir->buf = malloc(blocks * fs->blocksize);
		outdir->hashes = malloc(blocks * sizeof(ext2_dirhash_t));
		outdir->num = 0;
	}
	outdir->max = blocks;
	return 0;
}

static void free_out_dir(struct out_dir *outdir)
{
	free(outdir->buf);
	free(outdir->hashes);
	outdir->max = 0;
	outdir->num =0;
}

errcode_t get_next_block(ext2_filsys fs, struct out_dir *outdir,
			 char ** ret)
{
	errcode_t	retval;

	if (outdir->num >= outdir->max) {
		retval = alloc_size_dir(fs, outdir, outdir->max + 50);
		if (retval)
			return retval;
	}
	*ret = outdir->buf + (outdir->num++ * fs->blocksize);
	return 0;
}


struct ext2_dx_root_info *set_root_node(ext2_filsys fs, char *buf,
				    ext2_ino_t ino, ext2_ino_t parent)
{
	struct ext2_dir_entry 		*dir;
	struct ext2_dx_root_info  	*root;
	struct ext2_dx_countlimit	*limits;
	int				filetype;

	if (fs->super->s_feature_incompat & EXT2_FEATURE_INCOMPAT_FILETYPE)
		filetype = EXT2_FT_DIR << 8;
	
	memset(buf, 0, fs->blocksize);
	dir = (struct ext2_dir_entry *) buf;
	dir->inode = ino;
	dir->name[0] = '.';
	dir->name_len = 1 | filetype;
	dir->rec_len = 12;
	dir = (struct ext2_dir_entry *) (buf + 12);
	dir->inode = parent;
	dir->name[0] = '.';
	dir->name[1] = '.';
	dir->name_len = 2 | filetype;
	dir->rec_len = fs->blocksize - 12;
	
	root = (struct ext2_dx_root_info *) (buf+24);
	root->reserved_zero = 0;
	root->hash_version = fs->super->s_def_hash_version;
	root->info_length = 8;
	root->indirect_levels = 0;
	root->unused_flags = 0;

	limits = (struct ext2_dx_countlimit *) (buf+32);
	limits->limit = (fs->blocksize - 32) / sizeof(struct ext2_dx_entry);
	limits->count = 0;

	return root;
}


struct ext2_dx_entry *set_int_node(ext2_filsys fs, char *buf)
{
	struct ext2_dir_entry 		*dir;
	struct ext2_dx_countlimit	*limits;

	memset(buf, 0, fs->blocksize);
	dir = (struct ext2_dir_entry *) buf;
	dir->inode = 0;
	dir->rec_len = fs->blocksize;
	
	limits = (struct ext2_dx_countlimit *) (buf+8);
	limits->limit = (fs->blocksize - 8) / sizeof(struct ext2_dx_entry);
	limits->count = 0;

	return (struct ext2_dx_entry *) limits;
}


struct write_dir_struct {
	struct out_dir *outdir;
	errcode_t	err;
	e2fsck_t	ctx;
	int		cleared;
};

/*
 * This makes sure we have enough space to write out the modified
 * directory.
 */
static int write_dir_block(ext2_filsys fs,
			   blk_t	*block_nr,
			   e2_blkcnt_t blockcnt,
			   blk_t ref_block,
			   int ref_offset, 
			   void *priv_data)
{
	struct write_dir_struct	*wd = (struct write_dir_struct *) priv_data;
	blk_t	new_blk, blk;
	char	*dir;
	errcode_t	retval;

	if (*block_nr == 0)
		return 0;
	if (blockcnt >= wd->outdir->num) {
		e2fsck_read_bitmaps(wd->ctx);
		blk = *block_nr;
		ext2fs_unmark_block_bitmap(wd->ctx->block_found_map, blk);
		ext2fs_block_alloc_stats(fs, blk, -1);
		*block_nr = 0;
		wd->cleared++;
		return BLOCK_CHANGED;
	}
	if (blockcnt < 0)
		return 0;

	dir = wd->outdir->buf + (blockcnt * fs->blocksize);
	wd->err = ext2fs_write_dir_block(fs, *block_nr, dir);
	if (wd->err)
		return BLOCK_ABORT;
	return 0;
}


static errcode_t write_directory(e2fsck_t ctx, ext2_filsys fs,
				 struct out_dir *outdir, ext2_ino_t ino)
{
	struct write_dir_struct wd;
	errcode_t	retval;
	struct ext2_inode 	inode;

	retval = e2fsck_expand_directory(ctx, ino, -1, outdir->num);
	if (retval)
		return retval;

	wd.outdir = outdir;
	wd.err = 0;
	wd.ctx = ctx;
	wd.cleared = 0;

	retval = ext2fs_block_iterate2(fs, ino, 0, 0,
				       write_dir_block, &wd);
	if (retval)
		return retval;
	if (wd.err)
		return wd.err;

	e2fsck_read_inode(ctx, ino, &inode, "rehash_dir");
	inode.i_flags |= EXT2_INDEX_FL;
	inode.i_size = outdir->num * fs->blocksize;
	inode.i_blocks -= (fs->blocksize / 512) * wd.cleared;
	e2fsck_write_inode(ctx, ino, &inode, "rehash_dir");

	return 0;
}

errcode_t e2fsck_rehash_dir(e2fsck_t ctx, ext2_ino_t ino)
{
	ext2_filsys 		fs = ctx->fs;
	errcode_t		retval;
	struct ext2_inode 	inode;
	char			*dir_buf = 0, *block_start;
	struct hash_entry 	*ent;
	struct ext2_dir_entry	*dirent;
	struct fill_dir_struct	fd;
	int			i, rec_len, left, c1, c2, nblks;
	ext2_dirhash_t		prev_hash;
	int			offset;
	void			*new_mem;
	struct out_dir		outdir;
	struct ext2_dx_root_info  	*root_info;
	struct ext2_dx_entry 	*root, *dx_ent;
	struct ext2_dx_countlimit	*root_limit, *limit;
	
	e2fsck_read_inode(ctx, ino, &inode, "rehash_dir");
	if ((inode.i_size / fs->blocksize) < 3)
		return 0;

	retval = ENOMEM;
	fd.harray = 0;
	dir_buf = malloc(inode.i_size);
	if (!dir_buf)
		goto errout;

	fd.max_array = inode.i_size / 32;
	fd.num_array = 0;
	fd.harray = malloc(fd.max_array * sizeof(struct hash_entry));
	if (!fd.harray)
		goto errout;

	fd.ctx = ctx;
	fd.buf = dir_buf;
	fd.inode = &inode;
	fd.err = 0;
	fd.dir_size = 0;
	fd.parent = 0;

	/* Read in the entire directory into memory */
	retval = ext2fs_block_iterate2(fs, ino, 0, 0,
				       fill_dir_block, &fd);
	if (fd.err) {
		retval = fd.err;
		goto errout;
	}

#if 0
	printf("%d entries (%d bytes) found in inode %d\n",
	       fd.num_array, fd.dir_size, ino);
#endif

	/* Sort the list into hash order */
	qsort(fd.harray, fd.num_array, sizeof(struct hash_entry), hash_cmp);

	outdir.max = 0;
	retval = alloc_size_dir(fs, &outdir,
				(fd.dir_size / fs->blocksize) + 2);
	if (retval)
		goto errout;
	outdir.num = 1; offset = 0;
	outdir.hashes[0] = 0;
	prev_hash = 1;
	if ((retval = get_next_block(fs, &outdir, &block_start)))
		goto errout;
	for (i=0; i < fd.num_array; i++) {
		ent = fd.harray + i;
		rec_len = EXT2_DIR_REC_LEN(ent->dir->name_len & 0xFF);
		dirent = (struct ext2_dir_entry *) (block_start + offset);
		left = fs->blocksize - offset;
		if (rec_len > left) {
			if (left) {
				dirent->rec_len = left;
				dirent->inode = 0;
				dirent->name_len = 0;
				offset += left;
				left = 0;
			}
			if ((retval =  get_next_block(fs, &outdir,
						      &block_start)))
				goto errout;
			offset = 0; left = fs->blocksize;
			dirent = (struct ext2_dir_entry *) block_start;
		}
		if (offset == 0) {
			if (ent->hash == prev_hash)
				outdir.hashes[outdir.num-1] = ent->hash | 1;
			else
				outdir.hashes[outdir.num-1] = ent->hash;
		}
		dirent->inode = ent->dir->inode;
		dirent->name_len = ent->dir->name_len;
		dirent->rec_len = rec_len;
		memcpy(dirent->name, ent->dir->name, dirent->name_len & 0xFF);
		offset += rec_len;
		left -= rec_len;
		if (left < 12) {
			dirent->rec_len += left;
			offset += left;
		}
		prev_hash = ent->hash;
	}
	if (left)
		dirent->rec_len += left;
	free(dir_buf); dir_buf = 0;

	root_info = set_root_node(fs, outdir.buf, ino, fd.parent);
	root_limit = (struct ext2_dx_countlimit *)
		((char *)root_info + root_info->info_length);
	root = (struct ext2_dx_entry *) root_limit;
	c1 = root_limit->limit;
	nblks = outdir.num;

	/* Write out the pointer blocks */
	if (nblks-1 <= c1) {
		/* Just write out the root block, and we're done */
		for (i=1; i < nblks; i++) {
			root->block = ext2fs_cpu_to_le32(i);
			if (i != 1)
				root->hash =
					ext2fs_cpu_to_le32(outdir.hashes[i]);
			root++;
			c1--;
		}
	} else {
		c2 = 0;
		limit = 0;
		root_info->indirect_levels = 1;
		for (i=1; i < nblks; i++) {
			if (c1 == 0) {
				retval = ENOSPC;
				goto errout;
			}
			if (c2 == 0) {
				if (limit)
					limit->limit = limit->count = 
		ext2fs_cpu_to_le16(limit->limit);
				root->block = ext2fs_cpu_to_le32(outdir.num);
				if (i != 1)
					root->hash =
			ext2fs_cpu_to_le32(outdir.hashes[i]);
				if ((retval =  get_next_block(fs, &outdir,
							      &block_start)))
					goto errout;
				dx_ent = set_int_node(fs, block_start);
				limit = (struct ext2_dx_countlimit *) dx_ent;
				c2 = limit->limit;
				root++;
				c1--;
			}
			dx_ent->block = ext2fs_cpu_to_le32(i);
			if (c2 != limit->limit)
				dx_ent->hash =
					ext2fs_cpu_to_le32(outdir.hashes[i]);
			dx_ent++;
			c2--;
		}
		limit->count = ext2fs_cpu_to_le16(limit->limit - c2);
		limit->limit = ext2fs_cpu_to_le16(limit->limit);
	}
	root_limit->count = ext2fs_cpu_to_le16(root_limit->limit - c1);
	root_limit->limit = ext2fs_cpu_to_le16(root_limit->limit);

	retval = write_directory(ctx, fs, &outdir, ino);
	if (retval)
		goto errout;

errout:
	if (dir_buf)
		free(dir_buf);
	free_out_dir(&outdir);
	return retval;
}

void e2fsck_rehash_directories(e2fsck_t ctx)
{
	errcode_t	retval;
	struct problem_context	pctx;
	ext2_u32_iterate iter;
	ext2_ino_t	ino,lpf;
	static const char name[] = "lost+found";
	int	first = 1;

	if (!ctx->dirs_to_hash)
		return;

	retval = ext2fs_lookup(ctx->fs, EXT2_ROOT_INO, name,
			       sizeof(name)-1, 0, &lpf);
	if (retval)
		lpf = 0;
		
	clear_problem_context(&pctx);
	retval = ext2fs_u32_list_iterate_begin(ctx->dirs_to_hash, &iter);
	if (retval) {
		pctx.errcode = retval;
		fix_problem(ctx, PR_3A_REHASH_ITER, &pctx);
		return;
	}
	while (ext2fs_u32_list_iterate(iter, &ino)) {
		if (ino == lpf)
			continue;
		pctx.dir = ino;
		if (first) {
			fix_problem(ctx, PR_3A_PASS_HEADER, &pctx);
			first = 0;
		}
		fix_problem(ctx, PR_3A_REHASH_DIR, &pctx);
		pctx.errcode = e2fsck_rehash_dir(ctx, ino);
		if (pctx.errcode) {
			end_problem_latch(ctx, PR_LATCH_REHASH_DIR);
			fix_problem(ctx, PR_3A_REHASH_DIR_ERR, &pctx);
		}
	}
	end_problem_latch(ctx, PR_LATCH_REHASH_DIR);
	ext2fs_u32_list_iterate_end(iter);
	
	ext2fs_u32_list_free(ctx->dirs_to_hash);
	ctx->dirs_to_hash = 0;
}
