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

/* Make a special file which is block, character and fifo */
errcode_t do_mknod_internal(ext2_ino_t cwd, const char *name, struct stat *st)
{
	ext2_ino_t		ino;
	errcode_t 		retval;
	struct ext2_inode	inode;
	unsigned long		major, minor, mode;
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
	}

	if (!(current_fs->flags & EXT2_FLAG_RW)) {
		com_err(__func__, 0, "Filesystem opened read/only");
		return -1;
	}
	retval = ext2fs_new_inode(current_fs, cwd, 010755, 0, &ino);
	if (retval) {
		com_err(__func__, retval, 0);
		return retval;
	}

#ifdef DEBUGFS
	printf("Allocated inode: %u\n", ino);
#endif
	retval = ext2fs_link(current_fs, cwd, name, ino, filetype);
	if (retval == EXT2_ET_DIR_NO_SPACE) {
		retval = ext2fs_expand_dir(current_fs, cwd);
		if (retval) {
			com_err(__func__, retval, "while expanding directory");
			return retval;
		}
		retval = ext2fs_link(current_fs, cwd, name, ino, filetype);
	}
	if (retval) {
		com_err(name, retval, 0);
		return -1;
	}
        if (ext2fs_test_inode_bitmap2(current_fs->inode_map, ino))
		com_err(__func__, 0, "Warning: inode already set");
	ext2fs_inode_alloc_stats2(current_fs, ino, +1, 0);
	memset(&inode, 0, sizeof(inode));
	inode.i_mode = mode;
	inode.i_atime = inode.i_ctime = inode.i_mtime =
		current_fs->now ? current_fs->now : time(0);

	major = major(st->st_rdev);
	minor = minor(st->st_rdev);

	if ((major < 256) && (minor < 256)) {
		inode.i_block[0] = major * 256 + minor;
		inode.i_block[1] = 0;
	} else {
		inode.i_block[0] = 0;
		inode.i_block[1] = (minor & 0xff) | (major << 8) | ((minor & ~0xff) << 12);
	}
	inode.i_links_count = 1;

	retval = ext2fs_write_new_inode(current_fs, ino, &inode);
	if (retval)
		com_err(__func__, retval, "while creating inode %u", ino);

	return retval;
}

/* Make a symlink name -> target */
errcode_t do_symlink_internal(ext2_ino_t cwd, const char *name, char *target)
{
	char			*cp;
	ext2_ino_t		parent_ino;
	errcode_t		retval;
	struct ext2_inode	inode;
	struct stat		st;

	cp = strrchr(name, '/');
	if (cp) {
		*cp = 0;
		if ((retval =  ext2fs_namei(current_fs, root, cwd, name, &parent_ino))){
			com_err(name, retval, 0);
			return retval;
		}
		name = cp+1;
	} else
		parent_ino = cwd;

try_again:
	retval = ext2fs_symlink(current_fs, parent_ino, 0, name, target);
	if (retval == EXT2_ET_DIR_NO_SPACE) {
		retval = ext2fs_expand_dir(current_fs, parent_ino);
		if (retval) {
			com_err("do_symlink_internal", retval, "while expanding directory");
			return retval;
		}
		goto try_again;
	}
	if (retval) {
		com_err("ext2fs_symlink", retval, 0);
		return retval;
	}

}

/* Make a directory in the fs */
errcode_t do_mkdir_internal(ext2_ino_t cwd, const char *name, struct stat *st)
{
	char			*cp;
	ext2_ino_t		parent_ino, ino;
	errcode_t		retval;
	struct ext2_inode	inode;


	cp = strrchr(name, '/');
	if (cp) {
		*cp = 0;
		if ((retval =  ext2fs_namei(current_fs, root, cwd, name, &parent_ino))){
			com_err(name, retval, 0);
			return retval;
		}
		name = cp+1;
	} else
		parent_ino = cwd;

try_again:
	retval = ext2fs_mkdir(current_fs, parent_ino, 0, name);
	if (retval == EXT2_ET_DIR_NO_SPACE) {
		retval = ext2fs_expand_dir(current_fs, parent_ino);
		if (retval) {
			com_err(__func__, retval, "while expanding directory");
			return retval;
		}
		goto try_again;
	}
	if (retval) {
		com_err("ext2fs_mkdir", retval, 0);
		return retval;
	}
}

static errcode_t copy_file(int fd, ext2_ino_t newfile, int bufsize, int make_holes)
{
	ext2_file_t	e2_file;
	errcode_t	retval;
	int		got;
	unsigned int	written;
	char		*buf;
	char		*ptr;
	char		*zero_buf;
	int		cmp;

	retval = ext2fs_file_open(current_fs, newfile,
				  EXT2_FILE_WRITE, &e2_file);
	if (retval)
		return retval;

	retval = ext2fs_get_mem(bufsize, &buf);
	if (retval) {
		com_err("copy_file", retval, "can't allocate buffer\n");
		return retval;
	}

	/* This is used for checking whether the whole block is zero */
	retval = ext2fs_get_memzero(bufsize, &zero_buf);
	if (retval) {
		com_err("copy_file", retval, "can't allocate buffer\n");
		ext2fs_free_mem(&buf);
		return retval;
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
				retval = ext2fs_file_lseek(e2_file, got, EXT2_SEEK_CUR, NULL);
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
	ext2fs_free_mem(&buf);
	ext2fs_free_mem(&zero_buf);
	retval = ext2fs_file_close(e2_file);
	return retval;

fail:
	ext2fs_free_mem(&buf);
	ext2fs_free_mem(&zero_buf);
	(void) ext2fs_file_close(e2_file);
	return retval;
}

/* Copy the native file to the fs */
errcode_t do_write_internal(ext2_ino_t cwd, const char *src, const char *dest)
{
	int		fd;
	struct stat	statbuf;
	ext2_ino_t	newfile;
	errcode_t	retval;
	struct ext2_inode inode;
	int		bufsize = IO_BUFSIZE;
	int		make_holes = 0;

	fd = open(src, O_RDONLY);
	if (fd < 0) {
		com_err(src, errno, 0);
		return errno;
	}
	if (fstat(fd, &statbuf) < 0) {
		com_err(src, errno, 0);
		close(fd);
		return errno;
	}

	retval = ext2fs_namei(current_fs, root, cwd, dest, &newfile);
	if (retval == 0) {
		com_err(__func__, 0, "The file '%s' already exists\n", dest);
		close(fd);
		return errno;
	}

	retval = ext2fs_new_inode(current_fs, cwd, 010755, 0, &newfile);
	if (retval) {
		com_err(__func__, retval, 0);
		close(fd);
		return errno;
	}
#ifdef DEBUGFS
	printf("Allocated inode: %u\n", newfile);
#endif
	retval = ext2fs_link(current_fs, cwd, dest, newfile,
				EXT2_FT_REG_FILE);
	if (retval == EXT2_ET_DIR_NO_SPACE) {
		retval = ext2fs_expand_dir(current_fs, cwd);
		if (retval) {
			com_err(__func__, retval, "while expanding directory");
			close(fd);
			return errno;
		}
		retval = ext2fs_link(current_fs, cwd, dest, newfile,
					EXT2_FT_REG_FILE);
	}
	if (retval) {
		com_err(dest, retval, 0);
		close(fd);
		return errno;
	}
        if (ext2fs_test_inode_bitmap2(current_fs->inode_map, newfile))
		com_err(__func__, 0, "Warning: inode already set");
	ext2fs_inode_alloc_stats2(current_fs, newfile, +1, 0);
	memset(&inode, 0, sizeof(inode));
	inode.i_mode = (statbuf.st_mode & ~LINUX_S_IFMT) | LINUX_S_IFREG;
	inode.i_atime = inode.i_ctime = inode.i_mtime =
		current_fs->now ? current_fs->now : time(0);
	inode.i_links_count = 1;
	inode.i_size = statbuf.st_size;
	if (current_fs->super->s_feature_incompat &
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

	if ((retval = ext2fs_write_new_inode(current_fs, newfile, &inode))) {
		com_err(__func__, retval, "while creating inode %u", newfile);
		close(fd);
		return errno;
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
		retval = copy_file(fd, newfile, bufsize, make_holes);
		if (retval)
			com_err("copy_file", retval, 0);
	}
	close(fd);

	return 0;
}

/* Copy files from source_dir to fs */
errcode_t populate_fs(ext2_ino_t parent_ino, const char *source_dir)
{
	const char	*name;
	DIR		*dh;
	struct dirent	*dent;
	struct stat	st;
	char		ln_target[PATH_MAX];
	ext2_ino_t	ino;
	errcode_t	retval;
	int		read_cnt;

	root = EXT2_ROOT_INO;

	if (chdir(source_dir) < 0) {
		com_err(__func__, errno,
			_("while changing working directory to \"%s\""), source_dir);
		return errno;
	}

	if (!(dh = opendir("."))) {
		com_err(__func__, errno,
			_("while openning directory \"%s\""), source_dir);
		return errno;
	}

	while((dent = readdir(dh))) {
		if((!strcmp(dent->d_name, ".")) || (!strcmp(dent->d_name, "..")))
			continue;
		lstat(dent->d_name, &st);
		name = dent->d_name;

		switch(st.st_mode & S_IFMT) {
			case S_IFCHR:
			case S_IFBLK:
			case S_IFIFO:
				if ((retval = do_mknod_internal(parent_ino, name, &st))) {
					com_err(__func__, retval,
						_("while creating special file \"%s\""), name);
					return retval;
				}
				break;
			case S_IFSOCK:
				/* FIXME: there is no make socket function atm. */
				com_err(__func__, 0,
					_("ignoring socket file \"%s\""), name);
				continue;
			case S_IFLNK:
				if((read_cnt = readlink(name, ln_target, sizeof(ln_target))) == -1) {
					com_err(__func__, errno,
						_("while trying to readlink \"%s\""), name);
					return errno;
				}
				ln_target[read_cnt] = '\0';
				if ((retval = do_symlink_internal(parent_ino, name, ln_target))) {
					com_err(__func__, retval,
						_("while writing symlink\"%s\""), name);
					return retval;
				}
				break;
			case S_IFREG:
				if ((retval = do_write_internal(parent_ino, name, name))) {
					com_err(__func__, retval,
						_("while writing file \"%s\""), name);
					return retval;
				}
				break;
			case S_IFDIR:
				if ((retval = do_mkdir_internal(parent_ino, name, &st))) {
					com_err(__func__, retval,
						_("while making dir \"%s\""), name);
					return retval;
				}
				if ((retval = ext2fs_namei(current_fs, root, parent_ino, name, &ino))) {
					com_err(name, retval, 0);
						return retval;
				}
				/* Populate the dir recursively*/
				retval = populate_fs(ino, name);
				if (retval) {
					com_err(__func__, retval, _("while adding dir \"%s\""), name);
					return retval;
				}
				chdir("..");
				break;
			default:
				com_err(__func__, 0,
					_("ignoring entry \"%s\""), name);
		}
	}
	closedir(dh);
	return retval;
}
