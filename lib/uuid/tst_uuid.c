/*
 * tst_uuid.c --- test program from the UUID library
 *
 * Copyright (C) 1996, 1997, 1998 Theodore Ts'o.
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 */

#include <stdio.h>
#include <linux/ext2_fs.h>

#include "uuid.h"

int
main(int argc, char **argv)
{
	uuid_t		buf, tst;
	char		str[100];
	struct timeval	tv;
	time_t		time_reg;
	unsigned char	*cp;
	int i;
	int failed = 0;

	uuid_generate(buf);
	uuid_unparse(buf, str);
	printf("UUID string = %s\n", str);
	printf("UUID: ");
	for (i=0, cp = (unsigned char *) &buf; i < 16; i++) {
		printf("%02x", *cp++);
	}
	printf("\n");
	tv.tv_sec = 0;
	tv.tv_usec = 0;
	time_reg = uuid_time(buf, &tv);
	printf("UUID time is: (%d, %d): %s\n", tv.tv_sec, tv.tv_usec,
	       ctime(&time_reg));
	uuid_parse(str, tst);
	if (uuid_compare(buf, tst))
		printf("UUID parse and compare succeeded.\n");
	else {
		printf("UUID parse and compare failed!\n");
		failed++;
	}
	uuid_clear(tst);
	if (uuid_is_null(tst))
		printf("UUID clear and is null succeeded.\n");
	else {
		printf("UUID clear and is null failed!\n");
		failed++;
	}
	uuid_copy(buf, tst);
	if (uuid_compare(buf, tst))
		printf("UUID copy and compare succeeded.\n");
	else {
		printf("UUID copy and compare failed!\n");
		failed++;
	}
	if (failed) {
		printf("%d failures.\n", failed);
		exit(1);
	}
	return 0;
}

	

