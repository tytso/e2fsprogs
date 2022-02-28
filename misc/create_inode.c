/*
 * create_inode.c --- create an inode
 *
 * Copyright (C) 2014 Robert Yang <liezhi.yang@windriver.com>
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU library
 * General Public License, version 2.
 * %End-Header%
 */

#define _FILE_OFFSET_BITS       64
#define _LARGEFILE64_SOURCE     1
#define _GNU_SOURCE		1

#include "config.h"
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <limits.h> /* for PATH_MAX */
#include <dirent.h> /* for scandir() and alphasort() */
#if defined HAVE_SYS_XATTR_H
#include <sys/xattr.h>
#elif defined HAVE_ATTR_XATTR_H
#include <attr/xattr.h>
#endif
#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif
#ifdef HAVE_SYS_SYSMACROS_H
#include <sys/sysmacros.h>
#endif

#include <ext2fs/ext2fs.h>
#include <ext2fs/ext2_types.h>
#include <ext2fs/fiemap.h>

#include <archive.h>
#include <archive_entry.h>

#include "create_inode.h"
#include "support/nls-enable.h"

/* 64KiB is the minimum blksize to best minimize system call overhead. */
#define COPY_FILE_BUFLEN	65536

static int ext2_file_type(unsigned int mode)
{
	if (LINUX_S_ISREG(mode))
		return EXT2_FT_REG_FILE;

	if (LINUX_S_ISDIR(mode))
		return EXT2_FT_DIR;

	if (LINUX_S_ISCHR(mode))
		return EXT2_FT_CHRDEV;

	if (LINUX_S_ISBLK(mode))
		return EXT2_FT_BLKDEV;

	if (LINUX_S_ISLNK(mode))
		return EXT2_FT_SYMLINK;

	if (LINUX_S_ISFIFO(mode))
		return EXT2_FT_FIFO;

	if (LINUX_S_ISSOCK(mode))
		return EXT2_FT_SOCK;

	return 0;
}

/* Link an inode number to a directory */
static errcode_t add_link(ext2_filsys fs, ext2_ino_t parent_ino,
			  ext2_ino_t ino, const char *name)
{
	struct ext2_inode	inode;
	errcode_t		retval;

	retval = ext2fs_read_inode(fs, ino, &inode);
        if (retval) {
		com_err(__func__, retval, _("while reading inode %u"), ino);
		return retval;
	}

	retval = ext2fs_link(fs, parent_ino, name, ino,
			     ext2_file_type(inode.i_mode));
	if (retval == EXT2_ET_DIR_NO_SPACE) {
		retval = ext2fs_expand_dir(fs, parent_ino);
		if (retval) {
			com_err(__func__, retval,
				_("while expanding directory"));
			return retval;
		}
		retval = ext2fs_link(fs, parent_ino, name, ino,
				     ext2_file_type(inode.i_mode));
	}
	if (retval) {
		com_err(__func__, retval, _("while linking \"%s\""), name);
		return retval;
	}

	inode.i_links_count++;

	retval = ext2fs_write_inode(fs, ino, &inode);
	if (retval)
		com_err(__func__, retval, _("while writing inode %u"), ino);

	return retval;
}

/* Set the uid, gid, mode and time for the inode */
static errcode_t set_inode_extra(ext2_filsys fs, ext2_ino_t ino,
				 const struct stat *st)
{
	errcode_t		retval;
	struct ext2_inode	inode;

	retval = ext2fs_read_inode(fs, ino, &inode);
        if (retval) {
		com_err(__func__, retval, _("while reading inode %u"), ino);
		return retval;
	}

	inode.i_uid = st->st_uid;
	ext2fs_set_i_uid_high(inode, st->st_uid >> 16);
	inode.i_gid = st->st_gid;
	ext2fs_set_i_gid_high(inode, st->st_gid >> 16);
	inode.i_mode = (LINUX_S_IFMT & inode.i_mode) | (~S_IFMT & st->st_mode);
	inode.i_atime = st->st_atime;
	inode.i_mtime = st->st_mtime;
	inode.i_ctime = st->st_ctime;

	retval = ext2fs_write_inode(fs, ino, &inode);
	if (retval)
		com_err(__func__, retval, _("while writing inode %u"), ino);
	return retval;
}

#ifdef HAVE_LLISTXATTR
static errcode_t set_inode_xattr(ext2_filsys fs, ext2_ino_t ino,
				 const char *filename)
{
	errcode_t			retval, close_retval;
	struct ext2_xattr_handle	*handle;
	ssize_t				size, value_size;
	char				*list = NULL;
	int				i;

	if (no_copy_xattrs)
		return 0;

	size = llistxattr(filename, NULL, 0);
	if (size == -1) {
		retval = errno;
		com_err(__func__, retval, _("while listing attributes of \"%s\""),
			filename);
		return retval;
	} else if (size == 0) {
		return 0;
	}

	retval = ext2fs_xattrs_open(fs, ino, &handle);
	if (retval) {
		if (retval == EXT2_ET_MISSING_EA_FEATURE)
			return 0;
		com_err(__func__, retval, _("while opening inode %u"), ino);
		return retval;
	}

	retval = ext2fs_xattrs_read(handle);
	if (retval) {
		com_err(__func__, retval,
			_("while reading xattrs for inode %u"), ino);
		goto out;
	}

	retval = ext2fs_get_mem(size, &list);
	if (retval) {
		com_err(__func__, retval, _("while allocating memory"));
		goto out;
	}

	size = llistxattr(filename, list, size);
	if (size == -1) {
		retval = errno;
		com_err(__func__, retval, _("while listing attributes of \"%s\""),
			filename);
		goto out;
        }

	for (i = 0; i < size; i += strlen(&list[i]) + 1) {
		const char *name = &list[i];
		char *value;

		value_size = lgetxattr(filename, name, NULL, 0);
		if (value_size == -1) {
			retval = errno;
			com_err(__func__, retval,
				_("while reading attribute \"%s\" of \"%s\""),
				name, filename);
			break;
		}

		retval = ext2fs_get_mem(value_size, &value);
		if (retval) {
			com_err(__func__, retval, _("while allocating memory"));
			break;
		}

		value_size = lgetxattr(filename, name, value, value_size);
		if (value_size == -1) {
			ext2fs_free_mem(&value);
			retval = errno;
			com_err(__func__, retval,
				_("while reading attribute \"%s\" of \"%s\""),
				name, filename);
			break;
		}

		retval = ext2fs_xattr_set(handle, name, value, value_size);
		ext2fs_free_mem(&value);
		if (retval) {
			com_err(__func__, retval,
				_("while writing attribute \"%s\" to inode %u"),
				name, ino);
			break;
		}

	}
 out:
	ext2fs_free_mem(&list);
	close_retval = ext2fs_xattrs_close(&handle);
	if (close_retval) {
		com_err(__func__, retval, _("while closing inode %u"), ino);
		retval = retval ? retval : close_retval;
	}
	return retval;
}
#else /* HAVE_LLISTXATTR */
static errcode_t set_inode_xattr(ext2_filsys fs EXT2FS_ATTR((unused)),
				 ext2_ino_t ino EXT2FS_ATTR((unused)),
				 const char *filename EXT2FS_ATTR((unused)))
{
	return 0;
}
#endif  /* HAVE_LLISTXATTR */

static errcode_t set_inode_xattr_archive(ext2_filsys fs, ext2_ino_t ino,
				 struct archive_entry *entry)
{
	errcode_t			retval, close_retval;
	struct ext2_xattr_handle	*handle;

	if (no_copy_xattrs)
		return 0;

	retval = ext2fs_xattrs_open(fs, ino, &handle);
	if (retval) {
		if (retval == EXT2_ET_MISSING_EA_FEATURE)
			return 0;
		com_err(__func__, retval, _("while opening inode %u"), ino);
		return retval;
	}

	retval = ext2fs_xattrs_read(handle);
	if (retval) {
		com_err(__func__, retval,
			_("while reading xattrs for inode %u"), ino);
		goto out;
	}

	archive_entry_xattr_reset(entry);
	for (;;) {
		const char *name;
		const void *value;
		size_t value_size;

		retval = archive_entry_xattr_next(entry, &name, &value, &value_size);
		if (retval == ARCHIVE_WARN) {
			retval = 0;
			break;
		}

		retval = ext2fs_xattr_set(handle, name, value, value_size);
		if (retval) {
			com_err(__func__, retval,
				_("while writing attribute \"%s\" to inode %u"),
				name, ino);
			break;
		}
	}
 out:
	close_retval = ext2fs_xattrs_close(&handle);
	if (close_retval) {
		com_err(__func__, retval, _("while closing inode %u"), ino);
		retval = retval ? retval : close_retval;
	}
	return retval;
}

#ifndef _WIN32
/* Make a special files (block and character devices), fifo's, and sockets  */
errcode_t do_mknod_internal(ext2_filsys fs, ext2_ino_t cwd, const char *name,
			    unsigned int st_mode, unsigned int st_rdev)
{
	ext2_ino_t		ino;
	errcode_t		retval;
	struct ext2_inode	inode;
	unsigned long		devmajor, devminor, mode;
	int			filetype;

	switch(st_mode & S_IFMT) {
	case S_IFCHR:
		mode = LINUX_S_IFCHR;
		filetype = EXT2_FT_CHRDEV;
		break;
	case S_IFBLK:
		mode = LINUX_S_IFBLK;
		filetype =  EXT2_FT_BLKDEV;
		break;
	case S_IFIFO:
		mode = LINUX_S_IFIFO;
		filetype = EXT2_FT_FIFO;
		break;
#ifndef _WIN32
	case S_IFSOCK:
		mode = LINUX_S_IFSOCK;
		filetype = EXT2_FT_SOCK;
		break;
#endif
	default:
		return EXT2_ET_INVALID_ARGUMENT;
	}

	retval = ext2fs_new_inode(fs, cwd, 010755, 0, &ino);
	if (retval) {
		com_err(__func__, retval, _("while allocating inode \"%s\""),
			name);
		return retval;
	}

#ifdef DEBUGFS
	printf("Allocated inode: %u\n", ino);
#endif
	retval = ext2fs_link(fs, cwd, name, ino, filetype);
	if (retval == EXT2_ET_DIR_NO_SPACE) {
		retval = ext2fs_expand_dir(fs, cwd);
		if (retval) {
			com_err(__func__, retval,
				_("while expanding directory"));
			return retval;
		}
		retval = ext2fs_link(fs, cwd, name, ino, filetype);
	}
	if (retval) {
		com_err(name, retval, _("while creating inode \"%s\""), name);
		return retval;
	}
	if (ext2fs_test_inode_bitmap2(fs->inode_map, ino))
		com_err(__func__, 0, "Warning: inode already set");
	ext2fs_inode_alloc_stats2(fs, ino, +1, 0);
	memset(&inode, 0, sizeof(inode));
	inode.i_mode = mode;
	inode.i_atime = inode.i_ctime = inode.i_mtime =
		fs->now ? fs->now : time(0);

	if (filetype != S_IFIFO) {
		devmajor = major(st_rdev);
		devminor = minor(st_rdev);

		if ((devmajor < 256) && (devminor < 256)) {
			inode.i_block[0] = devmajor * 256 + devminor;
			inode.i_block[1] = 0;
		} else {
			inode.i_block[0] = 0;
			inode.i_block[1] = (devminor & 0xff) | (devmajor << 8) |
					   ((devminor & ~0xff) << 12);
		}
	}
	inode.i_links_count = 1;

	retval = ext2fs_write_new_inode(fs, ino, &inode);
	if (retval)
		com_err(__func__, retval, _("while writing inode %u"), ino);

	return retval;
}
#endif

/* Make a symlink name -> target */
errcode_t do_symlink_internal(ext2_filsys fs, ext2_ino_t cwd, const char *name,
			      const char *target, ext2_ino_t root)
{
	char			*cp;
	ext2_ino_t		parent_ino;
	errcode_t		retval;

	cp = strrchr(name, '/');
	if (cp) {
		*cp = 0;
		retval = ext2fs_namei(fs, root, cwd, name, &parent_ino);
		if (retval) {
			com_err(name, retval, 0);
			return retval;
		}
		name = cp+1;
	} else
		parent_ino = cwd;

	retval = ext2fs_symlink(fs, parent_ino, 0, name, target);
	if (retval == EXT2_ET_DIR_NO_SPACE) {
		retval = ext2fs_expand_dir(fs, parent_ino);
		if (retval) {
			com_err("do_symlink_internal", retval,
				_("while expanding directory"));
			return retval;
		}
		retval = ext2fs_symlink(fs, parent_ino, 0, name, target);
	}
	if (retval)
		com_err("ext2fs_symlink", retval,
			_("while creating symlink \"%s\""), name);
	return retval;
}

/* Make a directory in the fs */
errcode_t do_mkdir_internal(ext2_filsys fs, ext2_ino_t cwd, const char *name,
			    ext2_ino_t root)
{
	char			*cp;
	ext2_ino_t		parent_ino;
	errcode_t		retval;


	cp = strrchr(name, '/');
	if (cp) {
		*cp = 0;
		retval = ext2fs_namei(fs, root, cwd, name, &parent_ino);
		if (retval) {
			com_err(name, retval, _("while looking up \"%s\""),
				name);
			return retval;
		}
		name = cp+1;
	} else
		parent_ino = cwd;

	retval = ext2fs_mkdir(fs, parent_ino, 0, name);
	if (retval == EXT2_ET_DIR_NO_SPACE) {
		retval = ext2fs_expand_dir(fs, parent_ino);
		if (retval) {
			com_err(__func__, retval,
				_("while expanding directory"));
			return retval;
		}
		retval = ext2fs_mkdir(fs, parent_ino, 0, name);
	}
	if (retval)
		com_err("ext2fs_mkdir", retval,
			_("while creating directory \"%s\""), name);
	return retval;
}

#if !defined HAVE_PREAD64 && !defined HAVE_PREAD
static ssize_t my_pread(int fd, void *buf, size_t count, off_t offset)
{
	if (lseek(fd, offset, SEEK_SET) < 0)
		return 0;

	return read(fd, buf, count);
}
#endif /* !defined HAVE_PREAD64 && !defined HAVE_PREAD */

static errcode_t copy_file_chunk(ext2_filsys fs, int fd, ext2_file_t e2_file,
				 off_t start, off_t end, char *buf,
				 char *zerobuf)
{
	off_t off, bpos;
	ssize_t got, blen;
	unsigned int written;
	char *ptr;
	errcode_t err = 0;

	for (off = start; off < end; off += COPY_FILE_BUFLEN) {
#ifdef HAVE_PREAD64
		got = pread64(fd, buf, COPY_FILE_BUFLEN, off);
#elif HAVE_PREAD
		got = pread(fd, buf, COPY_FILE_BUFLEN, off);
#else
		got = my_pread(fd, buf, COPY_FILE_BUFLEN, off);
#endif
		if (got < 0) {
			err = errno;
			goto fail;
		}
		for (bpos = 0, ptr = buf; bpos < got; bpos += fs->blocksize) {
			blen = fs->blocksize;
			if (blen > got - bpos)
				blen = got - bpos;
			if (memcmp(ptr, zerobuf, blen) == 0) {
				ptr += blen;
				continue;
			}
			err = ext2fs_file_llseek(e2_file, off + bpos,
						 EXT2_SEEK_SET, NULL);
			if (err)
				goto fail;
			while (blen > 0) {
				err = ext2fs_file_write(e2_file, ptr, blen,
							&written);
				if (err)
					goto fail;
				if (written == 0) {
					err = EIO;
					goto fail;
				}
				blen -= written;
				ptr += written;
			}
		}
	}
fail:
	return err;
}

#if defined(SEEK_DATA) && defined(SEEK_HOLE)
static errcode_t try_lseek_copy(ext2_filsys fs, int fd, struct stat *statbuf,
				ext2_file_t e2_file, char *buf, char *zerobuf)
{
	off_t data = 0, hole;
	off_t data_blk, hole_blk;
	errcode_t err = 0;

	/* Try to use SEEK_DATA and SEEK_HOLE */
	while (data < statbuf->st_size) {
		data = lseek(fd, data, SEEK_DATA);
		if (data < 0) {
			if (errno == ENXIO)
				break;
			return EXT2_ET_UNIMPLEMENTED;
		}
		hole = lseek(fd, data, SEEK_HOLE);
		if (hole < 0)
			return EXT2_ET_UNIMPLEMENTED;

		data_blk = data & ~(off_t)(fs->blocksize - 1);
		hole_blk = ((hole + (off_t)(fs->blocksize - 1)) &
			    ~(off_t)(fs->blocksize - 1));
		err = copy_file_chunk(fs, fd, e2_file, data_blk, hole_blk, buf,
				      zerobuf);
		if (err)
			return err;

		data = hole;
	}

	return err;
}
#endif /* SEEK_DATA and SEEK_HOLE */

#if defined(FS_IOC_FIEMAP)
static errcode_t try_fiemap_copy(ext2_filsys fs, int fd, ext2_file_t e2_file,
				 char *buf, char *zerobuf)
{
#define EXTENT_MAX_COUNT 512
	struct fiemap *fiemap_buf;
	struct fiemap_extent *ext_buf, *ext;
	int ext_buf_size, fie_buf_size;
	off_t pos = 0;
	unsigned int i;
	errcode_t err;

	ext_buf_size = EXTENT_MAX_COUNT * sizeof(struct fiemap_extent);
	fie_buf_size = sizeof(struct fiemap) + ext_buf_size;

	err = ext2fs_get_memzero(fie_buf_size, &fiemap_buf);
	if (err)
		return err;

	ext_buf = fiemap_buf->fm_extents;
	memset(fiemap_buf, 0, fie_buf_size);
	fiemap_buf->fm_length = FIEMAP_MAX_OFFSET;
	fiemap_buf->fm_flags |= FIEMAP_FLAG_SYNC;
	fiemap_buf->fm_extent_count = EXTENT_MAX_COUNT;

	do {
		fiemap_buf->fm_start = pos;
		memset(ext_buf, 0, ext_buf_size);
		err = ioctl(fd, FS_IOC_FIEMAP, fiemap_buf);
		if (err < 0 && (errno == EOPNOTSUPP || errno == ENOTTY)) {
			err = EXT2_ET_UNIMPLEMENTED;
			goto out;
		} else if (err < 0) {
			err = errno;
			goto out;
		} else if (fiemap_buf->fm_mapped_extents == 0)
			goto out;
		for (i = 0, ext = ext_buf; i < fiemap_buf->fm_mapped_extents;
		     i++, ext++) {
			err = copy_file_chunk(fs, fd, e2_file, ext->fe_logical,
					      ext->fe_logical + ext->fe_length,
					      buf, zerobuf);
			if (err)
				goto out;
		}

		ext--;
		/* Record file's logical offset this time */
		pos = ext->fe_logical + ext->fe_length;
		/*
		 * If fm_extents array has been filled and
		 * there are extents left, continue to cycle.
		 */
	} while (fiemap_buf->fm_mapped_extents == EXTENT_MAX_COUNT &&
		 !(ext->fe_flags & FIEMAP_EXTENT_LAST));
out:
	ext2fs_free_mem(&fiemap_buf);
	return err;
}
#endif /* FS_IOC_FIEMAP */

static errcode_t copy_file(ext2_filsys fs, int fd, struct stat *statbuf,
			   ext2_ino_t ino)
{
	ext2_file_t e2_file;
	char *buf = NULL, *zerobuf = NULL;
	errcode_t err, close_err;

	err = ext2fs_file_open(fs, ino, EXT2_FILE_WRITE, &e2_file);
	if (err)
		return err;

	err = ext2fs_get_mem(COPY_FILE_BUFLEN, &buf);
	if (err)
		goto out;

	err = ext2fs_get_memzero(fs->blocksize, &zerobuf);
	if (err)
		goto out;

#if defined(SEEK_DATA) && defined(SEEK_HOLE)
	err = try_lseek_copy(fs, fd, statbuf, e2_file, buf, zerobuf);
	if (err != EXT2_ET_UNIMPLEMENTED)
		goto out;
#endif

#if defined(FS_IOC_FIEMAP)
	err = try_fiemap_copy(fs, fd, e2_file, buf, zerobuf);
	if (err != EXT2_ET_UNIMPLEMENTED)
		goto out;
#endif

	err = copy_file_chunk(fs, fd, e2_file, 0, statbuf->st_size, buf,
			      zerobuf);
out:
	ext2fs_free_mem(&zerobuf);
	ext2fs_free_mem(&buf);
	close_err = ext2fs_file_close(e2_file);
	if (err == 0)
		err = close_err;
	return err;
}

static int is_hardlink(struct hdlinks_s *hdlinks, dev_t dev, ino_t ino)
{
	int i;

	for (i = 0; i < hdlinks->count; i++) {
		if (hdlinks->hdl[i].src_dev == dev &&
		    hdlinks->hdl[i].src_ino == ino)
			return i;
	}
	return -1;
}

errcode_t __do_write_internal(ext2_filsys fs, ext2_ino_t cwd,
		size_t size, mode_t mode, const char *dest, ext2_ino_t root,
		ext2_ino_t *newfile)
{
	ext2_ino_t	parent_ino;
	errcode_t	retval;
	struct ext2_inode inode;
	char		*cp;

	cp = strrchr(dest, '/');
	if (cp) {
		*cp = 0;
		retval = ext2fs_namei(fs, root, cwd, dest, &parent_ino);
		if (retval) {
			com_err(dest, retval, _("while looking up \"%s\""),
				dest);
			goto out;
		}
		dest = cp+1;
	} else
		parent_ino = cwd;

	retval = ext2fs_namei(fs, root, parent_ino, dest, newfile);
	if (retval == 0) {
		retval = EXT2_ET_FILE_EXISTS;
		goto out;
	}

	retval = ext2fs_new_inode(fs, parent_ino, 010755, 0, newfile);
	if (retval)
		goto out;
#ifdef DEBUGFS
	printf("Allocated inode: %u\n", *newfile);
#endif
	retval = ext2fs_link(fs, parent_ino, dest, *newfile, EXT2_FT_REG_FILE);
	if (retval == EXT2_ET_DIR_NO_SPACE) {
		retval = ext2fs_expand_dir(fs, parent_ino);
		if (retval)
			goto out;
		retval = ext2fs_link(fs, parent_ino, dest, *newfile,
					EXT2_FT_REG_FILE);
	}
	if (retval)
		goto out;
	if (ext2fs_test_inode_bitmap2(fs->inode_map, *newfile))
		com_err(__func__, 0, "Warning: inode already set");
	ext2fs_inode_alloc_stats2(fs, *newfile, +1, 0);
	memset(&inode, 0, sizeof(inode));
	inode.i_mode = (mode & ~S_IFMT) | LINUX_S_IFREG;
	inode.i_atime = inode.i_ctime = inode.i_mtime =
		fs->now ? fs->now : time(0);
	inode.i_links_count = 1;
	retval = ext2fs_inode_size_set(fs, &inode, size);
	if (retval)
		goto out;
	if (ext2fs_has_feature_inline_data(fs->super)) {
		inode.i_flags |= EXT4_INLINE_DATA_FL;
	} else if (ext2fs_has_feature_extents(fs->super)) {
		ext2_extent_handle_t handle;

		inode.i_flags &= ~EXT4_EXTENTS_FL;
		retval = ext2fs_extent_open2(fs, *newfile, &inode, &handle);
		if (retval)
			goto out;
		ext2fs_extent_free(handle);
	}

	retval = ext2fs_write_new_inode(fs, *newfile, &inode);
	if (retval)
		goto out;
	if (inode.i_flags & EXT4_INLINE_DATA_FL) {
		retval = ext2fs_inline_data_init(fs, *newfile);
		if (retval)
			goto out;
	}
out:
	return retval;
}

/* Copy the native file to the fs */
errcode_t do_write_internal(ext2_filsys fs, ext2_ino_t cwd, const char *src,
			    const char *dest, ext2_ino_t root)
{
	int		fd;
	errcode_t	retval;
	struct stat	statbuf;
	ext2_ino_t	newfile;

	fd = ext2fs_open_file(src, O_RDONLY, 0);
	if (fd < 0) {
		retval = errno;
		com_err(__func__, retval, _("while opening \"%s\" to copy"),
			src);
		return retval;
	}
	if (fstat(fd, &statbuf) < 0) {
		retval = errno;
		goto out;
	}

	retval = __do_write_internal(fs, cwd, statbuf.st_size, statbuf.st_mode, dest, root, &newfile);
	if (retval)
		goto out;

	retval = copy_file(fs, fd, &statbuf, newfile);

out:
	close(fd);
	return retval;
}

/* Copy the native file to the fs */
errcode_t do_write_internal_archive(ext2_filsys fs, ext2_ino_t cwd, struct archive *a,
		struct archive_entry *entry, const char *dest, ext2_ino_t root)
{
	errcode_t	retval;
	ext2_file_t     e2_file;
	char            *blockbuf = NULL;
	char            *zerobuf = NULL;
	errcode_t 	close_err;
	ext2_ino_t	newfile;
	off_t		off;

	retval = __do_write_internal(fs, cwd, archive_entry_size(entry),
			archive_entry_mode(entry), dest, root, &newfile);
	if (retval)
		return retval;

	retval = ext2fs_file_open(fs, newfile, EXT2_FILE_WRITE, &e2_file);
	if (retval)
		return retval;

	retval = ext2fs_get_mem(fs->blocksize, &blockbuf);
	if (retval)
		goto out;

	retval = ext2fs_get_memzero(fs->blocksize, &zerobuf);
	if (retval)
		goto out;

	off = 0;
	while (off < archive_entry_size(entry)) {
		const void *buf;
		size_t len;

		retval = archive_read_data_block(a, &buf, &len, &off);
		if (retval) {
			retval = archive_errno(a);
			goto out;
		}
		while (len) {
			off_t block_offset;
			size_t block_len;
			int end;

			block_offset = off % fs->blocksize;
			if (len > (fs->blocksize - block_offset))
				block_len = fs->blocksize - block_offset;
			else
				block_len = len;

			end = off + block_len == archive_entry_size(entry);

			if (block_offset || (block_len != fs->blocksize && !end))
				memcpy(blockbuf + block_offset, buf, block_len);

			if (block_offset + block_len == fs->blocksize || end) {
				const void *ptr = block_offset ? blockbuf : buf;
				size_t blen = block_offset + block_len;

				if (memcmp(ptr, zerobuf, blen) != 0) {
					retval = ext2fs_file_llseek(e2_file, off - block_offset, EXT2_SEEK_SET, NULL);
					if (retval)
						goto out;
					while (blen > 0) {
						unsigned int written;
						retval = ext2fs_file_write(e2_file, ptr, blen,
								&written);
						if (retval)
							goto out;
						if (written == 0) {
							retval = EIO;
							goto out;
						}
						blen -= written;
						ptr += written;
					}
				}
			}

			buf += block_len;
			len -= block_len;
			off += block_len;
		}
	}

out:
	ext2fs_free_mem(&blockbuf);
	ext2fs_free_mem(&zerobuf);
	close_err = ext2fs_file_close(e2_file);
	if (retval == 0)
		retval = close_err;
	return retval;
}

struct file_info {
	char *path;
	size_t path_len;
	size_t path_max_len;
};

static errcode_t path_append(struct file_info *target, const char *file)
{
	if (strlen(file) + target->path_len + 1 > target->path_max_len) {
		void *p;
		target->path_max_len *= 2;
		p = realloc(target->path, target->path_max_len);
		if (p == NULL)
			return EXT2_ET_NO_MEMORY;
		target->path = p;
	}
	target->path_len += sprintf(target->path + target->path_len, "/%s",
				    file);
	return 0;
}

#ifdef _WIN32
static int scandir(const char *dir_name, struct dirent ***name_list,
		   int (*filter)(const struct dirent*),
		   int (*compar)(const struct dirent**, const struct dirent**)) {
	DIR *dir;
	struct dirent *dent;
	struct dirent **temp_list = NULL;
	size_t temp_list_size = 0; // unit: num of dirent
	size_t num_dent = 0;

	dir = opendir(dir_name);
	if (dir == NULL) {
		return -1;
	}

	while ((dent = readdir(dir))) {
		if (filter != NULL && !(*filter)(dent))
			continue;

		// re-allocate the list
		if (num_dent == temp_list_size) {
			size_t new_list_size = temp_list_size + 32;
			struct dirent **new_list = (struct dirent**)realloc(
				temp_list, new_list_size * sizeof(struct dirent*));
			if (new_list == NULL) {
				goto out;
			}
			temp_list_size = new_list_size;
			temp_list = new_list;
		}
		// add the copy of dirent to the list
		temp_list[num_dent] = (struct dirent*)malloc((dent->d_reclen + 3) & ~3);
		if (!temp_list[num_dent])
			goto out;
		memcpy(temp_list[num_dent], dent, dent->d_reclen);
		num_dent++;
	}

	if (compar != NULL) {
		qsort(temp_list, num_dent, sizeof(struct dirent*),
		      (int (*)(const void*, const void*))compar);
	}

        // release the temp list
	*name_list = temp_list;
	temp_list = NULL;

out:
	if (temp_list != NULL) {
		while (num_dent > 0) {
			free(temp_list[--num_dent]);
		}
		free(temp_list);
		num_dent = -1;
	}
	closedir(dir);
	return num_dent;
}

static int alphasort(const struct dirent **a, const struct dirent **b) {
	return strcoll((*a)->d_name, (*b)->d_name);
}
#endif

/* Copy files from source_dir to fs in alphabetical order */
static errcode_t __populate_fs(ext2_filsys fs, ext2_ino_t parent_ino,
			       const char *source_dir, ext2_ino_t root,
			       struct hdlinks_s *hdlinks,
			       struct file_info *target,
			       struct fs_ops_callbacks *fs_callbacks)
{
	const char	*name;
	struct dirent	**dent;
	struct stat	st;
	char		*ln_target = NULL;
	unsigned int	save_inode;
	ext2_ino_t	ino;
	errcode_t	retval = 0;
	int		read_cnt;
	int		hdlink;
	size_t		cur_dir_path_len;
	int		i, num_dents;

	if (chdir(source_dir) < 0) {
		retval = errno;
		com_err(__func__, retval,
			_("while changing working directory to \"%s\""),
			source_dir);
		return retval;
	}

	num_dents = scandir(".", &dent, NULL, alphasort);

	if (num_dents < 0) {
		retval = errno;
		com_err(__func__, retval,
			_("while scanning directory \"%s\""), source_dir);
		return retval;
	}

	for (i = 0; i < num_dents; free(dent[i]), i++) {
		name = dent[i]->d_name;
		if ((!strcmp(name, ".")) || (!strcmp(name, "..")))
			continue;
		if (lstat(name, &st)) {
			retval = errno;
			com_err(__func__, retval, _("while lstat \"%s\""),
				name);
			goto out;
		}

		/* Check for hardlinks */
		save_inode = 0;
		if (!S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode) &&
		    st.st_nlink > 1) {
			hdlink = is_hardlink(hdlinks, st.st_dev, st.st_ino);
			if (hdlink >= 0) {
				retval = add_link(fs, parent_ino,
						  hdlinks->hdl[hdlink].dst_ino,
						  name);
				if (retval) {
					com_err(__func__, retval,
						"while linking %s", name);
					goto out;
				}
				continue;
			} else
				save_inode = 1;
		}

		cur_dir_path_len = target->path_len;
		retval = path_append(target, name);
		if (retval) {
			com_err(__func__, retval,
				"while appending %s", name);
			goto out;
		}

		if (fs_callbacks && fs_callbacks->create_new_inode) {
			retval = fs_callbacks->create_new_inode(fs,
				target->path, name, parent_ino, root,
				st.st_mode & S_IFMT);
			if (retval)
				goto out;
		}

		switch(st.st_mode & S_IFMT) {
		case S_IFCHR:
		case S_IFBLK:
		case S_IFIFO:
#ifndef _WIN32
		case S_IFSOCK:
			retval = do_mknod_internal(fs, parent_ino, name,
						   st.st_mode, st.st_rdev);
			if (retval) {
				com_err(__func__, retval,
					_("while creating special file "
					  "\"%s\""), name);
				goto out;
			}
			break;
		case S_IFLNK:
			ln_target = malloc(st.st_size + 1);
			if (ln_target == NULL) {
				com_err(__func__, retval,
					_("malloc failed"));
				goto out;
			}
			read_cnt = readlink(name, ln_target,
					    st.st_size + 1);
			if (read_cnt == -1) {
				retval = errno;
				com_err(__func__, retval,
					_("while trying to read link \"%s\""),
					name);
				free(ln_target);
				goto out;
			}
			if (read_cnt > st.st_size) {
				com_err(__func__, retval,
					_("symlink increased in size "
					  "between lstat() and readlink()"));
				free(ln_target);
				goto out;
			}
			ln_target[read_cnt] = '\0';
			retval = do_symlink_internal(fs, parent_ino, name,
						     ln_target, root);
			free(ln_target);
			if (retval) {
				com_err(__func__, retval,
					_("while writing symlink\"%s\""),
					name);
				goto out;
			}
			break;
#endif
		case S_IFREG:
			retval = do_write_internal(fs, parent_ino, name, name,
						   root);
			if (retval) {
				com_err(__func__, retval,
					_("while writing file \"%s\""), name);
				goto out;
			}
			break;
		case S_IFDIR:
			/* Don't choke on /lost+found */
			if (parent_ino == EXT2_ROOT_INO &&
			    strcmp(name, "lost+found") == 0)
				goto find_lnf;
			retval = do_mkdir_internal(fs, parent_ino, name,
						   root);
			if (retval) {
				com_err(__func__, retval,
					_("while making dir \"%s\""), name);
				goto out;
			}
find_lnf:
			retval = ext2fs_namei(fs, root, parent_ino,
					      name, &ino);
			if (retval) {
				com_err(name, retval, 0);
					goto out;
			}
			/* Populate the dir recursively*/
			retval = __populate_fs(fs, ino, name, root, hdlinks,
					       target, fs_callbacks);
			if (retval)
				goto out;
			if (chdir("..")) {
				retval = errno;
				com_err(__func__, retval,
					_("while changing directory"));
				goto out;
			}
			break;
		default:
			com_err(__func__, 0,
				_("ignoring entry \"%s\""), name);
		}

		retval =  ext2fs_namei(fs, root, parent_ino, name, &ino);
		if (retval) {
			com_err(name, retval, _("while looking up \"%s\""),
				name);
			goto out;
		}

		retval = set_inode_extra(fs, ino, &st);
		if (retval) {
			com_err(__func__, retval,
				_("while setting inode for \"%s\""), name);
			goto out;
		}

		retval = set_inode_xattr(fs, ino, name);
		if (retval) {
			com_err(__func__, retval,
				_("while setting xattrs for \"%s\""), name);
			goto out;
		}

		if (fs_callbacks && fs_callbacks->end_create_new_inode) {
			retval = fs_callbacks->end_create_new_inode(fs,
				target->path, name, parent_ino, root,
				st.st_mode & S_IFMT);
			if (retval)
				goto out;
		}

		/* Save the hardlink ino */
		if (save_inode) {
			/*
			 * Check whether need more memory, and we don't need
			 * free() since the lifespan will be over after the fs
			 * populated.
			 */
			if (hdlinks->count == hdlinks->size) {
				void *p = realloc(hdlinks->hdl,
						(hdlinks->size + HDLINK_CNT) *
						sizeof(struct hdlink_s));
				if (p == NULL) {
					retval = EXT2_ET_NO_MEMORY;
					com_err(name, retval,
						_("while saving inode data"));
					goto out;
				}
				hdlinks->hdl = p;
				hdlinks->size += HDLINK_CNT;
			}
			hdlinks->hdl[hdlinks->count].src_dev = st.st_dev;
			hdlinks->hdl[hdlinks->count].src_ino = st.st_ino;
			hdlinks->hdl[hdlinks->count].dst_ino = ino;
			hdlinks->count++;
		}
		target->path_len = cur_dir_path_len;
		target->path[target->path_len] = 0;
	}

out:
	for (; i < num_dents; free(dent[i]), i++);
	free(dent);
	return retval;
}

errcode_t populate_fs2(ext2_filsys fs, ext2_ino_t parent_ino,
		       const char *source_dir, ext2_ino_t root,
		       struct fs_ops_callbacks *fs_callbacks)
{
	struct file_info file_info;
	struct hdlinks_s hdlinks;
	errcode_t retval;

	if (!(fs->flags & EXT2_FLAG_RW)) {
		com_err(__func__, 0, "Filesystem opened readonly");
		return EROFS;
	}

	hdlinks.count = 0;
	hdlinks.size = HDLINK_CNT;
	hdlinks.hdl = realloc(NULL, hdlinks.size * sizeof(struct hdlink_s));
	if (hdlinks.hdl == NULL) {
		retval = errno;
		com_err(__func__, retval, _("while allocating memory"));
		return retval;
	}

	file_info.path_len = 0;
	file_info.path_max_len = 255;
	file_info.path = calloc(file_info.path_max_len, 1);

	retval = set_inode_xattr(fs, root, source_dir);
	if (retval) {
		com_err(__func__, retval,
			_("while copying xattrs on root directory"));
		goto out;
	}

	retval = __populate_fs(fs, parent_ino, source_dir, root, &hdlinks,
			       &file_info, fs_callbacks);

out:
	free(file_info.path);
	free(hdlinks.hdl);
	return retval;
}

errcode_t populate_fs(ext2_filsys fs, ext2_ino_t parent_ino,
		      const char *source_dir, ext2_ino_t root)
{
	return populate_fs2(fs, parent_ino, source_dir, root, NULL);
}

/*
 * Canonicalize path name:
 *  - Eliminate trailing slash or trailing /.
 *  - Eliminate ./ elements and double slash
 */
static char *cleanup_path(const char *input_path)
{
	char *path = malloc(strlen(input_path) + 2);
	const char *ipos;
	char *opos;
	int start = 2;

	opos = path;
	ipos = input_path;
	while (*ipos) {
		if (ipos[0] == '/') {
			start = 1;
			ipos++;
		} else if (start && ipos[0] == '.' && !ipos[1]) {
			break;
		} else if (start && ipos[0] == '.' && ipos[1] == '/') {
			ipos += 2;
		} else {
			if (start == 1) {
				*(opos++) = '/';
			}
			start = 0;
			*(opos++) = *(ipos++);
		}
	}
	if (opos == path)
		*(opos++) = start == 2 ? '.' : '/';
	*opos = '\0';

	return path;
}

errcode_t populate_fs_archive(ext2_filsys fs, ext2_ino_t parent_ino,
		      const char *archive_file, ext2_ino_t root)
{
	struct archive  *a;
	struct archive_entry *entry;
	const char	*s;
	char		*name;
	const char	*basename;
	char		*ln_target = NULL;
	ext2_ino_t	ino;
	errcode_t	retval = 0;
	char	 	*linkname;
	char		*cwd_last_name;
	ext2_ino_t	cwd_last_ino;

	if (!(fs->flags & EXT2_FLAG_RW)) {
		com_err(__func__, 0, "Filesystem opened readonly");
		return EROFS;
	}

	a = archive_read_new();
	archive_read_support_filter_all(a);
	archive_read_support_format_all(a);
	if (!strcmp(archive_file, "-"))
		retval = archive_read_open_filename(a, NULL, 10240);
	else
		retval = archive_read_open_filename(a, archive_file, 10240);
	if (retval != ARCHIVE_OK) {
		retval = archive_errno(a);
		com_err(__func__, 0, "Failed to open archive %s: %s", archive_file, archive_error_string(a));
		archive_read_free(a);
		return retval;
	} else
		retval = 0;

	name = NULL;
	cwd_last_name = NULL;
	cwd_last_ino = parent_ino;
	for (;;) {
		const char *cp;
		const char *basename;

		retval = archive_read_next_header(a, &entry);
		if (retval == ARCHIVE_EOF) {
			retval = 0;
			break;
		} else if (retval == ARCHIVE_OK) {
			retval = 0;
		} else {
			retval = archive_errno(a);
			com_err(__func__, 0, "while extracting from %s: %s",
					archive_file, archive_error_string(a));
			break;
		}

		s = archive_entry_pathname(entry);
		free(name);
		name = cleanup_path(s);

		if (!strcmp(name, "/") || !strcmp(name, ".")) {
			ino = parent_ino;
			goto is_root_ino;
		}

		cp = strrchr(name, '/');
		if (!cp || cp == name) {
			free(cwd_last_name);
			cwd_last_name = NULL;
			cwd_last_ino = parent_ino;
		} else if (!cwd_last_name ||
			    strlen(cwd_last_name) != cp - name ||
			   strncmp(cwd_last_name, name, cp - name)) {
			free(cwd_last_name);
			cwd_last_name = strndup(name, cp - name);
			retval = ext2fs_namei(fs, root, parent_ino, cwd_last_name, &cwd_last_ino);
			if (retval != 0) {
				retval = archive_errno(a);
				com_err(__func__, 0, "Parent directory for %s, %s: %s",
						name, cwd_last_name,  archive_error_string(a));
				goto out;
			}
		}

		basename = cp ? cp + 1 : name;

		s = archive_entry_hardlink(entry);
		if (s) {
			linkname = cleanup_path(s);
			retval = ext2fs_namei(fs, root, parent_ino, linkname, &ino);
			free(linkname);
			if (retval != ARCHIVE_OK) {
				retval = archive_errno(a);
				com_err(__func__, 0, "while linking %s: %s",
						name, archive_error_string(a));
				goto out;
			}
			retval = add_link(fs, cwd_last_ino, ino, basename);
			if (retval) {
				com_err(__func__, retval,
					"while linking %s", name);
				goto out;
			}
			continue;
		}

		switch(archive_entry_filetype(entry) & AE_IFMT) {
		case AE_IFCHR:
		case AE_IFBLK:
		case AE_IFIFO:
		case AE_IFSOCK:
			retval = do_mknod_internal(fs, cwd_last_ino, basename,
						   archive_entry_mode(entry),
						   archive_entry_rdev(entry));
			if (retval) {
				com_err(__func__, retval,
					_("while creating special file "
					  "\"%s\""), name);
				goto out;
			}
			break;
		case AE_IFLNK:
			retval = do_symlink_internal(fs, cwd_last_ino, basename,
						     archive_entry_symlink(entry), root);
			if (retval) {
				com_err(__func__, retval,
					_("while writing symlink\"%s\""),
					name);
				goto out;
			}
			break;
		case AE_IFREG:
			retval = do_write_internal_archive(fs, cwd_last_ino,
							a, entry, basename, root);
			if (retval) {
				com_err(__func__, retval,
					_("while writing file \"%s\""), name);
				goto out;
			}
			break;
		case AE_IFDIR:
			retval = do_mkdir_internal(fs, cwd_last_ino, basename,
						   root);
			if (retval) {
				com_err(__func__, retval,
					_("while making dir \"%s\""), name);
				goto out;
			}
			break;
		default:
			com_err(__func__, 0,
				_("ignoring entry \"%s\""), name);
		}

		retval =  ext2fs_namei(fs, root, cwd_last_ino, basename, &ino);
		if (retval) {
			com_err(name, retval, _("while looking up \"%s\""),
				name);
			goto out;
		}

is_root_ino:
		retval = set_inode_extra(fs, ino, archive_entry_stat(entry));
		if (retval) {
			com_err(__func__, retval,
				_("while setting inode for \"%s\""), name);
			goto out;
		}

		retval = set_inode_xattr_archive(fs, ino, entry);
		if (retval) {
			com_err(__func__, retval,
				_("while setting xattrs for \"%s\""), name);
			goto out;
		}
	}

out:
	free(cwd_last_name);
	free(name);
	if (retval == 0)
		archive_read_free(a);
	else {
		retval = archive_read_free(a);
		retval = retval == ARCHIVE_OK ? 0 : archive_errno(a);
	}
	return retval;
}
