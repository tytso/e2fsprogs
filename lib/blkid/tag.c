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

#include "blkidP.h"

#ifdef DEBUG_TAG
#define DBG(x)	x
#else
#define DBG(x)
#endif

blkid_tag blkid_new_tag(void)
{
	blkid_tag tag;

	if (!(tag = (blkid_tag) calloc(1, sizeof(struct blkid_struct_tag))))
		return NULL;

	INIT_LIST_HEAD(&tag->bit_tags);
	INIT_LIST_HEAD(&tag->bit_names);

	return tag;
}

void blkid_free_tag(blkid_tag tag)
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
 * Find the desired tag on a device.  If value is NULL, then the
 * first such tag is returned, otherwise return only exact tag if found.
 */
blkid_tag blkid_find_tag_dev(blkid_dev dev, const char *type,
			     const char *value)
{
	blkid_tag found = NULL;
	struct list_head *p;

	if (!dev || !type || !value)
		return NULL;

	list_for_each(p, &dev->bid_tags) {
		blkid_tag tmp = list_entry(p, struct blkid_struct_tag,
					   bit_tags);

		if (!strcmp(tmp->bit_name, type) &&
		    (!value || !strcmp(tmp->bit_val, value))){
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
blkid_tag blkid_find_head_cache(blkid_cache cache, const char *type)
{
	blkid_tag head = NULL;
	struct list_head *p;

	if (!cache || !type)
		return NULL;

	list_for_each(p, &cache->bic_tags) {
		blkid_tag tmp = list_entry(p, struct blkid_struct_tag, 
					   bit_tags);

		if (!strcmp(tmp->bit_name, type)) {
			DBG(printf("    found cache tag head %s\n", type));
			head = tmp;
			break;
		}
	}

	return head;
}

/*
 * Add a single tag to the given device.
 * This function is not currently exported because adding arbitrary tags to
 * a device will just get lost as soon as we verify the device (which
 * uses the dev struct returned from the device probe).  At some point in
 * the future it may be desirable to allow adding arbitrary tags to a device,
 * and ensure that verify keeps all such tags (maybe lower case tag names?)
 */
static void add_tag_to_dev(blkid_dev dev, blkid_tag tag)
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
int blkid_create_tag(blkid_dev dev, const char *name,
		     const char *value, const int vlength)
{
	blkid_tag t, found;

	if (!dev)
		return -BLKID_ERR_PARAM;

	if (!name)
		return -BLKID_ERR_PARAM;

	t = blkid_new_tag();
	if (!t)
		return -BLKID_ERR_MEM;

	t->bit_name = string_copy(name);
	t->bit_val = stringn_copy(value, vlength);

	if ((found = blkid_find_tag_dev(dev, name, t->bit_val))) {
		blkid_free_tag(t);
		return 0;
	}

	add_tag_to_dev(dev, t);

	return 0;
}

/*
 * Parse a "NAME=value" string.  This is slightly different than
 * parse_token, because that will end an unquoted value at a space, while
 * this will assume that an unquoted value is the rest of the token (e.g.
 * if we are passed al alreay quoted string from the command-line we don't
 * have to both quote and escape quote so that the quotes make it to
 * us).
 *
 * Returns 0 on success, and -1 on failure.
 */
int blkid_parse_tag_string(const char *token, char **ret_type, char **ret_val)
{
	char *name, *value, *cp;

	DBG(printf("trying to parse '%s' as a tag\n", token));

	if (!token || !(cp = strchr(token, '=')))
		return -1;

	name = string_copy(token);
	if (!name)
		return -1;
	value = name + (cp - token);
	*value++ = '\0';
	if (*value == '"' || *value == '\'') {
		char c = *value++;
		if (!(cp = strrchr(value, c)))
			goto errout; /* missing closing quote */
		*cp = '\0';
	}
	value = string_copy(value);
	if (!value)
		goto errout;

	*ret_type = name;
	*ret_val = value;

	return 0;

errout:
	string_free(name);
	return -1;
}

/*
 * Tag iteration routines for the public libblkid interface.
 *
 * These routines do not expose the list.h implementation, which are a
 * contamination of the namespace, and which force us to reveal far, far
 * too much of our internal implemenation.  I'm not convinced I want
 * to keep list.h in the long term, anyway.  It's fine for kernel
 * programming, but performance is not the #1 priority for this
 * library, and I really don't like the tradeoff of type-safety for
 * performance for this application.  [tytso:20030125.2007EST]
 */

/*
 * This series of functions iterate over all tags in a device
 */
#define TAG_ITERATE_MAGIC	0x01a5284c
	
struct blkid_struct_tag_iterate {
	int			magic;
	blkid_dev		dev;
	struct list_head	*p;
};

extern blkid_tag_iterate blkid_tag_iterate_begin(blkid_dev dev)
{
	blkid_tag_iterate	iter;

	iter = malloc(sizeof(struct blkid_struct_tag_iterate));
	if (iter) {
		iter->magic = TAG_ITERATE_MAGIC;
		iter->dev = dev;
		iter->p	= dev->bid_tags.next;
	}
	return (iter);
}

/*
 * Return 0 on success, -1 on error
 */
extern int blkid_tag_next(blkid_tag_iterate iter,
			  const char **type, const char **value)
{
	blkid_tag tag;
	
	*type = 0;
	*value = 0;
	if (!iter || iter->magic != TAG_ITERATE_MAGIC ||
	    iter->p == &iter->dev->bid_tags)
		return -1;
	tag = list_entry(iter->p, struct blkid_struct_tag, bit_tags);
	*type = tag->bit_name;
	*value = tag->bit_val;
	iter->p = iter->p->next;
	return 0;
}

extern void blkid_tag_iterate_end(blkid_tag_iterate iter)
{
	if (!iter || iter->magic != TAG_ITERATE_MAGIC)
		return;
	iter->magic = 0;
	free(iter);
}

/*
 * This function returns a device which matches a particular
 * type/value pair.  Its behaviour is currently undefined if there is
 * more than one device which matches the search specification.
 * In the future we may have some kind of preference scheme so that if
 * there is more than one match for a given label/uuid (for example in
 * the case of snapshots) we return the preferred device.
 *
 * XXX there should also be an interface which uses an iterator so we
 * can get all of the devices which match a type/value search parameter.
 */
extern blkid_dev blkid_find_dev_with_tag(blkid_cache cache,
					 const char *type,
					 const char *value)
{
	blkid_tag head = 0, found;
	struct list_head *p;

	if (!cache || !type || !value)
		return NULL;

	DBG(printf("looking for %s=%s in cache\n", type, value));
	
try_again:
	if (!head)
		head = blkid_find_head_cache(cache, type);

	found = 0;
	list_for_each(p, &head->bit_names) {
		blkid_tag tmp = list_entry(p, struct blkid_struct_tag, 
					   bit_names);

		if (!strcmp(tmp->bit_val, value)) {
			found = tmp;
			break;
		}
	}

	if ((!head || !found) && !(cache->bic_flags & BLKID_BIC_FL_PROBED)) {
		blkid_probe_all(&cache);
		goto try_again;
	}
	return (found ? found->bit_dev : NULL);
}
