/*
 * badblocks.h --- internal device abstraction for badblocks
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 */

#ifndef BADBLOCKS_H
#define BADBLOCKS_H

struct bb_dev;

struct bb_dev_ops {
	errcode_t	(*open)(struct bb_dev *dev, int rw);
	ssize_t		(*pread)(struct bb_dev *dev, void *buf, size_t count,
				 ext2_loff_t offset);
	ssize_t		(*pwrite)(struct bb_dev *dev, const void *buf,
				  size_t count, ext2_loff_t offset);
	errcode_t	(*get_size)(struct bb_dev *dev,
				    unsigned int block_size, blk64_t *blocks);
	errcode_t	(*check_mounted)(struct bb_dev *dev, int *mount_flags);
	void		(*set_direct)(struct bb_dev *dev, int direct);
	errcode_t	(*flush)(struct bb_dev *dev);
	void		(*close)(struct bb_dev *dev);
};

struct bb_dev {
	const struct bb_dev_ops	*ops;
	const char		*name;
	int			fd;
	int			current_o_direct;
	void			*priv;
};

#endif /* BADBLOCKS_H */
