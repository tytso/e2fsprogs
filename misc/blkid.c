/*
 * blkid.c - User command-line interface for libblkid
 *
 * Copyright (C) 2001 Andreas Dilger
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 * %End-Header%
 */

#include <stdio.h>
#ifdef HAVE_GETOPT_H
#include <getopt.h>
#else
extern char *optarg;
extern int optind;
#endif

#include "blkid/blkid.h"

char *progname = "blkid";
void print_version(FILE *out)
{
	fprintf(stderr, "%s %s (%s)\n", progname, BLKID_VERSION, BLKID_DATE);
}

void usage(int error)
{
	FILE *out = error ? stderr : stdout;

	print_version(out);
	fprintf(out,
		"usage:\t%s [-c <file>] [-h] "
		"[-p] [-s <tag>] [-t <token>] [-v] [-w <file>] [dev ...]\n"
		"\t-c\tcache file (default: /etc/blkid.tab, /dev/null = none)\n"
		"\t-h\tprint this usage message and exit\n"
		"\t-s\tshow specified tag(s) (default show all tags)\n"
		"\t-t\tfind device with a specific token (NAME=value pair)\n"
		"\t-v\tprint version and exit\n"
		"\t-w\twrite cache to different file (/dev/null = no write)\n"
		"\tdev\tspecify device(s) to probe (default: all devices)\n",
		progname);
	exit(error);
}

#define PT_FL_START	0x0001
#define PT_FL_TYPE	0x0002

static void print_tag(blkid_dev *dev, blkid_tag *tag, int *flags)
{
	/* Print only one "dev:" per device */
	if (!*flags & PT_FL_START) {
		printf("%s: ", dev->bid_name);
		*flags |= PT_FL_START;
	}
	/* Print only the primary TYPE per device */
	if (!strcmp(tag->bit_name, "TYPE")) {
		if (*flags & PT_FL_TYPE)
			return;
		*flags |= PT_FL_TYPE;
	}
	printf("%s=\"%s\" ", tag->bit_name, tag->bit_val);
}

void print_tags(blkid_dev *dev, char *show[], int numtag)
{
	struct list_head *p;
	int flags = 0;

	if (!dev)
		return;

	list_for_each(p, &dev->bid_tags) {
		blkid_tag *tag = list_entry(p, blkid_tag, bit_tags);
		int i;

		/* Print all tokens if none is specified */
		if (numtag == 0 || !show) {
			print_tag(dev, tag, &flags);
		/* Otherwise, only print specific tokens */
		} else for (i = 0; i < numtag; i++) {
			if (!strcmp(tag->bit_name, show[i]))
				print_tag(dev, tag, &flags);
		}
	}

	if (flags)
		printf("\n");
}

int main(int argc, char **argv)
{
	blkid_cache *cache = NULL;
	char *devices[128] = { NULL, };
	char *show[128] = { NULL, };
	blkid_tag *tag = NULL;
	char *read = NULL;
	char *write = NULL;
	int numdev = 0, numtag = 0;
	int version = 0;
	int err = 4;
	int i;
	char c;

	while ((c = getopt (argc, argv, "c:d:f:hps:t:w:v")) != EOF)
		switch (c) {
		case 'd':	/* deprecated */
			if (numdev >= sizeof(devices) / sizeof(*devices)) {
				fprintf(stderr,
					"Too many devices specified\n");
				usage(err);
			}
			devices[numdev++] = optarg;
			break;
		case 'c':
			if (optarg && !*optarg)
				read = NULL;
			else
				read = optarg;
			if (!write)
				write = read;
			break;
		case 's':
			if (numtag >= sizeof(show) / sizeof(*show)) {
				fprintf(stderr, "Too many tags specified\n");
				usage(err);
			}
			show[numtag++] = optarg;
			break;
		case 't':
			if (tag) {
				fprintf(stderr, "Can only search for "
						"one NAME=value pair\n");
				usage(err);
			}
			if (!(tag = blkid_token_to_tag(optarg))) {
				fprintf(stderr, "-t needs NAME=value pair\n");
				usage(err);
			}
			break;
		case 'v':
			version = 1;
			break;
		case 'w':
			if (optarg && !*optarg)
				write = NULL;
			else
				write = optarg;
			break;
		case 'h':
			err = 0;
		default:
			usage(err);
		}

	while (optind < argc)
		devices[numdev++] = argv[optind++];

	if (version) {
		print_version(stdout);
		goto exit;
	}

	if (blkid_read_cache(&cache, read) < 0)
		goto exit;

	err = 2;
	/* If looking for a specific NAME=value pair, print only that */
	if (tag) {
		blkid_tag *found = NULL;

		/* Load any additional devices not in the cache */
		for (i = 0; i < numdev; i++)
			blkid_get_devname(cache, devices[i]);

		if ((found = blkid_get_tag_cache(cache, tag))) {
			print_tags(found->bit_dev, show, numtag);
			err = 0;
		}
	/* If we didn't specify a single device, show all available devices */
	} else if (!numdev) {
		struct list_head *p;

		blkid_probe_all(&cache);

		list_for_each(p, &cache->bic_devs) {
			blkid_dev *dev = list_entry(p, blkid_dev, bid_devs);
			print_tags(dev, show, numtag);
			err = 0;
		}
	/* Add all specified devices to cache (optionally display tags) */
	} else for (i = 0; i < numdev; i++) {
		blkid_dev *dev = blkid_get_devname(cache, devices[i]);

		if (dev) {
			print_tags(dev, show, numtag);
			err = 0;
		}
	}

exit:
	blkid_free_tag(tag);
	blkid_save_cache(cache, write);
	blkid_free_cache(cache);
	return err;
}
