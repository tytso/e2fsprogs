/*
 * tag.c - allocation/initialization/free routines for tag structs
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
#include <stdio.h>

#include "blkid/blkid.h"

#ifdef DEBUG_TAG
#define DBG(x)	x
#else
#define DBG(x)
#endif

blkid_tag *blkid_new_tag(void)
{
	blkid_tag *tag;

	if (!(tag = (blkid_tag *)calloc(1, sizeof(blkid_tag))))
		return NULL;

	INIT_LIST_HEAD(&tag->bit_tags);
	INIT_LIST_HEAD(&tag->bit_names);

	return tag;
}

void blkid_free_tag(blkid_tag *tag)
{
	if (!tag)
		return;

	DBG(printf("    freeing tag %s=%s\n", tag->bit_name, tag->bit_val));
	DEB_DUMP_TAG(tag);

	list_del(&tag->bit_tags);	/* list of tags for this device */
	list_del(&tag->bit_names);	/* list of tags with this type */

	if (tag->bit_name)
		string_free(tag->bit_name);
	if (tag->bit_val)
		string_free(tag->bit_val);

	free(tag);
}

/*
 * Find the desired tag on a list of tags with the same type.
 */
blkid_tag *blkid_find_tv_tags(blkid_tag *head, const char *value)
{
	struct blkid_tag *tag = NULL;
	struct list_head *p;

	if (!head || !value)
		return NULL;

	DBG(printf("looking for %s in %s list\n", value, head->bit_name));

	list_for_each(p, &head->bit_names) {
		blkid_tag *tmp = list_entry(p, blkid_tag, bit_names);

		if (!strcmp(tmp->bit_val, value)) {
			tag = tmp;
			break;
		}
	}

	return tag;
}

/*
 * Find the desired tag on a device.  If tag->bit_value is NULL, then the
 * first such tag is returned, otherwise return only exact tag if found.
 */
blkid_tag *blkid_find_tag_dev(blkid_dev *dev, blkid_tag *tag)
{
	blkid_tag *found = NULL;
	struct list_head *p;

	if (!dev || !tag)
		return NULL;

	list_for_each(p, &dev->bid_tags) {
		blkid_tag *tmp = list_entry(p, blkid_tag, bit_tags);

		if (!strcmp(tmp->bit_name, tag->bit_name) &&
		    (!tag->bit_val || !strcmp(tmp->bit_val, tag->bit_val))){
			found = tmp;
			break;
		}
	}

	return found;
}

/*
 * Find the desired tag type in the cache.
 * We return the head tag for this tag type.
 */
blkid_tag *blkid_find_head_cache(blkid_cache *cache, blkid_tag *tag)
{
	blkid_tag *head = NULL;
	struct list_head *p;

	if (!cache || !tag)
		return NULL;

	list_for_each(p, &cache->bic_tags) {
		blkid_tag *tmp = list_entry(p, blkid_tag, bit_tags);

		if (!strcmp(tmp->bit_name, tag->bit_name)) {
			DBG(printf("    found cache tag head %s\n", tag->bit_name));
			head = tmp;
			break;
		}
	}

	return head;
}

/*
 * Find a specific tag value in the cache.  If not found return NULL.
 */
blkid_tag *blkid_find_tag_cache(blkid_cache *cache, blkid_tag *tag)
{
	blkid_tag *head;

	DBG(printf("looking for %s=%s in cache\n", tag->bit_name, tag->bit_val));

	head = blkid_find_head_cache(cache, tag);

	return blkid_find_tv_tags(head, tag->bit_val);
}

/*
 * Get a specific tag value in the cache.  If not found return NULL.
 * If we have not already probed the devices, do so and search again.
 */
blkid_tag *blkid_get_tag_cache(blkid_cache *cache, blkid_tag *tag)
{
	blkid_tag *head, *found;

	if (!tag || !cache)
		return NULL;

	DBG(printf("looking for %s=%s in cache\n", tag->bit_name, tag->bit_val));

	head = blkid_find_head_cache(cache, tag);
	found = blkid_find_tv_tags(head, tag->bit_val);

	if ((!head || !found) && !(cache->bic_flags & BLKID_BIC_FL_PROBED)) {
		blkid_probe_all(&cache);
		if (!head)
			head = blkid_find_head_cache(cache, tag);
		found = blkid_find_tv_tags(head, tag->bit_val);
	}

	return found;
}

/*
 * Add a single tag to the given device.
 * This function is not currently exported because adding arbitrary tags to
 * a device will just get lost as soon as we verify the device (which
 * uses the dev struct returned from the device probe).  At some point in
 * the future it may be desirable to allow adding arbitrary tags to a device,
 * and ensure that verify keeps all such tags (maybe lower case tag names?)
 */
static void add_tag_to_dev(blkid_dev *dev, blkid_tag *tag)
{
	if (!dev)
		return;

	DBG(printf("adding tag %s=%s\n", tag->bit_name, tag->bit_val));

	tag->bit_dev = dev;

	list_add_tail(&tag->bit_tags, &dev->bid_tags);

	/* Link common tags directly to the device struct */
	if (!strcmp(tag->bit_name, "TYPE") && !dev->bid_type)
		dev->bid_type = tag->bit_val;
	else if (!strcmp(tag->bit_name, "LABEL"))
		dev->bid_label = tag->bit_val;
	else if (!strcmp(tag->bit_name, "UUID"))
		dev->bid_uuid = tag->bit_val;
}

/*
 * Allocate and fill out a tag struct.
 * If dev is valid, the tag will be added to the tags for this device
 * if an identical tag does not already exist.
 * If tag is valid, the tag will be returned in this pointer.
 */
int blkid_create_tag(blkid_dev *dev, blkid_tag **tag, const char *name,
		     const char *value, const int vlength)
{
	blkid_tag *t, *found;

	if (!tag && !dev)
		return -BLKID_ERR_PARAM;

	if (!name)
		return -BLKID_ERR_PARAM;

	t = blkid_new_tag();
	if (!t)
		return -BLKID_ERR_MEM;

	t->bit_name = string_copy(name);
	t->bit_val = stringn_copy(value, vlength);

	if ((found = blkid_find_tag_dev(dev, t))) {
		if (tag)
			*tag = found;
		blkid_free_tag(t);
		return 0;
	}

	add_tag_to_dev(dev, t);
	if (tag)
		*tag = t;

	return 0;
}

/*
 * Convert a NAME=value pair into a token.  This is slightly different than
 * parse_token, because that will end an unquoted value at a space, while
 * this will assume that an unquoted value is the rest of the token (e.g.
 * if we are passed al alreay quoted string from the command-line we don't
 * have to both quote and escape quote so that the quotes make it to us).
 */
blkid_tag *blkid_token_to_tag(const char *token)
{
	char *name, *value, *cp;
	blkid_tag *tag = NULL;
	int len;

	DBG(printf("trying to make '%s' into a tag\n", token));
	if (!token || !(cp = strchr(token, '=')))
		return NULL;

	name = string_copy(token);
	value = name + (cp - token);
	*value++ = '\0';
	if (*value == '"' || *value == '\'') {
		char c = *value++;
		if (!(cp = strrchr(value, c))) {
			fprintf(stderr, "Missing close quote for %s\n", token);
			return NULL;
		}
		*cp = '\0';
		len = cp - value;
	} else
		len = strlen(value);

	blkid_create_tag(NULL, &tag, name, value, len);

	string_free(name);

	return tag;
}
