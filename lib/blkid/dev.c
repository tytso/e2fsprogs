/*
 * dev.c - allocation/initialization/free routines for dev
 *
 * Copyright (C) 2001 Andreas Dilger
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 * %End-Header%
 */

#include <stdlib.h>
#include <string.h>

#include "blkid/blkid.h"

#ifdef DEBUG_DEV
#include <stdio.h>
#define DEB_DEV(fmt, arg...) printf("dev: " fmt, ## arg)
#else
#define DEB_DEV(fmt, arg...) do {} while (0)
#endif

blkid_dev *blkid_new_dev(void)
{
	blkid_dev *dev;

	if (!(dev = (blkid_dev *)calloc(1, sizeof(blkid_dev))))
		return NULL;

	INIT_LIST_HEAD(&dev->bid_devs);
	INIT_LIST_HEAD(&dev->bid_tags);

	return dev;
}

void blkid_free_dev(blkid_dev *dev)
{
	if (!dev)
		return;

	DEB_DEV("  freeing dev %s (%s)\n", dev->bid_name, dev->bid_type);
	DEB_DUMP_DEV(dev);

	list_del(&dev->bid_devs);
	while (!list_empty(&dev->bid_tags)) {
		blkid_tag *tag = list_entry(dev->bid_tags.next, blkid_tag,
					    bit_tags);
		blkid_free_tag(tag);
	}
	if (dev->bid_name)
		string_free(dev->bid_name);
	free(dev);
}

/*
 * This is kind of ugly, but I want to be able to compare two strings in
 * several different ways.  For example, in some cases, if both strings
 * are NULL, those would be considered different, but in other cases
 * they would be considered the same.  Hence the ugliness.
 *
 * Use as:	"ret == SC_SAME" if both strings exist and are equal
 *				this is equivalent to "!(ret & SC_DIFF)"
 *		"ret & SC_SAME" if both strings being NULL is also equal
 *				this is equivalent to "!(ret == SC_DIFF)"
 *		"ret == SC_DIFF" if both strings exist and are different
 *				this is equivalent to "!(ret & SC_SAME)"
 *		"ret & SC_DIFF" if both strings being NULL is also different
 *				this is equivalent to "!(ret == SC_SAME)"
 *		"ret == SC_NONE" to see if both strings do not exist
 */
#define SC_DIFF	0x0001
#define SC_NONE	0x0003
#define SC_SAME	0x0002

int string_compare(char *s1, char *s2)
{
	if (!s1 && !s2)
		return SC_NONE;

	if (!s1 || !s2)
		return SC_DIFF;

	if (strcmp(s1, s2))
		return SC_DIFF;

	return SC_SAME;
}

/*
 * Add a tag to the global cache tag list.
 */
static int add_tag_to_cache(blkid_cache *cache, blkid_tag *tag)
{
	blkid_tag *head = NULL;

	if (!cache || !tag)
		return 0;

	DEB_DEV("    adding tag %s=%s to cache\n", tag->bit_name, tag->bit_val);

	if (!(head = blkid_find_head_cache(cache, tag))) {
		head = blkid_new_tag();
		if (!head)
			return -BLKID_ERR_MEM;

		DEB_DEV("    creating new cache tag head %s\n",tag->bit_name);
		head->bit_name = string_copy(tag->bit_name);
		if (!head->bit_name) {
			blkid_free_tag(head);
			return -BLKID_ERR_MEM;
		}

		list_add_tail(&head->bit_tags, &cache->bic_tags);
	}

	/* Add this tag to global list */
	list_add_tail(&tag->bit_names, &head->bit_names);

	return 0;
}

/*
 * Add a device to the global cache list, along with all its tags.
 */
blkid_dev *blkid_add_dev_to_cache(blkid_cache *cache, blkid_dev *dev)
{
	struct list_head *p;

	if (!cache || !dev)
		return dev;

	if (!dev->bid_id)
		dev->bid_id = ++(cache->bic_idmax);

	list_for_each(p, &cache->bic_devs) {
		blkid_dev *odev = list_entry(p, blkid_dev, bid_devs);
		int dup_uuid, dup_label, dup_name, dup_type;

		dup_name = string_compare(odev->bid_name, dev->bid_name);
		dup_label = string_compare(odev->bid_label, dev->bid_label);
		dup_uuid = string_compare(odev->bid_uuid, dev->bid_uuid);

		if (odev->bid_id == dev->bid_id)
			dev->bid_id = ++(cache->bic_idmax);

		/* Fields different, do nothing (check more fields?) */
		if ((dup_name & SC_DIFF) && (dup_uuid & SC_DIFF) &&
		    (dup_label & SC_DIFF))
			continue;

		/* We can't simply allow duplicate fields if the bid_type is
		 * different, given that a filesystem may change from ext2
		 * to ext3 but it will have the same UUID and LABEL fields.
		 * We need to discard the old cache entry in this case.
		 */

		/* If the UUIDs are the same but one is unverified discard it */
		if (dup_uuid == SC_SAME) {
			DEB_DEV("  duplicate uuid %s\n", dev->bid_uuid);
			if (!(odev->bid_flags & BLKID_BID_FL_VERIFIED)) {
				dev->bid_id = odev->bid_id; /* keep old id */
				blkid_free_dev(odev);
				goto exit_new;
			} else if (!(dev->bid_flags & BLKID_BID_FL_VERIFIED)) {
				blkid_free_dev(dev);
				dev = odev;
				goto exit_old;
			}

			/* This shouldn't happen */
			fprintf(stderr, "blkid: same UUID for %s and %s\n",
				dev->bid_name, odev->bid_name);
		}

		/* If the device name is the same, discard one of them
		 * (prefer one that has been validated, or the first one).
		 */
		if (dup_name == SC_SAME) {
			DEB_DEV("  duplicate devname %s\n", dev->bid_name);
			if (odev->bid_flags & BLKID_BID_FL_VERIFIED ||
			    !(dev->bid_flags & BLKID_BID_FL_VERIFIED)) {
				if ((dup_uuid & SC_SAME) &&
				    (dup_label & SC_SAME))	/* use old id */
					dev->bid_id = odev->bid_id;
				blkid_free_dev(dev);
				dev = odev;
				goto exit_old;
			} else {
				blkid_free_dev(odev);
				goto exit_new;
			}
		}

		dup_type = string_compare(odev->bid_type, dev->bid_type);

		if (dup_label == SC_SAME && dup_type == SC_SAME) {
			DEB_DEV("  duplicate label %s\n", dev->bid_label);
			if (!(odev->bid_flags & BLKID_BID_FL_VERIFIED)) {
				blkid_free_dev(odev);
				goto exit_new;
			} else if (!(dev->bid_flags & BLKID_BID_FL_VERIFIED)) {
				blkid_free_dev(dev);
				dev = odev;
				goto exit_old;
			}
			fprintf(stderr, "blkid: same LABEL for %s and %s\n",
				dev->bid_name, odev->bid_name);
		}
	}

exit_new:
	DEB_DEV("  adding new devname %s to cache\n", dev->bid_name);

	cache->bic_flags |= BLKID_BIC_FL_CHANGED;

	list_add_tail(&dev->bid_devs, &cache->bic_devs);
	list_for_each(p, &dev->bid_tags) {
		blkid_tag *tag = list_entry(p, blkid_tag, bit_tags);
		add_tag_to_cache(cache, tag);
	}
	return dev;

exit_old:
	DEB_DEV("  using old devname %s from cache\n", dev->bid_name);
	return dev;
}

#ifdef TEST_PROGRAM
int main(int argc, char** argv)
{
	blkid_cache *cache;
	blkid_dev *dev, *newdev;

	if ((argc != 3)) {
		fprintf(stderr, "Usage:\t%s dev1 dev2\n"
			"Test that adding the same device to the cache fails\n",
			argv[0]);
		exit(1);
	}

	cache = blkid_new_cache();
	if (!cache) {
		perror(argv[0]);
		return 1;
	}
	dev = blkid_devname_to_dev(argv[1], 0);
	newdev = blkid_add_dev_to_cache(cache, dev);
	if (newdev != dev)
		printf("devices changed for %s (unexpected)\n", argv[1]);
	dev = blkid_devname_to_dev(argv[2], 0);
	newdev = blkid_add_dev_to_cache(cache, dev);
	if (newdev != dev)
		printf("devices changed for %s (unexpected)\n", argv[2]);
	dev = blkid_devname_to_dev(argv[2], 0);
	newdev = blkid_add_dev_to_cache(cache, dev);
	if (newdev != dev)
		printf("devices changed for %s (expected)\n", argv[2]);

	blkid_free_cache(cache);

	return 0;
}
#endif
