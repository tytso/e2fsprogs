/*
 * unix_io.c --- This is the Unix I/O interface to the I/O manager.
 *
 * Implements a one-block write-through cache.
 *
 * Copyright (C) 1993 Theodore Ts'o.  This file may be redistributed
 * under the terms of the GNU Public License.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "et/com_err.h"
#include "ext2_err.h"
#include "io.h"

struct unix_private_data {
	int	dev;
	int	flags;
	char	*buf;
	int	buf_block_nr;
};

static errcode_t unix_open(const char *name, int flags, io_channel *channel);
static errcode_t unix_close(io_channel channel);
static errcode_t unix_set_blksize(io_channel channel, int blksize);
static errcode_t unix_read_blk(io_channel channel, unsigned long block,
			       int count, void *data);
static errcode_t unix_write_blk(io_channel channel, unsigned long block,
				int count, const void *data);
static errcode_t unix_flush(io_channel channel);

struct struct_io_manager struct_unix_manager = {
	"Unix I/O Manager",
	unix_open,
	unix_close,
	unix_set_blksize,
	unix_read_blk,
	unix_write_blk,
	unix_flush
};

io_manager unix_io_manager = &struct_unix_manager;

static errcode_t unix_open(const char *name, int flags, io_channel *channel)
{
	io_channel	io = NULL;
	struct unix_private_data *data = NULL;
	errcode_t	retval;

	io = (io_channel) malloc(sizeof(struct struct_io_channel));
	if (!io)
		return ENOMEM;
	data = (struct unix_private_data *)
		malloc(sizeof(struct unix_private_data));
	if (!data) {
		retval = ENOMEM;
		goto cleanup;
	}
	io->manager = unix_io_manager;
	io->name = malloc(strlen(name)+1);
	if (!io->name) {
		retval = ENOMEM;
		goto cleanup;
	}
	strcpy(io->name, name);
	io->private_data = data;

	memset(data, 0, sizeof(struct unix_private_data));
	io->block_size = 1024;
	data->buf = malloc(io->block_size);
	data->buf_block_nr = -1;
	if (!data->buf) {
		retval = ENOMEM;
		goto cleanup;
	}
	data->dev = open(name, (flags & IO_FLAG_RW) ? O_RDWR : O_RDONLY);
	if (data->dev < 0) {
		retval = errno;
		goto cleanup;
	}
	*channel = io;
	return 0;

cleanup:
	if (io)
		free(io);
	if (data) {
		if (data->buf)
			free(data->buf);
		free(data);
	}
	return retval;
}

static errcode_t unix_close(io_channel channel)
{
	struct unix_private_data *data;
	errcode_t	retval = 0;

	data = (struct unix_private_data *) channel->private_data;
	if (close(data->dev) < 0)
		retval = errno;
	if (data->buf)
		free(data->buf);
	if (channel->private_data)
		free(channel->private_data);
	if (channel->name)
		free(channel->name);
	free(channel);
	return retval;
}

static errcode_t unix_set_blksize(io_channel channel, int blksize)
{
	struct unix_private_data *data;

	data = (struct unix_private_data *) channel->private_data;
	if (channel->block_size != blksize) {
		channel->block_size = blksize;
		free(data->buf);
		data->buf = malloc(blksize);
		if (!data->buf)
			return ENOMEM;
		data->buf_block_nr = -1;
	}
	return 0;
}


static errcode_t unix_read_blk(io_channel channel, unsigned long block,
			       int count, void *buf)
{
	struct unix_private_data *data;
	errcode_t	retval;
	size_t		size;
	int		actual = 0;

	data = (struct unix_private_data *) channel->private_data;

	/*
	 * If it's in the cache, use it!
	 */
	if ((count == 1) && (block == data->buf_block_nr)) {
		memcpy(buf, data->buf, channel->block_size);
		return 0;
	}
	size = (count < 0) ? -count : count * channel->block_size;
	if (lseek(data->dev, block * channel->block_size, SEEK_SET) !=
	    block * channel->block_size) {
		retval = errno;
		goto error_out;
	}
	actual = read(data->dev, buf, size);
	if (actual != size) {
		if (actual < 0)
			actual = 0;
		retval = EXT2_ET_SHORT_READ;
		goto error_out;
	}
	if (count == 1) {
		data->buf_block_nr = block;
		memcpy(data->buf, buf, size);	/* Update the cache */
	}
	return 0;
	
error_out:
	memset((char *) buf+actual, 0, size-actual);
	if (channel->read_error)
		retval = (channel->read_error)(channel, block, count, buf,
					       size, actual, retval);
	return retval;
}

static errcode_t unix_write_blk(io_channel channel, unsigned long block,
				int count, const void *buf)
{
	struct unix_private_data *data;
	size_t		size;
	int		actual = 0;
	errcode_t	retval;

	data = (struct unix_private_data *) channel->private_data;

	if (count == 1)
		size = channel->block_size;
	else {
		data->buf_block_nr = -1; 	/* Invalidate the cache */
		if (count < 0)
			size = -count;
		else
			size = count * channel->block_size;
	} 
		
	if (lseek(data->dev, block * channel->block_size, SEEK_SET) !=
	    block * channel->block_size) {
		retval = errno;
		goto error_out;
	}
	
	actual = write(data->dev, buf, size);
	if (actual != size) {
		retval = EXT2_ET_SHORT_WRITE;
		goto error_out;
	}

	if ((count == 1) && (block == data->buf_block_nr))
		memcpy(data->buf, buf, size); /* Update the cache */
	
	return 0;
	
error_out:
	if (channel->write_error)
		retval = (channel->write_error)(channel, block, count, buf,
						size, actual, retval);
	return retval;
}

/*
 * Flush data buffers to disk.  Since we are currently using a
 * write-through cache, this is a no-op.
 */
static errcode_t unix_flush(io_channel channel)
{
	return 0;
}

