/*
 * save.c - write the cache struct to disk
 *
 * Copyright (C) 2001 by Andreas Dilger
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 * %End-Header%
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#ifdef HAVE_SYS_MKDEV_H
#include <sys/mkdev.h>
#endif
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#include "blkid/blkid.h"

#ifdef DEBUG_SAVE
#define DEB_SAVE(fmt, arg...) printf("save: " fmt, ## arg)
#else
#define DEB_SAVE(fmt, arg...) do {} while (0)
#endif

static int save_dev(blkid_dev *dev, FILE *file)
{
	struct list_head *p;

	if (!dev)
		return 0;

	DEB_SAVE("device %s, type %s\n", dev->bid_name, dev->bid_type);

	fprintf(file,
		"<device TYPE=\"%s\" DEVNO=\"0x%04Lx\" ID=\"%d\" TIME=\"%Ld\"",
		dev->bid_type, dev->bid_devno,
		dev->bid_id, (long long)dev->bid_time);
	list_for_each(p, &dev->bid_tags) {
		blkid_tag *tag = list_entry(p, blkid_tag, bit_tags);
		if (strcmp(tag->bit_name, "TYPE"))
			fprintf(file, " %s=\"%s\"", tag->bit_name,tag->bit_val);
	}
	fprintf(file, ">%s</device>\n", dev->bid_name);

	return 0;
}

int blkid_save_cache_file(blkid_cache *cache, FILE *file)
{
	struct list_head *p;
	int ret = 0;

	if (!cache || !file)
		return -BLKID_ERR_PARAM;

	if (list_empty(&cache->bic_devs) ||
	    !cache->bic_flags & BLKID_BIC_FL_CHANGED)
		return 0;

	list_for_each(p, &cache->bic_devs) {
		blkid_dev *dev = list_entry(p, blkid_dev, bid_devs);
		if ((ret = save_dev(dev, file)) < 0)
			break;
	}

	if (ret >= 0) {
		cache->bic_flags &= ~BLKID_BIC_FL_CHANGED;
		ret = 1;
	}

	return ret;
}

/*
 * Write out the cache struct to the cache file on disk.
 */
int blkid_save_cache(blkid_cache *cache, char *filename)
{
	char tmp[4096] = { '\0', };
	char *opened = NULL;
	FILE *file = NULL;
	int fd, ret;

	if (!cache)
		return -BLKID_ERR_PARAM;

	if (list_empty(&cache->bic_devs) ||
	    !(cache->bic_flags & BLKID_BIC_FL_CHANGED)) {
		DEB_SAVE("empty cache, not saving\n");
		return 0;
	}

	if (!filename || !strlen(filename))
		filename = BLKID_CACHE_FILE;

	if (!strcmp(filename, "-") || !strcmp(filename, "stdout"))
		file = stdout;
	else {
		struct stat st;

		/* If we can't write to the cache file, then don't even try */
		if (((ret = stat(filename, &st)) < 0 && errno != ENOENT) ||
		    (ret == 0 && access(filename, W_OK) < 0)) {
			DEB_SAVE("can't write to cache file %s\n", filename);
			return 0;
		}

		/*
		 * Try and create a temporary file in the same directory so
		 * that in case of error we don't overwrite the cache file.
		 * If the cache file doesn't yet exist, it isn't a regular
		 * file (e.g. /dev/null or a socket), or we couldn't create
		 * a temporary file then we open it directly.
		 */
		if (ret == 0 && S_ISREG(st.st_mode)) {
			snprintf(tmp, sizeof(tmp) - 1, "%s-XXXXXX", filename);
			fd = mkstemp(tmp);
			if (fd >= 0) {
				file = fdopen(fd, "w");
				opened = tmp;
			}
		}

		if (!file) {
			file = fopen(filename, "w");
			opened = filename;
		}

		DEB_SAVE("cache file %s (really %s)\n", filename, opened);

		if (!file) {
			perror(opened);
			return errno;
		}
	}

	ret = blkid_save_cache_file(cache, file);

	if (file != stdout) {
		fclose(file);
		if (opened != filename) {
			if (ret < 0) {
				unlink(opened);
				DEB_SAVE("unlinked temp cache %s\n", opened);
			} else {
				char backup[4096];

				snprintf(backup, sizeof(backup) - 1, "%s.old",
					 filename);
				unlink(backup);
				link(filename, backup);
				rename(opened, filename);
				DEB_SAVE("moved temp cache %s\n", opened);
			}
		}
	}

	return ret;
}

#ifdef TEST_PROGRAM
int main(int argc, char **argv)
{
	blkid_cache *cache = NULL;
	int ret;

	if (argc > 2) {
		fprintf(stderr, "Usage: %s [filename]\n"
			"Test loading/saving a cache (filename)\n", argv[0]);
		exit(1);
	}
	if ((ret = blkid_probe_all(&cache) < 0))
		fprintf(stderr, "error probing devices\n");
	else if ((ret = blkid_save_cache(cache, argv[1])) < 0)
		fprintf(stderr, "error %d saving cache to %s\n", ret,
			argv[1] ? argv[1] : BLKID_CACHE_FILE);

	blkid_free_cache(cache);

	return ret;
}
#endif
