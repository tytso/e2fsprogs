/*
 * read.c - read the blkid cache from disk, to avoid scanning all devices
 *
 * Copyright (C) 2001 Theodore Y. Ts'o
 * Copyright (C) 2001 Andreas Dilger
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 * %End-Header%
 */

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#if HAVE_ERRNO_H
#include <errno.h>
#endif

#include "blkidP.h"
#include "uuid/uuid.h"

#ifdef DEBUG_CACHE
#define DBG(x)	x
#else
#define DBG(x)
#endif

#ifdef HAVE_STRTOULL
#define __USE_ISOC9X
#define STRTOULL strtoull /* defined in stdlib.h if you try hard enough */
#else
/* FIXME: need to support real strtoull here */
#define STRTOULL strtoul
#endif

#if HAVE_STDLIB_H
#include <stdlib.h>
#endif

/*
 * File format:
 *
 *	<device [<NAME="value"> ...]>device_name</device>
 *
 *	The following tags are required for each entry:
 *	<ID="id">	unique (within this file) ID number of this device
 *	<TIME="time">	(ascii time_t) time this entry was last read from disk
 *	<TYPE="type">	(detected) type of filesystem/data for this partition
 *
 *	The following tags may be present, depending on the device contents
 *	<LABEL="label">	(user supplied) label (volume name, etc)
 *	<UUID="uuid">	(generated) universally unique identifier (serial no)
 */

static char *skip_over_blank(char *cp)
{
	while (*cp && isspace(*cp))
		cp++;
	return cp;
}

static char *skip_over_word(char *cp)
{
	char ch;

	while ((ch = *cp)) {
		/* If we see a backslash, skip the next character */
		if (ch == '\\') {
			cp++;
			if (*cp == '\0')
				break;
			cp++;
			continue;
		}
		if (isspace(ch) || ch == '<' || ch == '>')
			break;
		cp++;
	}
	return cp;
}

static char *strip_line(char *line)
{
	char	*p;

	line = skip_over_blank(line);

	p = line + strlen(line) - 1;

	while (*line) {
		if (isspace(*p))
			*p-- = '\0';
		else
			break;
	}

	return line;
}

#if 0
static char *parse_word(char **buf)
{
	char *word, *next;

	word = *buf;
	if (*word == '\0')
		return NULL;

	word = skip_over_blank(word);
	next = skip_over_word(word);
	if (*next) {
		char *end = next - 1;
		if (*end == '"' || *end == '\'')
			*end = '\0';
		*next++ = '\0';
	}
	*buf = next;

	if (*word == '"' || *word == '\'')
		word++;
	return word;
}
#endif

/*
 * Start parsing a new line from the cache.
 *
 * line starts with "<device" return 1 -> continue parsing line
 * line starts with "<foo", empty, or # return 0 -> skip line
 * line starts with other, return -BLKID_ERR_CACHE -> error
 */
static int parse_start(char **cp)
{
	char *p;

	p = strip_line(*cp);

	/* Skip comment or blank lines.  We can't just NUL the first '#' char,
	 * in case it is inside quotes, or escaped.
	 */
	if (*p == '\0' || *p == '#')
		return 0;

	if (!strncmp(p, "<device", 7)) {
		DBG(printf("found device header: %8s\n", p));
		p += 7;

		*cp = p;
		return 1;
	}

	if (*p == '<')
		return 0;

	return -BLKID_ERR_CACHE;
}

/* Consume the remaining XML on the line (cosmetic only) */
static int parse_end(char **cp)
{
	*cp = skip_over_blank(*cp);

	if (!strncmp(*cp, "</device>", 9)) {
		DBG(printf("found device trailer %9s\n", *cp));
		*cp += 9;
		return 0;
	}

	return -BLKID_ERR_CACHE;
}

/*
 * Allocate a new device struct with device name filled in.  Will handle
 * finding the device on lines of the form:
 * <device foo=bar>devname</device>
 * <device>devname<foo>bar</foo></device>
 */
static int parse_dev(blkid_dev *dev, char **cp)
{
	char **name;
	char *start, *tmp, *end;
	int ret;

	if ((ret = parse_start(cp)) <= 0)
		return ret;

	start = tmp = strchr(*cp, '>');
	if (!start) {
		fprintf(stderr, "blkid: short line parsing dev: %s\n", *cp);
		return -BLKID_ERR_CACHE;
	}
	start = skip_over_blank(start + 1);
	end = skip_over_word(start);

	DBG(printf("device should be %*s\n", end - start, start));

	if (**cp == '>')
		*cp = end;
	else
		(*cp)++;

	*tmp = '\0';

	if (!(tmp = strrchr(end, '<')) || parse_end(&tmp) < 0)
		fprintf(stderr, "blkid: missing </device> ending: %s\n", end);
	else if (tmp)
		*tmp = '\0';

	if (end - start <= 1) {
		fprintf(stderr, "blkid: empty device name: %s\n", *cp);
		return -BLKID_ERR_CACHE;
	}

	if (!(*dev = blkid_new_dev()))
		return -BLKID_ERR_MEM;

	name = &(*dev)->bid_name;
	*name = (char *)malloc(end - start + 1);
	if (*name == NULL) {
		blkid_free_dev(*dev);
		return -BLKID_ERR_MEM;
	}

	memcpy(*name, start, end - start);
	(*name)[end - start] = '\0';

	DBG(printf("found dev %s\n", *name));

	return 1;
}

/*
 * Extract a tag of the form NAME="value" from the line.
 */
static int parse_token(char **name, char **value, char **cp)
{
	char *end;

	if (!name || !value || !cp)
		return -BLKID_ERR_PARAM;

	if (!(*value = strchr(*cp, '=')))
		return 0;

	**value = '\0';
	*name = strip_line(*cp);
	*value = skip_over_blank(*value + 1);

	if (**value == '"') {
		end = strchr(*value + 1, '"');
		if (!end) {
			fprintf(stderr, "unbalanced quotes at: %s\n", *value);
			*cp = *value;
			return -BLKID_ERR_CACHE;
		}
		(*value)++;
		*end = '\0';
		end++;
	} else {
		end = skip_over_word(*value);
		if (*end) {
			*end = '\0';
			end++;
		}
	}
	*cp = end;

	return 1;
}

/*
 * Extract a tag of the form <NAME>value</NAME> from the line.
 */
/*
static int parse_xml(char **name, char **value, char **cp)
{
	char *end;

	if (!name || !value || !cp)
		return -BLKID_ERR_PARAM;

	*name = strip_line(*cp);

	if ((*name)[0] != '<' || (*name)[1] == '/')
		return 0;

	FIXME: finish this.
}
*/

/*
 * Extract a tag from the line.
 *
 * Return 1 if a valid tag was found.
 * Return 0 if no tag found.
 * Return -ve error code.
 */
static int parse_tag(blkid_cache cache, blkid_dev dev, blkid_tag *tag,
		     char **cp)
{
	char *name;
	char *value;
	int ret;

	if (!cache || !dev)
		return -BLKID_ERR_PARAM;

	*tag = NULL;

	if ((ret = parse_token(&name, &value, cp)) <= 0 /* &&
	    (ret = parse_xml(&name, &value, cp)) <= 0 */)
		return ret;

	/* Some tags are stored directly in the device struct */
	if (!strcmp(name, "ID")) {
		dev->bid_id = (unsigned int)strtoul(value, 0, 0);
		if (dev->bid_id > cache->bic_idmax)
			cache->bic_idmax = dev->bid_id;
	} else if (!strcmp(name, "DEVNO"))
		dev->bid_devno = STRTOULL(value, 0, 0);
	else if (!strcmp(name, "DEVSIZE"))
		dev->bid_devno = STRTOULL(value, 0, 0);
	else if (!strcmp(name, "TIME"))
		/* FIXME: need to parse a long long eventually */
		dev->bid_time = strtol(value, 0, 0);
	else
		ret = blkid_create_tag(dev, tag, name, value, strlen(value));

	return ret < 0 ? ret : 1;
}

/*
 * Parse a single line of data, and return a newly allocated dev struct.
 * Add the new device to the cache struct, if one was read.
 *
 * Lines are of the form <device [TAG="value" ...]>/dev/foo</device>
 *
 * Returns -ve value on error.
 * Returns 0 otherwise.
 * If a valid device was read, *dev_p is non-NULL, otherwise it is NULL
 * (e.g. comment lines, unknown XML content, etc).
 */
static int blkid_parse_line(blkid_cache cache, blkid_dev *dev_p, char *cp)
{
	blkid_dev dev;
	blkid_tag tag;
	int ret;

	if (!cache || !dev_p)
		return -BLKID_ERR_PARAM;

	*dev_p = NULL;

	DBG(printf("line: %s\n", cp));

	if ((ret = parse_dev(dev_p, &cp)) <= 0)
		return ret;

	dev = *dev_p;

	while ((ret = parse_tag(cache, dev, &tag, &cp)) > 0) {
		/* Added to tags for this device struct already */
		DEB_DUMP_TAG(tag);
	}

	if (dev->bid_type == NULL) {
		fprintf(stderr, "blkid: device %s has no TYPE\n",dev->bid_name);
		blkid_free_dev(dev);
	}

	DEB_DUMP_DEV(dev);

	*dev_p = blkid_add_dev_to_cache(cache, dev);

	return ret;
}

/*
 * Read the given file stream for cached device data, and return it
 * in a newly allocated cache struct.
 *
 * Returns 0 on success, or -ve error value.
 */
int blkid_read_cache_file(blkid_cache *cache, FILE *file)
{
	char buf[4096];
	int lineno = 0;

	if (!file || !cache)
		return -BLKID_ERR_PARAM;

	if (!*cache)
		*cache = blkid_new_cache();

	if (!*cache)
		return -BLKID_ERR_MEM;

	while (fgets(buf, sizeof(buf), file)) {
		blkid_dev dev;

		int end = strlen(buf) - 1;

		lineno++;
		/* Continue reading next line if it ends with a backslash */
		while (buf[end] == '\\' && end < sizeof(buf) - 2 &&
		       fgets(buf + end, sizeof(buf) - end, stdin)) {
			end = strlen(buf) - 1;
			lineno++;
		}

		if (blkid_parse_line(*cache, &dev, buf) < 0) {
			fprintf(stderr, "blkid: bad format on line %d\n",
				lineno);
			continue;
		}
	}

	/*
	 * Initially assume that we do not need to write out the cache file.
	 * This would be incorrect if we probed first, and parsed the cache
	 * afterwards, or parsed two caches and wanted to write it out, but
	 * the alternative is to force manually marking the cache dirty when
	 * any device is added, and that is also prone to error.
	 */
	(*cache)->bic_flags &= ~BLKID_BIC_FL_CHANGED;

	return 0;
}

/*
 * Parse the specified filename, and return the data in the supplied or
 * a newly allocated cache struct.  If the file doesn't exist, return a
 * new empty cache struct.
 */
int blkid_read_cache(blkid_cache *cache, const char *filename)
{
	FILE *file;
	int ret;

	if (!cache)
		return -BLKID_ERR_PARAM;

	if (!filename || !strlen(filename))
		filename = BLKID_CACHE_FILE;

	DBG(printf("cache file %s\n", filename));

	/* If we read the standard cache file, do not do so again */
	if (!strcmp(filename, BLKID_CACHE_FILE) && (*cache) &&
	    ((*cache)->bic_flags & BLKID_BIC_FL_PARSED))
		return 0;

	if (!strcmp(filename, "-") || !strcmp(filename, "stdin"))
		file = stdin;
	else {
		/*
		 * If the file doesn't exist, then we just return an empty
		 * struct so that the cache can be populated.
		 */
		if (access(filename, R_OK) < 0) {
			*cache = blkid_new_cache();

			return *cache ? 0 : -BLKID_ERR_MEM;
		}

		file = fopen(filename, "r");
		if (!file) {
			perror(filename);
			return errno;
		}
	}

	ret = blkid_read_cache_file(cache, file);

	if (file != stdin)
		fclose(file);

	/* Mark us as having read the standard cache file */
	if (!strcmp(filename, BLKID_CACHE_FILE))
		(*cache)->bic_flags |= BLKID_BIC_FL_PARSED;

	return ret;
}

#ifdef TEST_PROGRAM
int main(int argc, char**argv)
{
	blkid_cache cache = NULL;
	int ret;

	if (argc > 2) {
		fprintf(stderr, "Usage: %s [filename]\n"
			"Test parsing of the cache (filename)\n", argv[0]);
		exit(1);
	}
	if ((ret = blkid_read_cache(&cache, argv[1])) < 0)
		fprintf(stderr, "error %d reading cache file %s\n", ret,
			argv[1] ? argv[1] : BLKID_CACHE_FILE);

	blkid_free_cache(cache);

	return ret;
}
#endif
