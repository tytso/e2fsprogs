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
#include "blkidP.h"

#ifdef DEBUG_SAVE
#define DBG(x)	x
#else
#define DBG(x)
#endif

static int save_dev(blkid_dev dev, FILE *file)
{
	struct list_head *p;

	if (!dev)
		return 0;

	DBG(printf("device %s, type %s\n", dev->bid_name, dev->bid_type));

	fprintf(file,
		"<device TYPE=\"%s\" DEVNO=\"0x%04lx\" ID=\"%d\" TIME=\"%lu\"",
		dev->bid_type, (unsigned long) dev->bid_devno,
		dev->bid_id, dev->bid_time);
	list_for_each(p, &dev->bid_tags) {
		blkid_tag tag = list_entry(p, struct blkid_struct_tag, bit_tags);
		if (strcmp(tag->bit_name, "TYPE"))
			fprintf(file, " %s=\"%s\"", tag->bit_name,tag->bit_val);
	}
	fprintf(file, ">%s</device>\n", dev->bid_name);

	return 0;
}

int blkid_save_cache_file(blkid_cache cache, FILE *file)
{
	struct list_head *p;
	int ret = 0;

	if (!cache || !file)
		return -BLKID_ERR_PARAM;

	if (list_empty(&cache->bic_devs) ||
	    !cache->bic_flags & BLKID_BIC_FL_CHANGED)
		return 0;

	list_for_each(p, &cache->bic_devs) {
		blkid_dev dev = list_entry(p, struct blkid_struct_dev, bid_devs);
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
int blkid_save_cache(blkid_cache cache, const char *filename)
{
	char *tmp = NULL;
	const char *opened = NULL;
	FILE *file = NULL;
	int fd, ret;

	if (!cache)
		return -BLKID_ERR_PARAM;

	if (list_empty(&cache->bic_devs) ||
	    !(cache->bic_flags & BLKID_BIC_FL_CHANGED)) {
		DBG(printf("empty cache, not saving\n"));
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
			DBG(printf("can't write to cache file %s\n", filename));
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
			tmp = malloc(strlen(filename) + 8);
			if (tmp) {
				sprintf(tmp, "%s-XXXXXX", filename);
				fd = mkstemp(tmp);
				if (fd >= 0) {
					file = fdopen(fd, "w");
					opened = tmp;
				}
			}
		}

		if (!file) {
			file = fopen(filename, "w");
			opened = filename;
		}

		DBG(printf("cache file %s (really %s)\n", filename, opened));

		if (!file) {
			perror(opened);
			if (tmp)
				free(tmp);
			return errno;
		}
	}

	ret = blkid_save_cache_file(cache, file);

	if (file != stdout) {
		fclose(file);
		if (opened != filename) {
			if (ret < 0) {
				unlink(opened);
				DBG(printf("unlinked temp cache %s\n", opened));
			} else {
				char *backup;

				backup = malloc(strlen(filename) + 5);
				if (backup) {
					sprintf(backup, "%s.old", filename);
					unlink(backup);
					link(filename, backup);
					free(backup);
				}
				rename(opened, filename);
				DBG(printf("moved temp cache %s\n", opened));
			}
		}
	}

	if (tmp)
		free(tmp);
	return ret;
}

#ifdef TEST_PROGRAM
int main(int argc, char **argv)
{
	blkid_cache cache = NULL;
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
