/*
 * resolve.c - resolve names and tags into specific devices
 *
 * Copyright (C) 2001 Theodore Ts'o.
 * Copyright (C) 2001 Andreas Dilger
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 * %End-Header%
 */

#include <stdio.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "blkid/blkid.h"
#include "probe.h"

#ifdef DEBUG_RESOLVE
#define DBG(x)	x
#else
#define DBG(x)
#endif


/*
 * Find a tagname (e.g. LABEL or UUID) on a specific device.
 */
char *blkid_get_tagname_devname(blkid_cache *cache, const char *tagname,
				const char *devname)
{
	blkid_tag *tag, *found;
	blkid_dev *dev;
	char *ret = NULL;

	DBG(printf("looking for %s on %s\n", tagname, devname));

	if (!devname)
		return NULL;

	if (blkid_create_tag(NULL, &tag, tagname, NULL, 0) < 0)
		return NULL;

	if (!cache)
		DBG(printf("no cache given, direct device probe\n"));

	if ((dev = blkid_get_devname(cache, devname)) &&
	    (found = blkid_find_tag_dev(dev, tag)))
		ret = string_copy(found->bit_val);

	if (!cache)
		blkid_free_dev(dev);

	return ret;
}

/*
 * Locate a device name from a token (NAME=value string), or (name, value)
 * pair.  In the case of a token, value is ignored.  If the "token" is not
 * of the form "NAME=value" and there is no value given, then it is assumed
 * to be the actual devname and a copy is returned.
 *
 * The string returned must be freed with string_free().
 */
char *blkid_get_token(blkid_cache *cache, const char *token,
		      const char *value)
{
	blkid_tag *tag = NULL, *found = NULL;
	blkid_cache *c = cache;
	char *name = NULL;

	DBG(printf("looking for %s%c%s %s\n", token, value ? '=' : ' ',
		   value ? value : "", cache ? "in cache" : "from disk"));

	if (!(tag = blkid_token_to_tag(token))) {
		if (!value)
			return string_copy(token);
		if (blkid_create_tag(NULL,&tag,token,value,strlen(value)) < 0)
			return NULL;
	}

	if (!cache) {
		if (blkid_read_cache(&c, NULL) < 0)
			c = blkid_new_cache();
		if (!c)
			return NULL;
	}

	if ((found = blkid_get_tag_cache(c, tag)) && found->bit_dev)
		name = string_copy(found->bit_dev->bid_name);

	if (!cache) {
		blkid_save_cache(c, NULL);
		blkid_free_cache(c);
	}


	blkid_free_tag(tag);
	return name;
}

#ifdef TEST_PROGRAM
int main(int argc, char **argv)
{
	char *value;

	if (argc != 2 && argc != 3) {
		fprintf(stderr, "Usage:\t%s tagname=value\n"
			"\t%s tagname devname\n"
			"Find which device holds a given token or\n"
			"Find what the value of a tag is in a device\n",
			argv[0], argv[0]);
		exit(1);
	}
	if (argv[2]) {
		value = blkid_get_tagname_devname(NULL, argv[1], argv[2]);
		printf("%s has tag %s=%s\n", argv[2], argv[1],
		       value ? value : "<missing>");
	} else {
		value = blkid_get_token(NULL, argv[1], NULL);
		printf("%s has tag %s\n", value ? value : "<none>", argv[1]);
	}
	return value ? 0 : 1;
}
#endif
