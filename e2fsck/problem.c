/*
 * problem.c --- report filesystem problems to the user
 *
 * Copyright 1996, 1997 by Theodore Ts'o
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <termios.h>

#include "e2fsck.h"

#include "problem.h"
#include "problemP.h"

#define PROMPT_NONE	0
#define PROMPT_FIX	1
#define PROMPT_CLEAR	2
#define PROMPT_RELOCATE	3
#define PROMPT_ALLOCATE 4
#define PROMPT_EXPAND	5
#define PROMPT_CONNECT 	6
#define PROMPT_CREATE	7
#define PROMPT_SALVAGE	8
#define PROMPT_TRUNCATE	9
#define PROMPT_CLEAR_INODE 10
#define PROMPT_ABORT 	11
#define PROMPT_SPLIT 	12
#define PROMPT_CONTINUE	13
#define PROMPT_CLONE	14
#define PROMPT_DELETE 	15
#define PROMPT_SUPPRESS 16
#define PROMPT_UNLINK	17

/*
 * These are the prompts which are used to ask the user if they want
 * to fix a problem.
 */
static const char *prompt[] = {
	"(no prompt)",		/* 0 */
	"Fix",			/* 1 */
	"Clear",		/* 2 */
	"Relocate",		/* 3 */
	"Allocate",		/* 4 */
	"Expand",		/* 5 */
	"Connect to /lost+found", /* 6 */
	"Create",		/* 7 */	
	"Salvage",		/* 8 */
	"Truncate",		/* 9 */
	"Clear inode",		/* 10 */
	"Abort",		/* 11 */
	"Split",		/* 12 */
	"Continue",		/* 13 */
	"Clone duplicate/bad blocks", /* 14 */
	"Delete file",		/* 15 */
	"Suppress messages",	/* 16 */
	"Unlink",		/* 17 */
};

/*
 * These messages are printed when we are preen mode and we will be
 * automatically fixing the problem.
 */
static const char *preen_msg[] = {
	"(NONE)",		/* 0 */
	"FIXED",		/* 1 */
	"CLEARED",		/* 2 */
	"RELOCATED",		/* 3 */
	"ALLOCATED",		/* 4 */
	"EXPANDED",		/* 5 */
	"RECONNECTED",		/* 6 */
	"CREATED",		/* 7 */
	"SALVAGED",		/* 8 */
	"TRUNCATED",		/* 9 */
	"INODE CLEARED",	/* 10 */
	"ABORTED",		/* 11 */
	"SPLIT",		/* 12 */
	"CONTINUING",		/* 13 */
	"DUPLICATE/BAD BLOCKS CLONED", /* 14 */
	"FILE DELETED",		/* 15 */
	"SUPPRESSED",		/* 16 */
	"UNLINKED",		/* 17 */
};

static const struct e2fsck_problem problem_table[] = {

	/* Pre-Pass 1 errors */

	/* Block bitmap not in group */
	{ PR_0_BB_NOT_GROUP, "@b @B for @g %g is not in @g.  (@b %b)\n",
	  PROMPT_RELOCATE, PR_LATCH_RELOC }, 

	/* Inode bitmap not in group */
	{ PR_0_IB_NOT_GROUP, "@i @B for @g %g is not in @g.  (@b %b)\n",
	  PROMPT_RELOCATE, PR_LATCH_RELOC }, 

	/* Inode table not in group */
	{ PR_0_ITABLE_NOT_GROUP,
	  "@i table for @g %g is not in @g.  (@b %b)\n"
	  "WARNING: SEVERE DATA LOSS POSSIBLE.\n",
	  PROMPT_RELOCATE, PR_LATCH_RELOC },

	/* Superblock corrupt */
	{ PR_0_SB_CORRUPT,
	  "\nThe @S could not be read or does not describe a correct ext2\n"
	  "@f.  If the device is valid and it really contains an ext2\n"
	  "@f (and not swap or ufs or something else), then the @S\n"
  "is corrupt, and you might try running e2fsck with an alternate @S:\n"
	  "    e2fsck -b %S <device>\n\n",
	  PROMPT_NONE, PR_FATAL },

	/* Filesystem size is wrong */
	{ PR_0_FS_SIZE_WRONG,
	  "The @f size (according to the @S) is %b @bs\n"
	  "The physical size of the device is %c @bs\n"
	  "Either the @S or the partition table is likely to be corrupt!\n",
	  PROMPT_ABORT, 0 },

	/* Fragments not supported */		  
	{ PR_0_NO_FRAGMENTS,
	  "@S @b_size = %b, fragsize = %c.\n"
	  "This version of e2fsck does not support fragment sizes different\n"
	  "from the @b size.\n",
	  PROMPT_NONE, PR_FATAL },

	  /* Bad blocks_per_group */
	{ PR_0_BLOCKS_PER_GROUP,
	  "@S @bs_per_group = %b, should have been %c\n",
	  PROMPT_NONE, PR_AFTER_CODE, PR_0_SB_CORRUPT },

	/* Bad first_data_block */
	{ PR_0_FIRST_DATA_BLOCK,
	  "@S first_data_@b = %b, should have been %c\n",
	  PROMPT_NONE, PR_AFTER_CODE, PR_0_SB_CORRUPT },
	
	/* Adding UUID to filesystem */
	{ PR_0_ADD_UUID,
	  "@f did not have a UUID; generating one.\n\n",
	  PROMPT_NONE, 0 },

	/* Relocate hint */
	{ PR_0_RELOCATE_HINT,
	  "Note: if there is several inode or block bitmap blocks\n"
	  "which require relocation, or one part of the inode table\n"
	  "which must be moved, you may wish to try running e2fsck\n"
	  "with the '-b %S' option first.  The problem may lie only\n"
	  "with the primary block group descriptor, and the backup\n"
	  "block group descriptor may be OK.\n\n",
	  PROMPT_NONE, PR_PREEN_OK | PR_NOCOLLATE },

	/* Miscellaneous superblock corruption */
	{ PR_0_MISC_CORRUPT_SUPER,
	  "Corruption found in @S.  (%s = %N).\n",	  
	  PROMPT_NONE, PR_AFTER_CODE, PR_0_SB_CORRUPT },

	/* Error determing physical device size of filesystem */
	{ PR_0_GETSIZE_ERROR,	  
	  "Error determining size of the physical device: %m\n",
	  PROMPT_NONE, PR_FATAL },

	/* Inode count in superblock is incorrect */
	{ PR_0_INODE_COUNT_WRONG,
	  "@i count in @S is %i, should be %j\n",
	  PROMPT_FIX, 0 },
		  
	/* Pass 1 errors */
	
	/* Pass 1: Checking inodes, blocks, and sizes */
	{ PR_1_PASS_HEADER,
	  "Pass 1: Checking @is, @bs, and sizes\n",
	  PROMPT_NONE, 0 },
		  
	/* Root directory is not an inode */
	{ PR_1_ROOT_NO_DIR, "@r is not a @d.  ",
	  PROMPT_CLEAR, 0 }, 

	/* Root directory has dtime set */
	{ PR_1_ROOT_DTIME,
	  "@r has dtime set (probably due to old mke2fs).  ",
	  PROMPT_FIX, PR_PREEN_OK },

	/* Reserved inode has bad mode */
	{ PR_1_RESERVED_BAD_MODE,
	  "Reserved @i %i has bad mode.  ",
	  PROMPT_CLEAR, PR_PREEN_OK },

	/* Deleted inode has zero dtime */
	{ PR_1_ZERO_DTIME,
	  "@D @i %i has zero dtime.  ",
	  PROMPT_FIX, PR_PREEN_OK },

	/* Inode in use, but dtime set */
	{ PR_1_SET_DTIME,
	  "@i %i is in use, but has dtime set.  ",
	  PROMPT_FIX, PR_PREEN_OK },

	/* Zero-length directory */
	{ PR_1_ZERO_LENGTH_DIR,
	  "@i %i is a @z @d.  ",
	  PROMPT_CLEAR, PR_PREEN_OK },

	/* Block bitmap conflicts with some other fs block */
	{ PR_1_BB_CONFLICT,
	  "@g %g's @b @B at %b @C.\n",
	  PROMPT_RELOCATE, 0 },

	/* Inode bitmap conflicts with some other fs block */
	{ PR_1_IB_CONFLICT,
	  "@g %g's @i @B at %b @C.\n",
	  PROMPT_RELOCATE, 0 },

	/* Inode table conflicts with some other fs block */
	{ PR_1_ITABLE_CONFLICT,
	  "@g %g's @i table at %b @C.\n",
	  PROMPT_RELOCATE, 0 },

	/* Block bitmap is on a bad block */
	{ PR_1_BB_BAD_BLOCK,
	  "@g %g's @b @B (%b) is bad.  ",
	  PROMPT_RELOCATE, 0 },

	/* Inode bitmap is on a bad block */
	{ PR_1_IB_BAD_BLOCK,
	  "@g %g's @i @B (%b) is bad.  ",
	  PROMPT_RELOCATE, 0 },

	/* Inode has incorrect i_size */
	{ PR_1_BAD_I_SIZE,
	  "@i %i, i_size is %Is, @s %N.  ",
	  PROMPT_FIX, PR_PREEN_OK },
		  
	/* Inode has incorrect i_blocks */
	{ PR_1_BAD_I_BLOCKS,
	  "@i %i, i_@bs is %Ib, @s %N.  ",
	  PROMPT_FIX, PR_PREEN_OK },

	/* Illegal block number in inode */
	{ PR_1_ILLEGAL_BLOCK_NUM,
	  "@I @b #%B (%b) in @i %i.  ",
	  PROMPT_CLEAR, PR_LATCH_BLOCK },

	/* Block number overlaps fs metadata */
	{ PR_1_BLOCK_OVERLAPS_METADATA,
	  "@b #%B (%b) overlaps @f metadata in @i %i.  ",
	  PROMPT_CLEAR, PR_LATCH_BLOCK },

	/* Inode has illegal blocks (latch question) */
	{ PR_1_INODE_BLOCK_LATCH,
	  "@i %i has illegal @b(s).  ",
	  PROMPT_CLEAR, 0 },

	/* Too many bad blocks in inode */
	{ PR_1_TOO_MANY_BAD_BLOCKS,
	  "Too many illegal @bs in @i %i.\n",
	  PROMPT_CLEAR_INODE, PR_NO_OK }, 	

	/* Illegal block number in bad block inode */
	{ PR_1_BB_ILLEGAL_BLOCK_NUM,
	  "@I @b #%B (%b) in bad @b @i.  ",
	  PROMPT_CLEAR, PR_LATCH_BBLOCK },

	/* Bad block inode has illegal blocks (latch question) */
	{ PR_1_INODE_BBLOCK_LATCH,
	  "Bad @b @i has illegal @b(s).  ",
	  PROMPT_CLEAR, 0 },

	/* Duplicate or bad blocks in use! */
	{ PR_1_DUP_BLOCKS_PREENSTOP,
	  "Duplicate or bad @b in use!\n",
	  PROMPT_NONE, 0 },

	/* Bad block used as bad block indirect block */	  
	{ PR_1_BBINODE_BAD_METABLOCK,
	  "Bad @b %b used as bad @b indirect @b?!?\n",
	  PROMPT_NONE, PR_AFTER_CODE, PR_1_BBINODE_BAD_METABLOCK_PROMPT },

	/* Inconsistency can't be fixed prompt */	  
	{ PR_1_BBINODE_BAD_METABLOCK_PROMPT,
	  "\nThis inconsistency can not be fixed with e2fsck; to fix it, use\n"
	  """dumpe2fs -b"" to dump out the bad @b "
	  "list and ""e2fsck -L filename""\n"
	  "to read it back in again.\n",
	  PROMPT_CONTINUE, PR_PREEN_NOMSG },

	/* Bad primary block */
	{ PR_1_BAD_PRIMARY_BLOCK,  
	  "\nIf the @b is really bad, the @f can not be fixed.\n",
	  PROMPT_NONE, PR_AFTER_CODE, PR_1_BAD_PRIMARY_BLOCK_PROMPT },
		  
	/* Bad primary block prompt */
	{ PR_1_BAD_PRIMARY_BLOCK_PROMPT,	  
	  "You can clear the this @b (and hope for the best) from the\n"
	  "bad @b list and hope that @b is really OK, but there are no\n"
	  "guarantees.\n\n",
	  PROMPT_CLEAR, PR_PREEN_NOMSG },

	/* Bad primary superblock */
	{ PR_1_BAD_PRIMARY_SUPERBLOCK,
	  "The primary @S (%b) is on the bad @b list.\n",
	  PROMPT_NONE, PR_AFTER_CODE, PR_1_BAD_PRIMARY_BLOCK },
		  
	/* Bad primary block group descriptors */
	{ PR_1_BAD_PRIMARY_GROUP_DESCRIPTOR,
	  "Block %b in the primary @g descriptors "
	  "is on the bad @b list\n",
	  PROMPT_NONE, PR_AFTER_CODE, PR_1_BAD_PRIMARY_BLOCK },
		  
	/* Bad superblock in group */
	{ PR_1_BAD_SUPERBLOCK,
	  "Warning: Group %g's @S (%b) is bad.\n",
	  PROMPT_NONE, PR_PREEN_OK | PR_PREEN_NOMSG },
		  
	/* Bad block group descriptors in group */
	{ PR_1_BAD_GROUP_DESCRIPTORS,
	  "Warning: Group %g's copy of the @g descriptors has a bad "
	  "@b (%b).\n",
	  PROMPT_NONE, PR_PREEN_OK | PR_PREEN_NOMSG },

	/* Block claimed for no reason */	  
	{ PR_1_PROGERR_CLAIMED_BLOCK,
	  "Programming error?  @b #%b claimed for no reason in "
	  "process_bad_@b.\n",
	  PROMPT_NONE, PR_PREEN_OK },

	/* Error allocating blocks for relocating metadata */
	{ PR_1_RELOC_BLOCK_ALLOCATE,
	  "@A %N @b(s) for %s: %m\n",
	  PROMPT_NONE, PR_PREEN_OK },
		
	/* Error allocating block buffer during relocation process */
	{ PR_1_RELOC_MEMORY_ALLOCATE,
	  "@A @b buffer for relocating %s\n",
	  PROMPT_NONE, PR_PREEN_OK },
		
	/* Relocating metadata group information from X to Y */	
	{ PR_1_RELOC_FROM_TO,
	  "Relocating @g %g's %s from %b to %c...\n",
	  PROMPT_NONE, PR_PREEN_OK },
		
	/* Relocating metatdata group information to X */
	{ PR_1_RELOC_TO,
	  "Relocating @g %g's %s to %c...\n",
	  PROMPT_NONE, PR_PREEN_OK },
		
	/* Block read error during relocation process */
	{ PR_1_RELOC_READ_ERR,
	  "Warning: could not read @b %b of %s: %m\n",
	  PROMPT_NONE, PR_PREEN_OK },
		
	/* Block write error during relocation process */
	{ PR_1_RELOC_WRITE_ERR,
	  "Warning: could not write @b %b for %s: %m\n",
	  PROMPT_NONE, PR_PREEN_OK },

	/* Error allocating inode bitmap */
	{ PR_1_ALLOCATE_IBITMAP_ERROR,
	  "@A @i @B (%N): %m\n",
	  PROMPT_NONE, PR_FATAL },

	/* Error allocating block bitmap */
	{ PR_1_ALLOCATE_BBITMAP_ERROR,
	  "@A @b @B (%N): %m\n",
	  PROMPT_NONE, PR_FATAL },

	/* Error allocating icount structure */
	{ PR_1_ALLOCATE_ICOUNT,
	  "@A icount link information: %m\n",
	  PROMPT_NONE, PR_FATAL },

	/* Error allocating dbcount */
	{ PR_1_ALLOCATE_DBCOUNT,
	  "@A @d @b array: %m\n",
	  PROMPT_NONE, PR_FATAL },

	/* Error while scanning inodes */
	{ PR_1_ISCAN_ERROR,
	  "Error while scanning @is (%i): %m\n",
	  PROMPT_NONE, PR_FATAL },

	/* Error while iterating over blocks */
	{ PR_1_BLOCK_ITERATE,
	  "Error while iterating over blocks in @i %i: %m\n",
	  PROMPT_NONE, PR_FATAL },

	/* Error while storing inode count information */	  
	{ PR_1_ICOUNT_STORE,
	  "Error storing @i count information (inode=%i, count=%N): %m\n",
	  PROMPT_NONE, PR_FATAL },

	/* Error while storing directory block information */	  
	{ PR_1_ADD_DBLOCK,
	  "Error storing @d @b information "
	  "(inode=%i, block=%b, num=%N): %m\n",
	  PROMPT_NONE, PR_FATAL },

	/* Error while reading inode (for clearing) */	  
	{ PR_1_READ_INODE,
	  "Error reading @i %i: %m\n",
	  PROMPT_NONE, PR_FATAL },

	/* Suppress messages prompt */
	{ PR_1_SUPPRESS_MESSAGES, "", PROMPT_SUPPRESS, PR_NO_OK },
		  
	/* Filesystem contains large files, but has no such flag in sb */
	{ PR_1_FEATURE_LARGE_FILES,
	  "@f contains large files, but lacks LARGE_FILE flag in @S.\n",
	  PROMPT_FIX, 0 },
	  
	/* Imagic flag set on an inode when filesystem doesn't support it */
	{ PR_1_SET_IMAGIC,
	  "@i %i has imagic flag set.  ",
	  PROMPT_CLEAR, 0 },

	/* Immutable flag set on a device or socket inode */
	{ PR_1_SET_IMMUTABLE,
	  "Special (device/socket/fifo) @i %i has immutable flag set.  ",
	  PROMPT_CLEAR, PR_PREEN_OK | PR_PREEN_NO | PR_NO_OK },

	/* Pass 1b errors */

	/* Pass 1B: Rescan for duplicate/bad blocks */
	{ PR_1B_PASS_HEADER,
	  "Duplicate @bs found... invoking duplicate @b passes.\n"
	  "Pass 1B: Rescan for duplicate/bad @bs\n",
	  PROMPT_NONE, 0 },

	/* Duplicate/bad block(s) header */
	{ PR_1B_DUP_BLOCK_HEADER,	  
	  "Duplicate/bad @b(s) in @i %i:",
	  PROMPT_NONE, 0 },

	/* Duplicate/bad block(s) in inode */
	{ PR_1B_DUP_BLOCK,	  
	  " %b",
	  PROMPT_NONE, PR_LATCH_DBLOCK },

	/* Duplicate/bad block(s) end */
	{ PR_1B_DUP_BLOCK_END,
	  "\n",
	  PROMPT_NONE, 0 },
		  
	/* Error while scanning inodes */
	{ PR_1B_ISCAN_ERROR,
	  "Error while scanning inodes (%i): %m\n",
	  PROMPT_NONE, PR_FATAL },

	/* Error allocating inode bitmap */
	{ PR_1B_ALLOCATE_IBITMAP_ERROR,
	  "@A @i @B (inode_dup_map): %m\n",
	  PROMPT_NONE, PR_FATAL },


	/* Pass 1C: Scan directories for inodes with dup blocks. */
	{ PR_1C_PASS_HEADER,
	  "Pass 1C: Scan directories for @is with dup @bs.\n",
	  PROMPT_NONE, 0 },

		  
	/* Pass 1D: Reconciling duplicate blocks */
	{ PR_1D_PASS_HEADER,
	  "Pass 1D: Reconciling duplicate @bs\n",
	  PROMPT_NONE, 0 },
		  
	/* File has duplicate blocks */
	{ PR_1D_DUP_FILE,
	  "File %Q (@i #%i, mod time %IM) \n"
	  "  has %B duplicate @b(s), shared with %N file(s):\n",
	  PROMPT_NONE, 0 },
		  
	/* List of files sharing duplicate blocks */	
	{ PR_1D_DUP_FILE_LIST,
	  "\t%Q (@i #%i, mod time %IM)\n",
	  PROMPT_NONE, 0 },
	  
	/* File sharing blocks with filesystem metadata  */	
	{ PR_1D_SHARE_METADATA,
	  "\t<@f metadata>\n",
	  PROMPT_NONE, 0 },

	/* Report of how many duplicate/bad inodes */	
	{ PR_1D_NUM_DUP_INODES,
	  "(There are %N @is containing duplicate/bad @bs.)\n\n",
	  PROMPT_NONE, 0 },

	/* Duplicated blocks already reassigned or cloned. */
	{ PR_1D_DUP_BLOCKS_DEALT,
	  "Duplicated @bs already reassigned or cloned.\n\n",
	  PROMPT_NONE, 0 },

	/* Clone duplicate/bad blocks? */
	{ PR_1D_CLONE_QUESTION,
	  "", PROMPT_CLONE, PR_NO_OK },
		  
	/* Delete file? */
	{ PR_1D_DELETE_QUESTION,
	  "", PROMPT_DELETE, 0 },

	/* Couldn't clone file (error) */
	{ PR_1D_CLONE_ERROR,
	  "Couldn't clone file: %m\n", PROMPT_NONE, 0 },

	/* Pass 2 errors */

	/* Pass 2: Checking directory structure */
	{ PR_2_PASS_HEADER,
	  "Pass 2: Checking @d structure\n",
	  PROMPT_NONE, 0 },
		  
	/* Bad inode number for '.' */
	{ PR_2_BAD_INODE_DOT,
	  "Bad @i number for '.' in @d @i %i.\n",
	  PROMPT_FIX, 0 },

	/* Directory entry has bad inode number */
	{ PR_2_BAD_INO, 
	  "@E has bad @i #: %Di.\n",
	  PROMPT_CLEAR, 0 },

	/* Directory entry has deleted or unused inode */
	{ PR_2_UNUSED_INODE, 
	  "@E has @D/unused @i %Di.  ",
	  PROMPT_CLEAR, PR_PREEN_OK },

	/* Directry entry is link to '.' */
	{ PR_2_LINK_DOT, 
	  "@E @L to '.'  ",
	  PROMPT_CLEAR, 0 },

	/* Directory entry points to inode now located in a bad block */
	{ PR_2_BB_INODE,
	  "@E points to @i (%Di) located in a bad @b.\n",
	  PROMPT_CLEAR, 0 },

	/* Directory entry contains a link to a directory */
	{ PR_2_LINK_DIR, 
	  "@E @L to @d %P (%Di).\n",
	  PROMPT_CLEAR, 0 },

	/* Directory entry contains a link to the root directry */
	{ PR_2_LINK_ROOT, 
	  "@E @L to the @r.\n",
	  PROMPT_CLEAR, 0 },

	/* Directory entry has illegal characters in its name */
	{ PR_2_BAD_NAME, 
	  "@E has illegal characters in its name.\n",
	  PROMPT_FIX, 0 },

	/* Missing '.' in directory inode */	  
	{ PR_2_MISSING_DOT,
	  "Missing '.' in @d @i %i.\n",
	  PROMPT_FIX, 0 },

	/* Missing '..' in directory inode */	  
	{ PR_2_MISSING_DOT_DOT,
	  "Missing '..' in @d @i %i.\n",
	  PROMPT_FIX, 0 },

	/* First entry in directory inode doesn't contain '.' */
	{ PR_2_1ST_NOT_DOT,
	  "First @e '%Dn' (inode=%Di) in @d @i %i (%p) @s '.'\n",
	  PROMPT_FIX, 0 },

	/* Second entry in directory inode doesn't contain '..' */
	{ PR_2_2ND_NOT_DOT_DOT,
	  "Second @e '%Dn' (inode=%Di) in @d @i %i @s '..'\n",
	  PROMPT_FIX, 0 },
		  
	/* i_faddr should be zero */
	{ PR_2_FADDR_ZERO,
	  "i_faddr @F %IF, @s zero.\n",
	  PROMPT_CLEAR, 0 },

  	/* i_file_acl should be zero */
	{ PR_2_FILE_ACL_ZERO,
	  "i_file_acl @F %If, @s zero.\n",
	  PROMPT_CLEAR, 0 },

  	/* i_dir_acl should be zero */
	{ PR_2_DIR_ACL_ZERO,
	  "i_dir_acl @F %Id, @s zero.\n",
	  PROMPT_CLEAR, 0 },

  	/* i_frag should be zero */
	{ PR_2_FRAG_ZERO,
	  "i_frag @F %N, @s zero.\n",
	  PROMPT_CLEAR, 0 },

  	/* i_fsize should be zero */
	{ PR_2_FSIZE_ZERO,
	  "i_fsize @F %N, @s zero.\n",
	  PROMPT_CLEAR, 0 },

	/* inode has bad mode */
	{ PR_2_BAD_MODE,
	  "@i %i (%Q) has a bad mode (%Im).\n",
	  PROMPT_CLEAR, 0 },

	/* directory corrupted */
	{ PR_2_DIR_CORRUPTED,	  
	  "@d @i %i, @b %B, offset %N: @d corrupted\n",
	  PROMPT_SALVAGE, 0 },
		  
	/* filename too long */
	{ PR_2_FILENAME_LONG,	  
	  "@d @i %i, @b %B, offset %N: filename too long\n",
	  PROMPT_TRUNCATE, 0 },

	/* Directory inode has a missing block (hole) */
	{ PR_2_DIRECTORY_HOLE,	  
	  "@d @i %i has an unallocated @b #%B.  ",
	  PROMPT_ALLOCATE, 0 },

	/* '.' is not NULL terminated */
	{ PR_2_DOT_NULL_TERM,
	  "'.' @d @e in @d @i %i is not NULL terminated\n",
	  PROMPT_FIX, 0 },

	/* '..' is not NULL terminated */
	{ PR_2_DOT_DOT_NULL_TERM,
	  "'..' @d @e in @d @i %i is not NULL terminated\n",
	  PROMPT_FIX, 0 },

	/* Illegal character device inode */
	{ PR_2_BAD_CHAR_DEV,
	  "@i %i (%Q) is an @I character device.\n",
	  PROMPT_CLEAR, 0 },

	/* Illegal block device inode */
	{ PR_2_BAD_BLOCK_DEV,
	  "@i %i (%Q) is an @I @b device.\n",
	  PROMPT_CLEAR, 0 },

	/* Duplicate '.' entry */
	{ PR_2_DUP_DOT,
	  "@E is duplicate '.' @e.\n",
	  PROMPT_FIX, 0 },	  

	/* Duplicate '..' entry */
	{ PR_2_DUP_DOT_DOT,
	  "@E is duplicate '..' @e.\n",
	  PROMPT_FIX, 0 },

	/* Internal error: couldn't find dir_info */
	{ PR_2_NO_DIRINFO,
	  "Internal error: couldn't find dir_info for %i.\n",
	  PROMPT_NONE, PR_FATAL },

	/* Final rec_len is wrong */
	{ PR_2_FINAL_RECLEN,
	  "@E has rec_len of %dr, should be %N.\n",
	  PROMPT_FIX, 0 },
		  
	/* Error allocating icount structure */
	{ PR_2_ALLOCATE_ICOUNT,
	  "@A icount structure: %m\n",
	  PROMPT_NONE, PR_FATAL },

	/* Error iterating over directory blocks */
	{ PR_2_DBLIST_ITERATE,
	  "Error interating over @d @bs: %m\n",
	  PROMPT_NONE, PR_FATAL },

	/* Error reading directory block */
	{ PR_2_READ_DIRBLOCK,
	  "Error reading @d @b %b (@i %i): %m\n",
	  PROMPT_CONTINUE, 0 },

	/* Error writing directory block */
	{ PR_2_WRITE_DIRBLOCK,
	  "Error writing @d @b %b (@i %i): %m\n",
	  PROMPT_CONTINUE, 0 },

	/* Error allocating new directory block */
	{ PR_2_ALLOC_DIRBOCK,
	  "@A new @d @b for @i %i (%s): %m\n",
	  PROMPT_NONE, 0 },

	/* Error deallocating inode */
	{ PR_2_DEALLOC_INODE,
	  "Error deallocating @i %i: %m\n",
	  PROMPT_NONE, PR_FATAL },

	/* Directory entry for '.' is big.  Split? */
	{ PR_2_SPLIT_DOT,
	  "@d @e for '.' is big.  ",
	  PROMPT_SPLIT, PR_NO_OK },

	/* Illegal FIFO inode */
	{ PR_2_BAD_FIFO,
	  "@i %i (%Q) is an @I FIFO.\n",
	  PROMPT_CLEAR, 0 },

	/* Illegal socket inode */
	{ PR_2_BAD_SOCKET,
	  "@i %i (%Q) is an @I socket.\n",
	  PROMPT_CLEAR, 0 },

	/* Directory filetype not set */
	{ PR_2_SET_FILETYPE,
	  "Setting filetype for @E to %N.\n",
	  PROMPT_NONE, PR_PREEN_OK | PR_NO_OK | PR_NO_NOMSG },

	/* Directory filetype incorrect */
	{ PR_2_BAD_FILETYPE,
	  "@E has an incorrect filetype (was %dt, should be %N)\n",
	  PROMPT_FIX, 0 },

	/* Directory filetype set on filesystem */
	{ PR_2_CLEAR_FILETYPE,
	  "@E has filetype set\n",
	  PROMPT_CLEAR, PR_PREEN_OK },

	/* Directory filename is null */
	{ PR_2_NULL_NAME,
	  "@E has a zero-length name\n",
	  PROMPT_CLEAR, 0 },

	/* Pass 3 errors */

	/* Pass 3: Checking directory connectivity */
	{ PR_3_PASS_HEADER,
	  "Pass 3: Checking @d connectivity\n",
	  PROMPT_NONE, 0 },
		  
	/* Root inode not allocated */
	{ PR_3_NO_ROOT_INODE,
	  "@r not allocated.  ",
	  PROMPT_ALLOCATE, 0 },	
		  
	/* No room in lost+found */
	{ PR_3_EXPAND_LF_DIR,
	  "No room in @l @d.  ",
	  PROMPT_EXPAND, 0 },

	/* Unconnected directory inode */
	{ PR_3_UNCONNECTED_DIR,
	  "Unconnected @d @i %i (%p)\n",
	  PROMPT_CONNECT, 0 },

	/* /lost+found not found */
	{ PR_3_NO_LF_DIR,
	  "/@l not found.  ",
	  PROMPT_CREATE, PR_PREEN_OK },

	/* .. entry is incorrect */
	{ PR_3_BAD_DOT_DOT,
	  "'..' in %Q (%i) is %P (%j), @s %q (%d).\n",
	  PROMPT_FIX, 0 },

	/* Bad or non-existent /lost+found.  Cannot reconnect */
	{ PR_3_NO_LPF,
	  "Bad or non-existent /@l.  Cannot reconnect\n",
	  PROMPT_NONE, 0 },

	/* Could not expand /lost+found */
	{ PR_3_CANT_EXPAND_LPF,
	  "Could not expand /@l: %m\n",
	  PROMPT_NONE, 0 },

	/* Could not reconnect inode */
	{ PR_3_CANT_RECONNECT,
	  "Could not reconnect %i: %m\n",
	  PROMPT_NONE, 0 },

	/* Error while trying to find /lost+found */
	{ PR_3_ERR_FIND_LPF,
	  "Error while trying to find /@l: %m\n",
	  PROMPT_NONE, 0 },

	/* Error in ext2fs_new_block while creating /lost+found */
	{ PR_3_ERR_LPF_NEW_BLOCK, 
	  "ext2fs_new_@b: %m while trying to create /@l @d\n",
	  PROMPT_NONE, 0 },
		  
	/* Error in ext2fs_new_inode while creating /lost+found */
	{ PR_3_ERR_LPF_NEW_INODE,
	  "ext2fs_new_@i: %m while trying to create /@l @d\n",
	  PROMPT_NONE, 0 },

	/* Error in ext2fs_new_dir_block while creating /lost+found */	  
	{ PR_3_ERR_LPF_NEW_DIR_BLOCK,
	  "ext2fs_new_dir_@b: %m while creating new @d @b\n",
	  PROMPT_NONE, 0 },
		  
	/* Error while writing directory block for /lost+found */
	{ PR_3_ERR_LPF_WRITE_BLOCK,
	  "ext2fs_write_dir_@b: %m while writing the @d @b for /@l\n",
	  PROMPT_NONE, 0 },

	/* Error while adjusting inode count */
	{ PR_3_ADJUST_INODE,
	  "Error while adjusting @i count on @i %i\n",
	  PROMPT_NONE, 0 },

	/* Couldn't fix parent directory -- error */
	{ PR_3_FIX_PARENT_ERR,
	  "Couldn't fix parent of @i %i: %m\n\n",
	  PROMPT_NONE, 0 },

	/* Couldn't fix parent directory -- couldn't find it */	  
	{ PR_3_FIX_PARENT_NOFIND,
	  "Couldn't fix parent of @i %i: Couldn't find parent @d entry\n\n",
	  PROMPT_NONE, 0 },

	/* Error allocating inode bitmap */
	{ PR_3_ALLOCATE_IBITMAP_ERROR,
	  "@A @i @B (%N): %m\n",
	  PROMPT_NONE, PR_FATAL },

	/* Error creating root directory */
	{ PR_3_CREATE_ROOT_ERROR,
	  "Error creating root @d (%s): %m\n",
	  PROMPT_NONE, PR_FATAL },	  

	/* Error creating lost and found directory */
	{ PR_3_CREATE_LPF_ERROR,
	  "Error creating /@l @d (%s): %m\n",
	  PROMPT_NONE, PR_FATAL },  

	/* Root inode is not directory; aborting */
	{ PR_3_ROOT_NOT_DIR_ABORT,
	  "@r is not a @d; aborting.\n",
	  PROMPT_NONE, PR_FATAL },  

	/* Cannot proceed without a root inode. */
	{ PR_3_NO_ROOT_INODE_ABORT,
	  "Cannot proceed without a @r.\n",
	  PROMPT_NONE, PR_FATAL },  

	/* Internal error: couldn't find dir_info */
	{ PR_3_NO_DIRINFO,
	  "Internal error: couldn't find dir_info for %i.\n",
	  PROMPT_NONE, PR_FATAL },

	/* Lost+found not a directory */
	{ PR_3_LPF_NOTDIR,
	  "/@l is not a @d (ino=%i)\n",
	  PROMPT_UNLINK, 0 }, 

	/* Pass 4 errors */
	
	/* Pass 4: Checking reference counts */
	{ PR_4_PASS_HEADER,
	  "Pass 4: Checking reference counts\n",
	  PROMPT_NONE, 0 },
		  
	/* Unattached zero-length inode */
	{ PR_4_ZERO_LEN_INODE,
	  "@u @z @i %i.  ",
	  PROMPT_CLEAR, PR_PREEN_OK|PR_NO_OK },

	/* Unattached inode */
	{ PR_4_UNATTACHED_INODE,
	  "@u @i %i\n",
	  PROMPT_CONNECT, 0 },

	/* Inode ref count wrong */
	{ PR_4_BAD_REF_COUNT,
	  "@i %i ref count is %Il, @s %N.  ",
	  PROMPT_FIX, PR_PREEN_OK },

	{ PR_4_INCONSISTENT_COUNT,
	  "WARNING: PROGRAMMING BUG IN E2FSCK!\n"
	  "\tOR SOME BONEHEAD (YOU) IS CHECKING A MOUNTED (LIVE) FILESYSTEM.\n"
	  "@i_link_info[%i] is %N, @i.i_links_count is %Il.  "
	  "They should be the same!\n",
	  PROMPT_NONE, 0 },

	/* Pass 5 errors */
		  
	/* Pass 5: Checking group summary information */
	{ PR_5_PASS_HEADER,
	  "Pass 5: Checking @g summary information\n",
	  PROMPT_NONE, 0 },
		  
	/* Padding at end of inode bitmap is not set. */
	{ PR_5_INODE_BMAP_PADDING,
	  "Padding at end of @i @B is not set. ",
	  PROMPT_FIX, PR_PREEN_OK },
		  
	/* Padding at end of block bitmap is not set. */
	{ PR_5_BLOCK_BMAP_PADDING,
	  "Padding at end of @b @B is not set. ",
	  PROMPT_FIX, PR_PREEN_OK },
		
	/* Block bitmap differences header */
	{ PR_5_BLOCK_BITMAP_HEADER,
	  "@b @B differences: ",
	  PROMPT_NONE, PR_PREEN_OK | PR_PREEN_NOMSG},

	/* Block not used, but marked in bitmap */
	{ PR_5_UNUSED_BLOCK,
	  " -%b",
	  PROMPT_NONE, PR_LATCH_BBITMAP | PR_PREEN_OK | PR_PREEN_NOMSG },
		  
	/* Block used, but not marked used in bitmap */
	{ PR_5_BLOCK_USED,
	  " +%b",
	  PROMPT_NONE, PR_LATCH_BBITMAP | PR_PREEN_OK | PR_PREEN_NOMSG },

	/* Block bitmap differences end */	  
	{ PR_5_BLOCK_BITMAP_END,
	  "\n",
	  PROMPT_FIX, PR_PREEN_OK | PR_PREEN_NOMSG },

	/* Inode bitmap differences header */
	{ PR_5_INODE_BITMAP_HEADER,
	  "@i @B differences: ",
	  PROMPT_NONE, PR_PREEN_OK | PR_PREEN_NOMSG },

	/* Inode not used, but marked in bitmap */
	{ PR_5_UNUSED_INODE,
	  " -%i",
	  PROMPT_NONE, PR_LATCH_IBITMAP | PR_PREEN_OK | PR_PREEN_NOMSG },
		  
	/* Inode used, but not marked used in bitmap */
	{ PR_5_INODE_USED,
	  " +%i",
	  PROMPT_NONE, PR_LATCH_IBITMAP | PR_PREEN_OK | PR_PREEN_NOMSG },

	/* Inode bitmap differences end */	  
	{ PR_5_INODE_BITMAP_END,
	  "\n",
	  PROMPT_FIX, PR_PREEN_OK | PR_PREEN_NOMSG },

	/* Free inodes count for group wrong */
	{ PR_5_FREE_INODE_COUNT_GROUP,
	  "Free @is count wrong for @g #%g (%i, counted=%j).\n",
	  PROMPT_FIX, PR_PREEN_OK | PR_PREEN_NOMSG },

	/* Directories count for group wrong */
	{ PR_5_FREE_DIR_COUNT_GROUP,
	  "Directories count wrong for @g #%g (%i, counted=%j).\n",
	  PROMPT_FIX, PR_PREEN_OK | PR_PREEN_NOMSG },

	/* Free inodes count wrong */
	{ PR_5_FREE_INODE_COUNT,
	  "Free @is count wrong (%i, counted=%j).\n",
	  PROMPT_FIX, PR_PREEN_OK | PR_PREEN_NOMSG },

	/* Free blocks count for group wrong */
	{ PR_5_FREE_BLOCK_COUNT_GROUP,
	  "Free @bs count wrong for @g #%g (%b, counted=%c).\n",
	  PROMPT_FIX, PR_PREEN_OK | PR_PREEN_NOMSG },

	/* Free blocks count wrong */
	{ PR_5_FREE_BLOCK_COUNT,
	  "Free @bs count wrong (%b, counted=%c).\n",
	  PROMPT_FIX, PR_PREEN_OK | PR_PREEN_NOMSG },

	/* Programming error: bitmap endpoints don't match */
	{ PR_5_BMAP_ENDPOINTS,
	  "PROGRAMMING ERROR: @f (#%N) @B endpoints (%b, %c) don't "
	  "match calculated @B endpoints (%i, %j)\n",
	  PROMPT_NONE, PR_FATAL },

	/* Internal error: fudging end of bitmap */
	{ PR_5_FUDGE_BITMAP_ERROR,
	  "Internal error: fudging end of bitmap (%N)\n",
	  PROMPT_NONE, PR_FATAL },	  
	  
	{ 0 }
};

/*
 * This is the latch flags register.  It allows several problems to be
 * "latched" together.  This means that the user has to answer but one
 * question for the set of problems, and all of the associated
 * problems will be either fixed or not fixed.
 */
static struct latch_descr pr_latch_info[] = {
	{ PR_LATCH_BLOCK, PR_1_INODE_BLOCK_LATCH, 0 },
	{ PR_LATCH_BBLOCK, PR_1_INODE_BBLOCK_LATCH, 0 },
	{ PR_LATCH_IBITMAP, PR_5_INODE_BITMAP_HEADER, PR_5_INODE_BITMAP_END },
	{ PR_LATCH_BBITMAP, PR_5_BLOCK_BITMAP_HEADER, PR_5_BLOCK_BITMAP_END },
	{ PR_LATCH_RELOC, PR_0_RELOCATE_HINT, 0 },
	{ PR_LATCH_DBLOCK, PR_1B_DUP_BLOCK_HEADER, PR_1B_DUP_BLOCK_END },
	{ -1, 0, 0 },
};

static const struct e2fsck_problem *find_problem(int code)
{
	int 	i;

	for (i=0; problem_table[i].e2p_code; i++) {
		if (problem_table[i].e2p_code == code)
			return &problem_table[i];
	}
	return 0;
}

static struct latch_descr *find_latch(int code)
{
	int	i;

	for (i=0; pr_latch_info[i].latch_code >= 0; i++) {
		if (pr_latch_info[i].latch_code == code)
			return &pr_latch_info[i];
	}
	return 0;
}

int end_problem_latch(e2fsck_t ctx, int mask)
{
	struct latch_descr *ldesc;
	struct problem_context pctx;
	int answer = -1;
	
	ldesc = find_latch(mask);
	if (ldesc->end_message && (ldesc->flags & PRL_LATCHED)) {
		clear_problem_context(&pctx);
		answer = fix_problem(ctx, ldesc->end_message, &pctx);
	}
	ldesc->flags &= ~(PRL_VARIABLE);
	return answer;
}

int set_latch_flags(int mask, int setflags, int clearflags)
{
	struct latch_descr *ldesc;

	ldesc = find_latch(mask);
	if (!ldesc)
		return -1;
	ldesc->flags |= setflags;
	ldesc->flags &= ~clearflags;
	return 0;
}

int get_latch_flags(int mask, int *value)
{
	struct latch_descr *ldesc;

	ldesc = find_latch(mask);
	if (!ldesc)
		return -1;
	*value = ldesc->flags;
	return 0;
}

void clear_problem_context(struct problem_context *ctx)
{
	memset(ctx, 0, sizeof(struct problem_context));
	ctx->blkcount = -1;
	ctx->group = -1;
}

int fix_problem(e2fsck_t ctx, problem_t code, struct problem_context *pctx)
{
	ext2_filsys fs = ctx->fs;
	const struct e2fsck_problem *ptr;
	struct latch_descr *ldesc = 0;
	const char *message;
	int 		def_yn, answer, ans;
	int		print_answer = 0;
	int		suppress = 0;

	ptr = find_problem(code);
	if (!ptr) {
		printf("Unhandled error code (%d)!\n", code);
		return 0;
	}
	def_yn = 1;
	if ((ptr->flags & PR_NO_DEFAULT) ||
	    ((ptr->flags & PR_PREEN_NO) && (ctx->options & E2F_OPT_PREEN)) ||
	    (ctx->options & E2F_OPT_NO))
		def_yn= 0;

	/*
	 * Do special latch processing.  This is where we ask the
	 * latch question, if it exists
	 */
	if (ptr->flags & PR_LATCH_MASK) {
		ldesc = find_latch(ptr->flags & PR_LATCH_MASK);
		if (ldesc->question && !(ldesc->flags & PRL_LATCHED)) {
			ans = fix_problem(ctx, ldesc->question, pctx);
			if (ans == 1)
				ldesc->flags |= PRL_YES;
			if (ans == 0)
				ldesc->flags |= PRL_NO;
			ldesc->flags |= PRL_LATCHED;
		}
		if (ldesc->flags & PRL_SUPPRESS)
			suppress++;
	}
	if ((ptr->flags & PR_PREEN_NOMSG) &&
	    (ctx->options & E2F_OPT_PREEN))
		suppress++;
	if ((ptr->flags & PR_NO_NOMSG) &&
	    (ctx->options & E2F_OPT_NO))
		suppress++;
	if (!suppress) {
		message = ptr->e2p_description;
		if (ctx->options & E2F_OPT_PREEN) {
			printf("%s: ", ctx->device_name);
#if 0
			if (ptr->e2p_preen_msg)
				message = ptr->e2p_preen_msg;
#endif
		}
		print_e2fsck_message(ctx, message, pctx, 1);
	}
	if (!(ptr->flags & PR_PREEN_OK) && (ptr->prompt != PROMPT_NONE))
		preenhalt(ctx);

	if (ptr->flags & PR_FATAL)
		fatal_error(ctx, 0);

	if (ptr->prompt == PROMPT_NONE) {
		if (ptr->flags & PR_NOCOLLATE)
			answer = -1;
		else
			answer = def_yn;
	} else {
		if (ctx->options & E2F_OPT_PREEN) {
			answer = def_yn;
			if (!(ptr->flags & PR_PREEN_NOMSG))
				print_answer = 1;
		} else if ((ptr->flags & PR_LATCH_MASK) &&
			   (ldesc->flags & (PRL_YES | PRL_NO))) {
			if (!suppress)
				print_answer = 1;
			if (ldesc->flags & PRL_YES)
				answer = 1;
			else
				answer = 0;
		} else
			answer = ask(ctx, prompt[(int) ptr->prompt], def_yn);
		if (!answer && !(ptr->flags & PR_NO_OK))
			ext2fs_unmark_valid(fs);
	
		if (print_answer)
			printf("%s.\n", answer ?
			       preen_msg[(int) ptr->prompt] : "IGNORED");
	
	}

	if (ptr->flags & PR_AFTER_CODE)
		(void) fix_problem(ctx, ptr->second_code, pctx);

	if ((ptr->prompt == PROMPT_ABORT) && answer)
		fatal_error(ctx, 0);

	return answer;
}
