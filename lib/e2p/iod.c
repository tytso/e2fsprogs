/*
 * iod.c		- Iterate a function on each entry of a directory
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

#include <dirent.h>

#include "e2p.h"

int iterate_on_dir (const char * dir_name,
		    int (*func) (const char *, struct dirent *, void *),
		    void * private)
{
	DIR * dir;
	struct dirent de;
	struct dirent *dep;

	dir = opendir (dir_name);
	if (dir == NULL)
		return -1;
	while ((dep = readdir (dir)))
	{
		de.d_ino = dep->d_ino;
		de.d_off = dep->d_off;
		de.d_reclen = dep->d_reclen;
		strcpy (de.d_name, dep->d_name);
		(*func) (dir_name, &de, private);
	}
	closedir (dir);
	return 0;
}
