/*
 * io.h --- the I/O manager abstraction
 * 
 * Copyright (C) 1993 Theodore Ts'o.  This file may be redistributed
 * under the terms of the GNU Public License.
 */

/*
 * ext2_loff_t is defined here since unix_io.c needs it.
 */
#if defined(__GNUC__) || defined(HAS_LONG_LONG)
typedef long long	ext2_loff_t;
#else
typedef long		ext2_loff_t;
#endif

/* llseek.c */
ext2_loff_t ext2_llseek (unsigned int, ext2_loff_t, unsigned int);

typedef struct struct_io_manager *io_manager;
typedef struct struct_io_channel *io_channel;

struct struct_io_channel {
	int		magic;
	io_manager	manager;
	char		*name;
	int		block_size;
	errcode_t	(*read_error)(io_channel channel,
				      unsigned long block,
				      int count,
				      void *data,
				      size_t size,
				      int actual_bytes_read,
				      errcode_t	error);
	errcode_t	(*write_error)(io_channel channel,
				       unsigned long block,
				       int count,
				       const void *data,
				       size_t size,
				       int actual_bytes_written,
				       errcode_t error);
	int		reserved[16];
	void		*private_data;
};

struct struct_io_manager {
	int magic;
	const char *name;
	errcode_t (*open)(const char *name, int flags, io_channel *channel);
	errcode_t (*close)(io_channel channel);
	errcode_t (*set_blksize)(io_channel channel, int blksize);
	errcode_t (*read_blk)(io_channel channel, unsigned long block,
			      int count, void *data);
	errcode_t (*write_blk)(io_channel channel, unsigned long block,
			       int count, const void *data);
	errcode_t (*flush)(io_channel channel);
	int		reserved[16];
};

#define IO_FLAG_RW	1

/*
 * Convenience functions....
 */
#define io_channel_close(c) 		((c)->manager->close((c)))
#define io_channel_set_blksize(c,s)	((c)->manager->set_blksize((c),s))
#define io_channel_read_blk(c,b,n,d)	((c)->manager->read_blk((c),b,n,d))
#define io_channel_write_blk(c,b,n,d)	((c)->manager->write_blk((c),b,n,d))
#define io_channel_flush(c) 		((c)->manager->flush((c)))
	
extern io_manager unix_io_manager;




