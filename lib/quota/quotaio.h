/** quotaio.h
 *
 * Header of IO operations for quota utilities
 * Jan Kara <jack@suse.cz>
 */

#ifndef GUARD_QUOTAIO_H
#define GUARD_QUOTAIO_H

#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "ext2fs/ext2fs.h"
#include "quota.h"
#include "dqblk_v2.h"

/*
 * Definitions for disk quotas imposed on the average user
 * (big brother finally hits Linux).
 *
 * The following constants define the default amount of time given a user
 * before the soft limits are treated as hard limits (usually resulting
 * in an allocation failure). The timer is started when the user crosses
 * their soft limit, it is reset when they go below their soft limit.
 */
#define MAX_IQ_TIME  604800	/* (7*24*60*60) 1 week */
#define MAX_DQ_TIME  604800	/* (7*24*60*60) 1 week */

#define IOFL_QUOTAON	0x01	/* Is quota enabled in kernel? */
#define IOFL_INFODIRTY	0x02	/* Did info change? */
#define IOFL_RO		0x04	/* Just RO access? */
#define IOFL_NFS_MIXED_PATHS	0x08	/* Should we trim leading slashes
					   from NFSv4 mountpoints? */

struct quotafile_ops;

/* Generic information about quotafile */
struct util_dqinfo {
	time_t dqi_bgrace;	/* Block grace time for given quotafile */
	time_t dqi_igrace;	/* Inode grace time for given quotafile */
	union {
		struct v2_mem_dqinfo v2_mdqi;
	} u;			/* Format specific info about quotafile */
};

struct quota_file {
	ext2_filsys fs;
	ext2_ino_t ino;
	ext2_file_t e2_file;
};

/* Structure for one opened quota file */
struct quota_handle {
	int qh_type;		/* Type of quotafile */
	int qh_fmt;		/* Quotafile format */
	int qh_io_flags;	/* IO flags for file */
	struct quota_file qh_qf;
	unsigned int (*e2fs_read)(struct quota_file *qf, loff_t offset,
			 void *buf, unsigned int size);
	unsigned int (*e2fs_write)(struct quota_file *qf, loff_t offset,
			  void *buf, unsigned int size);
	struct quotafile_ops *qh_ops;	/* Operations on quotafile */
	struct util_dqinfo qh_info;	/* Generic quotafile info */
};

/* Statistics gathered from kernel */
struct util_dqstats {
	u_int32_t lookups;
	u_int32_t drops;
	u_int32_t reads;
	u_int32_t writes;
	u_int32_t cache_hits;
	u_int32_t allocated_dquots;
	u_int32_t free_dquots;
	u_int32_t syncs;
	u_int32_t version;
};

/* Utility quota block */
struct util_dqblk {
	qsize_t dqb_ihardlimit;
	qsize_t dqb_isoftlimit;
	qsize_t dqb_curinodes;
	qsize_t dqb_bhardlimit;
	qsize_t dqb_bsoftlimit;
	qsize_t dqb_curspace;
	time_t dqb_btime;
	time_t dqb_itime;
	union {
		struct v2_mem_dqblk v2_mdqb;
	} u;			/* Format specific dquot information */
};

/* Structure for one loaded quota */
struct dquot {
	struct dquot *dq_next;	/* Pointer to next dquot in the list */
	qid_t dq_id;		/* ID dquot belongs to */
	int dq_flags;		/* Some flags for utils */
	struct quota_handle *dq_h;	/* Handle of quotafile for this dquot */
	struct util_dqblk dq_dqb;	/* Parsed data of dquot */
};

/* Flags for commit function (have effect only when quota in kernel is
 * turned on) */
#define COMMIT_USAGE QIF_USAGE
#define COMMIT_LIMITS QIF_LIMITS
#define COMMIT_TIMES QIF_TIMES
#define COMMIT_ALL (COMMIT_USAGE | COMMIT_LIMITS | COMMIT_TIMES)

/* Structure of quotafile operations */
struct quotafile_ops {
	/* Check whether quotafile is in our format */
	int (*check_file) (struct quota_handle *h, int type, int fmt);
	/* Open quotafile */
	int (*init_io) (struct quota_handle *h);
	/* Create new quotafile */
	int (*new_io) (struct quota_handle *h);
	/* Write all changes and close quotafile */
	int (*end_io) (struct quota_handle *h);
	/* Write info about quotafile */
	int (*write_info) (struct quota_handle *h);
	/* Read dquot into memory */
	struct dquot *(*read_dquot) (struct quota_handle *h, qid_t id);
	/* Write given dquot to disk */
	int (*commit_dquot) (struct dquot *dquot, int flag);
	/* Scan quotafile and call callback on every structure */
	int (*scan_dquots) (struct quota_handle *h,
			    int (*process_dquot) (struct dquot *dquot,
						  char *dqname));
	/* Function to print format specific file information */
	int (*report) (struct quota_handle *h, int verbose);
};

/* This might go into a special header file but that sounds a bit silly... */
extern struct quotafile_ops quotafile_ops_meta;

static inline void mark_quotafile_info_dirty(struct quota_handle *h)
{
	h->qh_io_flags |= IOFL_INFODIRTY;
}

#define QIO_ENABLED(h)	((h)->qh_io_flags & IOFL_QUOTAON)
#define QIO_RO(h)	((h)->qh_io_flags & IOFL_RO)

/* Check quota format used on specified medium and initialize it */
struct quota_handle *init_io(ext2_filsys fs, const char *mntpt, int type,
			     int fmt, int flags);

/* Create new quotafile of specified format on given filesystem */
int new_io(struct quota_handle *h, ext2_filsys fs, int type, int fmt);

/* Close quotafile */
int end_io(struct quota_handle *h);

/* Get empty quota structure */
struct dquot *get_empty_dquot(void);

void truncate_quota_inode(ext2_filsys fs, ext2_ino_t ino);

const char *type2name(int type);

#endif /* GUARD_QUOTAIO_H */
