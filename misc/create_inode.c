#include "create_inode.h"

#if __STDC_VERSION__ < 199901L
# if __GNUC__ >= 2
#  define __func__ __FUNCTION__
# else
#  define __func__ "<unknown>"
# endif
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
}

/* Make a directory in the fs */
errcode_t do_mkdir_internal(ext2_ino_t cwd, const char *name, struct stat *st)
{
}

/* Copy the native file to the fs */
errcode_t do_write_internal(ext2_ino_t cwd, const char *src, const char *dest)
{
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
