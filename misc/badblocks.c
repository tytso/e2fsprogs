/*
 * badblocks.c		- Bad blocks checker
 *
 * Copyright (C) 1992, 1993, 1994  Remy Card <card@masi.ibp.fr>
 *                                 Laboratoire MASI, Institut Blaise Pascal
 *                                 Universite Pierre et Marie Curie (Paris VI)
 *
 * This file is based on the minix file system programs fsck and mkfs
 * written and copyrighted by Linus Torvalds <Linus.Torvalds@cs.helsinki.fi>
 *
 * This file can be redistributed under the terms of the GNU General
 * Public License
 */

/*
 * History:
 * 93/05/26	- Creation from e2fsck
 * 94/02/27	- Made a separate bad blocks checker
 */

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/ioctl.h>

#include <linux/fd.h>
#include <linux/fs.h>

#include "et/com_err.h"

const char * program_name = "badblocks";

int v_flag = 0;			/* verbose */
int w_flag = 0;			/* do r/w test */
int s_flag = 0;			/* show progress of test */

static volatile void usage (void)
{
	fprintf (stderr, "Usage: %s [-b block_size] [-o output_file] [-w] device blocks_count\n",
		 program_name);
	exit (1);
}

/*
 * Perform a test of a block; return the number of blocks readable/writeable.
 */
static long do_test (int dev, char * buffer, int try, unsigned long block_size,
		     unsigned long current_block)
{
	long got;

	/* Seek to the correct loc. */
	if (lseek (dev, current_block * block_size, SEEK_SET) !=
	    current_block * block_size)
                 com_err (program_name, errno, "during seek");

	/* Try the read */
	got = read (dev, buffer, try * block_size);
	if (got < 0)
		got = 0;	
	if (got & (block_size - 1))
		fprintf (stderr, "Weird values in do_test: probably bugs\n");
	got /= block_size;
	return got;
}

static unsigned long currently_testing = 0;
static unsigned long num_blocks = 0;

static void alarm_intr (int alnum)
{
	signal (SIGALRM, alarm_intr);
	alarm(1);
	if (!num_blocks)
		return;
	fprintf(stderr, "%6ld/%6ld", currently_testing, num_blocks);
	fprintf(stderr, "\b\b\b\b\b\b\b\b\b\b\b\b\b");
	fflush (stderr);
}

static void test_ro (int dev, unsigned long blocks_count,
		     unsigned long block_size, FILE * out)
{
#define TEST_BUFFER_BLOCKS 16
	char * blkbuf;
	int try;
	long got;

	blkbuf = malloc (TEST_BUFFER_BLOCKS * block_size);
	if (!blkbuf)
	{
		com_err (program_name, ENOMEM, "while allocating buffers");
		exit (1);
	}

	if (v_flag)
		fprintf (stderr, "Flushing buffers\n");
	ioctl (dev, BLKFLSBUF, 0);	/* In case this is a HD */
	ioctl (dev, FDFLUSH, 0);	/* In case this is floppy */
	if (v_flag) 
		fprintf (stderr,
			 "Checking for bad blocks in read-only mode\n");
	try = TEST_BUFFER_BLOCKS;
	currently_testing = 0;
	num_blocks = blocks_count;
	if (s_flag) {
		fprintf(stderr, "Checking for bad blocks (read-only test): ");
		alarm_intr(SIGALRM);
	}
	while (currently_testing < blocks_count)
	{
		if (currently_testing + try > blocks_count)
			try = blocks_count - currently_testing;
		got = do_test (dev, blkbuf, try, block_size, currently_testing);
		currently_testing += got;
		if (got == try) {
			try = TEST_BUFFER_BLOCKS;
			continue;
		}
		else
			try = 1;
		if (got == 0)
			fprintf (out, "%lu\n", currently_testing++);
	}
	num_blocks = 0;
	alarm(0);
	if (s_flag)
		fprintf(stderr, "done         \n");
	free (blkbuf);
}

static void test_rw (int dev, unsigned long blocks_count,
		     unsigned long block_size, FILE * out)
{
	int i;
	int j;
	char * buffer;
	unsigned char pattern[] = {0xaa, 0x55, 0xff, 0x00};

	buffer = malloc (2 * block_size);
	if (!buffer)
	{
		com_err (program_name, ENOMEM, "while allocating buffers");
		exit (1);
	}

	if (v_flag)
		fprintf (stderr, "Flushing buffers\n");
	ioctl (dev, BLKFLSBUF, 0);	/* In case this is a HD */
	ioctl (dev, FDFLUSH, 0);	/* In case this is floppy */
	if (v_flag)
		fprintf (stderr, "Checking for bad blocks in read-write mode\n");
	for (i = 0; i < sizeof (pattern); i++)
	{
		memset (buffer, pattern[i], block_size);
		if (v_flag)
			fprintf (stderr, "Writing pattern 0x%08x\n",
				 *((int *) buffer));
		for (j = 0; j < blocks_count; j++)
		{
			if (lseek (dev, j * block_size, SEEK_SET) != j * block_size)
				com_err (program_name, errno,
					 "during seek on block %d", j);
			write (dev, buffer, block_size);
		}
		if (v_flag)
			fprintf (stderr, "Flushing buffers\n");
		if (fsync (dev) == -1)
			com_err (program_name, errno, "during fsync");
		ioctl (dev, BLKFLSBUF, 0);	/* In case this is a HD */
		ioctl (dev, FDFLUSH, 0);	/* In case this is floppy */
		if (v_flag)
			fprintf (stderr, "Reading and comparing\n");
		for (j = 0; j < blocks_count; j++)
		{
			if (lseek (dev, j * block_size, SEEK_SET) != j * block_size)
				com_err (program_name, errno,
					 "during seek on block %d", j);
			if (read (dev, buffer + block_size, block_size) < block_size)
				fprintf (out, "%d\n", j);
			else if (memcmp (buffer, buffer + block_size, block_size))
				fprintf (out, "%d\n", j);
				
		}
		if (v_flag)
			fprintf (stderr, "Flushing buffers\n");
		ioctl (dev, BLKFLSBUF, 0);	/* In case this is a HD */
		ioctl (dev, FDFLUSH, 0);	/* In case this is floppy */
	}
}

void main (int argc, char ** argv)
{
	char c;
	char * tmp;
	char * device_name;
	char * output_file = NULL;
	FILE * out;
	unsigned long block_size = 1024;
	unsigned long blocks_count;
	int dev;

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);
	if (argc && *argv)
		program_name = *argv;
	while ((c = getopt (argc, argv, "b:o:svw")) != EOF) {
		switch (c) {
		case 'b':
			block_size = strtoul (optarg, &tmp, 0);
			if (*tmp || block_size > 4096) {
				com_err (program_name, 0,
					 "bad block size - %s", optarg);
				exit (1);
			}
			break;
		case 'o':
			output_file = optarg;
			break;
		case 's':
			s_flag = 1;
			break;
		case 'v':
			v_flag = 1;
			break;
		case 'w':
			w_flag = 1;
			break;
		default:
			usage ();
		}
	}
	if (optind > argc - 1)
		usage ();
	device_name = argv[optind++];
	if (optind > argc - 1)
		usage ();
	blocks_count = strtoul (argv[optind], &tmp, 0);
	if (*tmp)
	{
		com_err (program_name, 0, "bad blocks count - %s", argv[optind]);
		exit (1);
	}
	dev = open (device_name, w_flag ? O_RDWR : O_RDONLY);
	if (dev == -1)
	{
		com_err (program_name, errno,"while trying to open %s",
			 device_name);
		exit (1);
	}
	if (output_file && strcmp (output_file, "-") != 0)
	{
		out = fopen (output_file, "w");
		if (out == NULL)
		{
			com_err (program_name, errno,"while trying to open %s",
				 device_name);
			exit (1);
		}
	}
	else
		out = stdout;
	if (w_flag)
		test_rw (dev, blocks_count, block_size, out);
	else
		test_ro (dev, blocks_count, block_size, out);
	close (dev);
	if (out != stdout)
		fclose (out);
}
