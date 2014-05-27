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

#include <time.h>
#include <unistd.h>
#include <limits.h> /* for PATH_MAX */

#include "create_inode.h"

#if __STDC_VERSION__ < 199901L
# if __GNUC__ >= 2
#  define __func__ __FUNCTION__
# else
#  define __func__ "<unknown>"
# endif
#endif

/* 64KiB is the minimium blksize to best minimize system call overhead. */
#ifndef IO_BUFSIZE
#define IO_BUFSIZE 64*1024
#endif

/* Block size for `st_blocks' */
#ifndef S_BLKSIZE
#define S_BLKSIZE 512
#endif

/* Link an inode number to a directory */
static errcode_t add_link(ext2_filsys fs, ext2_ino_t parent_ino,
			  ext2_ino_t ino, const char *name)
{
	struct ext2_inode	inode;
	errcode_t		retval;

	retval = ext2fs_read_inode(fs, ino, &inode);
        if (retval) {
		com_err(__func__, retval, "while reading inode %u", ino);
		return retval;
	}

	retval = ext2fs_link(fs, parent_ino, name, ino, inode.i_flags);
	if (retval == EXT2_ET_DIR_NO_SPACE) {
		retval = ext2fs_expand_dir(fs, parent_ino);
		if (retval) {
			com_err(__func__, retval, "while expanding directory");
			return retval;
		}
		retval = ext2fs_link(fs, parent_ino, name, ino, inode.i_flags);
	}
	if (retval) {
		com_err(__func__, retval, "while linking %s", name);
		return retval;
	}

	inode.i_links_count++;

	retval = ext2fs_write_inode(fs, ino, &inode);
	if (retval)
		com_err(__func__, retval, "while writing inode %u", ino);

	return retval;
}

/* Fill the uid, gid, mode and time for the inode */
static void fill_inode(struct ext2_inode *inode, struct stat *st)
{
	if (st != NULL) {
		inode->i_uid = st->st_uid;
		inode->i_gid = st->st_gid;
		inode->i_mode |= st->st_mode;
		inode->i_atime = st->st_atime;
		inode->i_mtime = st->st_mtime;
		inode->i_ctime = st->st_ctime;
	}
}

/* Set the uid, gid, mode and time for the inode */
static errcode_t set_inode_extra(ext2_filsys fs, ext2_ino_t cwd,
				 ext2_ino_t ino, struct stat *st)
{
	errcode_t		retval;
	struct ext2_inode	inode;

	retval = ext2fs_read_inode(fs, ino, &inode);
        if (retval) {
		com_err(__func__, retval, "while reading inode %u", ino);
		return retval;
	}

	fill_inode(&inode, st);

	retval = ext2fs_write_inode(fs, ino, &inode);
	if (retval)
		com_err(__func__, retval, "while writing inode %u", ino);
	return retval;
}

/* Make a special files (block and character devices), fifo's, and sockets  */
errcode_t do_mknod_internal(ext2_filsys fs, ext2_ino_t cwd, const char *name,
			    struct stat *st)
{
	ext2_ino_t		ino;
	errcode_t		retval;
	struct ext2_inode	inode;
	unsigned long		devmajor, devminor, mode;
	int			filetype;

	switch(st->st_mode & S_IFMT) {
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
	case S_IFSOCK:
		mode = LINUX_S_IFSOCK;
		filetype = EXT2_FT_SOCK;
		break;
	default:
		return EXT2_ET_INVALID_ARGUMENT;
	}

	if (!(fs->flags & EXT2_FLAG_RW)) {
		com_err(__func__, 0, "Filesystem opened read/only");
		return EROFS;
	}
	retval = ext2fs_new_inode(fs, cwd, 010755, 0, &ino);
	if (retval) {
		com_err(__func__, retval, 0);
		return retval;
	}

#ifdef DEBUGFS
	printf("Allocated inode: %u\n", ino);
#endif
	retval = ext2fs_link(fs, cwd, name, ino, filetype);
	if (retval == EXT2_ET_DIR_NO_SPACE) {
		retval = ext2fs_expand_dir(fs, cwd);
		if (retval) {
			com_err(__func__, retval, "while expanding directory");
			return retval;
		}
		retval = ext2fs_link(fs, cwd, name, ino, filetype);
	}
	if (retval) {
		com_err(name, retval, 0);
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
		devmajor = major(st->st_rdev);
		devminor = minor(st->st_rdev);

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
		com_err(__func__, retval, "while creating inode %u", ino);

	return retval;
}

/* Make a symlink name -> target */
errcode_t do_symlink_internal(ext2_filsys fs, ext2_ino_t cwd, const char *name,
			      char *target, ext2_ino_t root)
{
	char			*cp;
	ext2_ino_t		parent_ino;
	errcode_t		retval;
	struct ext2_inode	inode;
	struct stat		st;

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

try_again:
	retval = ext2fs_symlink(fs, parent_ino, 0, name, target);
	if (retval == EXT2_ET_DIR_NO_SPACE) {
		retval = ext2fs_expand_dir(fs, parent_ino);
		if (retval) {
			com_err("do_symlink_internal", retval,
				"while expanding directory");
			return retval;
		}
		goto try_again;
	}
	if (retval)
		com_err("ext2fs_symlink", retval, 0);
	return retval;
}

/* Make a directory in the fs */
errcode_t do_mkdir_internal(ext2_filsys fs, ext2_ino_t cwd, const char *name,
			    struct stat *st, ext2_ino_t root)
{
	char			*cp;
	ext2_ino_t		parent_ino, ino;
	errcode_t		retval;
	struct ext2_inode	inode;


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

try_again:
	retval = ext2fs_mkdir(fs, parent_ino, 0, name);
	if (retval == EXT2_ET_DIR_NO_SPACE) {
		retval = ext2fs_expand_dir(fs, parent_ino);
		if (retval) {
			com_err(__func__, retval, "while expanding directory");
			return retval;
		}
		goto try_again;
	}
	if (retval)
		com_err("ext2fs_mkdir", retval, 0);
	return retval;
}

static errcode_t copy_file(ext2_filsys fs, int fd, ext2_ino_t newfile,
			   int bufsize, int make_holes)
{
	ext2_file_t	e2_file;
	errcode_t	retval, close_ret;
	int		got;
	unsigned int	written;
	char		*buf;
	char		*ptr;
	char		*zero_buf;
	int		cmp;

	retval = ext2fs_file_open(fs, newfile,
				  EXT2_FILE_WRITE, &e2_file);
	if (retval)
		return retval;

	retval = ext2fs_get_mem(bufsize, &buf);
	if (retval) {
		com_err("copy_file", retval, "can't allocate buffer\n");
		goto out_close;
	}

	/* This is used for checking whether the whole block is zero */
	retval = ext2fs_get_memzero(bufsize, &zero_buf);
	if (retval) {
		com_err("copy_file", retval, "can't allocate zero buffer\n");
		goto out_free_buf;
	}

	while (1) {
		got = read(fd, buf, bufsize);
		if (got == 0)
			break;
		if (got < 0) {
			retval = errno;
			goto fail;
		}
		ptr = buf;

		/* Sparse copy */
		if (make_holes) {
			/* Check whether all is zero */
			cmp = memcmp(ptr, zero_buf, got);
			if (cmp == 0) {
				 /* The whole block is zero, make a hole */
				retval = ext2fs_file_lseek(e2_file, got,
							   EXT2_SEEK_CUR,
							   NULL);
				if (retval)
					goto fail;
				got = 0;
			}
		}

		/* Normal copy */
		while (got > 0) {
			retval = ext2fs_file_write(e2_file, ptr,
						   got, &written);
			if (retval)
				goto fail;

			got -= written;
			ptr += written;
		}
	}

fail:
	ext2fs_free_mem(&zero_buf);
out_free_buf:
	ext2fs_free_mem(&buf);
out_close:
	close_ret = ext2fs_file_close(e2_file);
	if (retval == 0)
		retval = close_ret;
	return retval;
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

/* Copy the native file to the fs */
errcode_t do_write_internal(ext2_filsys fs, ext2_ino_t cwd, const char *src,
			    const char *dest, ext2_ino_t root)
{
	int		fd;
	struct stat	statbuf;
	ext2_ino_t	newfile;
	errcode_t	retval;
	struct ext2_inode inode;
	int		bufsize = IO_BUFSIZE;
	int		make_holes = 0;

	fd = ext2fs_open_file(src, O_RDONLY, 0);
	if (fd < 0) {
		com_err(src, errno, 0);
		return errno;
	}
	if (fstat(fd, &statbuf) < 0) {
		com_err(src, errno, 0);
		close(fd);
		return errno;
	}

	retval = ext2fs_namei(fs, root, cwd, dest, &newfile);
	if (retval == 0) {
		close(fd);
		return EXT2_ET_FILE_EXISTS;
	}

	retval = ext2fs_new_inode(fs, cwd, 010755, 0, &newfile);
	if (retval) {
		com_err(__func__, retval, 0);
		close(fd);
		return retval;
	}
#ifdef DEBUGFS
	printf("Allocated inode: %u\n", newfile);
#endif
	retval = ext2fs_link(fs, cwd, dest, newfile,
				EXT2_FT_REG_FILE);
	if (retval == EXT2_ET_DIR_NO_SPACE) {
		retval = ext2fs_expand_dir(fs, cwd);
		if (retval) {
			com_err(__func__, retval, "while expanding directory");
			close(fd);
			return retval;
		}
		retval = ext2fs_link(fs, cwd, dest, newfile,
					EXT2_FT_REG_FILE);
	}
	if (retval) {
		com_err(dest, retval, 0);
		close(fd);
		return errno;
	}
	if (ext2fs_test_inode_bitmap2(fs->inode_map, newfile))
		com_err(__func__, 0, "Warning: inode already set");
	ext2fs_inode_alloc_stats2(fs, newfile, +1, 0);
	memset(&inode, 0, sizeof(inode));
	inode.i_mode = (statbuf.st_mode & ~LINUX_S_IFMT) | LINUX_S_IFREG;
	inode.i_atime = inode.i_ctime = inode.i_mtime =
		fs->now ? fs->now : time(0);
	inode.i_links_count = 1;
	inode.i_size = statbuf.st_size;
	if (EXT2_HAS_INCOMPAT_FEATURE(fs->super,
				      EXT4_FEATURE_INCOMPAT_INLINE_DATA)) {
		inode.i_flags |= EXT4_INLINE_DATA_FL;
	} else if (fs->super->s_feature_incompat &
		   EXT3_FEATURE_INCOMPAT_EXTENTS) {
		int i;
		struct ext3_extent_header *eh;

		eh = (struct ext3_extent_header *) &inode.i_block[0];
		eh->eh_depth = 0;
		eh->eh_entries = 0;
		eh->eh_magic = ext2fs_cpu_to_le16(EXT3_EXT_MAGIC);
		i = (sizeof(inode.i_block) - sizeof(*eh)) /
			sizeof(struct ext3_extent);
		eh->eh_max = ext2fs_cpu_to_le16(i);
		inode.i_flags |= EXT4_EXTENTS_FL;
	}

	retval = ext2fs_write_new_inode(fs, newfile, &inode);
	if (retval) {
		com_err(__func__, retval, "while creating inode %u", newfile);
		close(fd);
		return retval;
	}
	if (inode.i_flags & EXT4_INLINE_DATA_FL) {
		retval = ext2fs_inline_data_init(fs, newfile);
		if (retval) {
			com_err("copy_file", retval, 0);
			close(fd);
			return retval;
		}
	}
	if (LINUX_S_ISREG(inode.i_mode)) {
		if (statbuf.st_blocks < statbuf.st_size / S_BLKSIZE) {
			make_holes = 1;
			/*
			 * Use I/O blocksize as buffer size when
			 * copying sparse files.
			 */
			bufsize = statbuf.st_blksize;
		}
		retval = copy_file(fs, fd, newfile, bufsize, make_holes);
		if (retval)
			com_err("copy_file", retval, 0);
	}
	close(fd);

	return retval;
}

/* Copy files from source_dir to fs */
static errcode_t __populate_fs(ext2_filsys fs, ext2_ino_t parent_ino,
			       const char *source_dir, ext2_ino_t root,
			       struct hdlinks_s *hdlinks)
{
	const char	*name;
	DIR		*dh;
	struct dirent	*dent;
	struct stat	st;
	char		ln_target[PATH_MAX];
	unsigned int	save_inode;
	ext2_ino_t	ino;
	errcode_t	retval = 0;
	int		read_cnt;
	int		hdlink;

	if (chdir(source_dir) < 0) {
		com_err(__func__, errno,
			_("while changing working directory to \"%s\""),
			source_dir);
		return errno;
	}

	if (!(dh = opendir("."))) {
		com_err(__func__, errno,
			_("while opening directory \"%s\""), source_dir);
		return errno;
	}

	while ((dent = readdir(dh))) {
		if ((!strcmp(dent->d_name, ".")) ||
		    (!strcmp(dent->d_name, "..")))
			continue;
		if (lstat(dent->d_name, &st)) {
			com_err(__func__, errno, _("while lstat \"%s\""),
				dent->d_name);
			goto out;
		}
		name = dent->d_name;

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

		switch(st.st_mode & S_IFMT) {
		case S_IFCHR:
		case S_IFBLK:
		case S_IFIFO:
		case S_IFSOCK:
			retval = do_mknod_internal(fs, parent_ino, name, &st);
			if (retval) {
				com_err(__func__, retval,
					_("while creating special file "
					  "\"%s\""), name);
				goto out;
			}
			break;
		case S_IFLNK:
			read_cnt = readlink(name, ln_target,
					    sizeof(ln_target) - 1);
			if (read_cnt == -1) {
				com_err(__func__, errno,
					_("while trying to readlink \"%s\""),
					name);
				retval = errno;
				goto out;
			}
			ln_target[read_cnt] = '\0';
			retval = do_symlink_internal(fs, parent_ino, name,
						     ln_target, root);
			if (retval) {
				com_err(__func__, retval,
					_("while writing symlink\"%s\""),
					name);
				goto out;
			}
			break;
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
			retval = do_mkdir_internal(fs, parent_ino, name, &st,
						   root);
			if (retval) {
				com_err(__func__, retval,
					_("while making dir \"%s\""), name);
				goto out;
			}
			retval = ext2fs_namei(fs, root, parent_ino,
					      name, &ino);
			if (retval) {
				com_err(name, retval, 0);
					goto out;
			}
			/* Populate the dir recursively*/
			retval = __populate_fs(fs, ino, name, root, hdlinks);
			if (retval) {
				com_err(__func__, retval,
					_("while adding dir \"%s\""), name);
				goto out;
			}
			if (chdir("..")) {
				com_err(__func__, errno, _("during cd .."));
				retval = errno;
				goto out;
			}
			break;
		default:
			com_err(__func__, 0,
				_("ignoring entry \"%s\""), name);
		}

		retval =  ext2fs_namei(fs, root, parent_ino, name, &ino);
		if (retval) {
			com_err(name, retval, 0);
			goto out;
		}

		retval = set_inode_extra(fs, parent_ino, ino, &st);
		if (retval) {
			com_err(__func__, retval,
				_("while setting inode for \"%s\""), name);
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
					com_err(name, errno,
						_("Not enough memory"));
					retval = EXT2_ET_NO_MEMORY;
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
	}

out:
	closedir(dh);
	return retval;
}

errcode_t populate_fs(ext2_filsys fs, ext2_ino_t parent_ino,
		      const char *source_dir, ext2_ino_t root)
{
	struct hdlinks_s hdlinks;
	errcode_t retval;

	hdlinks.count = 0;
	hdlinks.size = HDLINK_CNT;
	hdlinks.hdl = realloc(NULL, hdlinks.size * sizeof(struct hdlink_s));
	if (hdlinks.hdl == NULL) {
		com_err(__func__, errno, "Not enough memory");
		return errno;
	}

	retval = __populate_fs(fs, parent_ino, source_dir, root, &hdlinks);

	free(hdlinks.hdl);
	return retval;
}
