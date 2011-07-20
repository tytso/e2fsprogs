/** mkquota.h
 *
 * Interface to the quota library.
 *
 * The quota library provides interface for creating and updating the quota
 * files and the ext4 superblock fields. It supports the new VFS_V1 quota
 * format. The quota library also provides support for keeping track of quotas
 * in memory.
 * The typical way to use the quota library is as follows:
 * {
 *	quota_ctx_t qctx;
 *
 *	init_quota_context(&qctx, fs, -1);
 *	{
 *		compute_quota(qctx, -1);
 *		AND/OR
 *		quota_data_add/quota_data_sub/quota_data_inodes();
 *	}
 *	write_quota_inode(qctx, USRQUOTA);
 *	write_quota_inode(qctx, GRPQUOTA);
 *	release_quota_context(&qctx);
 * }
 *
 * This initial version does not support reading the quota files. This support
 * will be added in near future.
 *
 * Aditya Kali <adityakali@google.com>
 */

#ifndef __QUOTA_QUOTAIO_H__
#define __QUOTA_QUOTAIO_H__

#include "ext2fs/ext2_fs.h"
#include "ext2fs/ext2fs.h"
#include "quota.h"
#include "../e2fsck/dict.h"

typedef struct quota_ctx *quota_ctx_t;

struct quota_ctx {
	ext2_filsys	fs;
	dict_t		*quota_dict[MAXQUOTAS];
};

void init_quota_context(quota_ctx_t *qctx, ext2_filsys fs, int qtype);
void quota_data_inodes(quota_ctx_t qctx, struct ext2_inode *inode, ext2_ino_t ino,
		int adjust);
void quota_data_add(quota_ctx_t qctx, struct ext2_inode *inode, ext2_ino_t ino,
		qsize_t space);
void quota_data_sub(quota_ctx_t qctx, struct ext2_inode *inode, ext2_ino_t ino,
		qsize_t space);
errcode_t write_quota_inode(quota_ctx_t qctx, int qtype);
errcode_t compute_quota(quota_ctx_t qctx, int qtype);
void release_quota_context(quota_ctx_t *qctx);

errcode_t remove_quota_inode(ext2_filsys fs, int qtype);
int is_quota_on(ext2_filsys fs, int type);
int quota_file_exists(ext2_filsys fs, int qtype, int fmt);
void set_sb_quota_inum(ext2_filsys fs, ext2_ino_t ino, int qtype);

/* in quotaio.c */
const char *get_qf_name(int type, int fmt, char *buf);
const char *get_qf_path(const char *mntpt, int qtype, int fmt,
			char *path_buf, size_t path_buf_size);

#endif  /* __QUOTA_QUOTAIO_H__ */
