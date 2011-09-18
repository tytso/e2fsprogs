/*
 * Common things for all utilities
 *
 * Jan Kara <jack@suse.cz> - sponsored by SuSE CR
 *
 * Jani Jaakkola <jjaakkol@cs.helsinki.fi> - syslog support
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "common.h"

void *smalloc(size_t size)
{
	void *ret = malloc(size);

	if (!ret) {
		fputs("Not enough memory.\n", stderr);
		exit(3);
	}
	return ret;
}

void *srealloc(void *ptr, size_t size)
{
	void *ret = realloc(ptr, size);

	if (!ret) {
		fputs("Not enough memory.\n", stderr);
		exit(3);
	}
	return ret;
}

void sstrncpy(char *d, const char *s, size_t len)
{
	strncpy(d, s, len);
	d[len - 1] = 0;
}

void sstrncat(char *d, const char *s, size_t len)
{
	strncat(d, s, len);
	d[len - 1] = 0;
}

char *sstrdup(const char *s)
{
	char *r = strdup(s);

	if (!r) {
		puts("Not enough memory.");
		exit(3);
	}
	return r;
}
