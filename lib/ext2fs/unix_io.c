/*
 * unix_io.c --- This is the Unix I/O interface to the I/O manager.
 *
 * Implements a one-block write-through cache.
 *
 * Copyright (C) 1993, 1994, 1995 Theodore Ts'o.
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 */

#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE

#include <stdio.h>
#include <string.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#if HAVE_ERRNO_H
#include <errno.h>
#endif
#include <fcntl.h>
#include <time.h>
#if HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#include "ext2_fs.h"
#include "ext2fs.h"

/*
 * For checking structure magic numbers...
 */

#define EXT2_CHECK_MAGIC(struct, code) \
	  if ((struct)->magic != (code)) return (code)

struct unix_cache {
	char		*buf;
	unsigned long	block;
	int		access_time;
	int		dirty:1;
	int		in_use:1;
};

#define CACHE_SIZE 8
#define WRITE_VIA_CACHE_SIZE 4	/* Must be smaller than CACHE_SIZE */

struct unix_private_data {
	int	magic;
	int	dev;
	int	flags;
	int	access_time;
	struct unix_cache cache[CACHE_SIZE];
};

static errcode_t unix_open(const char *name, int flags, io_channel *channel);
static errcode_t unix_close(io_channel channel);
static errcode_t unix_set_blksize(io_channel channel, int blksize);
static errcode_t unix_read_blk(io_channel channel, unsigned long block,
			       int count, void *data);
static errcode_t unix_write_blk(io_channel channel, unsigned long block,
				int count, const void *data);
static errcode_t unix_flush(io_channel channel);
static errcode_t unix_write_byte(io_channel channel, unsigned long offset,
				int size, const void *data);

static struct struct_io_manager struct_unix_manager = {
	EXT2_ET_MAGIC_IO_MANAGER,
	"Unix I/O Manager",
	unix_open,
	unix_close,
	unix_set_blksize,
	unix_read_blk,
	unix_write_blk,
	unix_flush,
	unix_write_byte
};

io_manager unix_io_manager = &struct_unix_manager;

/*
 * Here are the raw I/O functions
 */
static errcode_t raw_read_blk(io_channel channel,
			      struct unix_private_data *data,
			      unsigned long block,
			      int count, void *buf)
{
	errcode_t	retval;
	size_t		size;
	ext2_loff_t	location;
	int		actual = 0;

	size = (count < 0) ? -count : count * channel->block_size;
	location = (ext2_loff_t) block * channel->block_size;
	if (ext2fs_llseek(data->dev, location, SEEK_SET) != location) {
		retval = errno ? errno : EXT2_ET_LLSEEK_FAILED;
		goto error_out;
	}
	actual = read(data->dev, buf, size);
	if (actual != size) {
		if (actual < 0)
			actual = 0;
		retval = EXT2_ET_SHORT_READ;
		goto error_out;
	}
	return 0;
	
error_out:
	memset((char *) buf+actual, 0, size-actual);
	if (channel->read_error)
		retval = (channel->read_error)(channel, block, count, buf,
					       size, actual, retval);
	return retval;
}

static errcode_t raw_write_blk(io_channel channel,
			       struct unix_private_data *data,
			       unsigned long block,
			       int count, const void *buf)
{
	size_t		size;
	ext2_loff_t	location;
	int		actual = 0;
	errcode_t	retval;

	if (count == 1)
		size = channel->block_size;
	else {
		if (count < 0)
			size = -count;
		else
			size = count * channel->block_size;
	}

	location = (ext2_loff_t) block * channel->block_size;
	if (ext2fs_llseek(data->dev, location, SEEK_SET) != location) {
		retval = errno ? errno : EXT2_ET_LLSEEK_FAILED;
		goto error_out;
	}
	
	actual = write(data->dev, buf, size);
	if (actual != size) {
		retval = EXT2_ET_SHORT_WRITE;
		goto error_out;
	}
	return 0;
	
error_out:
	if (channel->write_error)
		retval = (channel->write_error)(channel, block, count, buf,
						size, actual, retval);
	return retval;
}


/*
 * Here we implement the cache functions
 */

/* Allocate the cache buffers */
static errcode_t alloc_cache(io_channel channel,
			     struct unix_private_data *data)
{
	errcode_t		retval;
	struct unix_cache	*cache;
	int			i;
	
	data->access_time = 0;
	for (i=0, cache = data->cache; i < CACHE_SIZE; i++, cache++) {
		cache->block = 0;
		cache->access_time = 0;
		cache->dirty = 0;
		cache->in_use = 0;
		if ((retval = ext2fs_get_mem(channel->block_size,
					     (void **) &cache->buf)))
			return retval;
	}
	return 0;
}

/* Free the cache buffers */
static void free_cache(io_channel channel, 
		       struct unix_private_data *data)
{
	struct unix_cache	*cache;
	int			i;
	
	data->access_time = 0;
	for (i=0, cache = data->cache; i < CACHE_SIZE; i++, cache++) {
		cache->block = 0;
		cache->access_time = 0;
		cache->dirty = 0;
		cache->in_use = 0;
		if (cache->buf)
			ext2fs_free_mem((void **) &cache->buf);
		cache->buf = 0;
	}
}

/*
 * Try to find a block in the cache.  If get_cache is non-zero, then
 * if the block isn't in the cache, evict the oldest block in the
 * cache and create a new cache entry for the requested block.
 */
static struct unix_cache *find_cached_block(io_channel channel,
					    struct unix_private_data *data,
					    unsigned long block,
					    int get_cache)
{
	struct unix_cache	*cache, *unused_cache, *oldest_cache;
	int			i;
	
	unused_cache = oldest_cache = 0;
	for (i=0, cache = data->cache; i < CACHE_SIZE; i++, cache++) {
		if (!cache->in_use) {
			unused_cache = cache;
			continue;
		}
		if (cache->block == block) {
			cache->access_time = ++data->access_time;
			return cache;
		}
		if (!oldest_cache ||
		    (cache->access_time < oldest_cache->access_time))
			oldest_cache = cache;
	}
	if (!get_cache)
		return 0;
	
	/*
	 * Try to allocate cache slot.
	 */
	if (unused_cache)
		cache = unused_cache;
	else {
		cache = oldest_cache;
		if (cache->dirty)
			raw_write_blk(channel, data,
				      cache->block, 1, cache->buf);
	}
	cache->in_use = 1;
	cache->block = block;
	cache->access_time = ++data->access_time;
	return cache;
}

/*
 * Flush all of the blocks in the cache
 */
static errcode_t flush_cached_blocks(io_channel channel,
				     struct unix_private_data *data,
				     int invalidate)

{
	struct unix_cache	*cache;
	errcode_t		retval, retval2;
	int			i;
	
	retval2 = 0;
	for (i=0, cache = data->cache; i < CACHE_SIZE; i++, cache++) {
		if (!cache->in_use)
			continue;
		
		if (invalidate)
			cache->in_use = 0;
		
		if (!cache->dirty)
			continue;
		
		retval = raw_write_blk(channel, data,
				       cache->block, 1, cache->buf);
		if (retval)
			retval2 = retval;
		else
			cache->dirty = 0;
	}
	return retval2;
}



static errcode_t unix_open(const char *name, int flags, io_channel *channel)
{
	io_channel	io = NULL;
	struct unix_private_data *data = NULL;
	errcode_t	retval;
	int		open_flags;

	if (name == 0)
		return EXT2_ET_BAD_DEVICE_NAME;
	retval = ext2fs_get_mem(sizeof(struct struct_io_channel),
				(void **) &io);
	if (retval)
		return retval;
	memset(io, 0, sizeof(struct struct_io_channel));
	io->magic = EXT2_ET_MAGIC_IO_CHANNEL;
	retval = ext2fs_get_mem(sizeof(struct unix_private_data),
				(void **) &data);
	if (retval)
		goto cleanup;

	io->manager = unix_io_manager;
	retval = ext2fs_get_mem(strlen(name)+1, (void **) &io->name);
	if (retval)
		goto cleanup;

	strcpy(io->name, name);
	io->private_data = data;
	io->block_size = 1024;
	io->read_error = 0;
	io->write_error = 0;
	io->refcount = 1;

	memset(data, 0, sizeof(struct unix_private_data));
	data->magic = EXT2_ET_MAGIC_UNIX_IO_CHANNEL;

	if ((retval = alloc_cache(io, data)))
		goto cleanup;
	
	open_flags = (flags & IO_FLAG_RW) ? O_RDWR : O_RDONLY;
#ifdef HAVE_OPEN64
	data->dev = open64(name, open_flags);
#else
	data->dev = open(name, open_flags);
#endif
	if (data->dev < 0) {
		retval = errno;
		goto cleanup;
	}
	*channel = io;
	return 0;

cleanup:
	if (data) {
		free_cache(io, data);
		ext2fs_free_mem((void **) &data);
	}
	if (io)
		ext2fs_free_mem((void **) &io);
	return retval;
}

static errcode_t unix_close(io_channel channel)
{
	struct unix_private_data *data;
	errcode_t	retval = 0;

	EXT2_CHECK_MAGIC(channel, EXT2_ET_MAGIC_IO_CHANNEL);
	data = (struct unix_private_data *) channel->private_data;
	EXT2_CHECK_MAGIC(data, EXT2_ET_MAGIC_UNIX_IO_CHANNEL);

	if (--channel->refcount > 0)
		return 0;

	retval = flush_cached_blocks(channel, data, 0);

	if (close(data->dev) < 0)
		retval = errno;
	free_cache(channel, data);
	if (channel->private_data)
		ext2fs_free_mem((void **) &channel->private_data);
	if (channel->name)
		ext2fs_free_mem((void **) &channel->name);
	ext2fs_free_mem((void **) &channel);
	return retval;
}

static errcode_t unix_set_blksize(io_channel channel, int blksize)
{
	struct unix_private_data *data;
	errcode_t		retval;

	EXT2_CHECK_MAGIC(channel, EXT2_ET_MAGIC_IO_CHANNEL);
	data = (struct unix_private_data *) channel->private_data;
	EXT2_CHECK_MAGIC(data, EXT2_ET_MAGIC_UNIX_IO_CHANNEL);

	if (channel->block_size != blksize) {
		if ((retval = flush_cached_blocks(channel, data, 0)))
			return retval;
		
		channel->block_size = blksize;
		free_cache(channel, data);
		if ((retval = alloc_cache(channel, data)))
			return retval;
	}
	return 0;
}


static errcode_t unix_read_blk(io_channel channel, unsigned long block,
			       int count, void *buf)
{
	struct unix_private_data *data;
	struct unix_cache *cache;
	errcode_t	retval;
	char		*cp;
	int		i, j;

	EXT2_CHECK_MAGIC(channel, EXT2_ET_MAGIC_IO_CHANNEL);
	data = (struct unix_private_data *) channel->private_data;
	EXT2_CHECK_MAGIC(data, EXT2_ET_MAGIC_UNIX_IO_CHANNEL);

	/*
	 * If we're doing an odd-sized read, flush out the cache and
	 * then do a direct read.
	 */
	if (count < 0) {
		if ((retval = flush_cached_blocks(channel, data, 0)))
			return retval;
		return raw_read_blk(channel, data, block, count, buf);
	}

	cp = buf;
	while (count > 0) {
		/* If it's in the cache, use it! */
		if ((cache = find_cached_block(channel, data, block, 0))) {
#ifdef DEBUG
			printf("Using cached block %d\n", block);
#endif
			memcpy(cp, cache->buf, channel->block_size);
			count--;
			block++;
			cp += channel->block_size;
			continue;
		}
		/*
		 * Find the number of uncached blocks so we can do a
		 * single read request
		 */
		for (i=1; i < count; i++)
			if (find_cached_block(channel, data, block+i, 0))
				break;
#ifdef DEBUG
		printf("Reading %d blocks starting at %d\n", i, block);
#endif
		if ((retval = raw_read_blk(channel, data, block, i, cp)))
			return retval;
		
		/* Save the results in the cache */
		for (j=0; j < i; j++) {
			count--;
			cache = find_cached_block(channel, data, block++, 1);
			if (cache)
				memcpy(cache->buf, cp, channel->block_size);
			cp += channel->block_size;
		}
	}
	return 0;
}

static errcode_t unix_write_blk(io_channel channel, unsigned long block,
				int count, const void *buf)
{
	struct unix_private_data *data;
	struct unix_cache *cache;
	errcode_t	retval = 0, retval2;
	const char	*cp;
	int		writethrough;

	EXT2_CHECK_MAGIC(channel, EXT2_ET_MAGIC_IO_CHANNEL);
	data = (struct unix_private_data *) channel->private_data;
	EXT2_CHECK_MAGIC(data, EXT2_ET_MAGIC_UNIX_IO_CHANNEL);

	/*
	 * If we're doing an odd-sized write or a very large write,
	 * flush out the cache completely and then do a direct write.
	 */
	if (count < 0 || count > WRITE_VIA_CACHE_SIZE) {
		if ((retval = flush_cached_blocks(channel, data, 1)))
			return retval;
		return raw_write_blk(channel, data, block, count, buf);
	}

	/*
	 * For a moderate-sized multi-block write, first force a write
	 * if we're in write-through cache mode, and then fill the
	 * cache with the blocks.
	 */
	writethrough = channel->flags & CHANNEL_FLAGS_WRITETHROUGH;
	if (writethrough)
		retval = raw_write_blk(channel, data, block, count, buf);
	
	cp = buf;
	while (count > 0) {
		cache = find_cached_block(channel, data, block, 1);
		if (!cache) {
			/*
			 * Oh shit, we couldn't get cache descriptor.
			 * Force the write directly.
			 */
			if ((retval2 = raw_write_blk(channel, data, block,
						1, cp)))
				retval = retval2;
		} else {
			memcpy(cache->buf, cp, channel->block_size);
			cache->dirty = !writethrough;
		}
		count--;
		block++;
		cp += channel->block_size;
	}
	return retval;
}

static errcode_t unix_write_byte(io_channel channel, unsigned long offset,
				 int size, const void *buf)
{
	struct unix_private_data *data;
	errcode_t	retval = 0;
	size_t		actual;

	EXT2_CHECK_MAGIC(channel, EXT2_ET_MAGIC_IO_CHANNEL);
	data = (struct unix_private_data *) channel->private_data;
	EXT2_CHECK_MAGIC(data, EXT2_ET_MAGIC_UNIX_IO_CHANNEL);

	/*
	 * Flush out the cache completely
	 */
	if ((retval = flush_cached_blocks(channel, data, 1)))
		return retval;

	if (lseek(data->dev, offset, SEEK_SET) < 0)
		return errno;
	
	actual = write(data->dev, buf, size);
	if (actual != size)
		return EXT2_ET_SHORT_WRITE;

	return 0;
}

/*
 * Flush data buffers to disk.  
 */
static errcode_t unix_flush(io_channel channel)
{
	struct unix_private_data *data;
	errcode_t retval = 0;
	
	EXT2_CHECK_MAGIC(channel, EXT2_ET_MAGIC_IO_CHANNEL);
	data = (struct unix_private_data *) channel->private_data;
	EXT2_CHECK_MAGIC(data, EXT2_ET_MAGIC_UNIX_IO_CHANNEL);

	retval = flush_cached_blocks(channel, data, 0);
	fsync(data->dev);
	return retval;
}

