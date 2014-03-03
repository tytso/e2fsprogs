/*
 * inline_data.c --- data in inode
 *
 * Copyright (C) 2012 Zheng Liu <wenqing.lz@taobao.com>
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU library
 * General Public License, version 2.
 * %End-Header%
 */

#include "config.h"
#include <stdio.h>
#include <time.h>

#include "ext2_fs.h"
#include "ext2_ext_attr.h"

#include "ext2fs.h"
#include "ext2fsP.h"

struct ext2_inline_data {
	ext2_filsys fs;
	ext2_ino_t ino;
	size_t ea_size;	/* the size of inline data in ea area */
	void *ea_data;
};

static errcode_t ext2fs_inline_data_ea_set(struct ext2_inline_data *data)
{
	struct ext2_xattr_handle *handle;
	errcode_t retval;

	retval = ext2fs_xattrs_open(data->fs, data->ino, &handle);
	if (retval)
		return retval;

	retval = ext2fs_xattrs_read(handle);
	if (retval)
		goto err;

	retval = ext2fs_xattr_set(handle, "system.data",
				  data->ea_data, data->ea_size);
	if (retval)
		goto err;

	retval = ext2fs_xattrs_write(handle);

err:
	(void) ext2fs_xattrs_close(&handle);
	return retval;
}

static errcode_t ext2fs_inline_data_ea_get(struct ext2_inline_data *data)
{
	struct ext2_xattr_handle *handle;
	errcode_t retval;

	data->ea_size = 0;
	data->ea_data = 0;

	retval = ext2fs_xattrs_open(data->fs, data->ino, &handle);
	if (retval)
		return retval;

	retval = ext2fs_xattrs_read(handle);
	if (retval)
		goto err;

	retval = ext2fs_xattr_get(handle, "system.data",
				  (void **)&data->ea_data, &data->ea_size);
	if (retval)
		goto err;

err:
	(void) ext2fs_xattrs_close(&handle);
	return retval;
}

errcode_t ext2fs_inline_data_init(ext2_filsys fs, ext2_ino_t ino)
{
	struct ext2_inline_data data;

	data.fs = fs;
	data.ino = ino;
	data.ea_size = 0;
	data.ea_data = "";
	return ext2fs_inline_data_ea_set(&data);
}

errcode_t ext2fs_inline_data_size(ext2_filsys fs, ext2_ino_t ino, size_t *size)
{
	struct ext2_inode inode;
	struct ext2_inline_data data;
	errcode_t retval;

	retval = ext2fs_read_inode(fs, ino, &inode);
	if (retval)
		return retval;

	if (!(inode.i_flags & EXT4_INLINE_DATA_FL))
		return EXT2_ET_NO_INLINE_DATA;

	data.fs = fs;
	data.ino = ino;
	retval = ext2fs_inline_data_ea_get(&data);
	if (retval)
		return retval;

	*size = EXT4_MIN_INLINE_DATA_SIZE + data.ea_size;
	return ext2fs_free_mem(&data.ea_data);
}

int ext2fs_inline_data_dir_iterate(ext2_filsys fs, ext2_ino_t ino,
				   void *priv_data)
{
	struct dir_context *ctx;
	struct ext2_inode inode;
	struct ext2_dir_entry dirent;
	struct ext2_inline_data data;
	int ret = BLOCK_ABORT;
	e2_blkcnt_t blockcnt = 0;

	ctx = (struct dir_context *)priv_data;

	ctx->errcode = ext2fs_read_inode(fs, ino, &inode);
	if (ctx->errcode)
		goto out;

	if (!(inode.i_flags & EXT4_INLINE_DATA_FL)) {
		ctx->errcode = EXT2_ET_NO_INLINE_DATA;
		goto out;
	}

	if (!LINUX_S_ISDIR(inode.i_mode)) {
		ctx->errcode = EXT2_ET_NO_DIRECTORY;
		goto out;
	}
	ret = 0;

	/* we first check '.' and '..' dir */
	dirent.inode = ino;
	dirent.name_len = 1;
	ext2fs_set_rec_len(fs, EXT2_DIR_REC_LEN(2), &dirent);
	dirent.name[0] = '.';
	dirent.name[1] = '\0';
	ctx->buf = (char *)&dirent;
	ext2fs_get_rec_len(fs, &dirent, &ctx->buflen);
	ret |= ext2fs_process_dir_block(fs, 0, blockcnt++, 0, 0, priv_data);
	if (ret & BLOCK_ABORT)
		goto out;

	dirent.inode = ext2fs_le32_to_cpu(inode.i_block[0]);
	dirent.name_len = 2;
	ext2fs_set_rec_len(fs, EXT2_DIR_REC_LEN(3), &dirent);
	dirent.name[0] = '.';
	dirent.name[1] = '.';
	dirent.name[2] = '\0';
	ctx->buf = (char *)&dirent;
	ext2fs_get_rec_len(fs, &dirent, &ctx->buflen);
	ret |= ext2fs_process_dir_block(fs, 0, blockcnt++, 0, 0, priv_data);
	if (ret & BLOCK_INLINE_DATA_CHANGED) {
		errcode_t err;

		inode.i_block[0] = ext2fs_cpu_to_le32(dirent.inode);
		err = ext2fs_write_inode(fs, ino, &inode);
		if (err)
			goto out;
		ret &= ~BLOCK_INLINE_DATA_CHANGED;
	}
	if (ret & BLOCK_ABORT)
		goto out;

	ctx->buf = (char *)inode.i_block + EXT4_INLINE_DATA_DOTDOT_SIZE;
	ctx->buflen = EXT4_MIN_INLINE_DATA_SIZE - EXT4_INLINE_DATA_DOTDOT_SIZE;
#ifdef WORDS_BIGENDIAN
	ctx->errcode = ext2fs_dirent_swab_in2(fs, ctx->buf, ctx->buflen, 0);
	if (ctx->errcode) {
		ret |= BLOCK_ABORT;
		goto out;
	}
#endif
	ret |= ext2fs_process_dir_block(fs, 0, blockcnt++, 0, 0, priv_data);
	if (ret & BLOCK_INLINE_DATA_CHANGED) {
#ifdef WORDS_BIGENDIAN
		ctx->errcode = ext2fs_dirent_swab_out2(fs, ctx->buf,
						       ctx->buflen, 0);
		if (ctx->errcode) {
			ret |= BLOCK_ABORT;
			goto out;
		}
#endif
		ctx->errcode = ext2fs_write_inode(fs, ino, &inode);
		if (ctx->errcode)
			ret |= BLOCK_ABORT;
		ret &= ~BLOCK_INLINE_DATA_CHANGED;
	}
	if (ret & BLOCK_ABORT)
		goto out;

	data.fs = fs;
	data.ino = ino;
	ctx->errcode = ext2fs_inline_data_ea_get(&data);
	if (ctx->errcode) {
		ret |= BLOCK_ABORT;
		goto out;
	}
	if (data.ea_size <= 0)
		goto out;

	ctx->buf = data.ea_data;
	ctx->buflen = data.ea_size;
#ifdef WORDS_BIGENDIAN
	ctx.errcode = ext2fs_dirent_swab_in2(fs, ctx->buf, ctx->buflen, 0);
	if (ctx.errcode) {
		ret |= BLOCK_ABORT;
		goto out;
	}
#endif

	ret |= ext2fs_process_dir_block(fs, 0, blockcnt++, 0, 0, priv_data);
	if (ret & BLOCK_INLINE_DATA_CHANGED) {
#ifdef WORDS_BIGENDIAN
		ctx->errcode = ext2fs_dirent_swab_out2(fs, ctx->buf,
						      ctx->buflen, 0);
		if (ctx->errcode) {
			ret |= BLOCK_ABORT;
			goto out1;
		}
#endif
		ctx->errcode = ext2fs_inline_data_ea_set(&data);
		if (ctx->errcode)
			ret |= BLOCK_ABORT;
	}

out1:
	ext2fs_free_mem(&data.ea_data);
	ctx->buf = 0;

out:
	ret &= ~(BLOCK_ABORT | BLOCK_INLINE_DATA_CHANGED);
	return ret;
}

static errcode_t ext2fs_inline_data_ea_remove(ext2_filsys fs, ext2_ino_t ino)
{
	struct ext2_xattr_handle *handle;
	errcode_t retval;

	retval = ext2fs_xattrs_open(fs, ino, &handle);
	if (retval)
		return retval;

	retval = ext2fs_xattrs_read(handle);
	if (retval)
		goto err;

	retval = ext2fs_xattr_remove(handle, "system.data");
	if (retval)
		goto err;

	retval = ext2fs_xattrs_write(handle);

err:
	(void) ext2fs_xattrs_close(&handle);
	return retval;
}

static errcode_t ext2fs_inline_data_convert_dir(ext2_filsys fs, ext2_ino_t ino,
						char *bbuf, char *ibuf, int size)
{
	struct ext2_dir_entry *dir, *dir2;
	struct ext2_dir_entry_tail *t;
	errcode_t retval;
	unsigned int offset;
	int csum_size = 0;
	int filetype = 0;
	int rec_len;

	if (EXT2_HAS_RO_COMPAT_FEATURE(fs->super,
				       EXT4_FEATURE_RO_COMPAT_METADATA_CSUM))
		csum_size = sizeof(struct ext2_dir_entry_tail);

	/* Create '.' and '..' */
	if (EXT2_HAS_INCOMPAT_FEATURE(fs->super,
				      EXT2_FEATURE_INCOMPAT_FILETYPE))
		filetype = EXT2_FT_DIR;

	/*
	 * Set up entry for '.'
	 */
	dir = (struct ext2_dir_entry *) bbuf;
	dir->inode = ino;
	ext2fs_dirent_set_name_len(dir, 1);
	ext2fs_dirent_set_file_type(dir, filetype);
	dir->name[0] = '.';
	rec_len = (fs->blocksize - csum_size) - EXT2_DIR_REC_LEN(1);
	dir->rec_len = EXT2_DIR_REC_LEN(1);

	/*
	 * Set up entry for '..'
	 */
	dir = (struct ext2_dir_entry *) (bbuf + dir->rec_len);
	dir->rec_len = EXT2_DIR_REC_LEN(2);
	dir->inode = ext2fs_le32_to_cpu(((__u32 *)ibuf)[0]);
	ext2fs_dirent_set_name_len(dir, 2);
	ext2fs_dirent_set_file_type(dir, filetype);
	dir->name[0] = '.';
	dir->name[1] = '.';

	/*
	 * Ajust the last rec_len
	 */
	offset = EXT2_DIR_REC_LEN(1) + EXT2_DIR_REC_LEN(2);
	dir = (struct ext2_dir_entry *) (bbuf + offset);
	memcpy(bbuf + offset, ibuf + EXT4_INLINE_DATA_DOTDOT_SIZE,
	       size - EXT4_INLINE_DATA_DOTDOT_SIZE);
	size += EXT2_DIR_REC_LEN(1) + EXT2_DIR_REC_LEN(2) -
		EXT4_INLINE_DATA_DOTDOT_SIZE;

	do {
		dir2 = dir;
		retval = ext2fs_get_rec_len(fs, dir, &rec_len);
		if (retval)
			goto err;
		offset += rec_len;
		dir = (struct ext2_dir_entry *) (bbuf + offset);
	} while (offset < size);
	rec_len += fs->blocksize - csum_size - offset;
	retval = ext2fs_set_rec_len(fs, rec_len, dir2);
	if (retval)
		goto err;

	if (csum_size) {
		t = EXT2_DIRENT_TAIL(bbuf, fs->blocksize);
		ext2fs_initialize_dirent_tail(fs, t);
	}

err:
	return retval;
}

errcode_t ext2fs_inline_data_expand(ext2_filsys fs, ext2_ino_t ino)
{
	struct ext2_inode inode;
	struct ext2_inline_data data;
	errcode_t retval;
	blk64_t blk;
	char *inline_buf = 0;
	char *blk_buf = 0;

	EXT2_CHECK_MAGIC(fs, EXT2_ET_MAGIC_EXT2FS_FILSYS);

	retval = ext2fs_read_inode(fs, ino, &inode);
	if (retval)
		return retval;

	if (!(inode.i_flags & EXT4_INLINE_DATA_FL))
		return EXT2_ET_NO_INLINE_DATA;

	/* Get inline data first */
	data.fs = fs;
	data.ino = ino;
	retval = ext2fs_inline_data_ea_get(&data);
	if (retval)
		return retval;
	retval = ext2fs_get_mem(EXT4_MIN_INLINE_DATA_SIZE + data.ea_size,
				&inline_buf);
	if (retval)
		goto errout;

	memcpy(inline_buf, (void *)inode.i_block, EXT4_MIN_INLINE_DATA_SIZE);
	if (data.ea_size > 0) {
		memcpy(inline_buf + EXT4_MIN_INLINE_DATA_SIZE,
		       data.ea_data, data.ea_size);
	}

#ifdef WORDS_BIGENDIAN
	retval = ext2fs_dirent_swab_in2(fs, inline_buf,
			data.ea_size + EXT4_MIN_INLINE_DATA_SIZE);
	if (retval)
		goto errout;
#endif

	memset((void *)inode.i_block, 0, EXT4_MIN_INLINE_DATA_SIZE);
	retval = ext2fs_inline_data_ea_remove(fs, ino);
	if (retval)
		goto errout;

	retval = ext2fs_get_mem(fs->blocksize, &blk_buf);
	if (retval)
		goto errout;

	/* Adjust the rec_len */
	retval = ext2fs_inline_data_convert_dir(fs, ino, blk_buf, inline_buf,
						EXT4_MIN_INLINE_DATA_SIZE +
							data.ea_size);
	if (retval)
		goto errout;

	/* Allocate a new block */
	retval = ext2fs_new_block2(fs, 0, 0, &blk);
	if (retval)
		goto errout;
	retval = ext2fs_write_dir_block4(fs, blk, blk_buf, 0, ino);
	if (retval)
		goto errout;

	/* Update inode */
	if (EXT2_HAS_INCOMPAT_FEATURE(fs->super, EXT3_FEATURE_INCOMPAT_EXTENTS))
		inode.i_flags |= EXT4_EXTENTS_FL;
	inode.i_flags &= ~EXT4_INLINE_DATA_FL;
	ext2fs_iblk_set(fs, &inode, 1);
	inode.i_size = fs->blocksize;
	retval = ext2fs_bmap2(fs, ino, &inode, 0, BMAP_SET, 0, 0, &blk);
	if (retval)
		goto errout;
	retval = ext2fs_write_inode(fs, ino, &inode);
	if (retval)
		goto errout;
	ext2fs_block_alloc_stats2(fs, blk, +1);

errout:
	if (blk_buf)
		ext2fs_free_mem(&blk_buf);
	if (inline_buf)
		ext2fs_free_mem(&inline_buf);
	ext2fs_free_mem(&data.ea_data);
	return retval;
}
