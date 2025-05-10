/*
 * $Header$
 * $Source$
 * $Locker$
 *
 * Copyright 1987 by the Student Information Processing Board
 * of the Massachusetts Institute of Technology
 *
 * Permission to use, copy, modify, and distribute this software and
 * its documentation for any purpose is hereby granted, provided that
 * the names of M.I.T. and the M.I.T. S.I.P.B. not be used in
 * advertising or publicity pertaining to distribution of the software
 * without specific, written prior permission.  M.I.T. and the
 * M.I.T. S.I.P.B. make no representations about the suitability of
 * this software for any purpose.  It is provided "as is" without
 * express or implied warranty.
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifdef HAVE_SYS_AUXV_H
#include <sys/auxv.h> // for getauxval()
#endif
#ifdef HAVE_SYS_PRCTL_H
#include <sys/prctl.h>
#else
#define PR_GET_DUMPABLE 3
#endif
#if (!defined(HAVE_PRCTL) && defined(linux))
#include <sys/syscall.h>
#endif
#ifdef HAVE_SEMAPHORE_H
#include <semaphore.h>
#endif
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#if HAVE_FCNTL
#include <fcntl.h>
#endif
#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#include "com_err.h"
#include "error_table.h"
#include "internal.h"

#ifdef TLS
#define THREAD_LOCAL static TLS
#else
#define THREAD_LOCAL static
#endif

THREAD_LOCAL char buffer[25];

struct et_list * _et_list = (struct et_list *) NULL;
struct et_list * _et_dynamic_list = (struct et_list *) NULL;

#ifdef __GNUC__
#define COMERR_ATTR(x) __attribute__(x)
#else
#define COMERR_ATTR(x)
#endif

#ifdef HAVE_SEM_INIT
static sem_t _et_lock;
static int _et_lock_initialized;

static void COMERR_ATTR((constructor)) setup_et_lock(void)
{
	sem_init(&_et_lock, 0, 1);
	_et_lock_initialized = 1;
}

static void COMERR_ATTR((destructor)) fini_et_lock(void)
{
	sem_destroy(&_et_lock);
	_et_lock_initialized = 0;
}
#endif


int et_list_lock(void)
{
#ifdef HAVE_SEM_INIT
	if (!_et_lock_initialized)
		setup_et_lock();
	return sem_wait(&_et_lock);
#else
	return 0;
#endif
}

int et_list_unlock(void)
{
#ifdef HAVE_SEM_INIT
	if (_et_lock_initialized)
		return sem_post(&_et_lock);
#endif
	return 0;
}

typedef char *(*gettextf) (const char *);

static gettextf com_err_gettext = NULL;

gettextf set_com_err_gettext(gettextf new_proc)
{
    gettextf x = com_err_gettext;

    com_err_gettext = new_proc;

    return x;
}

#ifdef __GNU__
#define SYS_ERR_BASE 0x40000000
#else
#define SYS_ERR_BASE 0
#endif

const char * error_message (errcode_t code)
{
    int offset;
    struct et_list *et;
    errcode_t table_num;
    int started = 0;
    char *cp;

    offset = (int) (code & ((1<<ERRCODE_RANGE)-1));
    table_num = code - offset;
    if (table_num == SYS_ERR_BASE) {
#ifdef HAS_SYS_ERRLIST
	if (code < sys_nerr)
	    return(sys_errlist[code]);
	else
	    goto oops;
#else
	cp = strerror(code);
	if (cp)
	    return(cp);
	else
	    goto oops;
#endif
    }
    et_list_lock();
    for (et = _et_list; et; et = et->next) {
	if ((et->table->base & 0xffffffL) == (table_num & 0xffffffL)) {
	    /* This is the right table */
	    if (et->table->n_msgs <= offset) {
		break;
	    } else {
		const char *msg = et->table->msgs[offset];
		et_list_unlock();
		if (com_err_gettext)
		    return (*com_err_gettext)(msg);
		else
		    return msg;
	    }
	}
    }
    for (et = _et_dynamic_list; et; et = et->next) {
	if ((et->table->base & 0xffffffL) == (table_num & 0xffffffL)) {
	    /* This is the right table */
	    if (et->table->n_msgs <= offset) {
		break;
	    } else {
		const char *msg = et->table->msgs[offset];
		et_list_unlock();
		if (com_err_gettext)
		    return (*com_err_gettext)(msg);
		else
		    return msg;
	    }
	}
    }
    et_list_unlock();
oops:
    strcpy (buffer, "Unknown code ");
    if (table_num) {
	strcat (buffer, error_table_name (table_num));
	strcat (buffer, " ");
    }
    for (cp = buffer; *cp; cp++)
	;
    if (offset >= 100) {
	*cp++ = '0' + offset / 100;
	offset %= 100;
	started++;
    }
    if (started || offset >= 10) {
	*cp++ = '0' + offset / 10;
	offset %= 10;
    }
    *cp++ = '0' + offset;
    *cp = '\0';
    return(buffer);
}

/*
 * This routine will only return a value if the we are not running as
 * a privileged process.
 */
static char *safe_getenv(const char *arg)
{
#if !defined(_WIN32)
#if defined(HAVE_SYS_AUXV_H) && defined(AT_SECURE)
	if (getauxval(AT_SECURE))
		return NULL;
#else
	if ((getuid() != geteuid()) || (getgid() != getegid()))
		return NULL;
#endif
#endif
#if HAVE_PRCTL
	if (prctl(PR_GET_DUMPABLE, 0, 0, 0, 0) == 0)
		return NULL;
#else
#if (defined(linux) && defined(SYS_prctl))
	if (syscall(SYS_prctl, PR_GET_DUMPABLE, 0, 0, 0, 0) == 0)
		return NULL;
#endif
#endif

#if defined(HAVE_SECURE_GETENV)
	return secure_getenv(arg);
#elif defined(HAVE___SECURE_GETENV)
	return __secure_getenv(arg);
#else
	return getenv(arg);
#endif
}

#define DEBUG_INIT	0x8000
#define DEBUG_ADDREMOVE 0x0001

static int debug_mask = 0;
static FILE *debug_f = 0;

static void init_debug(void)
{
	char	*dstr, *fn, *tmp;

	if (debug_mask & DEBUG_INIT)
		return;

	dstr = getenv("COMERR_DEBUG");
	if (dstr) {
		debug_mask = strtoul(dstr, &tmp, 0);
		if (*tmp || errno)
			debug_mask = 0;
	}

	debug_mask |= DEBUG_INIT;
	if (debug_mask == DEBUG_INIT)
		return;

	fn = safe_getenv("COMERR_DEBUG_FILE");
	if (fn)
		debug_f = fopen(fn, "a");
	if (!debug_f)
		debug_f = fopen("/dev/tty", "a");
	if (debug_f) {
#ifdef HAVE_FCNTL
		int fd = fileno(debug_f);

		if (fd >= 0) {
			int flags = fcntl(fd, F_GETFD);

			if (flags >= 0)
				flags = fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
			if (flags < 0) {
				fprintf(debug_f, "Couldn't set FD_CLOEXEC "
					"on debug FILE: %s\n", strerror(errno));
				fclose(debug_f);
				debug_f = NULL;
				debug_mask = DEBUG_INIT;
			}
		}
#endif
	} else
		debug_mask = DEBUG_INIT;
}

/*
 * New interface provided by krb5's com_err library
 */
errcode_t add_error_table(const struct error_table * et)
{
	struct et_list *el;

	if (!(el = (struct et_list *) malloc(sizeof(struct et_list))))
		return ENOMEM;

	if (et_list_lock() != 0) {
		free(el);
		return errno;
	}

	el->table = et;
	el->next = _et_dynamic_list;
	_et_dynamic_list = el;

	init_debug();
	if (debug_mask & DEBUG_ADDREMOVE)
		fprintf(debug_f, "add_error_table: %s (0x%p)\n",
			error_table_name(et->base),
			(const void *) et);

	et_list_unlock();
	return 0;
}

/*
 * New interface provided by krb5's com_err library
 */
errcode_t remove_error_table(const struct error_table * et)
{
	struct et_list *el;
	struct et_list *el2 = 0;

	if (et_list_lock() != 0)
		return ENOENT;

	el = _et_dynamic_list;
	init_debug();
	while (el) {
		if (el->table->base == et->base) {
			if (el2)	/* Not the beginning of the list */
				el2->next = el->next;
			else
				_et_dynamic_list = el->next;
			(void) free(el);
			if (debug_mask & DEBUG_ADDREMOVE)
				fprintf(debug_f,
					"remove_error_table: %s (0x%p)\n",
					error_table_name(et->base),
					(const void *) et);
			et_list_unlock();
			return 0;
		}
		el2 = el;
		el = el->next;
	}
	if (debug_mask & DEBUG_ADDREMOVE)
		fprintf(debug_f, "remove_error_table FAILED: %s (0x%p)\n",
			error_table_name(et->base),
			(const void *) et);
	et_list_unlock();
	return ENOENT;
}

/*
 * Variant of the interface provided by Heimdal's com_err library
 */
void
add_to_error_table(struct et_list *new_table)
{
	add_error_table(new_table->table);
}
