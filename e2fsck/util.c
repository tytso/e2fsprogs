/*
 * util.c --- miscellaneous utilities
 * 
 * Copyright (C) 1993, 1994 Theodore Ts'o.  This file may be
 * redistributed under the terms of the GNU Public License.
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <termios.h>
#include <sys/resource.h>

#include "e2fsck.h"

const char * fix_msg[2] = { "IGNORED", "FIXED" };
const char * clear_msg[2] = { "IGNORED", "CLEARED" };

void fatal_error (const char *msg)
{
	if (msg) 
		fprintf (stderr, "%s: %s\n", program_name, msg);
	exit(FSCK_ERROR);
}

void *allocate_memory(int size, const char *description)
{
	void *ret;
	char buf[256];

#ifdef DEBUG_ALLOCATE_MEMORY
	printf("Allocating %d bytes for %s...\n", size, description);
#endif
	ret = malloc(size);
	if (!ret) {
		sprintf(buf, "%%s: Can't allocate %s\n", description);
		fatal_error(buf);
	}
	memset(ret, 0, size);
	return ret;
}


int ask_yn(const char * string, int def)
{
	int		c;
	struct termios	termios, tmp;
	const char	*defstr;

	tcgetattr (0, &termios);
	tmp = termios;
	tmp.c_lflag &= ~(ICANON | ECHO);
	tcsetattr (0, TCSANOW, &tmp);

	if (def == 1)
		defstr = "<y>";
	else if (def == 0)
		defstr = "<n>";
	else
		defstr = " (y/n)";
	printf("%s%s? ", string, defstr);
	while (1) {
		fflush (stdout);
		if ((c = getchar()) == EOF)
			break;
		c = toupper(c);
		if (c == 'Y') {
			def = 1;
			break;
		}
		else if (c == 'N') {
			def = 0;
			break;
		}
		else if ((c == ' ' || c == '\n') && (def != -1))
			break;
	}
	if (def)
		printf ("yes\n\n");
	else
		printf ("no\n\n");
	tcsetattr (0, TCSANOW, &termios);
	return def;
}

int ask (const char * string, int def)
{
	if (nflag) {
		printf ("%s? no\n\n", string);
		return 0;
	}
	if (yflag) {
		printf ("%s? yes\n\n", string);
		return 1;
	}
	if (preen) {
		printf ("%s? %s\n\n", string, def ? "yes" : "no");
		return def;
	}
	return ask_yn(string, def);
}

void read_bitmaps(ext2_filsys fs)
{
	errcode_t	retval;

	if (!fs->inode_map) {
		ehandler_operation("reading inode bitmaps");
		retval = ext2fs_read_inode_bitmap(fs);
		ehandler_operation(0);
		if (retval) {
			com_err(program_name, retval,
				"while retrying to read inode bitmaps for %s",
				device_name);
			fatal_error(0);
		}
	}
	
	if (!fs->block_map) {
		ehandler_operation("reading block bitmaps");
		retval = ext2fs_read_block_bitmap(fs);
		ehandler_operation(0);
		if (retval) {
			com_err(program_name, retval,
				"while retrying to read block bitmaps for %s",
				device_name);
			fatal_error(0);
		}
	}
}

void write_bitmaps(ext2_filsys fs)
{
	errcode_t	retval;

	if (ext2fs_test_bb_dirty(fs)) {
		ehandler_operation("writing block bitmaps");
		retval = ext2fs_write_block_bitmap(fs);
		ehandler_operation(0);
		if (retval) {
			com_err(program_name, retval,
				"while retrying to write block bitmaps for %s",
				device_name);
			fatal_error(0);
		}
	}

	if (ext2fs_test_ib_dirty(fs)) {
		ehandler_operation("writing inode bitmaps");
		retval = ext2fs_write_inode_bitmap(fs);
		ehandler_operation(0);
		if (retval) {
			com_err(program_name, retval,
				"while retrying to write inode bitmaps for %s",
				device_name);
			fatal_error(0);
		}
	}
}

void preenhalt(NOARGS)
{
	if (!preen)
		return;
	fprintf(stderr, "\n\n%s: UNEXPECTED INCONSISTENCY; RUN fsck MANUALLY.\n",
	       device_name);
	exit(FSCK_UNCORRECTED);
}

void init_resource_track(struct resource_track *track)
{
	struct rusage r;
	
	track->brk_start = sbrk(0);
	gettimeofday(&track->time_start, 0);
	getrusage(RUSAGE_SELF, &r);
	track->user_start = r.ru_utime;
	track->system_start = r.ru_stime;
}

static __inline__ float timeval_subtract(struct timeval *tv1,
					 struct timeval *tv2)
{
	return ((tv1->tv_sec - tv2->tv_sec) +
		((float) (tv1->tv_usec - tv2->tv_usec)) / 1000000);
}

void print_resource_track(struct resource_track *track)
{
	struct rusage r;
	struct timeval time_end;

	gettimeofday(&time_end, 0);
	getrusage(RUSAGE_SELF, &r);

	printf("Memory used: %d, elapsed time: %6.3f/%6.3f/%6.3f\n",
	       (int) (((char *) sbrk(0)) - ((char *) track->brk_start)),
	       timeval_subtract(&time_end, &track->time_start),
	       timeval_subtract(&r.ru_utime, &track->user_start),
	       timeval_subtract(&r.ru_stime, &track->system_start));
}

/*
 * This function returns 1 if the inode's block entries actually
 * contain block entries.
 */
int inode_has_valid_blocks(struct ext2_inode *inode)
{
	/*
	 * Only directories, regular files, and some symbolic links
	 * have valid block entries.
	 */
	if (!S_ISDIR(inode->i_mode) && !S_ISREG(inode->i_mode) &&
	    !S_ISLNK(inode->i_mode))
		return 0;
	
	/*
	 * If the symbolic link is a "fast symlink", then the symlink
	 * target is stored in the block entries.
	 */
	if (S_ISLNK (inode->i_mode) && inode->i_blocks == 0 &&
	    inode->i_size < EXT2_N_BLOCKS * sizeof (unsigned long))
		return 0;

	return 1;
}

#ifdef MTRACE
void mtrace_print(char *mesg)
{
	FILE	*malloc_get_mallstream();
	FILE	*f = malloc_get_mallstream();

	if (f)
		fprintf(f, "============= %s\n", mesg);
}
#endif


