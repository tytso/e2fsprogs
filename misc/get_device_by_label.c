/*
 * get_device_by_label.h
 *
 * Copyright 1999 by Andries Brouwer and Theodore Ts'o
 *
 * This file may be redistributed under the terms of the GNU Public
 * License.
 *
 * Taken from aeb's mount, 990619
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include "get_device_by_label.h"

#define PROC_PARTITIONS "/proc/partitions"
#define DEVLABELDIR	"/dev"

#define EXT2_SUPER_MAGIC    0xEF53
struct ext2_super_block {
        unsigned char   s_dummy1[56];
        unsigned char   s_magic[2];
        unsigned char   s_dummy2[46];
        unsigned char   s_uuid[16];
        unsigned char   s_volume_name[16];
};
#define ext2magic(s)    ((unsigned int) s.s_magic[0] + (((unsigned int) s.s_magic[1]) << 8))


static FILE *procpt;

static void
procptclose(void) {
    if (procpt)
        fclose (procpt);
    procpt = 0;
}

static int
procptopen(void) {
    return ((procpt = fopen(PROC_PARTITIONS, "r")) != NULL);
}

static char *
procptnext(void) {
   char line[100];
   char *s;
   int ma, mi, sz;
   static char ptname[100];

   while (fgets(line, sizeof(line), procpt)) {
      if (sscanf (line, " %d %d %d %[^\n]\n", &ma, &mi, &sz, ptname) != 4)
	      continue;

      /* skip extended partitions (heuristic: size 1) */
      if (sz == 1)
	      continue;

      /* skip entire disk (minor 0, 64, ... on ide; 0, 16, ... on sd) */
      /* heuristic: partition name ends in a digit */
      for(s = ptname; *s; s++);
      if (isdigit(s[-1]))
	      return ptname;
   }
   return 0;
}

#define UUID	1
#define VOL	2

/* for now, only ext2 is supported */
static int
has_right_label(const char *device, int n, const void *label) {

	/* start with a test for ext2, taken from mount_guess_fstype */
	int fd;
	struct ext2_super_block e2sb;

	fd = open(device, O_RDONLY);
	if (fd < 0)
		return 0;

	if (lseek(fd, 1024, SEEK_SET) != 1024
	    || read(fd, (char *) &e2sb, sizeof(e2sb)) != sizeof(e2sb)
	    || (ext2magic(e2sb) != EXT2_SUPER_MAGIC)) {
		close(fd);
		return 0;
	}

	close(fd);

	/* superblock is ext2 - now what is its label? */
	if (n == UUID)
		return (memcmp(e2sb.s_uuid, label, 16) == 0);
	else
		return (strncmp(e2sb.s_volume_name,
				(const char *) label, 16) == 0);
}

static char *
get_spec_by_x(int n, const void *t) {
	char *pt;
	char device[110];

	if(!procptopen())
		return NULL;
	while((pt = procptnext()) != NULL) {
		/* Note: this is a heuristic only - there is no reason
		   why these devices should live in /dev.
		   Perhaps this directory should be specifiable by option.
		   One might for example have /devlabel with links to /dev
		   for the devices that may be accessed in this way.
		   (This is useful, if the cdrom on /dev/hdc must not
		   be accessed.)
		*/
		sprintf(device, "%s/%s", DEVLABELDIR, pt);
		if (has_right_label(device, n, t)) {
			procptclose();
			return strdup(device);
		}
	}
	procptclose();
	return NULL;
}

static unsigned char
fromhex(char c) {
	if (isdigit(c))
		return (c - '0');
	else if (islower(c))
		return (c - 'a' + 10);
	else
		return (c - 'A' + 10);
}

char *
get_spec_by_uuid(const char *s0) {
	unsigned char uuid[16];
	int i;
	const char *s = s0;

	if (strlen(s) != 36 ||
	    s[8] != '-' || s[13] != '-' || s[18] != '-' || s[23] != '-')
		goto bad_uuid;
	for (i=0; i<16; i++) {
	    if (*s == '-') s++;
	    if (!isxdigit(s[0]) || !isxdigit(s[1]))
		    goto bad_uuid;
	    uuid[i] = ((fromhex(s[0])<<4) | fromhex(s[1]));
	    s += 2;
	}
	return get_spec_by_x(UUID, uuid);

 bad_uuid:
	fprintf(stderr, "WARNING: %s: bad UUID", s0);
	return NULL;
}

char *
get_spec_by_volume_label(const char *s) {
	return get_spec_by_x(VOL, s);
}
