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
#include <linux/ext2_fs.h>

#include "e2p.h"

void print_flags (FILE * f, unsigned long flags)
{
	if (flags & EXT2_SYNC_FL)
		fprintf (f, "S");
	else
		fprintf (f, "-");
	if (flags & EXT2_COMPR_FL)
		fprintf (f, "c");
	else
		fprintf (f, "-");
	if (flags & EXT2_SECRM_FL)
		fprintf (f, "s");
	else
		fprintf (f, "-");
	if (flags & EXT2_UNRM_FL)
		fprintf (f, "u");
	else
		fprintf (f, "-");
}
