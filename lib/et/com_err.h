/*
 * Header file for common error description library.
 *
 * Copyright 1988, Student Information Processing Board of the
 * Massachusetts Institute of Technology.
 *
 * For copyright and distribution info, see the documentation supplied
 * with this package.
 */

#ifndef __COM_ERR_H

#include <stdarg.h>

typedef long errcode_t;

struct error_table {
	char const * const * msgs;
	long base;
	unsigned int n_msgs;
};

struct et_list;

extern void com_err (const char *, long, const char *, ...);
extern void com_err_va (const char *whoami, errcode_t code, const char *fmt,
		 va_list args);
extern char const *error_message (long);
extern void (*com_err_hook) (const char *, long, const char *, va_list);
extern void (*set_com_err_hook (void (*) (const char *, long, 
					  const char *, va_list)))
	(const char *, long, const char *, va_list);
extern void (*reset_com_err_hook (void)) (const char *, long, 
					  const char *, va_list);
extern int init_error_table(const char * const *msgs, int base, int count);

extern errcode_t add_error_table(const struct error_table * et);
extern errcode_t remove_error_table(const struct error_table * et);
extern void add_to_error_table(struct et_list *new_table);


#define __COM_ERR_H
#endif /* ! defined(__COM_ERR_H) */
