/*
 * mkjournal.c --- make a journal for a filesystem
 *
 * Copyright (C) 2000 Theodore Ts'o.
 * 
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 */

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

#if EXT2_FLAT_INCLUDES
#include "ext2_fs.h"
#else
#include <linux/ext2_fs.h>
#endif

#include "ext2fs.h"
#include "jfs_dat.h"

static void init_journal_superblock(journal_superblock_t *jsb,
				    __u32 blocksize, __u32 size)
{
	jsb->s_header.h_magic = htonl(JFS_MAGIC_NUMBER);
	jsb->s_header.h_blocktype = htonl(JFS_SUPERBLOCK);
	jsb->s_header.h_sequence = 0;
	jsb->s_blocksize = htonl(blocksize);
	jsb->s_maxlen = htonl(size);
	jsb->s_first = htonl(1);
	jsb->s_sequence = htonl(1);
	jsb->s_start = 0;
}
	
/*
 * This function adds a journal device to a filesystem
 */
errcode_t ext2fs_add_journal_device(ext2_filsys fs, char *device,
				    blk_t size)
{
	journal_superblock_t	jsb;
	struct stat	st;
	errcode_t	retval;
	char		*buf = 0;
	blk_t		dev_size;
	int		i, fd, ret_size;

	/* Make sure the device exists and is a block device */
	if (stat(device, &st) < 0)
		return errno;
	if (!S_ISBLK(st.st_mode))
		return EXT2_JOURNAL_NOT_BLOCK;	/* Must be a block device */
	
	/* Get the size of the device */
	if ((retval = ext2fs_get_device_size(device, fs->blocksize,
					     &dev_size)))
		return retval;
	
	if (!size)
		size = dev_size; /* Default to the size of the device */
	else if (size > dev_size) 
		return EINVAL;	/* Requested size bigger than device */

	init_journal_superblock(&jsb, fs->blocksize, size);

	/* Create a block buffer */
	buf = malloc(fs->blocksize);
	if (!buf)
		return ENOMEM;

	/* Open the device */
	if ((fd = open(device, O_WRONLY)) < 0) {
		retval = errno;
		goto errout;
	}

	/* Write the superblock out */
	memset(buf, 0, fs->blocksize);
	memcpy(buf, &jsb, sizeof(jsb));
	retval = EXT2_ET_SHORT_WRITE;
	ret_size = write(fd, buf, fs->blocksize);
	if (ret_size < 0) {
		errno = retval;
		goto errout;
	}
	if (ret_size != fs->blocksize)
		goto errout;
	memset(buf, 0, fs->blocksize);
	
	for (i=1; i < size; i++) {
		ret_size = write(fd, buf, fs->blocksize);
		if (ret_size < 0) {
			retval = errno;
			goto errout;
		}
		if (ret_size != fs->blocksize)
			goto errout;
	}
	close(fd);

	fs->super->s_journal_dev = st.st_rdev;
	fs->super->s_feature_compat |= EXT3_FEATURE_COMPAT_HAS_JOURNAL;
	ext2fs_mark_super_dirty(fs);

	return 0;
errout:
	if (buf)
		free(buf);
	return retval;
}

/*
 * Helper function for creating the journal in the filesystem
 */
struct mkjournal_struct {
	int		num_blocks;
	int		newblocks;
	char		*buf;
	errcode_t	err;
};

static int mkjournal_proc(ext2_filsys		fs,
			   blk_t		*blocknr,
			   e2_blkcnt_t		blockcnt,
			   blk_t		ref_block,
			   int			ref_offset,
			   void			*priv_data)
{
	struct mkjournal_struct *es = (struct mkjournal_struct *) priv_data;
	blk_t	new_blk;
	static blk_t	last_blk = 0;
	char		*block;
	errcode_t	retval;
	int		group;
	
	if (*blocknr) {
		last_blk = *blocknr;
		return 0;
	}
	retval = ext2fs_new_block(fs, last_blk, 0, &new_blk);
	if (retval) {
		es->err = retval;
		return BLOCK_ABORT;
	}
	if (blockcnt > 0)
		es->num_blocks--;

	es->newblocks++;
	retval = io_channel_write_blk(fs->io, new_blk, 1, es->buf);

	if (blockcnt == 0)
		memset(es->buf, 0, fs->blocksize);

	if (retval) {
		es->err = retval;
		return BLOCK_ABORT;
	}
	*blocknr = new_blk;
	ext2fs_mark_block_bitmap(fs->block_map, new_blk);
	ext2fs_mark_bb_dirty(fs);
	group = ext2fs_group_of_blk(fs, new_blk);
	fs->group_desc[group].bg_free_blocks_count--;
	fs->super->s_free_blocks_count--;
	ext2fs_mark_super_dirty(fs);

	if (es->num_blocks == 0)
		return (BLOCK_CHANGED | BLOCK_ABORT);
	else
		return BLOCK_CHANGED;
	
}

/*
 * This function adds a journal inode to a filesystem
 */
errcode_t ext2fs_add_journal_fs(ext2_filsys fs, blk_t size)
{
	journal_superblock_t	jsb;
	errcode_t		retval;
	struct ext2_inode	inode;
	struct mkjournal_struct	es;
	char			*buf;

	init_journal_superblock(&jsb, fs->blocksize, size);

	if ((retval = ext2fs_read_bitmaps(fs)))
		return retval;

	if ((retval = ext2fs_read_inode(fs, EXT2_JOURNAL_INO, &inode)))
		return retval;

	if (inode.i_blocks > 0)
		return EEXIST;

	/* Create the block buffer */
	buf = malloc(fs->blocksize);
	if (!buf)
		return ENOMEM;

	memset(buf, 0, fs->blocksize);
	memcpy(buf, &jsb, sizeof(jsb));

	es.num_blocks = size;
	es.newblocks = 0;
	es.buf = buf;
	es.err = 0;

	retval = ext2fs_block_iterate2(fs, EXT2_JOURNAL_INO, BLOCK_FLAG_APPEND,
				       0, mkjournal_proc, &es);
	free(buf);
	if (es.err)
		return es.err;

	if ((retval = ext2fs_read_inode(fs, EXT2_JOURNAL_INO, &inode)))
		return retval;

 	inode.i_size += fs->blocksize * size;
	inode.i_blocks += (fs->blocksize / 512) * es.newblocks;
	inode.i_mtime = inode.i_ctime = time(0);
	inode.i_links_count = 1;
	inode.i_mode = LINUX_S_IFREG | 0600;

	if ((retval = ext2fs_write_inode(fs, EXT2_JOURNAL_INO, &inode)))
		return retval;

	fs->super->s_feature_compat |= EXT3_FEATURE_COMPAT_HAS_JOURNAL;
	fs->super->s_journal_inum = EXT2_JOURNAL_INO;

	ext2fs_mark_super_dirty(fs);
	return 0;
}

#ifdef DEBUG
main(int argc, char **argv)
{
	errcode_t	retval;
	char		*device_name;
	ext2_filsys 	fs;

	if (argc < 2) {
		fprintf(stderr, "Usage: %s filesystem\n", argv[0]);
		exit(1);
	}
	device_name = argv[1];
	
	retval = ext2fs_open (device_name, EXT2_FLAG_RW, 0, 0,
			      unix_io_manager, &fs);
	if (retval) {
		com_err(argv[0], retval, "while opening %s", device_name);
		exit(1);
	}

	retval = ext2fs_add_journal_fs(fs, 1024);
	if (retval) {
		com_err(argv[0], retval, "while adding journal to %s",
			device_name);
		exit(1);
	}
	retval = ext2fs_flush(fs);
	if (retval) {
		printf("Warning, had trouble writing out superblocks.\n");
	}
	ext2fs_close(fs);
	exit(0);
	
}
#endif
