/*
 * uuid_time.c --- Interpret the time field from a uuid
 *
 * Copyright (C) 1998 Theodore Ts'o.
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>
#include <linux/ext2_fs.h>

#include "uuidP.h"

time_t uuid_time(uuid_t uu, struct timeval *ret_tv)
{
	struct uuid		uuid;
	__u32			high;
	struct timeval		tv;
	unsigned long long	clock_reg;

	uuid_unpack(uu, &uuid);
	
	high = uuid.time_mid | ((uuid.time_hi_and_version & 0xFFF) << 16);
	clock_reg = uuid.time_low | ((unsigned long long) high << 32);

	clock_reg -= (((unsigned long long) 0x01B21DD2) << 32) + 0x13814000;
	tv.tv_sec = clock_reg / 10000000;
	tv.tv_usec = (clock_reg % 10000000) / 10;

	if (ret_tv)
		*ret_tv = tv;

	return tv.tv_sec;
}

#ifdef DEBUG
int
main(int argc, char **argv)
{
	uuid_t		buf;
	time_t		time_reg;
	struct timeval	tv;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s uuid\n", argv[0]);
		exit(1);
	}
	if (uuid_parse(argv[1], buf)) {
		fprintf(stderr, "Invalid UUID: %s\n", argv[1]);
		exit(1);
	}
	time_reg = uuid_time(buf, &tv);

	printf("UUID time is: (%d, %d): %s\n", tv.tv_sec, tv.tv_usec,
	       ctime(&time_reg));
	
	return 0;
}
#endif
