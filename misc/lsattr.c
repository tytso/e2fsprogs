/*
 * lsattr.c		- List file attributes on an ext2 file system
 *
 * Copyright (C) 1993, 1994  Remy Card <card@masi.ibp.fr>
 *                           Laboratoire MASI, Institut Blaise Pascal
 *                           Universite Pierre et Marie Curie (Paris VI)
 *
 * This file can be redistributed under the terms of the GNU General
 * Public License
 */

/*
 * History:
 * 93/10/30	- Creation
 * 93/11/13	- Replace stat() calls by lstat() to avoid loops
 * 94/02/27	- Integrated in Ted's distribution
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <linux/ext2_fs.h>

#include "et/com_err.h"
#include "e2p/e2p.h"

#include "../version.h"

const char * program_name = "lsattr";

int all = 0;
int d_opt = 0;
int recursive = 0;
int v_opt = 0;

static void volatile usage (void)
{
	fprintf (stderr, "Usage: %s [-Radv] [files...]\n", program_name);
	exit (1);
}

static void list_attributes (const char * name)
{
	unsigned long flags;
	unsigned long version;

	if (fgetflags (name, &flags) == -1)
		com_err (program_name, errno, "While reading flags on %s",
			 name);
	else if (fgetversion (name, &version) == -1)
		com_err (program_name, errno, "While reading version on %s",
			 name);
	else
	{
		if (v_opt)
			printf ("%5lu ", version);
		print_flags (stdout, flags);
		printf (" %s\n", name);
	}
}

static int lsattr_dir_proc (const char *, struct dirent *, void *);

static void lsattr_args (const char * name)
{
	struct stat st;

	if (lstat (name, &st) == -1)
		com_err (program_name, errno, "while stating %s", name);
	else
	{
		if (S_ISDIR(st.st_mode) && !d_opt)
			iterate_on_dir (name, lsattr_dir_proc, (void *) NULL);
		else
			list_attributes (name);
	}
}

static int lsattr_dir_proc (const char * dir_name, struct dirent * de, void * private)
{
	char path [MAXPATHLEN];
	struct stat st;

	sprintf (path, "%s/%s", dir_name, de->d_name);
	if (lstat (path, &st) == -1)
		perror (path);
	else
	{
		if (de->d_name[0] != '.' || all)
		{
			list_attributes (path);
			if (S_ISDIR(st.st_mode) && recursive &&
			    strcmp (de->d_name, ".") && strcmp (de->d_name, ".."))
			{
				printf ("\n%s:\n", path);
				iterate_on_dir (path, lsattr_dir_proc, (void *) NULL);
				printf ("\n");
			}
		}
	}
	return 0;
}

void main (int argc, char ** argv)
{
	char c;
	int i;

	fprintf (stderr, "lsattr %s, %s for EXT2 FS %s, %s\n",
		 E2FSPROGS_VERSION, E2FSPROGS_DATE,
		 EXT2FS_VERSION, EXT2FS_DATE);
	if (argc && *argv)
		program_name = *argv;
	while ((c = getopt (argc, argv, "Radlv")) != EOF)
		switch (c)
		{
			case 'R':
				recursive = 1;
				break;
			case 'a':
				all = 1;
				break;
			case 'd':
				d_opt = 1;
				break;
			case 'v':
				v_opt = 1;
				break;
			default:
				usage ();
		}

	if (optind > argc - 1)
		lsattr_args (".");
	else
		for (i = optind; i < argc; i++)
			lsattr_args (argv[i]);
}
