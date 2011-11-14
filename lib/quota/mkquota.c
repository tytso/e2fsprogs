/*
 * mkquota.c --- create quota files for a filesystem
 *
 * Aditya Kali <adityakali@google.com>
 */
#include "config.h"
#include <sys/types.h>
#include <sys/stat.h>
#ifdef HAVE_SYS_QUOTA_H
#include <sys/quota.h>
#endif
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>

#include "ext2fs/ext2_fs.h"
#include "ext2fs/ext2fs.h"
#include "e2p/e2p.h"

#include "quota.h"
#include "quotaio.h"
#include "quotaio_v2.h"
#include "quotaio_tree.h"
#include "mkquota.h"
#include "common.h"

/* Needed for architectures where sizeof(int) != sizeof(void *) */
#define UINT_TO_VOIDPTR(val)  ((void *)(intptr_t)(val))
#define VOIDPTR_TO_UINT(ptr)  ((unsigned int)(intptr_t)(ptr))

static void print_inode(struct ext2_inode *inode)
{
	if (!inode)
		return;

	fprintf(stderr, "  i_mode = %d\n", inode->i_mode);
	fprintf(stderr, "  i_uid = %d\n", inode->i_uid);
	fprintf(stderr, "  i_size = %d\n", inode->i_size);
	fprintf(stderr, "  i_atime = %d\n", inode->i_atime);
	fprintf(stderr, "  i_ctime = %d\n", inode->i_ctime);
	fprintf(stderr, "  i_mtime = %d\n", inode->i_mtime);
	fprintf(stderr, "  i_dtime = %d\n", inode->i_dtime);
	fprintf(stderr, "  i_gid = %d\n", inode->i_gid);
	fprintf(stderr, "  i_links_count = %d\n", inode->i_links_count);
	fprintf(stderr, "  i_blocks = %d\n", inode->i_blocks);
	fprintf(stderr, "  i_flags = %d\n", inode->i_flags);

	return;
}

int quota_is_on(ext2_filsys fs, int type)
{
	char tmp[1024];
	qid_t id = (type == USRQUOTA) ? getuid() : getgid();

#ifdef HAVE_QUOTACTL
	if (!quotactl(QCMD(Q_V2_GETQUOTA, type), fs->device_name, id, tmp))
		return 1;
#endif
	return 0;
}

/*
 * Returns 0 if not able to find the quota file, otherwise returns its
 * inode number.
 */
int quota_file_exists(ext2_filsys fs, int qtype, int fmt)
{
	char qf_name[256];
	errcode_t ret;
	ext2_ino_t ino;

	if (qtype >= MAXQUOTAS)
		return -EINVAL;

	quota_get_qf_name(qtype, fmt, qf_name);

	ret = ext2fs_lookup(fs, EXT2_ROOT_INO, qf_name, strlen(qf_name), 0,
			    &ino);
	if (ret)
		return 0;

	return ino;
}

/*
 * Set the value for reserved quota inode number field in superblock.
 */
void quota_set_sb_inum(ext2_filsys fs, ext2_ino_t ino, int qtype)
{
	ext2_ino_t *inump;

	inump = (qtype == USRQUOTA) ? &fs->super->s_usr_quota_inum :
		&fs->super->s_grp_quota_inum;

	log_debug("setting quota ino in superblock: ino=%u, type=%d", ino,
		 qtype);
	*inump = ino;
	ext2fs_mark_super_dirty(fs);
}

errcode_t quota_remove_inode(ext2_filsys fs, int qtype)
{
	ext2_ino_t qf_ino;

	ext2fs_read_bitmaps(fs);
	qf_ino = (qtype == USRQUOTA) ? fs->super->s_usr_quota_inum :
		fs->super->s_grp_quota_inum;
	quota_set_sb_inum(fs, 0, qtype);
	/* Truncate the inode only if its a reserved one. */
	if (qf_ino < EXT2_FIRST_INODE(fs->super))
		quota_inode_truncate(fs, qf_ino);

	ext2fs_mark_super_dirty(fs);
	ext2fs_write_bitmaps(fs);
	return 0;
}

static void write_dquots(dict_t *dict, struct quota_handle *qh)
{
	dnode_t		*n;
	struct dquot	*dq;

	for (n = dict_first(dict); n; n = dict_next(dict, n)) {
		dq = dnode_get(n);
		if (dq) {
			dq->dq_h = qh;
			update_grace_times(dq);
			qh->qh_ops->commit_dquot(dq);
		}
	}
}

errcode_t quota_write_inode(quota_ctx_t qctx, int qtype)
{
	int		retval = 0, i;
	dict_t		*dict;
	ext2_filsys	fs;
	struct quota_handle *h;
	int		fmt = QFMT_VFS_V1;

	if (!qctx)
		return 0;

	fs = qctx->fs;
	retval = ext2fs_get_mem(sizeof(struct quota_handle), &h);
	if (retval) {
		log_err("Unable to allocate quota handle", "");
		goto out;
	}

	ext2fs_read_bitmaps(fs);

	for (i = 0; i < MAXQUOTAS; i++) {
		if ((qtype != -1) && (i != qtype))
			continue;

		dict = qctx->quota_dict[i];
		if (!dict)
			continue;

		retval = quota_file_create(h, fs, i, fmt);
		if (retval < 0) {
			log_err("Cannot initialize io on quotafile", "");
			continue;
		}

		write_dquots(dict, h);
		retval = quota_file_close(h);
		if (retval < 0) {
			log_err("Cannot finish IO on new quotafile: %s",
				strerror(errno));
			if (h->qh_qf.e2_file)
				ext2fs_file_close(h->qh_qf.e2_file);
			quota_inode_truncate(fs, h->qh_qf.ino);
			continue;
		}

		/* Set quota inode numbers in superblock. */
		quota_set_sb_inum(fs, h->qh_qf.ino, i);
		ext2fs_mark_super_dirty(fs);
		ext2fs_mark_bb_dirty(fs);
		fs->flags &= ~EXT2_FLAG_SUPER_ONLY;
	}

	ext2fs_write_bitmaps(fs);
out:
	if (h)
		ext2fs_free_mem(&h);
	return retval;
}

/******************************************************************/
/* Helper functions for computing quota in memory.                */
/******************************************************************/

static int dict_uint_cmp(const void *a, const void *b)
{
	unsigned int	c, d;

	c = VOIDPTR_TO_UINT(a);
	d = VOIDPTR_TO_UINT(b);

	return c - d;
}

static inline qid_t get_qid(struct ext2_inode *inode, int qtype)
{
	if (qtype == USRQUOTA)
		return inode_uid(*inode);
	return inode_gid(*inode);
}

static void quota_dnode_free(dnode_t *node,
			     void *context EXT2FS_ATTR((unused)))
{
	void *ptr = node ? dnode_get(node) : 0;

	ext2fs_free_mem(&ptr);
	free(node);
}

/*
 * Set up the quota tracking data structures.
 */
errcode_t quota_init_context(quota_ctx_t *qctx, ext2_filsys fs, int qtype)
{
	int	i, err = 0;
	dict_t	*dict;
	quota_ctx_t ctx;

	err = ext2fs_get_mem(sizeof(struct quota_ctx), &ctx);
	if (err) {
		log_err("Failed to allocate quota context", "");
		return err;
	}

	memset(ctx, 0, sizeof(struct quota_ctx));
	for (i = 0; i < MAXQUOTAS; i++) {
		if ((qtype != -1) && (i != qtype))
			continue;
		err = ext2fs_get_mem(sizeof(dict_t), &dict);
		if (err) {
			log_err("Failed to allocate dictionary", "");
			return err;
		}
		ctx->quota_dict[i] = dict;
		dict_init(dict, DICTCOUNT_T_MAX, dict_uint_cmp);
		dict_set_allocator(dict, NULL, quota_dnode_free, NULL);
	}

	ctx->fs = fs;
	*qctx = ctx;
	return 0;
}

void quota_release_context(quota_ctx_t *qctx)
{
	dict_t	*dict;
	int	i;
	quota_ctx_t ctx;

	if (!qctx)
		return;

	ctx = *qctx;
	for (i = 0; i < MAXQUOTAS; i++) {
		dict = ctx->quota_dict[i];
		ctx->quota_dict[i] = 0;
		if (dict) {
			dict_free_nodes(dict);
			free(dict);
		}
	}
	*qctx = NULL;
	free(ctx);
}

static struct dquot *get_dq(dict_t *dict, __u32 key)
{
	struct dquot	*dq;
	dnode_t		*n;

	n = dict_lookup(dict, UINT_TO_VOIDPTR(key));
	if (n)
		dq = dnode_get(n);
	else {
		if (ext2fs_get_mem(sizeof(struct dquot), &dq)) {
			log_err("Unable to allocate dquot", "");
			return NULL;
		}
		memset(dq, 0, sizeof(struct dquot));
		dict_alloc_insert(dict, UINT_TO_VOIDPTR(key), dq);
		dq->dq_id = key;
	}
	return dq;
}


/*
 * Called to update the blocks used by a particular inode
 */
void quota_data_add(quota_ctx_t qctx, struct ext2_inode *inode, ext2_ino_t ino,
		    qsize_t space)
{
	struct dquot	*dq;
	dict_t		*dict;
	int		i;

	if (!qctx)
		return;

	log_debug("ADD_DATA: Inode: %u, UID/GID: %u/%u, space: %ld", ino,
			inode_uid(*inode),
			inode_gid(*inode), space);
	for (i = 0; i < MAXQUOTAS; i++) {
		dict = qctx->quota_dict[i];
		if (dict) {
			dq = get_dq(dict, get_qid(inode, i));
			if (dq)
				dq->dq_dqb.dqb_curspace += space;
		}
	}
}

/*
 * Called to remove some blocks used by a particular inode
 */
void quota_data_sub(quota_ctx_t qctx, struct ext2_inode *inode, ext2_ino_t ino,
		    qsize_t space)
{
	struct dquot	*dq;
	dict_t		*dict;
	int		i;

	if (!qctx)
		return;

	log_debug("SUB_DATA: Inode: %u, UID/GID: %u/%u, space: %ld", ino,
			inode_uid(*inode),
			inode_gid(*inode), space);
	for (i = 0; i < MAXQUOTAS; i++) {
		dict = qctx->quota_dict[i];
		if (dict) {
			dq = get_dq(dict, get_qid(inode, i));
			dq->dq_dqb.dqb_curspace -= space;
		}
	}
}

/*
 * Called to count the files used by an inode's user/group
 */
void quota_data_inodes(quota_ctx_t qctx, struct ext2_inode *inode,
		       ext2_ino_t ino, int adjust)
{
	struct dquot	*dq;
	dict_t		*dict;
	int		i;

	if (!qctx)
		return;

	log_debug("ADJ_INODE: Inode: %u, UID/GID: %u/%u, adjust: %d", ino,
			inode_uid(*inode),
			inode_gid(*inode), adjust);
	for (i = 0; i < MAXQUOTAS; i++) {
		dict = qctx->quota_dict[i];
		if (dict) {
			dq = get_dq(dict, get_qid(inode, i));
			dq->dq_dqb.dqb_curinodes += adjust;
		}
	}
}

errcode_t quota_compute_usage(quota_ctx_t qctx)
{
	ext2_filsys fs;
	ext2_ino_t ino;
	errcode_t ret;
	struct ext2_inode inode;
	qsize_t space;
	ext2_inode_scan scan;

	if (!qctx)
		return 0;

	fs = qctx->fs;
	ret = ext2fs_open_inode_scan(fs, 0, &scan);
	if (ret) {
		log_err("while opening inode scan. ret=%ld", ret);
		return ret;
	}

	while (1) {
		ret = ext2fs_get_next_inode(scan, &ino, &inode);
		if (ret) {
			log_err("while getting next inode. ret=%ld", ret);
			ext2fs_close_inode_scan(scan);
			return ret;
		}
		if (ino == 0)
			break;
		if (inode.i_links_count) {
			space = ext2fs_inode_i_blocks(fs, &inode) << 9;
			quota_data_add(qctx, &inode, ino, space);
			quota_data_inodes(qctx, &inode, ino, +1);
		}
	}

	ext2fs_close_inode_scan(scan);

	return 0;
}

struct scan_dquots_data {
	quota_ctx_t         qctx;
	int                 limit_only; /* read limit only */
};

static int scan_dquots_callback(struct dquot *dquot, void *cb_data)
{
	struct scan_dquots_data *scan_data =
		(struct scan_dquots_data *)cb_data;
	quota_ctx_t qctx = scan_data->qctx;
	struct dquot *dq;

	dq = get_dq(qctx->quota_dict[dquot->dq_h->qh_type], dquot->dq_id);

	dq->dq_id = dquot->dq_id;
	if (scan_data->limit_only) {
		dq->dq_dqb.u.v2_mdqb.dqb_off = dquot->dq_dqb.u.v2_mdqb.dqb_off;
		dq->dq_dqb.dqb_ihardlimit = dquot->dq_dqb.dqb_ihardlimit;
		dq->dq_dqb.dqb_isoftlimit = dquot->dq_dqb.dqb_isoftlimit;
		dq->dq_dqb.dqb_bhardlimit = dquot->dq_dqb.dqb_bhardlimit;
		dq->dq_dqb.dqb_bsoftlimit = dquot->dq_dqb.dqb_bsoftlimit;
	} else {
		dq->dq_dqb = dquot->dq_dqb;
	}
	return 0;
}

/*
 * Read all dquots from quota file into memory
 */
static errcode_t quota_read_all_dquots(struct quota_handle *qh,
                                       quota_ctx_t qctx, int limit_only)
{
	struct scan_dquots_data scan_data;

	scan_data.qctx = qctx;
	scan_data.limit_only = limit_only;

	return qh->qh_ops->scan_dquots(qh, scan_dquots_callback, &scan_data);
}

/*
 * Write all memory dquots into quota file
 */
static errcode_t quota_write_all_dquots(struct quota_handle *qh,
                                        quota_ctx_t qctx)
{
	errcode_t err;

	err = ext2fs_read_bitmaps(qctx->fs);
	if (err)
		return err;
	write_dquots(qctx->quota_dict[qh->qh_type], qh);
	ext2fs_mark_bb_dirty(qctx->fs);
	qctx->fs->flags &= ~EXT2_FLAG_SUPER_ONLY;
	ext2fs_write_bitmaps(qctx->fs);
	return 0;
}

/*
 * Update usage of in quota file, limits keep unchaged
 */
errcode_t quota_update_inode(quota_ctx_t qctx, ext2_ino_t qf_ino, int type)
{
	struct quota_handle *qh;
	errcode_t err;

	if (!qctx)
		return 0;

	err = ext2fs_get_mem(sizeof(struct quota_handle), &qh);
	if (err) {
		log_err("Unable to allocate quota handle", "");
		return err;
	}

	err = quota_file_open(qh, qctx->fs, qf_ino, type, -1, EXT2_FILE_WRITE);
	if (err) {
		log_err("Open quota file failed", "");
		goto out;
	}

	quota_read_all_dquots(qh, qctx, 1);
	quota_write_all_dquots(qh, qctx);

	err = quota_file_close(qh);
	if (err) {
		log_err("Cannot finish IO on new quotafile: %s",
			strerror(errno));
		if (qh->qh_qf.e2_file)
			ext2fs_file_close(qh->qh_qf.e2_file);
	}
out:
	ext2fs_free_mem(&qh);
	return err;
}
