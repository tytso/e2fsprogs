/*
 * Copyright 1988 by the Student Information Processing Board of the
 * Massachusetts Institute of Technology.
 *
 * This file may be copied under the terms of the GNU Public License.
 */

#ifndef _ET_H
/* Are we using ANSI C? */
#ifndef __STDC__
#define const
#endif

struct error_table {
    char const * const * msgs;
    long base;
    int n_msgs;
};
struct et_list {
    struct et_list *next;
    const struct error_table *table;
};
extern struct et_list * _et_list;

#define	ERRCODE_RANGE	8	/* # of bits to shift table number */
#define	BITS_PER_CHAR	6	/* # bits to shift per character in name */

#ifdef __STDC__
extern const char *error_table_name(errcode_t num);
#else
extern const char *error_table_name();
#endif

#define _ET_H
#endif
