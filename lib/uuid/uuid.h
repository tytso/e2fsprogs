/*
 * Public include file for the UUID library
 * 
 * Copyright (C) 1996, 1997, 1998 Theodore Ts'o.
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU 
 * Library General Public License.
 * %End-Header%
 */

#include <sys/types.h>
#include <sys/time.h>
#include <time.h>

typedef unsigned char uuid_t[16];

/* UUID Variant definitions */
#define UUID_VARIANT_NCS 	0
#define UUID_VARIANT_DCE 	1
#define UUID_VARIANT_MICROSOFT	2
#define UUID_VARIANT_OTHER	3

/* clear.c */
void uuid_clear(uuid_t uu);

/* compare.c */
int uuid_compare(uuid_t uu1, uuid_t uu2);

/* copy.c */
void uuid_copy(uuid_t uu1, uuid_t uu2);

/* gen_uuid.c */
void uuid_generate(uuid_t out);
void uuid_generate_random(uuid_t out);
void uuid_generate_time(uuid_t out);

/* isnull.c */
int uuid_is_null(uuid_t uu);

/* parse.c */
int uuid_parse(char *in, uuid_t uu);

/* unparse.c */
void uuid_unparse(uuid_t uu, char *out);

/* uuid_time.c */
time_t uuid_time(uuid_t uu, struct timeval *ret_tv);
int uuid_type(uuid_t uu);
int uuid_variant(uuid_t uu);
