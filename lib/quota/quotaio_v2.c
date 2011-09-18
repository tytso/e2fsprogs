/*
 * Implementation of new quotafile format
 *
 * Jan Kara <jack@suse.cz> - sponsored by SuSE CR
 */

#include "config.h"
#include <sys/types.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <asm/byteorder.h>

#include "common.h"
#include "quotaio_v2.h"
#include "dqblk_v2.h"
#include "quotaio.h"
#include "quotaio_tree.h"

typedef char *dqbuf_t;

static int v2_check_file(struct quota_handle *h, int type, int fmt);
static int v2_init_io(struct quota_handle *h);
static int v2_new_io(struct quota_handle *h);
static int v2_write_info(struct quota_handle *h);
static struct dquot *v2_read_dquot(struct quota_handle *h, qid_t id);
static int v2_commit_dquot(struct dquot *dquot, int flags);
static int v2_scan_dquots(struct quota_handle *h,
			  int (*process_dquot) (struct dquot *dquot,
						char *dqname));
static int v2_report(struct quota_handle *h, int verbose);

struct quotafile_ops quotafile_ops_2 = {
check_file:	v2_check_file,
init_io:	v2_init_io,
new_io:		v2_new_io,
write_info:	v2_write_info,
read_dquot:	v2_read_dquot,
commit_dquot:	v2_commit_dquot,
scan_dquots:	v2_scan_dquots,
report:	v2_report
};

#define getdqbuf() smalloc(V2_DQBLKSIZE)
#define freedqbuf(buf) free(buf)

/*
 * Copy dquot from disk to memory
 */
static void v2r1_disk2memdqblk(struct dquot *dquot, void *dp)
{
	struct util_dqblk *m = &dquot->dq_dqb;
	struct v2r1_disk_dqblk *d = dp, empty;

	dquot->dq_id = __le32_to_cpu(d->dqb_id);
	m->dqb_ihardlimit = __le64_to_cpu(d->dqb_ihardlimit);
	m->dqb_isoftlimit = __le64_to_cpu(d->dqb_isoftlimit);
	m->dqb_bhardlimit = __le64_to_cpu(d->dqb_bhardlimit);
	m->dqb_bsoftlimit = __le64_to_cpu(d->dqb_bsoftlimit);
	m->dqb_curinodes = __le64_to_cpu(d->dqb_curinodes);
	m->dqb_curspace = __le64_to_cpu(d->dqb_curspace);
	m->dqb_itime = __le64_to_cpu(d->dqb_itime);
	m->dqb_btime = __le64_to_cpu(d->dqb_btime);

	memset(&empty, 0, sizeof(struct v2r1_disk_dqblk));
	empty.dqb_itime = __cpu_to_le64(1);
	if (!memcmp(&empty, dp, sizeof(struct v2r1_disk_dqblk)))
		m->dqb_itime = 0;
}

/*
 * Copy dquot from memory to disk
 */
static void v2r1_mem2diskdqblk(void *dp, struct dquot *dquot)
{
	struct util_dqblk *m = &dquot->dq_dqb;
	struct v2r1_disk_dqblk *d = dp;

	d->dqb_ihardlimit = __cpu_to_le64(m->dqb_ihardlimit);
	d->dqb_isoftlimit = __cpu_to_le64(m->dqb_isoftlimit);
	d->dqb_bhardlimit = __cpu_to_le64(m->dqb_bhardlimit);
	d->dqb_bsoftlimit = __cpu_to_le64(m->dqb_bsoftlimit);
	d->dqb_curinodes = __cpu_to_le64(m->dqb_curinodes);
	d->dqb_curspace = __cpu_to_le64(m->dqb_curspace);
	d->dqb_itime = __cpu_to_le64(m->dqb_itime);
	d->dqb_btime = __cpu_to_le64(m->dqb_btime);
	d->dqb_id = __cpu_to_le32(dquot->dq_id);
	if (qtree_entry_unused(&dquot->dq_h->qh_info.u.v2_mdqi.dqi_qtree, dp))
		d->dqb_itime = __cpu_to_le64(1);
}

static int v2r1_is_id(void *dp, struct dquot *dquot)
{
	struct v2r1_disk_dqblk *d = dp;
	struct qtree_mem_dqinfo *info =
			&dquot->dq_h->qh_info.u.v2_mdqi.dqi_qtree;

	if (qtree_entry_unused(info, dp))
		return 0;
	return __le32_to_cpu(d->dqb_id) == dquot->dq_id;
}

static struct qtree_fmt_operations v2r1_fmt_ops = {
	.mem2disk_dqblk = v2r1_mem2diskdqblk,
	.disk2mem_dqblk = v2r1_disk2memdqblk,
	.is_id = v2r1_is_id,
};

/*
 * Copy dqinfo from disk to memory
 */
static inline void v2_disk2memdqinfo(struct util_dqinfo *m,
				     struct v2_disk_dqinfo *d)
{
	m->dqi_bgrace = __le32_to_cpu(d->dqi_bgrace);
	m->dqi_igrace = __le32_to_cpu(d->dqi_igrace);
	m->u.v2_mdqi.dqi_flags = __le32_to_cpu(d->dqi_flags) & V2_DQF_MASK;
	m->u.v2_mdqi.dqi_qtree.dqi_blocks = __le32_to_cpu(d->dqi_blocks);
	m->u.v2_mdqi.dqi_qtree.dqi_free_blk = __le32_to_cpu(d->dqi_free_blk);
	m->u.v2_mdqi.dqi_qtree.dqi_free_entry =
				__le32_to_cpu(d->dqi_free_entry);
}

/*
 * Copy dqinfo from memory to disk
 */
static inline void v2_mem2diskdqinfo(struct v2_disk_dqinfo *d,
				     struct util_dqinfo *m)
{
	d->dqi_bgrace = __cpu_to_le32(m->dqi_bgrace);
	d->dqi_igrace = __cpu_to_le32(m->dqi_igrace);
	d->dqi_flags = __cpu_to_le32(m->u.v2_mdqi.dqi_flags & V2_DQF_MASK);
	d->dqi_blocks = __cpu_to_le32(m->u.v2_mdqi.dqi_qtree.dqi_blocks);
	d->dqi_free_blk = __cpu_to_le32(m->u.v2_mdqi.dqi_qtree.dqi_free_blk);
	d->dqi_free_entry =
			__cpu_to_le32(m->u.v2_mdqi.dqi_qtree.dqi_free_entry);
}

/* Convert kernel quotablock format to utility one */
static inline void v2_kern2utildqblk(struct util_dqblk *u,
				     struct v2_kern_dqblk *k)
{
	u->dqb_ihardlimit = k->dqb_ihardlimit;
	u->dqb_isoftlimit = k->dqb_isoftlimit;
	u->dqb_bhardlimit = k->dqb_bhardlimit;
	u->dqb_bsoftlimit = k->dqb_bsoftlimit;
	u->dqb_curinodes = k->dqb_curinodes;
	u->dqb_curspace = k->dqb_curspace;
	u->dqb_itime = k->dqb_itime;
	u->dqb_btime = k->dqb_btime;
}

/* Convert utility quotablock format to kernel one */
static inline void v2_util2kerndqblk(struct v2_kern_dqblk *k,
				     struct util_dqblk *u)
{
	k->dqb_ihardlimit = u->dqb_ihardlimit;
	k->dqb_isoftlimit = u->dqb_isoftlimit;
	k->dqb_bhardlimit = u->dqb_bhardlimit;
	k->dqb_bsoftlimit = u->dqb_bsoftlimit;
	k->dqb_curinodes = u->dqb_curinodes;
	k->dqb_curspace = u->dqb_curspace;
	k->dqb_itime = u->dqb_itime;
	k->dqb_btime = u->dqb_btime;
}

static int v2_read_header(struct quota_handle *h, struct v2_disk_dqheader *dqh)
{
	if (h->e2fs_read(&h->qh_qf, 0, dqh, sizeof(struct v2_disk_dqheader)) !=
			sizeof(struct v2_disk_dqheader))
		return 0;

	return 1;
}

/*
 * Check whether given quota file is in our format
 */
static int v2_check_file(struct quota_handle *h, int type, int fmt)
{
	struct v2_disk_dqheader dqh;
	int file_magics[] = INITQMAGICS;
	int known_versions[] = INIT_V2_VERSIONS;
	int version;

	if (!v2_read_header(h, &dqh))
		return 0;
	if (fmt == QFMT_VFS_V0)
		version = 0;
	else if (fmt == QFMT_VFS_V1)
		version = 1;
	else
		return 0;

	if (__le32_to_cpu(dqh.dqh_magic) != file_magics[type]) {
		if (__be32_to_cpu(dqh.dqh_magic) == file_magics[type])
			log_fatal(3, "Your quota file is stored in wrong "
				  "endianity.", "");
		return 0;
	}
	if (__le32_to_cpu(dqh.dqh_version) > known_versions[type])
		return 0;
	if (version != __le32_to_cpu(dqh.dqh_version))
		return 0;
	return 1;
}

/*
 * Open quotafile
 */
static int v2_init_io(struct quota_handle *h)
{
	log_err("Not Implemented.", "");
	BUG_ON(1);
	return 0;
}

/*
 * Initialize new quotafile
 */
static int v2_new_io(struct quota_handle *h)
{
	int file_magics[] = INITQMAGICS;
	struct v2_disk_dqheader ddqheader;
	struct v2_disk_dqinfo ddqinfo;
	int version = 1;

	BUG_ON(h->qh_fmt != QFMT_VFS_V1);

	/* Write basic quota header */
	ddqheader.dqh_magic = __cpu_to_le32(file_magics[h->qh_type]);
	ddqheader.dqh_version = __cpu_to_le32(version);
	if (h->e2fs_write(&h->qh_qf, 0, &ddqheader, sizeof(ddqheader)) !=
			sizeof(ddqheader))
		return -1;

	/* Write information about quotafile */
	h->qh_info.dqi_bgrace = MAX_DQ_TIME;
	h->qh_info.dqi_igrace = MAX_IQ_TIME;
	h->qh_info.u.v2_mdqi.dqi_flags = 0;
	h->qh_info.u.v2_mdqi.dqi_qtree.dqi_blocks = QT_TREEOFF + 1;
	h->qh_info.u.v2_mdqi.dqi_qtree.dqi_free_blk = 0;
	h->qh_info.u.v2_mdqi.dqi_qtree.dqi_free_entry = 0;
	h->qh_info.u.v2_mdqi.dqi_qtree.dqi_entry_size =
				sizeof(struct v2r1_disk_dqblk);
	h->qh_info.u.v2_mdqi.dqi_qtree.dqi_ops = &v2r1_fmt_ops;
	v2_mem2diskdqinfo(&ddqinfo, &h->qh_info);
	if (h->e2fs_write(&h->qh_qf, V2_DQINFOOFF, &ddqinfo,
			  sizeof(ddqinfo)) !=
	    sizeof(ddqinfo))
		return -1;

	return 0;
}

/*
 * Write information (grace times to file)
 */
static int v2_write_info(struct quota_handle *h)
{
	struct v2_disk_dqinfo ddqinfo;

	v2_mem2diskdqinfo(&ddqinfo, &h->qh_info);
	if (h->e2fs_write(&h->qh_qf, V2_DQINFOOFF, &ddqinfo, sizeof(ddqinfo)) !=
			sizeof(ddqinfo))
		return -1;

	return 0;
}

/*
 * Read dquot (either from disk or from kernel)
 * User can use errno to detect errstr when NULL is returned
 */
static struct dquot *v2_read_dquot(struct quota_handle *h, qid_t id)
{
	return qtree_read_dquot(h, id);
}

/*
 * Commit changes of dquot to disk - it might also mean deleting it when quota
 * became fake one and user has no blocks.
 * User can process use 'errno' to detect errstr.
 */
static int v2_commit_dquot(struct dquot *dquot, int flags)
{
	struct util_dqblk *b = &dquot->dq_dqb;

	if (!b->dqb_curspace && !b->dqb_curinodes && !b->dqb_bsoftlimit &&
	    !b->dqb_isoftlimit && !b->dqb_bhardlimit && !b->dqb_ihardlimit)
		qtree_delete_dquot(dquot);
	else
		qtree_write_dquot(dquot);
	return 0;
}

static int v2_scan_dquots(struct quota_handle *h,
			  int (*process_dquot) (struct dquot *, char *))
{
	return qtree_scan_dquots(h, process_dquot);
}

/* Report information about quotafile.
 * TODO: Not used right now, but we should be able to use this when we add
 * support to debugfs to read quota files.
 */
static int v2_report(struct quota_handle *h, int verbose)
{
	log_err("Not Implemented.", "");
	BUG_ON(1);
	return 0;
}
