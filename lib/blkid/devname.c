/*
 * devname.c - get a dev by its device inode name
 *
 * Copyright (C) Andries Brouwer
 * Copyright (C) 1999, 2000, 2001 Theodore Ts'o
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
#include <ctype.h>
#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#if HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#if HAVE_ERRNO_H
#include <errno.h>
#endif
#if HAVE_SYS_MKDEV_H
#include <sys/mkdev.h>
#endif
#include <time.h>

#include "blkid/blkid.h"

/* #define DEBUG_DEVNAME */
#ifdef DEBUG_DEVNAME
#define DEB_DEV(fmt, arg...) printf("devname: " fmt, ## arg)
#else
#define DEB_DEV(fmt, arg...) do {} while (0)
#endif

/*
 * Find a dev struct in the cache by device name, if available.
 */
blkid_dev *blkid_find_devname(blkid_cache *cache, const char *devname)
{
	blkid_dev *dev = NULL;
	struct list_head *p;

	if (!cache || !devname)
		return NULL;

	list_for_each(p, &cache->bic_devs) {
		blkid_dev *tmp = list_entry(p, blkid_dev, bid_devs);

		if (strcmp(tmp->bid_name, devname))
			continue;

		DEB_DEV("found devname %s in cache\n", tmp->bid_name);
		dev = blkid_verify_devname(cache, tmp);
		break;
	}

	return dev;
}

/*
 * Return a pointer to an dev struct, either from cache or by probing.
 */
blkid_dev *blkid_get_devname(blkid_cache *cache, const char *devname)
{
	blkid_dev *dev;

	if ((dev = blkid_find_devname(cache, devname)))
		return dev;

	dev = blkid_devname_to_dev(devname, 0);
	return blkid_add_dev_to_cache(cache, dev);
}

/*
 * Probe a single block device to add to the device cache.
 * If the size is not specified, it will be found in blkid_devname_to_dev().
 */
static blkid_dev *probe_one(blkid_cache *cache, const char *ptname,
			    int major, int minor, unsigned long long size)
{
	dev_t devno = makedev(major, minor);
	blkid_dev *dev;
	const char **dir;
	char *devname = NULL;

	/* See if we already have this device number in the cache. */
	if ((dev = blkid_find_devno(cache, devno)))
		return dev;

	/*
	 * Take a quick look at /dev/ptname for the device number.  We check
	 * all of the likely device directories.  If we don't find it, or if
	 * the stat information doesn't check out, use blkid_devno_to_devname()
	 * to find it via an exhaustive search for the device major/minor.
	 */
	for (dir = devdirs; *dir; dir++) {
		struct stat st;
		char device[256];

		sprintf(device, "%s/%s", *dir, ptname);
		if ((dev = blkid_find_devname(cache, device)) &&
		    dev->bid_devno == devno)
			return dev;

		if (stat(device, &st) == 0 && st.st_rdev == devno) {
			devname = string_copy(device);
			break;
		}
	}
	if (!devname) {
		devname = blkid_devno_to_devname(devno);
		if (!devname)
			return NULL;
	}
	dev = blkid_devname_to_dev(devname, size);
	string_free(devname);

	return blkid_add_dev_to_cache(cache, dev);
}

#define PROC_PARTITIONS "/proc/partitions"
#define VG_DIR		"/proc/lvm/VGs"

/*
 * This function initializes the UUID cache with devices from the LVM
 * proc hierarchy.  We currently depend on the names of the LVM
 * hierarchy giving us the device structure in /dev.  (XXX is this a
 * safe thing to do?)
 */
#ifdef VG_DIR
#include <dirent.h>
static int lvm_get_devno(const char *lvm_device, int *major, int *minor,
			 blkid_loff_t *size)
{
	FILE *lvf;
	char buf[1024];
	int ret = 1;

	*major = *minor = 0;
	*size = 0;

	DEB_DEV("opening %s\n", lvm_device);
	if ((lvf = fopen(lvm_device, "r")) == NULL) {
		ret = errno;
		DEB_DEV("%s: (%d) %s\n", lvm_device, ret, strerror(ret));
		return -ret;
	}

	while (fgets(buf, sizeof(buf), lvf)) {
		if (sscanf(buf, "size: %Ld", size) == 1) { /* sectors */
			*size <<= 9;
		}
		if (sscanf(buf, "device: %d:%d", major, minor) == 2) {
			ret = 0;
			break;
		}
	}
	fclose(lvf);

	return ret;
}

static void lvm_probe_all(blkid_cache **cache)
{
	DIR		*vg_list;
	struct dirent	*vg_iter;
	int		vg_len = strlen(VG_DIR);

	if ((vg_list = opendir(VG_DIR)) == NULL)
		return;

	DEB_DEV("probing LVM devices under %s\n", VG_DIR);

	while ((vg_iter = readdir(vg_list)) != NULL) {
		DIR		*lv_list;
		char		*vdirname;
		char		*vg_name;
		struct dirent	*lv_iter;

		vg_name = vg_iter->d_name;
		if (!strcmp(vg_name, ".") || !strcmp(vg_name, ".."))
			continue;
		vdirname = malloc(vg_len + strlen(vg_name) + 8);
		if (!vdirname)
			goto exit;
		sprintf(vdirname, "%s/%s/LVs", VG_DIR, vg_name);

		lv_list = opendir(vdirname);
		free(vdirname);
		if (lv_list == NULL)
			continue;

		while ((lv_iter = readdir(lv_list)) != NULL) {
			char		*lv_name, *lvm_device;
			int		major, minor;
			blkid_loff_t	size;

			lv_name = lv_iter->d_name;
			if (!strcmp(lv_name, ".") || !strcmp(lv_name, ".."))
				continue;

			lvm_device = malloc(vg_len + strlen(vg_name) +
					    strlen(lv_name) + 8);
			if (!lvm_device) {
				closedir(lv_list);
				goto exit;
			}
			sprintf(lvm_device, "%s/%s/LVs/%s", VG_DIR, vg_name,
				lv_name);
			if (lvm_get_devno(lvm_device, &major, &minor, &size)) {
				free(lvm_device);
				continue;
			}
			sprintf(lvm_device, "%s/%s", vg_name, lv_name);
			DEB_DEV("LVM dev %s: devno 0x%02X%02X, size %Ld\n",
				lvm_device, major, minor, size);
			probe_one(*cache, lvm_device, major, minor, size);
			free(lvm_device);
		}
		closedir(lv_list);
	}
exit:
	closedir(vg_list);
}
#endif

/*
 * Read the device data for all available block devices in the system.
 */
int blkid_probe_all(blkid_cache **cache)
{
	FILE *proc;
	int firstPass;

	if (!cache)
		return -BLKID_ERR_PARAM;

	if (!*cache)
		*cache = blkid_new_cache();

	if (!*cache)
		return -BLKID_ERR_MEM;

	if ((*cache)->bic_flags & BLKID_BIC_FL_PROBED &&
	    time(0) - (*cache)->bic_time < BLKID_PROBE_INTERVAL)
		return 0;

#ifdef VG_DIR
	lvm_probe_all(cache);
#endif

	proc = fopen(PROC_PARTITIONS, "r");
	if (!proc)
		return -BLKID_ERR_PROC;

	for (firstPass = 1; firstPass >= 0; firstPass--) {
		char line[1024];
		char ptname0[128], ptname1[128];
		char *ptnames[2] = { ptname0, ptname1 };
		int majors[2], minors[2];
		unsigned long long sizes[2];
		int lens[2] = { 0, 0 };
		int handleOnFirst;
		int which = 0, last = 0;

		fseek(proc, 0, SEEK_SET);

		while (fgets(line, sizeof(line), proc)) {
			last = which;
			which ^= 1;

			if (sscanf(line, " %d %d %Ld %128[^\n ]",
				   &majors[which], &minors[which],
				   &sizes[which], ptnames[which]) != 4)
				continue;

			DEB_DEV("read partition name %s\n", ptnames[which]);

			/* look only at md devices on first pass */
			handleOnFirst = !strncmp(ptnames[which], "md", 2);
			if (firstPass != handleOnFirst)
				continue;

			/* Skip whole disk devs unless they have no partitions
			 * If we don't have a partition on this dev, also
			 * check previous dev to see if it didn't have a partn.
			 * heuristic: partition name ends in a digit.
			 *
			 * Skip extended partitions.
			 * heuristic: size is 1
			 *
			 * FIXME: skip /dev/{ida,cciss,rd} whole-disk devs
			 */

			lens[which] = strlen(ptnames[which]);
			if (isdigit(ptnames[which][lens[which] - 1])) {
				DEB_DEV("partition dev %s, devno 0x%02X%02X\n",
					ptnames[which], majors[which],
					minors[which]);

				if (sizes[which] > 1)
					probe_one(*cache, ptnames[which],
						  majors[which], minors[which],
						  sizes[which] << 10);
				lens[which] = 0;
				lens[last] = 0;
			} else if (lens[last] &&
				   strncmp(ptnames[last], ptnames[which],
					   lens[last])) {
				DEB_DEV("whole dev %s, devno 0x%02X%02X\n",
					ptnames[last], majors[last],
					minors[last]);
				probe_one(*cache, ptnames[last], majors[last],
					  minors[last], sizes[last] << 10);
				lens[last] = 0;
			}
		}

		/* Handle the last device if it wasn't partitioned */
		if (lens[which])
			probe_one(*cache, ptnames[which], majors[which],
				  minors[which], sizes[which] << 10);
	}
	fclose(proc);

	(*cache)->bic_time = time(0);
	(*cache)->bic_flags |= BLKID_BIC_FL_PROBED;

	return 0;
}

#ifdef TEST_PROGRAM
int main(int argc, char **argv)
{
	blkid_cache *cache = NULL;

	if (argc != 1) {
		fprintf(stderr, "Usage: %s\n"
			"Probe all devices and exit\n", argv[0]);
		exit(1);
	}
	if (blkid_probe_all(&cache) < 0)
		printf("%s: error probing devices\n", argv[0]);

	blkid_free_cache(cache);
	return (0);
}
#endif
