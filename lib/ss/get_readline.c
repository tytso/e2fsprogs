/*
 * Copyright 2003 by MIT Student Information Processing Board
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

#ifdef HAS_STDLIB_H
#include <stdlib.h>
#endif
#include "ss_internal.h"
#define	size	sizeof(ss_data *)
#ifdef HAVE_DLOPEN
#include <dlfcn.h>
#endif

static void ss_release_readline(ss_data *info)
{
#ifdef HAVE_DLOPEN
	if (!info->readline_handle)
		return;
	
	info->readline = 0;
	info->add_history = 0;
	info->redisplay = 0;
	info->rl_completion_matches = 0;
	dlclose(info->readline_handle);
	info->readline_handle = 0;
#endif
}

void ss_get_readline(int sci_idx)
{
#ifdef HAVE_DLOPEN
	void	*handle;
	ss_data *info = ss_info(sci_idx);
	const char **t;
	char **(**completion_func)(const char *, int, int);
	
	if (info->readline_handle ||
	    getenv("SS_NO_READLINE") ||
	    ((handle = dlopen("libreadline.so", RTLD_NOW)) == NULL))
		return;

	info->readline_handle = handle;
	info->readline = (char *(*)(const char *))
		dlsym(handle, "readline");
	info->add_history = (void (*)(const char *))
		dlsym(handle, "add_history");
	info->redisplay = (void (*)(void))
		dlsym(handle, "rl_forced_update_display");
	info->rl_completion_matches = (char **(*)(const char *,
				    char *(*)(const char *, int)))
		dlsym(handle, "rl_completion_matches");
	if ((t = dlsym(handle, "rl_readline_name")) != NULL)
		*t = info->subsystem_name;
	if ((completion_func =
	     dlsym(handle, "rl_attempted_completion_function")) != NULL)
		*completion_func = ss_rl_completion;
	info->readline_shutdown = ss_release_readline;
#endif
}

	
