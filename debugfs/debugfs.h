/*
 * debugfs.h --- header file for the debugfs program
 */

#include <linux/ext2_fs.h>
#include "ext2fs/ext2fs.h"

extern ext2_filsys fs;
extern ino_t	root, cwd;

extern FILE *open_pager(void);
extern void close_pager(FILE *stream);
extern int check_fs_open(char *name);
extern int check_fs_not_open(char *name);
extern ino_t string_to_inode(char *str);



