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
#if HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif

#if EXT2_FLAT_INCLUDES
#include "ext2_fs.h"
#else
#include <linux/ext2_fs.h>
#endif

#include "e2p/e2p.h"
#include "ext2fs.h"
#include "jfs_user.h"

static void init_journal_superblock(journal_superblock_t *jsb,
				    __u32 blocksize, __u32 size, int flags)
{
	memset (jsb, 0, sizeof(*jsb));

	jsb->s_header.h_magic = htonl(JFS_MAGIC_NUMBER);
	if (flags & EXT2_MKJOURNAL_V1_SUPER)
		jsb->s_header.h_blocktype = htonl(JFS_SUPERBLOCK_V1);
	else
		jsb->s_header.h_blocktype = htonl(JFS_SUPERBLOCK_V2);
	jsb->s_blocksize = htonl(blocksize);
	jsb->s_maxlen = htonl(size);
	jsb->s_first = htonl(1);
	jsb->s_sequence = htonl(1);
}

/*
 * This function writes a journal using POSIX routines.  It is used
 * for creating external journals and creating journals on live
 * filesystems.
 */
static errcode_t write_journal_file(ext2_filsys fs, char *device,
				    blk_t size, int flags)
{
	errcode_t	retval;
	char		*buf = 0;
	journal_superblock_t	jsb;
	int		i, fd, ret_size;

	init_journal_superblock(&jsb, fs->blocksize, size, flags);

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

	for (i = 1; i < size; i++) {
		ret_size = write(fd, buf, fs->blocksize);
		if (ret_size < 0) {
			retval = errno;
			goto errout;
		}
		if (ret_size != fs->blocksize)
			goto errout;
	}
	close(fd);

	retval = 0;
errout:
	free(buf);
	return retval;
}

/*
 * Helper function for creating the journal using direct I/O routines
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
 * This function creates a journal using direct I/O routines.
 */
static errcode_t write_journal_inode(ext2_filsys fs, ino_t journal_ino,
				     blk_t size, int flags)
{
	journal_superblock_t	jsb;
	errcode_t		retval;
	struct ext2_inode	inode;
	struct mkjournal_struct	es;
	char			*buf;

	init_journal_superblock(&jsb, fs->blocksize, size, flags);

	if ((retval = ext2fs_read_bitmaps(fs)))
		return retval;

	if ((retval = ext2fs_read_inode(fs, journal_ino, &inode)))
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

	retval = ext2fs_block_iterate2(fs, journal_ino, BLOCK_FLAG_APPEND,
				       0, mkjournal_proc, &es);
	free(buf);
	if (es.err)
		return es.err;

	if ((retval = ext2fs_read_inode(fs, journal_ino, &inode)))
		return retval;

 	inode.i_size += fs->blocksize * size;
	inode.i_blocks += (fs->blocksize / 512) * es.newblocks;
	inode.i_mtime = inode.i_ctime = time(0);
	inode.i_links_count = 1;
	inode.i_mode = LINUX_S_IFREG | 0600;

	if ((retval = ext2fs_write_inode(fs, journal_ino, &inode)))
		return retval;

	return 0;
}

/*
 * This function adds a journal device to a filesystem
 */
errcode_t ext2fs_add_journal_device(ext2_filsys fs, char *device,
				    blk_t size, int flags)
{
	struct stat	st;
	errcode_t	retval;
	blk_t		dev_size;

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

	retval = write_journal_file(fs, device, size, flags);
	if (retval)
		return retval;
	
	fs->super->s_journal_inum = 0;
	fs->super->s_journal_dev = st.st_rdev;
	memset(fs->super->s_journal_uuid, 0,
	       sizeof(fs->super->s_journal_uuid));
	fs->super->s_feature_compat |= EXT3_FEATURE_COMPAT_HAS_JOURNAL;
	ext2fs_mark_super_dirty(fs);
	return 0;
}

/*
 * This function adds a journal inode to a filesystem, using either
 * POSIX routines if the filesystem is mounted, or using direct I/O
 * functions if it is not.
 */
errcode_t ext2fs_add_journal_inode(ext2_filsys fs, blk_t size, int flags)
{
	errcode_t		retval;
	ino_t			journal_ino;
	struct stat		st;
	char			jfile[1024];
	int			fd, mount_flags;

	if ((retval = ext2fs_check_mount_point(fs->device_name, &mount_flags,
					       jfile, sizeof(jfile)-10)))
		return retval;

	if (mount_flags & EXT2_MF_MOUNTED) {
		strcat(jfile, "/.journal");

		/* Create the journal file */
		if ((fd = open(jfile, O_CREAT|O_WRONLY, 0600)) < 0)
			return errno;
		close(fd);

		if ((retval = write_journal_file(fs, jfile, size, flags)))
			return retval;

		/* Get inode number of the journal file */
		if (stat(jfile, &st) < 0)
			return errno;

		if ((retval = fsetflags(jfile,
					EXT2_NODUMP_FL | EXT2_IMMUTABLE_FL)))
			return retval;
		
		journal_ino = st.st_ino;
	} else {
		journal_ino = EXT2_JOURNAL_INO;
		if ((retval = write_journal_inode(fs, journal_ino,
						  size, flags)))
			return retval;
	}
	
	fs->super->s_journal_inum = journal_ino;
	fs->super->s_journal_dev = 0;
	memset(fs->super->s_journal_uuid, 0,
	       sizeof(fs->super->s_journal_uuid));
	fs->super->s_feature_compat |= EXT3_FEATURE_COMPAT_HAS_JOURNAL;

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

	retval = ext2fs_add_journal_inode(fs, 1024);
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
