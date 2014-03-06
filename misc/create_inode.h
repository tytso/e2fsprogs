#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "et/com_err.h"
#include "e2p/e2p.h"
#include "ext2fs/ext2fs.h"
#include "nls-enable.h"

struct hdlink_s
{
	ext2_ino_t src_ino;
	ext2_ino_t dst_ino;
};

struct hdlinks_s
{
	int count;
	struct hdlink_s *hdl;
};

struct hdlinks_s hdlinks;

ext2_filsys    current_fs;
ext2_ino_t     root;

/* For saving the hard links */
#define HDLINK_CNT     4
extern int hdlink_cnt;

/* For populating the filesystem */
extern errcode_t populate_fs(ext2_ino_t parent_ino, const char *source_dir);
extern errcode_t do_mknod_internal(ext2_ino_t cwd, const char *name, struct stat *st);
extern errcode_t do_symlink_internal(ext2_ino_t cwd, const char *name, char *target);
extern errcode_t do_mkdir_internal(ext2_ino_t cwd, const char *name, struct stat *st);
extern errcode_t do_write_internal(ext2_ino_t cwd, const char *src, const char *dest);
