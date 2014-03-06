#include "create_inode.h"

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
}
