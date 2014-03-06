#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "et/com_err.h"
#include "e2p/e2p.h"
#include "ext2fs/ext2fs.h"
#include "nls-enable.h"

ext2_filsys    current_fs;
ext2_ino_t     root;

/* For populating the filesystem */
extern errcode_t populate_fs(ext2_ino_t parent_ino, const char *source_dir);
extern errcode_t do_mknod_internal(ext2_ino_t cwd, const char *name, struct stat *st);
extern errcode_t do_symlink_internal(ext2_ino_t cwd, const char *name, char *target);
extern errcode_t do_mkdir_internal(ext2_ino_t cwd, const char *name, struct stat *st);
extern errcode_t do_write_internal(ext2_ino_t cwd, const char *src, const char *dest);
