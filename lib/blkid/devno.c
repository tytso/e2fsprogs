/*
 * devno.c - find a particular device by its device number (major/minor)
 *
 * Copyright (C) 2000, 2001 Theodore Ts'o
 * Copyright (C) 2001 Andreas Dilger
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 * %End-Header%
 */

#include <stdio.h>
#include <string.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <stdlib.h>
#include <string.h>
#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#if HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#include <dirent.h>
#if HAVE_ERRNO_H
#include <errno.h>
#endif
#if HAVE_SYS_MKDEV_H
#include <sys/mkdev.h>
#endif

#include "blkidP.h"

#ifdef DEBUG_DEVNO
#define DBG(x)	x
#else
#define DBG(x)
#endif

struct dir_list {
	char	*name;
	struct dir_list *next;
};

char *stringn_copy(const char *s, const int length)
{
	char *ret;

	if (!s)
		return NULL;

	ret = malloc(length + 1);
	if (ret) {
		strncpy(ret, s, length);
		ret[length] = '\0';
	}
	return ret;
}

char *string_copy(const char *s)
{
	if (!s)
		return NULL;

	return stringn_copy(s, strlen(s));
}

void string_free(char *s)
{
	if (s)
		free(s);
}

/*
 * This function adds an entry to the directory list
 */
static void add_to_dirlist(const char *name, struct dir_list **list)
{
	struct dir_list *dp;

	dp = malloc(sizeof(struct dir_list));
	if (!dp)
		return;
	dp->name = string_copy(name);
	if (!dp->name) {
		free(dp);
		return;
	}
	dp->next = *list;
	*list = dp;
}

/*
 * This function frees a directory list
 */
static void free_dirlist(struct dir_list **list)
{
	struct dir_list *dp, *next;

	for (dp = *list; dp; dp = next) {
		next = dp->next;
		string_free(dp->name);
		free(dp);
	}
	*list = NULL;
}

static int scan_dir(char *dirname, dev_t devno, struct dir_list **list,
		    char **devname)
{
	DIR	*dir;
	struct dirent *dp;
	char	path[1024];
	int	dirlen;
	struct stat st;
	int	ret = 0;

	dirlen = strlen(dirname);
	if ((dir = opendir(dirname)) == NULL)
		return errno;
	dp = readdir(dir);
	while (dp) {
		if (dirlen + strlen(dp->d_name) + 2 >= sizeof(path))
			goto skip_to_next;

		if (dp->d_name[0] == '.' &&
		    ((dp->d_name[1] == 0) ||
		     ((dp->d_name[1] == '.') && (dp->d_name[2] == 0))))
			goto skip_to_next;

		sprintf(path, "%s/%s", dirname, dp->d_name);
		if (stat(path, &st) < 0)
			goto skip_to_next;

		if (S_ISDIR(st.st_mode))
			add_to_dirlist(path, list);
		else if (S_ISBLK(st.st_mode) && st.st_rdev == devno) {
			*devname = string_copy(path);
			DBG(printf("found 0x%Lx at %s (%p)\n", devno,
				   *devname, *devname));
			if (!*devname)
				ret = -BLKID_ERR_MEM;
			break;
		}
	skip_to_next:
		dp = readdir(dir);
	}
	closedir(dir);
	return ret;
}

/* Directories where we will try to search for device numbers */
const char *devdirs[] = { "/dev", "/devfs", "/devices", NULL };

/*
 * This function finds the pathname to a block device with a given
 * device number.  It returns a pointer to allocated memory to the
 * pathname on success, and NULL on failure.
 */
char *blkid_devno_to_devname(dev_t devno)
{
	struct dir_list *list = NULL, *new_list = NULL;
	char *devname = NULL;
	const char **dir;

	/*
	 * Add the starting directories to search in reverse order of
	 * importance, since we are using a stack...
	 */
	for (dir = devdirs; *dir; dir++)
		/* go to end of list */;

	do {
		--dir;
		add_to_dirlist(*dir, &list);
	} while (dir != devdirs);

	while (list) {
		struct dir_list *current = list;

		list = list->next;
		DBG(printf("directory %s\n", current->name));
		scan_dir(current->name, devno, &new_list, &devname);
		string_free(current->name);
		free(current);
		if (devname)
			break;
		/*
		 * If we're done checking at this level, descend to
		 * the next level of subdirectories. (breadth-first)
		 */
		if (list == NULL) {
			list = new_list;
			new_list = NULL;
		}
	}
	free_dirlist(&list);
	free_dirlist(&new_list);

	if (!devname)
		fprintf(stderr, "blkid: couldn't find devno 0x%04lx\n", 
			(unsigned long) devno);
	else
		DBG(printf("found devno 0x%04Lx as %s\n", devno, devname));

	return devname;
}

blkid_dev blkid_find_devno(blkid_cache cache, dev_t devno)
{
	blkid_dev dev = NULL;
	struct list_head *p, *n;

	if (!cache)
		return NULL;

	/* This cannot be a standard list_for_each() because we may be
	 * deleting the referenced struct in blkid_verify_devname() and
	 * pointing to another one that was probed from disk, and "p"
	 * would point to freed memory.
	 */
	list_for_each_safe(p, n, &cache->bic_devs) {
		blkid_dev tmp = list_entry(p, struct blkid_struct_dev, bid_devs);
		if (tmp->bid_devno != devno)
			continue;

		tmp = blkid_verify_devname(cache, tmp);
		if (!tmp || tmp->bid_devno != devno)
			continue;

		dev = tmp;
		break;
	}

	if (dev)
		DBG(printf("found devno 0x%04LX in cache as %s\n",
			   devno, dev->bid_name));

	return dev;
}

blkid_dev blkid_get_devno(blkid_cache cache, dev_t devno)
{
	char *devname;
	blkid_dev dev;

	if (!(dev = blkid_find_devno(cache, devno)) &&
	    (devname = blkid_devno_to_devname(devno))) {
		dev = blkid_get_devname(cache, devname);
		string_free(devname);
	}

	return dev;
}

#ifdef TEST_PROGRAM
int main(int argc, char** argv)
{
	char	*devname, *tmp;
	int	major, minor;
	dev_t	devno;
	const char *errmsg = "Couldn't parse %s: %s\n";

	if ((argc != 2) && (argc != 3)) {
		fprintf(stderr, "Usage:\t%s device_number\n\t%s major minor\n"
			"Resolve a device number to a device name\n",
			argv[0], argv[0]);
		exit(1);
	}
	if (argc == 2) {
		devno = strtoul(argv[1], &tmp, 0);
		if (*tmp) {
			fprintf(stderr, errmsg, "device number", argv[1]);
			exit(1);
		}
	} else {
		major = strtoul(argv[1], &tmp, 0);
		if (*tmp) {
			fprintf(stderr, errmsg, "major number", argv[1]);
			exit(1);
		}
		minor = strtoul(argv[2], &tmp, 0);
		if (*tmp) {
			fprintf(stderr, errmsg, "minor number", argv[2]);
			exit(1);
		}
		devno = makedev(major, minor);
	}
	printf("Looking for device 0x%04Lx\n", devno);
	devname = blkid_devno_to_devname(devno);
	if (devname)
		string_free(devname);
	return 0;
}
#endif
