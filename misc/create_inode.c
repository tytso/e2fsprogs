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
