/*
 * get_device_by_label.h
 *
 * Copyright 1999 by Andries Brouwer
 * Copyright 1999, 2000 by Theodore Ts'o
 *
 * This file may be redistributed under the terms of the GNU Public
 * License.
 *
 * Taken from aeb's mount, 990619
 * Updated from aeb's mount, 20000725
 * Added call to ext2fs_find_block_device, so that we can find devices
 * 	even if devfs (ugh) is compiled in, but not mounted, since
 * 	this messes up /proc/partitions, by TYT.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifdef HAVE_SYS_MKDEV_H
#include <sys/mkdev.h>
#endif
#ifdef HAVE_SYS_SYSMACROS_H
#include <sys/sysmacros.h>
#endif
#include "nls-enable.h"
#include "get_device_by_label.h"
#include "fsck.h"

/* function prototype from libext2 */
extern char *ext2fs_find_block_device(dev_t device);

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

#define XFS_SUPER_MAGIC "XFSB"
struct xfs_super_block {
	unsigned char	s_magic[4];
	unsigned char	s_dummy[28];
	unsigned char	s_uuid[16];
	unsigned char	s_dummy2[60];
	unsigned char	s_fname[12];
};

static struct uuidCache_s {
	struct uuidCache_s *next;
	char uuid[16];
	char *label;
	char *device;
} *uuidCache = NULL;

char *string_copy(const char *s)
{
	char	*ret;

	ret = malloc(strlen(s)+1);
	if (ret)
		strcpy(ret, s);
	return ret;
}

/* for now, only ext2 and xfs are supported */
static int
get_label_uuid(const char *device, char **label, char *uuid) {

	/* start with ext2 and xfs tests, taken from mount_guess_fstype */
	/* should merge these later */
	int fd;
	size_t label_size;
	char *sb_uuid = 0, *sb_label = 0;
	struct ext2_super_block e2sb;
	struct xfs_super_block xfsb;

	fd = open(device, O_RDONLY);
	if (fd < 0)
		return 1;

	if (lseek(fd, 1024, SEEK_SET) == 1024
	    && read(fd, (char *) &e2sb, sizeof(e2sb)) == sizeof(e2sb)
	    && (ext2magic(e2sb) == EXT2_SUPER_MAGIC)) {
		sb_uuid = e2sb.s_uuid;
		sb_label = e2sb.s_volume_name;
		label_size = sizeof(e2sb.s_volume_name);
	} else if (lseek(fd, 0, SEEK_SET) == 0
	    && read(fd, (char *) &xfsb, sizeof(xfsb)) == sizeof(xfsb)
	    && strncmp((char *) &xfsb.s_magic, XFS_SUPER_MAGIC, 4) == 0) {
		sb_uuid = xfsb.s_uuid;
		sb_label = xfsb.s_fname;
		label_size = sizeof(xfsb.s_fname);
	} else {
		close(fd);
		return 1;
	}

	close(fd);
	if (sb_uuid)
		memcpy(uuid, sb_uuid, sizeof(e2sb.s_uuid));
	if (sb_label) {
		if ((*label = calloc(label_size + 1, 1)) != NULL)
			memcpy(*label, sb_label, label_size);
	}
	return 0;
}

static void
uuidcache_addentry(char *device, char *label, char *uuid) {
	struct uuidCache_s *last;

	if (!uuidCache) {
		last = uuidCache = malloc(sizeof(*uuidCache));
	} else {
		for (last = uuidCache; last->next; last = last->next) ;
		last->next = malloc(sizeof(*uuidCache));
		last = last->next;
	}
	last->next = NULL;
	last->device = device;
	last->label = label;
	memcpy(last->uuid, uuid, sizeof(last->uuid));
}

static void
uuidcache_init(void) {
	char line[100];
	char *s;
	int ma, mi, sz;
	static char ptname[100];
	FILE *procpt;
	char uuid[16], *label, *devname;
	char device[110];
	dev_t	dev;
	struct stat statbuf;
	int firstPass;
	int handleOnFirst;

	if (uuidCache)
		return;

	procpt = fopen(PROC_PARTITIONS, "r");
	if (!procpt)
		return;

	for (firstPass = 1; firstPass >= 0; firstPass--) {
	    fseek(procpt, 0, SEEK_SET);

	    while (fgets(line, sizeof(line), procpt)) {
		if (sscanf (line, " %d %d %d %[^\n ]",
			    &ma, &mi, &sz, ptname) != 4)
			continue;

		/* skip extended partitions (heuristic: size 1) */
		if (sz == 1)
			continue;

		/* look only at md devices on first pass */
		handleOnFirst = !strncmp(ptname, "md", 2);
		if (firstPass != handleOnFirst)
			continue;

		/* skip entire disk (minor 0, 64, ... on ide;
		   0, 16, ... on sd) */
		/* heuristic: partition name ends in a digit */

		for(s = ptname; *s; s++);
		if (isdigit(s[-1])) {
			/*
			 * We first look in /dev for the device, but
			 * if we don't find it, or if the stat
			 * information doesn't check out, we use
			 * ext2fs_find_block_device to find it.
			 */
			sprintf(device, "%s/%s", DEVLABELDIR, ptname);
			dev = makedev(ma, mi);
			if ((stat(device, &statbuf) < 0) ||
			    (statbuf.st_rdev != dev)) {
				devname = ext2fs_find_block_device(dev);
			} else
				devname = string_copy(device);
			if (!devname)
				continue;
			if (!get_label_uuid(devname, &label, uuid))
				uuidcache_addentry(devname, label, uuid);
			else
				free(devname);
		}
	    }
	}

	fclose(procpt);
}

#define UUID   1
#define VOL    2

static char *
get_spec_by_x(int n, const char *t) {
	struct uuidCache_s *uc;

	uuidcache_init();
	uc = uuidCache;

	if (t == NULL)
		return NULL;

	while(uc) {
		switch (n) {
		case UUID:
			if (!memcmp(t, uc->uuid, sizeof(uc->uuid)))
				return string_copy(uc->device);
			break;
		case VOL:
			if (!strcmp(t, uc->label))
				return string_copy(uc->device);
			break;
		}
		uc = uc->next;
	}
	return NULL;
}

static char fromhex(char c)
{
	if (isdigit(c))
		return (c - '0');
	else if (islower(c))
		return (c - 'a' + 10);
	else
		return (c - 'A' + 10);
}

char *
get_spec_by_uuid(const char *s)
{
	char uuid[16];
	int i;

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
	fprintf(stderr, _("WARNING: %s: bad UUID"), s);
	return NULL;
}

char *
get_spec_by_volume_label(const char *s) {
	return get_spec_by_x(VOL, s);
}

const char *
get_volume_label_by_spec(const char *spec) {
        struct uuidCache_s *uc;

        uuidcache_init();
        uc = uuidCache;

	while(uc) {
		if (!strcmp(spec, uc->device))
			return uc->label;
		uc = uc->next;
	}
	return NULL;
}

/*
 * Interpret the device name if necessary.
 * Frees the pointer passed to it if we return a different device string.
 */
char *interpret_spec(char *spec)
{
	char *dev = NULL;

	if (!spec)
		return NULL;

	if (!strncmp(spec, "UUID=", 5))
		dev = get_spec_by_uuid(spec+5);
	else if (!strncmp(spec, "LABEL=", 6))
		dev = get_spec_by_volume_label(spec+6);
	else
		dev = string_copy(spec);
	return dev;
}
