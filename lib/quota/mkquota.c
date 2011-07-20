/*
 * mkquota.c --- create quota files for a filesystem
 *
 * Aditya Kali <adityakali@google.com>
 */
#include <sys/types.h>
#include <sys/stat.h>
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

int is_quota_on(ext2_filsys fs, int type)
{
	char tmp[1024];
	qid_t id = (type == USRQUOTA) ? getuid() : getgid();

	if (!quotactl(QCMD(Q_V2_GETQUOTA, type), fs->device_name, id, tmp))
		return 1;
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

	if (qtype >= MAXQUOTAS || fmt > QFMT_VFS_V1)
		return -EINVAL;

	get_qf_name(qtype, fmt, qf_name);

	ret = ext2fs_lookup(fs, EXT2_ROOT_INO, qf_name, strlen(qf_name), 0,
			    &ino);
	if (ret)
		return 0;

	return ino;
}

/*
 * Set the value for reserved quota inode number field in superblock.
 */
void set_sb_quota_inum(ext2_filsys fs, ext2_ino_t ino, int qtype)
{
	ext2_ino_t *inump;

	inump = (qtype == USRQUOTA) ? &fs->super->s_usr_quota_inum :
		&fs->super->s_grp_quota_inum;

	log_debug("setting quota ino in superblock: ino=%u, type=%d", ino,
		 qtype);
	*inump = ino;
	ext2fs_mark_super_dirty(fs);
}

errcode_t remove_quota_inode(ext2_filsys fs, int qtype)
{
	ext2_ino_t qf_ino;

	ext2fs_read_bitmaps(fs);
	qf_ino = (qtype == USRQUOTA) ? fs->super->s_usr_quota_inum :
		fs->super->s_grp_quota_inum;
	set_sb_quota_inum(fs, 0, qtype);
	/* Truncate the inode only if its a reserved one. */
	if (qf_ino < EXT2_FIRST_INODE(fs->super))
		truncate_quota_inode(fs, qf_ino);

	ext2fs_mark_super_dirty(fs);
	ext2fs_write_bitmaps(fs);
	return 0;
}

static void write_dquots(dict_t *dict, struct quota_handle *qh)
{
	int		i = 0;
	dnode_t		*n;
	struct dquot	*dq;
	__u32		key;

	for (n = dict_first(dict); n; n = dict_next(dict, n)) {
		dq = dnode_get(n);
		if (dq) {
			dq->dq_h = qh;
			update_grace_times(dq);
			qh->qh_ops->commit_dquot(dq, COMMIT_ALL);
		}
	}
}

errcode_t write_quota_inode(quota_ctx_t qctx, int qtype)
{
	int		retval, i;
	unsigned long	qf_inums[MAXQUOTAS];
	struct dquot	*dquot;
	dict_t		*dict;
	ext2_filsys	fs;
	struct quota_handle *h;
	int		fmt = QFMT_VFS_V1;

	if (!qctx)
		return;

	fs = qctx->fs;
	h = smalloc(sizeof(struct quota_handle));
	ext2fs_read_bitmaps(fs);

	for (i = 0; i < MAXQUOTAS; i++) {
		if ((qtype != -1) && (i != qtype))
			continue;

		dict = qctx->quota_dict[i];
		if (!dict)
			continue;

		retval = new_io(h, fs, i, fmt);
		if (retval < 0) {
			log_err("Cannot initialize io on quotafile", "");
			continue;
		}

		write_dquots(dict, h);
		retval = end_io(h);
		if (retval < 0) {
			log_err("Cannot finish IO on new quotafile: %s",
				strerror(errno));
			if (h->qh_qf.e2_file)
				ext2fs_file_close(h->qh_qf.e2_file);
			truncate_quota_inode(fs, h->qh_qf.ino);
			continue;
		}

		/* Set quota inode numbers in superblock. */
		set_sb_quota_inum(fs, h->qh_qf.ino, i);
		ext2fs_mark_super_dirty(fs);
		ext2fs_mark_bb_dirty(fs);
		fs->flags &= ~EXT2_FLAG_SUPER_ONLY;
	}

	ext2fs_write_bitmaps(fs);
out:
	free(h);
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

static qid_t get_qid(struct ext2_inode *inode, int qtype)
{
	switch (qtype) {
	case USRQUOTA:
		return inode_uid(*inode);
	case GRPQUOTA:
		return inode_gid(*inode);
	default:
		log_err("Invalid quota type: %d", qtype);
		BUG_ON(1);
	}
}

static void quota_dnode_free(dnode_t *node,
			     void *context EXT2FS_ATTR((unused)))
{
	void *ptr = node ? dnode_get(node) : 0;

	free(ptr);
	free(node);
}

/*
 * Called in Pass #1 to set up the quota tracking data structures.
 */
void init_quota_context(quota_ctx_t *qctx, ext2_filsys fs, int qtype)
{
	int	i;
	dict_t	*dict;
	quota_ctx_t ctx;

	ctx = (quota_ctx_t)smalloc(sizeof(struct quota_ctx));
	memset(ctx, 0, sizeof(struct quota_ctx));
	for (i = 0; i < MAXQUOTAS; i++) {
		if ((qtype != -1) && (i != qtype))
			continue;
		dict = (dict_t *)smalloc(sizeof(dict_t));
		ctx->quota_dict[i] = dict;
		dict_init(dict, DICTCOUNT_T_MAX, dict_uint_cmp);
		dict_set_allocator(dict, NULL, quota_dnode_free, NULL);
	}

	ctx->fs = fs;
	*qctx = ctx;
}

void release_quota_context(quota_ctx_t *qctx)
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
		dq = smalloc(sizeof(struct dquot));
		memset(dq, 0, sizeof(struct dquot));
		dict_alloc_insert(dict, UINT_TO_VOIDPTR(key), dq);
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

errcode_t compute_quota(quota_ctx_t qctx, int qtype)
{
	ext2_filsys fs;
	const char *name = "lost+found";
	ext2_ino_t ino;
	errcode_t ret;
	struct ext2_inode inode;
	qsize_t space;
	ext2_inode_scan scan;

	if (!qctx)
		return;

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
			/* Convert i_blocks to # of 1k blocks */
			space = (ext2fs_inode_i_blocks(fs, &inode) + 1) >> 1;
			quota_data_add(qctx, &inode, ino, space);
			quota_data_inodes(qctx, &inode, ino, +1);
		}
	}

	ext2fs_close_inode_scan(scan);

	return 0;
}
