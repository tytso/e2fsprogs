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
#include <limits.h> /* for PATH_MAX */

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
	if (retval == EXT2_ET_EA_KEY_NOT_FOUND) {
		data->ea_size = 0;
		data->ea_data = NULL;
		retval = 0;
	} else if (retval)
		goto err;

err:
	(void) ext2fs_xattrs_close(&handle);
	return retval;
}

errcode_t ext2fs_inline_data_init(ext2_filsys fs, ext2_ino_t ino)
{
	struct ext2_inline_data data;
	char empty[1] = { '\0' };

	data.fs = fs;
	data.ino = ino;
	data.ea_size = 0;
	data.ea_data = empty;
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
	char *old_buf;
	unsigned int old_buflen;
	int old_flags;

	ctx = (struct dir_context *)priv_data;
	old_buf = ctx->buf;
	old_buflen = ctx->buflen;
	old_flags = ctx->flags;
	ctx->flags |= DIRENT_FLAG_INCLUDE_INLINE_DATA;

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
		goto out1;

	ctx->buf = data.ea_data;
	ctx->buflen = data.ea_size;
#ifdef WORDS_BIGENDIAN
	ctx->errcode = ext2fs_dirent_swab_in2(fs, ctx->buf, ctx->buflen, 0);
	if (ctx->errcode) {
		ret |= BLOCK_ABORT;
		goto out1;
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
out:
	ctx->buf = old_buf;
	ctx->buflen = old_buflen;
	ctx->flags = old_flags;
	ret &= ~(BLOCK_ABORT | BLOCK_INLINE_DATA_CHANGED);
	return ret;
}

errcode_t ext2fs_inline_data_ea_remove(ext2_filsys fs, ext2_ino_t ino)
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
	int offset;
	unsigned int rec_len;
	int csum_size = 0;
	int filetype = 0;

	if (ext2fs_has_feature_metadata_csum(fs->super))
		csum_size = sizeof(struct ext2_dir_entry_tail);

	/* Create '.' and '..' */
	if (ext2fs_has_feature_filetype(fs->super))
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
	 * Adjust the last rec_len
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

static errcode_t
ext2fs_inline_data_dir_expand(ext2_filsys fs, ext2_ino_t ino,
			      struct ext2_inode *inode, char *buf, size_t size)
{
	errcode_t retval;
	blk64_t blk;
	char *blk_buf;

	retval = ext2fs_get_memzero(fs->blocksize, &blk_buf);
	if (retval)
		return retval;

#ifdef WORDS_BIGENDIAN
	retval = ext2fs_dirent_swab_in2(fs, buf + EXT4_INLINE_DATA_DOTDOT_SIZE,
					size, 0);
	if (retval)
		goto errout;
#endif

	/* Adjust the rec_len */
	retval = ext2fs_inline_data_convert_dir(fs, ino, blk_buf, buf, size);
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
	if (ext2fs_has_feature_extents(fs->super))
		inode->i_flags |= EXT4_EXTENTS_FL;
	inode->i_flags &= ~EXT4_INLINE_DATA_FL;
	retval = ext2fs_iblk_add_blocks(fs, inode, 1);
	if (retval)
		goto errout;
	inode->i_size = fs->blocksize;
	retval = ext2fs_bmap2(fs, ino, inode, 0, BMAP_SET, 0, 0, &blk);
	if (retval)
		goto errout;
	retval = ext2fs_write_inode(fs, ino, inode);
	if (retval)
		goto errout;
	ext2fs_block_alloc_stats(fs, blk, +1);

errout:
	ext2fs_free_mem(&blk_buf);
	return retval;
}

static errcode_t
ext2fs_inline_data_file_expand(ext2_filsys fs, ext2_ino_t ino,
			       struct ext2_inode *inode, char *buf, size_t size)
{
	ext2_file_t e2_file;
	errcode_t retval;

	/* Update inode */
	memset(inode->i_block, 0, sizeof(inode->i_block));
	if (ext2fs_has_feature_extents(fs->super)) {
		ext2_extent_handle_t handle;

		inode->i_flags &= ~EXT4_EXTENTS_FL;
		retval = ext2fs_extent_open2(fs, ino, inode, &handle);
		if (retval)
			return retval;
		ext2fs_extent_free(handle);
	}
	inode->i_flags &= ~EXT4_INLINE_DATA_FL;
	inode->i_size = 0;
	retval = ext2fs_write_inode(fs, ino, inode);
	if (retval)
		return retval;

	/* Write out the block buffer */
	retval = ext2fs_file_open(fs, ino, EXT2_FILE_WRITE, &e2_file);
	if (retval)
		return retval;
	retval = ext2fs_file_write(e2_file, buf, size, 0);
	ext2fs_file_close(e2_file);
	return retval;
}

errcode_t ext2fs_inline_data_expand(ext2_filsys fs, ext2_ino_t ino)
{
	struct ext2_inode inode;
	struct ext2_inline_data data;
	errcode_t retval;
	size_t inline_size;
	char *inline_buf = 0;

	EXT2_CHECK_MAGIC(fs, EXT2_ET_MAGIC_EXT2FS_FILSYS);

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
	inline_size = data.ea_size + EXT4_MIN_INLINE_DATA_SIZE;
	retval = ext2fs_get_mem(inline_size, &inline_buf);
	if (retval)
		goto errout;

	memcpy(inline_buf, (void *)inode.i_block, EXT4_MIN_INLINE_DATA_SIZE);
	if (data.ea_size > 0) {
		memcpy(inline_buf + EXT4_MIN_INLINE_DATA_SIZE,
		       data.ea_data, data.ea_size);
	}

	memset((void *)inode.i_block, 0, EXT4_MIN_INLINE_DATA_SIZE);
	/*
	 * NOTE: We must do this write -> ea_remove -> read cycle here because
	 * removing the inline data EA can free the EA block, which is a change
	 * that our stack copy of the inode will never see.  If that happens,
	 * we can end up with the EA block and lblk 0 pointing to the same
	 * pblk, which is bad news.
	 */
	retval = ext2fs_write_inode(fs, ino, &inode);
	if (retval)
		goto errout;
	retval = ext2fs_inline_data_ea_remove(fs, ino);
	if (retval)
		goto errout;
	retval = ext2fs_read_inode(fs, ino, &inode);
	if (retval)
		goto errout;

	if (LINUX_S_ISDIR(inode.i_mode)) {
		retval = ext2fs_inline_data_dir_expand(fs, ino, &inode,
						inline_buf, inline_size);
	} else {
		retval = ext2fs_inline_data_file_expand(fs, ino, &inode,
						inline_buf, inline_size);
	}

errout:
	if (inline_buf)
		ext2fs_free_mem(&inline_buf);
	ext2fs_free_mem(&data.ea_data);
	return retval;
}

/*
 * When caller uses this function to retrieve the inline data, it must
 * allocate a buffer which has the size of inline data.  The size of
 * inline data can be know by ext2fs_inline_data_get_size().
 */
errcode_t ext2fs_inline_data_get(ext2_filsys fs, ext2_ino_t ino,
				 struct ext2_inode *inode,
				 void *buf, size_t *size)
{
	struct ext2_inode inode_buf;
	struct ext2_inline_data data;
	errcode_t retval;

	if (!inode) {
		retval = ext2fs_read_inode(fs, ino, &inode_buf);
		if (retval)
			return retval;
		inode = &inode_buf;
	}

	data.fs = fs;
	data.ino = ino;
	retval = ext2fs_inline_data_ea_get(&data);
	if (retval)
		return retval;

	memcpy(buf, (void *)inode->i_block, EXT4_MIN_INLINE_DATA_SIZE);
	if (data.ea_size > 0)
		memcpy((char *) buf + EXT4_MIN_INLINE_DATA_SIZE,
		       data.ea_data, data.ea_size);

	if (size)
		*size = EXT4_MIN_INLINE_DATA_SIZE + data.ea_size;
	ext2fs_free_mem(&data.ea_data);
	return 0;
}

errcode_t ext2fs_inline_data_set(ext2_filsys fs, ext2_ino_t ino,
				 struct ext2_inode *inode,
				 void *buf, size_t size)
{
	struct ext2_inode inode_buf;
	struct ext2_inline_data data = {
		.fs = fs,
		.ino = ino,
	};
	errcode_t retval;
	size_t free_ea_size, existing_size, free_inode_size;

	if (!inode) {
		retval = ext2fs_read_inode(fs, ino, &inode_buf);
		if (retval)
			return retval;
		inode = &inode_buf;
	}

	if (size <= EXT4_MIN_INLINE_DATA_SIZE) {
		memcpy((void *)inode->i_block, buf, size);
	} else {
		retval = ext2fs_xattr_inode_max_size(fs, ino, &free_ea_size);
		if (retval)
			return retval;

		retval = ext2fs_inline_data_size(fs, ino, &existing_size);
		if (retval)
			return retval;

		if (existing_size < EXT4_MIN_INLINE_DATA_SIZE) {
			free_inode_size = EXT4_MIN_INLINE_DATA_SIZE -
					  existing_size;
		} else {
			free_inode_size = 0;
		}

		if (size != existing_size &&
		    size > existing_size + free_ea_size + free_inode_size)
			return EXT2_ET_INLINE_DATA_NO_SPACE;

		memcpy((void *)inode->i_block, buf, EXT4_MIN_INLINE_DATA_SIZE);
		if (size > EXT4_MIN_INLINE_DATA_SIZE)
			data.ea_size = size - EXT4_MIN_INLINE_DATA_SIZE;
		data.ea_data = (char *) buf + EXT4_MIN_INLINE_DATA_SIZE;
	}
	retval = ext2fs_write_inode(fs, ino, inode);
	if (retval)
		return retval;
	return ext2fs_inline_data_ea_set(&data);
}

#ifdef DEBUG
#include <stdlib.h>
#include <string.h>

static unsigned char *mem_disk;
static unsigned long long mem_disk_size;
static io_manager mem_io_manager;

static errcode_t mem_open(const char *name, int flags EXT2FS_ATTR((unused)),
			  io_channel *channel)
{
	io_channel	io = NULL;
	errcode_t	retval;

	if (!name)
		return EXT2_ET_BAD_DEVICE_NAME;
	retval = ext2fs_get_memzero(sizeof(struct struct_io_channel), &io);
	if (retval)
		return retval;
	io->magic = EXT2_ET_MAGIC_IO_CHANNEL;
	io->manager = mem_io_manager;
	retval = ext2fs_get_mem(strlen(name) + 1, &io->name);
	if (retval) {
		ext2fs_free_mem(&io);
		return retval;
	}
	strcpy(io->name, name);
	io->block_size = 1024;
	io->refcount = 1;
	*channel = io;
	return 0;
}

static errcode_t mem_close(io_channel channel)
{
	EXT2_CHECK_MAGIC(channel, EXT2_ET_MAGIC_IO_CHANNEL);
	if (--channel->refcount > 0)
		return 0;
	if (channel->name)
		ext2fs_free_mem(&channel->name);
	ext2fs_free_mem(&channel);
	return 0;
}

static errcode_t mem_set_blksize(io_channel channel, int blksize)
{
	EXT2_CHECK_MAGIC(channel, EXT2_ET_MAGIC_IO_CHANNEL);
	channel->block_size = blksize;
	return 0;
}

static errcode_t mem_read_blk64(io_channel channel, unsigned long long block,
				int count, void *buf)
{
	size_t			size;
	unsigned long long	offset;

	EXT2_CHECK_MAGIC(channel, EXT2_ET_MAGIC_IO_CHANNEL);
	size = (count < 0) ? (size_t) (-count) :
		(size_t) count * channel->block_size;
	offset = block * channel->block_size;
	if (!mem_disk || offset >= mem_disk_size) {
		memset(buf, 0, size);
		return 0;
	}
	if (offset + size > mem_disk_size) {
		size_t avail = (size_t) (mem_disk_size - offset);

		memcpy(buf, mem_disk + offset, avail);
		memset((char *) buf + avail, 0, size - avail);
		return 0;
	}
	memcpy(buf, mem_disk + offset, size);
	return 0;
}

static errcode_t mem_write_blk64(io_channel channel, unsigned long long block,
				 int count, const void *buf)
{
	size_t			size;
	unsigned long long	offset;

	EXT2_CHECK_MAGIC(channel, EXT2_ET_MAGIC_IO_CHANNEL);
	size = (count < 0) ? (size_t) (-count) :
		(size_t) count * channel->block_size;
	offset = block * channel->block_size;
	if (!mem_disk || offset + size > mem_disk_size)
		return EXT2_ET_LLSEEK_FAILED;
	memcpy(mem_disk + offset, buf, size);
	return 0;
}

static errcode_t mem_read_blk(io_channel channel, unsigned long block,
			      int count, void *buf)
{
	return mem_read_blk64(channel, block, count, buf);
}

static errcode_t mem_write_blk(io_channel channel, unsigned long block,
			       int count, const void *buf)
{
	return mem_write_blk64(channel, block, count, buf);
}

static errcode_t mem_flush(io_channel channel)
{
	EXT2_CHECK_MAGIC(channel, EXT2_ET_MAGIC_IO_CHANNEL);
	return 0;
}

static struct struct_io_manager struct_mem_manager = {
	.magic		= EXT2_ET_MAGIC_IO_MANAGER,
	.name		= "In-memory test I/O Manager",
	.open		= mem_open,
	.close		= mem_close,
	.set_blksize	= mem_set_blksize,
	.read_blk	= mem_read_blk,
	.write_blk	= mem_write_blk,
	.flush		= mem_flush,
	.read_blk64	= mem_read_blk64,
	.write_blk64	= mem_write_blk64,
};

static io_manager mem_io_manager = &struct_mem_manager;

static ext2_filsys setup_test_fs(unsigned int blocks, unsigned int inodes,
				 unsigned int inode_size, int with_csum)
{
	ext2_filsys		fs = NULL;
	struct ext2_super_block	param;
	errcode_t		retval;

	mem_disk_size = (unsigned long long) blocks * 1024ULL;
	retval = ext2fs_get_memzero((size_t) mem_disk_size, &mem_disk);
	if (retval) {
		com_err("setup", retval, "while allocating mem_disk");
		exit(1);
	}

	memset(&param, 0, sizeof(param));
	ext2fs_blocks_count_set(&param, blocks);
	param.s_inodes_count = inodes;
	param.s_rev_level = EXT2_DYNAMIC_REV;
	param.s_inode_size = inode_size;
	param.s_feature_compat = EXT2_FEATURE_COMPAT_EXT_ATTR;
	param.s_feature_incompat = EXT4_FEATURE_INCOMPAT_INLINE_DATA |
				   EXT3_FEATURE_INCOMPAT_EXTENTS |
				   EXT2_FEATURE_INCOMPAT_FILETYPE;
	if (with_csum) {
		param.s_feature_ro_compat =
			EXT4_FEATURE_RO_COMPAT_METADATA_CSUM;
		param.s_checksum_type = EXT2_CRC32C_CHKSUM;
	}

	retval = ext2fs_initialize("test fs",
				   EXT2_FLAG_RW | EXT2_FLAG_64BITS |
				   EXT2_FLAG_EXCLUSIVE,
				   &param, mem_io_manager, &fs);
	if (retval) {
		com_err("setup", retval, "while initializing filesystem");
		exit(1);
	}
	retval = ext2fs_allocate_tables(fs);
	if (retval) {
		com_err("setup", retval, "while allocating tables");
		exit(1);
	}
	return fs;
}

static void teardown_test_fs(ext2_filsys *fs)
{
	if (*fs)
		ext2fs_close_free(fs);
	if (mem_disk)
		ext2fs_free_mem(&mem_disk);
	mem_disk_size = 0;
}

static errcode_t file_test(ext2_filsys fs)
{
	struct ext2_inode	inode;
	struct dir_context	dctx;
	ext2_ino_t		newfile, noninline;
	ext2_file_t		e2file = NULL;
	errcode_t		retval;
	size_t			size = 0;
	unsigned int		got = 0;
	int			i;
	char			*buf = NULL, *cmpbuf = NULL;

	/* Non-inline inode rejects size and expand with EXT2_ET_NO_INLINE_DATA */
	retval = ext2fs_new_inode(fs, 2, LINUX_S_IFREG | 0644, 0, &noninline);
	if (retval)
		return 1;
	ext2fs_inode_alloc_stats2(fs, noninline, +1, 0);
	memset(&inode, 0, sizeof(inode));
	inode.i_mode = LINUX_S_IFREG | 0644;
	retval = ext2fs_write_new_inode(fs, noninline, &inode);
	if (retval)
		return 1;
	if (ext2fs_inline_data_size(fs, noninline, &size) !=
	    EXT2_ET_NO_INLINE_DATA) {
		fprintf(stderr, "tst_inline_data: expected NO_INLINE_DATA\n");
		return 1;
	}
	if (ext2fs_inline_data_expand(fs, noninline) !=
	    EXT2_ET_NO_INLINE_DATA) {
		fprintf(stderr, "tst_inline_data: expand non-inline failed\n");
		return 1;
	}

	/* Create inline regular file */
	retval = ext2fs_new_inode(fs, 2, LINUX_S_IFREG | 0755, 0, &newfile);
	if (retval)
		return 1;
	ext2fs_inode_alloc_stats2(fs, newfile, +1, 0);
	memset(&inode, 0, sizeof(inode));
	inode.i_flags = EXT4_INLINE_DATA_FL;
	inode.i_size = EXT4_MIN_INLINE_DATA_SIZE;
	inode.i_mode = LINUX_S_IFREG | 0755;
	retval = ext2fs_write_new_inode(fs, newfile, &inode);
	if (retval)
		return 1;
	retval = ext2fs_inline_data_init(fs, newfile);
	if (retval)
		return 1;

	/* dir_iterate on regular inline file sets EXT2_ET_NO_DIRECTORY */
	memset(&dctx, 0, sizeof(dctx));
	(void) ext2fs_inline_data_dir_iterate(fs, newfile, &dctx);
	if (dctx.errcode != EXT2_ET_NO_DIRECTORY) {
		fprintf(stderr, "tst_inline_data: expected NO_DIRECTORY\n");
		return 1;
	}

	retval = ext2fs_inline_data_size(fs, newfile, &size);
	if (retval || size != EXT4_MIN_INLINE_DATA_SIZE) {
		fprintf(stderr, "tst_inline_data: initial size wrong\n");
		return 1;
	}

	retval = ext2fs_get_mem(512, &buf);
	if (retval)
		return 1;
	retval = ext2fs_get_memzero(512, &cmpbuf);
	if (retval) {
		ext2fs_free_mem(&buf);
		return 1;
	}
	for (i = 0; i < 512; i++)
		buf[i] = (char) (0x21 + (i % 90));

	/* 1. Small <= 60B payload using explicit inode pointer */
	retval = ext2fs_read_inode(fs, newfile, &inode);
	if (retval)
		goto err;
	retval = ext2fs_inline_data_set(fs, newfile, &inode, buf, 40);
	if (retval)
		goto err;
	retval = ext2fs_inline_data_get(fs, newfile, NULL, cmpbuf, &size);
	if (retval || size != EXT4_MIN_INLINE_DATA_SIZE ||
	    memcmp(buf, cmpbuf, 40) != 0) {
		fprintf(stderr, "tst_inline_data: <=60B roundtrip failed\n");
		retval = 1;
		goto err;
	}

	/* 2. Grow to 108B (spills into system.data ibody EA) */
	retval = ext2fs_inline_data_set(fs, newfile, NULL, buf, 108);
	if (retval)
		goto err;
	retval = ext2fs_inline_data_get(fs, newfile, NULL, cmpbuf, &size);
	if (retval || size != 108 || memcmp(buf, cmpbuf, 108) != 0) {
		fprintf(stderr, "tst_inline_data: 108B EA roundtrip failed\n");
		retval = 1;
		goto err;
	}

	/* 3. Shrink EA payload from 108B to 76B */
	retval = ext2fs_inline_data_set(fs, newfile, NULL, buf + 10, 76);
	if (retval)
		goto err;
	retval = ext2fs_inline_data_get(fs, newfile, NULL, cmpbuf, &size);
	if (retval || size != 76 || memcmp(buf + 10, cmpbuf, 76) != 0) {
		fprintf(stderr, "tst_inline_data: 76B shrink failed\n");
		retval = 1;
		goto err;
	}

	/* 4. Exceeding ibody space returns EXT2_ET_INLINE_DATA_NO_SPACE */
	if (ext2fs_inline_data_set(fs, newfile, NULL, buf, 400) !=
	    EXT2_ET_INLINE_DATA_NO_SPACE) {
		fprintf(stderr, "tst_inline_data: expected NO_SPACE\n");
		retval = 1;
		goto err;
	}

	/*
	 * 5. Remove system.data EA, verify key is gone in xattrs and
	 *    a second ea_remove call is idempotent (returns 0).
	 */
	retval = ext2fs_inline_data_ea_remove(fs, newfile);
	if (retval)
		goto err;
	retval = ext2fs_inline_data_size(fs, newfile, &size);
	if (retval || size != EXT4_MIN_INLINE_DATA_SIZE) {
		fprintf(stderr, "tst_inline_data: ea_remove size wrong\n");
		retval = 1;
		goto err;
	}
	{
		struct ext2_xattr_handle *xh = NULL;
		void *val = NULL;
		size_t vlen = 0;

		retval = ext2fs_xattrs_open(fs, newfile, &xh);
		if (retval)
			goto err;
		retval = ext2fs_xattrs_read(xh);
		if (!retval)
			retval = ext2fs_xattr_get(xh, "system.data",
						  &val, &vlen);
		ext2fs_xattrs_close(&xh);
		if (val)
			ext2fs_free_mem(&val);
		if (retval != EXT2_ET_EA_KEY_NOT_FOUND) {
			fprintf(stderr,
				"tst_inline_data: expected EA_KEY_NOT_FOUND\n");
			retval = 1;
			goto err;
		}
	}
	if (ext2fs_inline_data_ea_remove(fs, newfile) != 0) {
		fprintf(stderr, "tst_inline_data: idempotent ea_remove\n");
		retval = 1;
		goto err;
	}

	/* 6. Set 96B, punch to 0, verify i_size == 0 */
	retval = ext2fs_inline_data_set(fs, newfile, NULL, buf, 96);
	if (retval)
		goto err;
	retval = ext2fs_punch(fs, newfile, NULL, NULL, 0, ~0ULL);
	if (retval)
		goto err;
	retval = ext2fs_read_inode(fs, newfile, &inode);
	if (retval || inode.i_size != 0) {
		fprintf(stderr, "tst_inline_data: i_size should be 0\n");
		retval = 1;
		goto err;
	}

	/* 7. Re-populate 104B and expand inline file to extent-mapped block */
	retval = ext2fs_inline_data_set(fs, newfile, NULL, buf, 104);
	if (retval)
		goto err;
	retval = ext2fs_read_inode(fs, newfile, &inode);
	if (retval)
		goto err;
	inode.i_size = 104;
	retval = ext2fs_write_inode(fs, newfile, &inode);
	if (retval)
		goto err;

	retval = ext2fs_inline_data_expand(fs, newfile);
	if (retval)
		goto err;
	retval = ext2fs_read_inode(fs, newfile, &inode);
	if (retval || (inode.i_flags & EXT4_INLINE_DATA_FL) ||
	    !(inode.i_flags & EXT4_EXTENTS_FL)) {
		fprintf(stderr, "tst_inline_data: file expand flags wrong\n");
		retval = 1;
		goto err;
	}
	memset(cmpbuf, 0, 512);
	retval = ext2fs_file_open(fs, newfile, 0, &e2file);
	if (retval)
		goto err;
	retval = ext2fs_file_read(e2file, cmpbuf, 104, &got);
	ext2fs_file_close(e2file);
	if (retval || got != 104 || memcmp(buf, cmpbuf, 104) != 0) {
		fprintf(stderr, "tst_inline_data: expanded file data wrong\n");
		retval = 1;
		goto err;
	}

err:
	if (cmpbuf)
		ext2fs_free_mem(&cmpbuf);
	if (buf)
		ext2fs_free_mem(&buf);
	return retval;
}

static errcode_t inode128_test(void)
{
	ext2_filsys		fs;
	struct ext2_inode	inode;
	ext2_ino_t		ino;
	char			buf[64];
	errcode_t		retval;

	fs = setup_test_fs(256, 32, EXT2_GOOD_OLD_INODE_SIZE, 0);
	retval = ext2fs_new_inode(fs, 2, LINUX_S_IFREG | 0644, 0, &ino);
	if (retval)
		goto out;
	ext2fs_inode_alloc_stats2(fs, ino, +1, 0);
	memset(&inode, 0, sizeof(inode));
	inode.i_mode = LINUX_S_IFREG | 0644;
	inode.i_flags = EXT4_INLINE_DATA_FL;
	retval = ext2fs_write_new_inode(fs, ino, &inode);
	if (retval)
		goto out;

	memset(buf, 'z', sizeof(buf));
	/* > 60B on a 128-byte inode must return EXT2_ET_INLINE_DATA_NO_SPACE */
	retval = ext2fs_inline_data_set(fs, ino, NULL, buf, sizeof(buf));
	if (retval == EXT2_ET_INLINE_DATA_NO_SPACE)
		retval = 0;
	else
		retval = 1;

out:
	teardown_test_fs(&fs);
	return retval;
}

struct iter_walk {
	int		count;
	int		abort_after;
	const char	*rename_from;
	char		rename_to;
};

static int walk_inline_dir_cb(ext2_ino_t dir EXT2FS_ATTR((unused)),
			      int entry EXT2FS_ATTR((unused)),
			      struct ext2_dir_entry *dirent,
			      int offset EXT2FS_ATTR((unused)),
			      int blocksize EXT2FS_ATTR((unused)),
			      char *buf EXT2FS_ATTR((unused)),
			      void *priv_data)
{
	struct iter_walk *w = (struct iter_walk *) priv_data;
	int len = ext2fs_dirent_name_len(dirent);

	w->count++;
	if (w->rename_from &&
	    len == (int) strlen(w->rename_from) &&
	    strncmp(dirent->name, w->rename_from, len) == 0) {
		dirent->name[0] = w->rename_to;
		return DIRENT_CHANGED;
	}
	if (w->abort_after && w->count >= w->abort_after)
		return DIRENT_ABORT;
	return 0;
}

static errcode_t dir_test(ext2_filsys fs)
{
	const char		*dot_name = ".";
	const char		*stub_name = "stub";
	const char		*parent_name = "test";
	ext2_ino_t		parent, tmp;
	struct ext2_inode	inode;
	struct iter_walk	walk;
	blk64_t			blk = 0;
	char			*blkbuf = NULL;
	errcode_t		retval;
	char			dirname[32];
	int			i, created = 0;

	retval = ext2fs_mkdir(fs, 11, 11, stub_name);
	if (retval)
		return retval;

	retval = ext2fs_mkdir(fs, 11, 0, parent_name);
	if (retval)
		return retval;

	retval = ext2fs_lookup(fs, 11, parent_name, strlen(parent_name),
			       0, &parent);
	if (retval)
		return retval;

	retval = ext2fs_lookup(fs, parent, dot_name, strlen(dot_name),
			       0, &tmp);
	if (retval || parent != tmp)
		return 1;

	/* Fill i_block (4 entries d0..d3 fit in 56 bytes) until DIR_NO_SPACE */
	for (i = 0; i < 8; i++) {
		snprintf(dirname, sizeof(dirname), "d%d", i);
		retval = ext2fs_mkdir(fs, parent, 0, dirname);
		if (retval == EXT2_ET_DIR_NO_SPACE)
			break;
		if (retval)
			return retval;
		created++;
		retval = ext2fs_lookup(fs, parent, dirname, strlen(dirname),
				       0, &tmp);
		if (retval)
			return retval;
	}
	if (created != 4) {
		fprintf(stderr, "tst_inline_data: expected 4 i_block entries\n");
		return 1;
	}

	/*
	 * Add a 64-byte system.data ibody EA directory region with two entries
	 * ("e0" and "e1") to exercise kernel-created EA inline directories.
	 */
	{
		char ibuf[124];
		struct ext2_dir_entry *de;
		size_t cur_sz = 0;

		memset(ibuf, 0, sizeof(ibuf));
		retval = ext2fs_inline_data_get(fs, parent, NULL, ibuf,
						&cur_sz);
		if (retval || cur_sz != EXT4_MIN_INLINE_DATA_SIZE)
			return 1;

		de = (struct ext2_dir_entry *) (ibuf + 60);
		de->inode = 11;
		ext2fs_dirent_set_name_len(de, 2);
		ext2fs_dirent_set_file_type(de, EXT2_FT_DIR);
		de->rec_len = 12;
		memcpy(de->name, "e0", 2);

		de = (struct ext2_dir_entry *) (ibuf + 72);
		de->inode = 11;
		ext2fs_dirent_set_name_len(de, 2);
		ext2fs_dirent_set_file_type(de, EXT2_FT_DIR);
		de->rec_len = 52;
		memcpy(de->name, "e1", 2);

		retval = ext2fs_read_inode(fs, parent, &inode);
		if (retval)
			return retval;
		inode.i_size = sizeof(ibuf);
		retval = ext2fs_inline_data_set(fs, parent, &inode, ibuf,
						sizeof(ibuf));
		if (retval)
			return retval;
		created += 2;
	}

	/* Exercise inline dir_iterate2: full walk, DIRENT_ABORT, DIRENT_CHANGED */
	memset(&walk, 0, sizeof(walk));
	retval = ext2fs_dir_iterate2(fs, parent, 0, NULL,
				     walk_inline_dir_cb, &walk);
	if (retval || walk.count != created + 2) {
		fprintf(stderr, "tst_inline_data: walk count %d != %d\n",
			walk.count, created + 2);
		return 1;
	}

	memset(&walk, 0, sizeof(walk));
	walk.abort_after = 2;
	retval = ext2fs_dir_iterate2(fs, parent, 0, NULL,
				     walk_inline_dir_cb, &walk);
	if (retval || walk.count != 2) {
		fprintf(stderr, "tst_inline_data: abort walk failed\n");
		return 1;
	}

	/* Mutate both an i_block entry ("d0" -> "x0") and an EA entry ("e0" -> "y0") */
	memset(&walk, 0, sizeof(walk));
	walk.rename_from = "d0";
	walk.rename_to = 'x';
	retval = ext2fs_dir_iterate2(fs, parent, 0, NULL,
				     walk_inline_dir_cb, &walk);
	if (retval || ext2fs_lookup(fs, parent, "x0", 2, 0, &tmp)) {
		fprintf(stderr, "tst_inline_data: i_block DIRENT_CHANGED\n");
		return 1;
	}

	memset(&walk, 0, sizeof(walk));
	walk.rename_from = "e0";
	walk.rename_to = 'y';
	retval = ext2fs_dir_iterate2(fs, parent, 0, NULL,
				     walk_inline_dir_cb, &walk);
	if (retval || ext2fs_lookup(fs, parent, "y0", 2, 0, &tmp)) {
		fprintf(stderr, "tst_inline_data: EA DIRENT_CHANGED\n");
		return 1;
	}

	/* Expand inline directory into checksummed directory block */
	retval = ext2fs_expand_dir(fs, parent);
	if (retval)
		return retval;

	retval = ext2fs_read_inode(fs, parent, &inode);
	if (retval || (inode.i_flags & EXT4_INLINE_DATA_FL) ||
	    inode.i_size != fs->blocksize) {
		fprintf(stderr, "tst_inline_data: expanded dir inode wrong\n");
		return 1;
	}

	/* Verify metadata_csum on expanded directory block and lookup entries */
	retval = ext2fs_bmap2(fs, parent, &inode, NULL, 0, 0, NULL, &blk);
	if (retval || !blk)
		return 1;
	retval = ext2fs_get_mem(fs->blocksize, &blkbuf);
	if (retval)
		return retval;
	retval = ext2fs_read_dir_block4(fs, blk, blkbuf, 0, parent);
	ext2fs_free_mem(&blkbuf);
	if (retval) {
		fprintf(stderr, "tst_inline_data: dir block csum invalid\n");
		return 1;
	}

	if (ext2fs_lookup(fs, parent, "x0", 2, 0, &tmp) ||
	    ext2fs_lookup(fs, parent, "y0", 2, 0, &tmp) ||
	    ext2fs_lookup(fs, parent, "e1", 2, 0, &tmp) ||
	    ext2fs_lookup(fs, parent, "..", 2, 0, &tmp) || tmp != 11) {
		fprintf(stderr, "tst_inline_data: post-expand lookup failed\n");
		return 1;
	}

	return 0;
}

int main(int argc EXT2FS_ATTR((unused)), char *argv[] EXT2FS_ATTR((unused)))
{
	ext2_filsys	fs;

	initialize_ext2_error_table();

	fs = setup_test_fs(1024, 128, 256, 1);
	if (file_test(fs) || inode128_test()) {
		fprintf(stderr, "tst_inline_data(FILE): FAILED\n");
		teardown_test_fs(&fs);
		return 1;
	}
	teardown_test_fs(&fs);
	printf("tst_inline_data(FILE): OK\n");

	fs = setup_test_fs(1024, 128, 256, 1);
	if (dir_test(fs)) {
		fprintf(stderr, "tst_inline_data(DIR): FAILED\n");
		teardown_test_fs(&fs);
		return 1;
	}
	teardown_test_fs(&fs);
	printf("tst_inline_data(DIR): OK\n");

	return 0;
}
#endif
