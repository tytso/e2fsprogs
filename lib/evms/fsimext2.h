/*
 *
 *   Copyright (c) International Business Machines  Corp., 2000
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or 
 *   (at your option) any later version.
 * 
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY;  without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program;  if not, write to the Free Software 
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 *   Module: fsimext2.h
 *
 */

/* If EVMS_ABI_CODE is not defined, default to EVMS ABI 1.2 */
#ifndef EVMS_ABI_CODE
#define EVMS_ABI_CODE 120
#endif

/*
 * EVMS 1.0 backwards compatibility functions
 */
#if (EVMS_ABI_CODE == 100)
#define EVMS_IS_MOUNTED(vol)	((vol)->mount_point != 0)
#define EVMS_GET_DEVNAME(vol)	((vol)->name)
#else
#define EVMS_IS_MOUNTED(vol)	(EngFncs->is_mounted((vol)->dev_node, NULL))
#define EVMS_GET_DEVNAME(vol)	((vol)->dev_node)
#endif
                   
/********************
 ********************
 **                **
 **  EVMS defines  **
 **                **
 ********************
 ********************/
 
extern plugin_record_t ext2_plugrec;
engine_functions_t *EngFncs;
                 
/*  file system type ... used by the SetPluginID macro */
#define FS_TYPE_EXT2     7
                 
/*  logging macros  */
#define MESSAGE(msg, args...)		EngFncs->user_message(pMyPluginRecord, NULL, NULL, msg, ##args)
#define LOGENTRY()			EngFncs->write_log_entry(ENTRY_EXIT, pMyPluginRecord, "%s:  Enter.\n",         __FUNCTION__ )
#define LOGEXIT()			EngFncs->write_log_entry(ENTRY_EXIT, pMyPluginRecord, "%s:  Exit.\n",          __FUNCTION__ )
#define LOGEXITRC()			EngFncs->write_log_entry(ENTRY_EXIT, pMyPluginRecord, "%s:  Exit. rc = %d.\n", __FUNCTION__ , rc)
#define LOG_CRITICAL(msg, args...)	EngFncs->write_log_entry(CRITICAL,   pMyPluginRecord, "%s: " msg, __FUNCTION__ , ## args)
#define LOG_SERIOUS(msg, args...)	EngFncs->write_log_entry(SERIOUS,    pMyPluginRecord, "%s: " msg, __FUNCTION__ , ## args)
#define LOG_ERROR(msg, args...)		EngFncs->write_log_entry(ERROR,      pMyPluginRecord, "%s: " msg, __FUNCTION__ , ## args)
#define LOG_WARNING(msg, args...)	EngFncs->write_log_entry(WARNING,    pMyPluginRecord, "%s: " msg, __FUNCTION__ , ## args)
#define LOG(msg, args...)		EngFncs->write_log_entry(DEFAULT,    pMyPluginRecord, "%s: " msg, __FUNCTION__ , ## args)
#define LOG_DETAILS(msg, args...)	EngFncs->write_log_entry(DETAILS,    pMyPluginRecord, "%s: " msg, __FUNCTION__ , ## args)
#define LOG_DEBUG(msg, args...)		EngFncs->write_log_entry(DEBUG,      pMyPluginRecord, "%s: " msg, __FUNCTION__ , ## args)
#define LOG_EXTRA(msg, args...)		EngFncs->write_log_entry(EXTRA,      pMyPluginRecord, "%s: " msg, __FUNCTION__ , ## args)

/*  useful macro for option code */
#define SET_STRING_FIELD(a,b)\
a = EngFncs->engine_alloc( strlen(b)+1 );\
if (a ) {\
    strcpy(a, b);\
}\
else {\
    return -ENOMEM;\
}

#define SET_STRING(a,b) a = EngFncs->engine_alloc( strlen(b)+1 );if (a ) { strcpy(a, b); } else { rc = ENOMEM; LOG_EXIT(rc);}
#define LOG_EXIT(x)     LOG_PROC("Exiting: rc = %d\n", x)
#define LOG_PROC(msg, args...)    EngFncs->write_log_entry(ENTRY_EXIT, pMyPluginRecord, "%s: " msg, __FUNCTION__ , ## args)


/**********************************
 **********************************
 **                              **
 **  fsck.jfs, mkfs.jfs defines  **
 **                              **
 **********************************
 **********************************/

/* fsck.jfs, mkfs.jfs option counts */

/* fsck.jfs option array indices */
#define FSCK_FORCE_INDEX      	0
#define FSCK_READONLY_INDEX   	1
#define FSCK_CHECKBB_INDEX      2
#define FSCK_CHECKRW_INDEX      3
#define FSCK_TIMING_INDEX	4
#define FSCK_EXT2_OPTIONS_COUNT  5

/* mkfs.jfs option array indices */
#define MKFS_CHECKBB_INDEX      0
#define MKFS_CHECKRW_INDEX      1
#define MKFS_SETVOL_INDEX       2
#define MKFS_JOURNAL_INDEX      3
#define MKFS_EXT2_OPTIONS_COUNT 4

/* fsck exit codes */
#define FSCK_OK                    0
#define FSCK_CORRECTED             1
#define FSCK_REBOOT                2
#define FSCK_ERRORS_UNCORRECTED    4
#define FSCK_OP_ERROR              8
#define FSCK_USAGE_ERROR          16


/*
 * EXT2/3 defines and structs
 */

/* generic defines */
#define FSIM_SUCCESS            0
#define FSIM_ERROR             -1
#define GET                     0
#define PUT                     1

#define EXT2_SUPER_LOC		1024

#define EXT2_SUPER_MAGIC	0xEF53

#define EXT3_FEATURE_INCOMPAT_RECOVER		0x0004 /* Needs recovery */

#define	EXT2_VALID_FS			0x0001	/* Unmounted cleanly */
#define	EXT2_ERROR_FS			0x0002	/* Errors detected */

/*
 * Structure of the ext2 super block
 */
struct ext2_super_block {
	u_int32_t	s_inodes_count;		/* Inodes count */
	u_int32_t	s_blocks_count;		/* Blocks count */
	u_int32_t	s_r_blocks_count;	/* Reserved blocks count */
	u_int32_t	s_free_blocks_count;	/* Free blocks count */
	u_int32_t	s_free_inodes_count;	/* Free inodes count */
	u_int32_t	s_first_data_block;	/* First Data Block */
	u_int32_t	s_log_block_size;	/* Block size */
	int32_t		s_log_frag_size;	/* Fragment size */
	u_int32_t	s_blocks_per_group;	/* # Blocks per group */
	u_int32_t	s_frags_per_group;	/* # Fragments per group */
	u_int32_t	s_inodes_per_group;	/* # Inodes per group */
	u_int32_t	s_mtime;		/* Mount time */
	u_int32_t	s_wtime;		/* Write time */
	u_int16_t	s_mnt_count;		/* Mount count */
	int16_t		s_max_mnt_count;	/* Maximal mount count */
	u_int16_t	s_magic;		/* Magic signature */
	u_int16_t	s_state;		/* File system state */
	u_int16_t	s_errors;		/* Behaviour when detecting errors */
	u_int16_t	s_minor_rev_level; 	/* minor revision level */
	u_int32_t	s_lastcheck;		/* time of last check */
	u_int32_t	s_checkinterval;	/* max. time between checks */
	u_int32_t	s_creator_os;		/* OS */
	u_int32_t	s_rev_level;		/* Revision level */
	u_int16_t	s_def_resuid;		/* Default uid for reserved blocks */
	u_int16_t	s_def_resgid;		/* Default gid for reserved blocks */
	/*
	 * These fields are for EXT2_DYNAMIC_REV superblocks only.
	 *
	 * Note: the difference between the compatible feature set and
	 * the incompatible feature set is that if there is a bit set
	 * in the incompatible feature set that the kernel doesn't
	 * know about, it should refuse to mount the filesystem.
	 * 
	 * e2fsck's requirements are more strict; if it doesn't know
	 * about a feature in either the compatible or incompatible
	 * feature set, it must abort and not try to meddle with
	 * things it doesn't understand...
	 */
	u_int32_t	s_first_ino; 		/* First non-reserved inode */
	u_int16_t	s_inode_size; 		/* size of inode structure */
	u_int16_t	s_block_group_nr; 	/* block group # of this superblock */
	u_int32_t	s_feature_compat; 	/* compatible feature set */
	u_int32_t	s_feature_incompat; 	/* incompatible feature set */
	u_int32_t	s_feature_ro_compat; 	/* readonly-compatible feature set */
	u_int8_t	s_uuid[16];		/* 128-bit uuid for volume */
	int8_t		s_volume_name[16]; 	/* volume name */
	int8_t		s_last_mounted[64]; 	/* directory where last mounted */
	u_int32_t	s_algorithm_usage_bitmap; /* For compression */
	/*
	 * Performance hints.  Directory preallocation should only
	 * happen if the EXT2_FEATURE_COMPAT_DIR_PREALLOC flag is on.
	 */
	u_int8_t	s_prealloc_blocks;	/* Nr of blocks to try to preallocate*/
	u_int8_t	s_prealloc_dir_blocks;	/* Nr to preallocate for dirs */
	u_int16_t	s_padding1;
	/* 
	 * Journaling support valid if EXT2_FEATURE_COMPAT_HAS_JOURNAL set.
	 */
	u_int8_t	s_journal_uuid[16];	/* uuid of journal superblock */
	u_int32_t	s_journal_inum;		/* inode number of journal file */
	u_int32_t	s_journal_dev;		/* device number of journal file */
	u_int32_t	s_last_orphan;		/* start of list of inodes to delete */
	
	u_int32_t	s_reserved[197];	/* Padding to the end of the block */
};

#define L2MEGABYTE      20
#define MEGABYTE        (1 << L2MEGABYTE)
#define MEGABYTE32     (MEGABYTE << 5)
#define MAX_LOG_PERCENTAGE  10              /* Log can be at most 10% of disk */

/*
 *	buffer cache configuration
 */
/* page size */
#ifdef PSIZE
#undef PSIZE
#endif
#define	PSIZE		4096	/* page size (in byte) */

#define	PBSIZE		512	/* physical block size (in byte) */

/*
 * Minimum number of bytes supported for an ext2 partition
 * (64k, quite small!)
 */
#define MINEXT2		(64*1024)

/*
 * SIZE_OF_SUPER defines the total amount of space reserved on disk for the
 * superblock.  This is not the same as the superblock structure, since all of
 * this space is not currently being used.
 */
#define SIZE_OF_SUPER	sizeof(struct ext2_super_block)

/*
 * SIZE_OF_MAP_PAGE defines the amount of disk space reserved for each page of
 * the inode allocation map (to hold iag)
 */
#define SIZE_OF_MAP_PAGE	PSIZE

/*
 *	directory configuration
 */
#define JFS_NAME_MAX	255
#define JFS_PATH_MAX	BPSIZE      

/*
 *	file system state (superblock state)
 */
#define FM_CLEAN 0x00000000	/* file system is unmounted and clean */
#define FM_MOUNT 0x00000001	/* file system is mounted cleanly */
#define FM_DIRTY 0x00000002	/* file system was not unmounted and clean 
            				 * when mounted or 
			            	 * commit failure occurred while being mounted:
				             * fsck() must be run to repair 
				             */
#define	FM_LOGREDO 0x00000004	/* log based recovery (logredo()) failed:
				                 * fsck() must be run to repair 
				                 */
#define	FM_EXTENDFS 0x00000008	/* file system extendfs() in progress */


/*******************
 *******************
 **               **
 **  Common code  **
 **               **
 *******************
 *******************/

int fsim_get_ext2_superblock( logical_volume_t *, struct ext2_super_block * );
int fsim_unmkfs( logical_volume_t * );
int fsim_mkfs( logical_volume_t *, option_array_t * );
int fsim_fsck( logical_volume_t *, option_array_t *, int * );
int fsim_get_volume_limits( struct ext2_super_block *, sector_count_t *,
			                   sector_count_t *, sector_count_t * );
int fsim_test_version( void );
