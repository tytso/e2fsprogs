/*
 * $Header$
 * $Source$
 * $Locker$
 *
 * Copyright 1986, 1987, 1988 by MIT Information Systems and
 *	the MIT Student Information Processing Board.
 *
 * For copyright info, see mit-sipb-copyright.h.
 */

#include <stdio.h>
#include <errno.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#include "com_err.h"
#include "error_table.h"
#include "mit-sipb-copyright.h"

#ifndef __STDC__
#define const
#endif

struct foobar {
    struct et_list etl;
    struct error_table et;
};

extern struct et_list * _et_list;

#ifdef __STDC__
int init_error_table(const char * const *msgs, int base, int count)
#else
int init_error_table(msgs, base, count)
    const char * const * msgs;
    int base;
    int count;
#endif
{
    struct foobar * new_et;

    if (!base || !count || !msgs)
	return 0;

    new_et = (struct foobar *) malloc(sizeof(struct foobar));
    if (!new_et)
	return ENOMEM;	/* oops */
    new_et->etl.table = &new_et->et;
    new_et->et.msgs = msgs;
    new_et->et.base = base;
    new_et->et.n_msgs= count;

    new_et->etl.next = _et_list;
    _et_list = &new_et->etl;
    return 0;
}
