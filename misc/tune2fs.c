/*
 * tune2fs.c		- Change the file system parameters on
 *			  an unmounted second extended file system
 *
 * Copyright (C) 1992, 1993, 1994  Remy Card <card@masi.ibp.fr>
 *                                 Laboratoire MASI, Institut Blaise Pascal
 *                                 Universite Pierre et Marie Curie (Paris VI)
 *
 * This file can be redistributed under the terms of the GNU General
 * Public License
 */

/*
 * History:
 * 93/06/01	- Creation
 * 93/10/31	- Added the -c option to change the maximal mount counts
 * 93/12/14	- Added -l flag to list contents of superblock
 *                M.J.E. Mol (marcel@duteca.et.tudelft.nl)
 *                F.W. ten Wolde (franky@duteca.et.tudelft.nl)
 * 93/12/29	- Added the -e option to change errors behavior
 * 94/02/27	- Ported to use the ext2fs library
 * 94/03/06	- Added the checks interval from Uwe Ohse (uwe@tirka.gun.de)
 */

#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <linux/ext2_fs.h>

#include "ext2fs/ext2fs.h"
#include "et/com_err.h"
#include "e2p/e2p.h"

#include "../version.h"

const char * program_name = "tune2fs";
char * device_name = NULL;
int c_flag = 0;
int e_flag = 0;
int i_flag = 0;
int l_flag = 0;
int m_flag = 0;
int max_mount_count;
unsigned long interval;
unsigned long reserved_ratio = 0;
unsigned short errors;

static volatile void usage (void)
{
	fprintf (stderr, "Usage: %s [-c max-mounts-count] [-e errors-behavior] "
		 "[-i interval[d|m]]\n"
		 "\t[-l] [-m reserved-blocks-percent] device\n", program_name);
	exit (1);
}

void main (int argc, char ** argv)
{
	char c;
	char * tmp;
	errcode_t retval;
	ext2_filsys fs;

	fprintf (stderr, "tune2fs %s, %s for EXT2 FS %s, %s\n",
		 E2FSPROGS_VERSION, E2FSPROGS_DATE,
		 EXT2FS_VERSION, EXT2FS_DATE);
	if (argc && *argv)
		program_name = *argv;
	initialize_ext2_error_table();
	while ((c = getopt (argc, argv, "c:e:i:lm:")) != EOF)
		switch (c)
		{
			case 'c':
				max_mount_count = strtoul (optarg, &tmp, 0);
				if (*tmp || max_mount_count > 16000)
				{
					com_err (program_name, 0,
						 "bad mounts count - %s",
						 optarg);
					usage ();
				}
				c_flag = 1;
				break;
			case 'e':
				if (strcmp (optarg, "continue") == 0)
					errors = EXT2_ERRORS_CONTINUE;
				else if (strcmp (optarg, "remount-ro") == 0)
					errors = EXT2_ERRORS_RO;
				else if (strcmp (optarg, "panic") == 0)
					errors = EXT2_ERRORS_PANIC;
				else
				{
					com_err (program_name, 0,
						 "bad error behavior - %s",
						 optarg);
					usage ();
				}
				e_flag = 1;
				break;
			case 'i':
				interval = strtoul (optarg, &tmp, 0);
				switch (*tmp)
				{
					case '\0':
					case 'd':
					case 'D': /* days */
						interval *= 86400;
						if (*tmp != '\0')
							tmp++;
						break;
					case 'm':
					case 'M': /* months! */
						interval *= 86400 * 30;
						tmp++;
						break;
				}
				if (*tmp || interval > (365 * 86400))
				{
					com_err (program_name, 0,
						 "bad interval - %s", optarg);
					usage ();
				}
				i_flag = 1;
				break;
			case 'l':
				l_flag = 1;
				break;
			case 'm':
				reserved_ratio = strtoul (optarg, &tmp, 0);
				if (*tmp || reserved_ratio > 50)
				{
					com_err (program_name, 0,
						 "bad reserved block ratio - %s",
						 optarg);
					usage ();
				}
				m_flag = 1;
				break;
			default:
				usage ();
		}
	if (optind < argc - 1 || optind == argc)
		usage ();
	if (!c_flag && !e_flag && !i_flag && !m_flag && !l_flag)
		usage ();
	device_name = argv[optind];
	retval = ext2fs_open (device_name,
			      (c_flag || e_flag || i_flag || m_flag) ? EXT2_FLAG_RW : 0,
			      0, 0, unix_io_manager, &fs);
        if (retval)
	{
		com_err (program_name, retval, "while trying to open %s",
			 device_name);
		printf("Couldn't find valid filesystem superblock.\n");
		exit(1);
	}

	if (c_flag)
	{
		fs->super->s_max_mnt_count = max_mount_count;
		ext2fs_mark_super_dirty(fs);
		printf ("Setting maximal mount count to %d\n", max_mount_count);
	}
	if (e_flag)
	{
		fs->super->s_errors = errors;
		ext2fs_mark_super_dirty(fs);
		printf ("Setting error behavior to %d\n", errors);
	}
	if (i_flag)
	{
		fs->super->s_checkinterval = interval;
		ext2fs_mark_super_dirty(fs);
		printf ("Setting interval between check %lu seconds\n", interval);
	}
	if (m_flag)
	{
		fs->super->s_r_blocks_count = (fs->super->s_blocks_count / 100)
			* reserved_ratio;
		ext2fs_mark_super_dirty(fs);
		printf ("Setting reserved blocks percentage to %lu (%lu blocks)\n",
			reserved_ratio, fs->super->s_r_blocks_count);
	}
	if (l_flag)
		list_super (fs->super);
	ext2fs_close (fs);
	exit (0);
}
