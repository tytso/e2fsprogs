/*
 * Header file for disk format of new quotafile format
 *
 * Jan Kara <jack@suse.cz> - sponsored by SuSE CR
 */

#ifndef __QUOTA_DQBLK_V2_H__
#define __QUOTA_DQBLK_V2_H__

#include <sys/types.h>
#include "quotaio_tree.h"

#define Q_V2_GETQUOTA	0x0D00	/* Get limits and usage */
#define Q_V2_SETQUOTA	0x0E00	/* Set limits and usage */
#define Q_V2_SETUSE	0x0F00	/* Set only usage */
#define Q_V2_SETQLIM	0x0700	/* Set only limits */
#define Q_V2_GETINFO	0x0900	/* Get information about quota */
#define Q_V2_SETINFO	0x0A00	/* Set information about quota */
#define Q_V2_SETGRACE	0x0B00	/* Set just grace times in quotafile
				 * information */
#define Q_V2_SETFLAGS	0x0C00	/* Set just flags in quotafile information */
#define Q_V2_GETSTATS	0x1100	/* get collected stats (before proc was used) */

/* Structure for format specific information */
struct v2_mem_dqinfo {
	struct qtree_mem_dqinfo dqi_qtree;
	uint dqi_flags;		/* Flags set in quotafile */
	uint dqi_used_entries;	/* Number of entries in file -
				   updated by scan_dquots */
	uint dqi_data_blocks;	/* Number of data blocks in file -
				   updated by scan_dquots */
};

struct v2_mem_dqblk {
	long long dqb_off;	/* Offset of dquot in file */
};

struct quotafile_ops;		/* Will be defined later in quotaio.h */

/* Operations above this format */
extern struct quotafile_ops quotafile_ops_2;

#endif  /* __QUOTA_DQBLK_V2_H__ */
