/*
 * pf.c			- Print file attributes on an ext2 file system
 *
 * Copyright (C) 1993, 1994  Remy Card <card@masi.ibp.fr>
 *                           Laboratoire MASI, Institut Blaise Pascal
 *                           Universite Pierre et Marie Curie (Paris VI)
 *
 * This file can be redistributed under the terms of the GNU Library General
 * Public License
 */

/*
 * History:
 * 93/10/30	- Creation
 */

#include <stdio.h>

#include "e2p.h"

struct flags_name {
	unsigned long	flag;
	char 		*short_name;
	char 		*long_name;
};

static struct flags_name flags_array[] = {
	{ EXT2_SECRM_FL, "s", "Secure_Deletion" },
	{ EXT2_UNRM_FL, "u" , "Undelete" },
	{ EXT2_SYNC_FL, "S", "Synchronous_Updates" },
	{ EXT2_IMMUTABLE_FL, "i", "Immutable" },
	{ EXT2_APPEND_FL, "a", "Append_Only" },
	{ EXT2_NODUMP_FL, "d", "No_Dump" },
	{ EXT2_NOATIME_FL, "A", "No_Atime" },
	{ EXT2_COMPR_FL, "c", "Compression_Requested" },
	{ EXT2_COMPRBLK_FL, "B", "Compressed_File" },
	{ EXT2_DIRTY_FL, "D", "Compressed_Dirty_File" },
	{ EXT2_NOCOMPR_FL, "X", "Compression_Raw_Access" },
	{ EXT2_ECOMPR_FL, "E", "Compression_Error" },
	{ EXT3_JOURNAL_DATA_FL, "j", "Journaled_Data" },
	{ 0, NULL, NULL }
};

void print_flags (FILE * f, unsigned long flags, unsigned options)
{
	int long_opt = (options & PFOPT_LONG);
	struct flags_name *fp;
	int	first = 1;

	for (fp = flags_array; fp->flag != 0; fp++) {
		if (flags & fp->flag) {
			if (long_opt) {
				if (first)
					first = 0;
				else
					fputs(", ", f);
				fputs(fp->long_name, f);
			} else
				fputs(fp->short_name, f);
		} else {
			if (!long_opt)
				fputs("-", f);
		}
	}
	if (long_opt && first)
		fputs("---", f);
}

