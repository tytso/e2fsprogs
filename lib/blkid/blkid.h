/*
 * blkid.h - Interface for libblkid, a library to identify block devices
 *
 * Copyright (C) 2001 Andreas Dilger
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 * %End-Header%
 */

#ifndef _BLKID_BLKID_H
#define _BLKID_BLKID_H

#include <sys/types.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BLKID_VERSION	"1.2.0"
#define BLKID_DATE	"22-Nov-2001"

#include "blkid/list.h"
#include "blkid/blkid_types.h"

typedef __s64 blkid_loff_t;

/*
 * This describes the attributes of a specific device.
 * We can traverse all of the tags by bid_tags (linking to the tag bit_names).
 * The bid_label and bid_uuid fields are shortcuts to the LABEL and UUID tag
 * values, if they exist.
 */
typedef struct blkid_dev
{
	struct list_head	bid_devs;	/* All devices in the cache */
	struct list_head	bid_tags;	/* All tags for this device */
	char			*bid_name;	/* Device inode pathname */
	char			*bid_type;	/* Preferred device TYPE */
	blkid_loff_t		bid_size;	/* Filesystem size in bytes */
	blkid_loff_t		bid_free;	/* Filesystem free in bytes */
	blkid_loff_t		bid_devsize;	/* Device size in bytes */
	dev_t			bid_devno;	/* Device major/minor number */
	time_t			bid_time;	/* Last update time of device */
	unsigned int		bid_id;		/* Unique cache id for device */
	unsigned int		bid_flags;	/* Device status bitflags */
	char			*bid_label;	/* Shortcut to device LABEL */
	char			*bid_uuid;	/* Shortcut to binary UUID */
	unsigned long		bid_unused[13];	/* Fields for future use */
} blkid_dev;

#define BLKID_BID_FL_VERIFIED	0x0001	/* Device data validated from disk */
#define BLKID_BID_FL_MTYPE	0x0002	/* Device has multiple type matches */

/*
 * Each tag defines a NAME=value pair for a particular device.  The tags
 * are linked via bit_names for a single device, so that traversing the
 * names list will get you a list of all tags associated with a device.
 * They are also linked via bit_values for all devices, so one can easily
 * search all tags with a given NAME for a specific value.
 */
typedef struct blkid_tag
{
	struct list_head	bit_tags;	/* All tags for this device */
	struct list_head	bit_names;	/* All tags with given NAME */
	char			*bit_name;	/* NAME of tag (shared) */
	char			*bit_val;	/* value of tag */
	struct blkid_dev	*bit_dev;	/* pointer to device */
	unsigned long		bit_unused[9];	/* Fields for future use */
} blkid_tag;

/*
 * Minimum number of seconds between device probes, even when reading
 * from the cache.  This is to avoid re-probing all devices which were
 * just probed by another program that does not share the cache.
 */
#define BLKID_PROBE_MIN		2

/*
 * Time in seconds an entry remains verified in the in-memory cache
 * before being reverified (in case of long-running processes that
 * keep a cache in memory and continue to use it for a long time).
 */
#define BLKID_PROBE_INTERVAL	200

/* This describes an entire blkid cache file and probed devices.
 * We can traverse all of the found devices via bic_list.
 * We can traverse all of the tag types by bic_tags, which hold empty tags
 * for each tag type.  Those tags can be used as list_heads for iterating
 * through all devices with a specific tag type (e.g. LABEL).
 */
typedef struct blkid_cache
{
	struct list_head	bic_devs;	/* List head of all devices */
	struct list_head	bic_tags;	/* List head of all tag types */
	time_t			bic_time;	/* Last probe time */
	unsigned int		bic_idmax;	/* Highest ID assigned */
	unsigned int		bic_flags;	/* Status flags of the cache */
	unsigned long		bic_unused[9];	/* Fields for future use */
} blkid_cache;

#define BLKID_BIC_FL_PARSED	0x0001	/* We parsed a cache file */
#define BLKID_BIC_FL_PROBED	0x0002	/* We probed /proc/partition devices */
#define BLKID_BIC_FL_CHANGED	0x0004	/* Cache has changed from disk */

extern char *string_copy(const char *s);
extern char *stringn_copy(const char *s, const int length);
extern void string_free(char *s);
extern blkid_cache *blkid_new_cache(void);
extern void blkid_free_cache(blkid_cache *cache);

#define BLKID_CACHE_FILE "/etc/blkid.tab"
extern const char *devdirs[];

#define BLKID_ERR_IO	 5
#define BLKID_ERR_PROC	 9
#define BLKID_ERR_MEM	12
#define BLKID_ERR_CACHE	14
#define BLKID_ERR_DEV	19
#define BLKID_ERR_PARAM	22
#define BLKID_ERR_BIG	27

#ifdef DEBUG
#define DEBUG_CACHE
#define DEBUG_DUMP
#define DEBUG_DEV
#define DEBUG_DEVNAME
#define DEBUG_DEVNO
#define DEBUG_PROBE
#define DEBUG_READ
#define DEBUG_RESOLVE
#define DEBUG_SAVE
#define DEBUG_TAG
#define CHECK_TAG
#endif

#if defined(TEST_PROGRAM) && !defined(DEBUG_DUMP)
#define DEBUG_DUMP
#endif

#ifdef DEBUG_DUMP
static inline void DEB_DUMP_TAG(blkid_tag *tag)
{
	if (!tag) {
		printf("    tag: NULL\n");
		return;
	}

	printf("    tag: %s=\"%s\"\n", tag->bit_name, tag->bit_val);
}

static inline void DEB_DUMP_DEV(blkid_dev *dev)
{
	struct list_head *p;

	if (!dev) {
		printf("  dev: NULL\n");
		return;
	}

	printf("  dev: name = %s\n", dev->bid_name);
	printf("  dev: DEVNO=\"0x%0Lx\"\n", dev->bid_devno);
	printf("  dev: ID=\"%u\"\n", dev->bid_id);
	printf("  dev: TIME=\"%lu\"\n", dev->bid_time);
	printf("  dev: size = %Lu\n", dev->bid_size);
	printf("  dev: flags = 0x%08X\n", dev->bid_flags);

	list_for_each(p, &dev->bid_tags) {
		blkid_tag *tag = list_entry(p, blkid_tag, bit_tags);
		DEB_DUMP_TAG(tag);
	}
	printf("\n");
}

static inline void DEB_DUMP_CACHE(blkid_cache *cache)
{
	struct list_head *p;

	if (!cache) {
		printf("cache: NULL\n");
		return;
	}

	printf("cache: time = %lu\n", cache->bic_time);
	printf("cache: idmax = %u\n", cache->bic_idmax);
	printf("cache: flags = 0x%08X\n", cache->bic_flags);

	list_for_each(p, &cache->bic_devs) {
		blkid_dev *dev = list_entry(p, blkid_dev, bid_devs);
		DEB_DUMP_DEV(dev);
	}
}
#else
#define DEB_DUMP_TAG(tag) do {} while (0)
#define DEB_DUMP_DEV(dev) do {} while (0)
#define DEB_DUMP_CACHE(cache) do {} while (0)
#endif

/*
 * Primitive disk functions: llseek.c, getsize.c
 */
extern blkid_loff_t blkid_llseek(int fd, blkid_loff_t offset, int whence);
extern blkid_loff_t blkid_get_dev_size(int fd);

/*
 * Getting data from the cache file: read.c
 */
int blkid_read_cache_line(blkid_cache *cache, blkid_dev **dev_p, char *cp);
int blkid_read_cache_file(blkid_cache **cache, FILE *file);
int blkid_read_cache(blkid_cache **cache, const char *filename);

/*
 * Save data to the cache file: save.c
 */
int blkid_save_cache_file(blkid_cache *cache, FILE *file);
int blkid_save_cache(blkid_cache *cache, char *filename);

/*
 * Identify a device by inode name: probe.c
 */
extern blkid_dev *blkid_devname_to_dev(const char *devname,
				       blkid_loff_t size);

/*
 * Locate a device by inode name: devname.c
 */
extern blkid_dev *blkid_find_devname(blkid_cache *cache, const char *devname);
extern blkid_dev *blkid_verify_devname(blkid_cache *cache, blkid_dev *dev);
extern blkid_dev *blkid_get_devname(blkid_cache *cache, const char *devname);
extern int blkid_probe_all(blkid_cache **cache);

/*
 * Locate a device by device major/minor number: devno.c
 */
extern char *blkid_devno_to_devname(dev_t devno);
extern blkid_dev *blkid_find_devno(blkid_cache *cache, dev_t devno);
extern blkid_dev *blkid_get_devno(blkid_cache *cache, dev_t devno);

/*
 * Functions to create and find a specific tag type: tag.c
 */
extern blkid_tag *blkid_new_tag(void);
extern void blkid_free_tag(blkid_tag *tag);
extern int blkid_create_tag(blkid_dev *dev, blkid_tag **tag,
			    const char *name, const char *value,
			    const int vlength);
extern blkid_tag *blkid_token_to_tag(const char *token);
extern blkid_tag *blkid_find_tv_tags(blkid_tag *head, const char *value);
extern blkid_tag *blkid_find_tag_dev(blkid_dev *dev, blkid_tag *tag);
extern blkid_tag *blkid_find_head_cache(blkid_cache *cache, blkid_tag *tag);
extern blkid_tag *blkid_find_tag_cache(blkid_cache *cache, blkid_tag *tag);
extern blkid_tag *blkid_get_tag_cache(blkid_cache *cache, blkid_tag *tag);

/*
 * Functions to create and find a specific tag type: dev.c
 */
extern blkid_dev *blkid_new_dev(void);
extern void blkid_free_dev(blkid_dev *dev);
extern blkid_dev *blkid_add_dev_to_cache(blkid_cache *cache, blkid_dev *dev);

/*
 * Helper functions for primarily single use: resolve.c
 */
extern char *blkid_get_tagname_devname(blkid_cache *cache, const char *tagname,
				       const char *devname);
extern char *blkid_get_token(blkid_cache *cache, const char *token,
			     const char *value);

#ifdef __cplusplus
}
#endif

#endif /* _BLKID_BLKID_H */
