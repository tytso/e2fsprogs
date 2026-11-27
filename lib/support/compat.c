/*
 * compat.c --- compatibilty functions
 *
 * Theodore Ts'o <tytso@mit.edu>
 */

#include "compat.h"
#include <fcntl.h>
#include <stdlib.h>

int compat_daemon(int nochdir, int noclose)
{
#if defined(HAVE_DAEMON) && !defined(__APPLE__)
	return daemon(nochdir, noclose);
#elif defined(_WIN32)
	/* No fork() or setsid() on Windows */
	errno = ENOSYS;
	return -1;
#else
	pid_t pid;

	pid = fork();
	if (pid == -1) {
		return -1;
	} else if (pid != 0) {
	    exit(0);
	}
	if (setsid() < 0)
		return -1;

	if (!noclose) {
		close(0);
		close(1);
		close(2);
		open("/dev/null", O_RDWR);
		open("/dev/null", O_RDWR);
		open("/dev/null", O_RDWR);
	}

	if (!nochdir) {
		if (chdir("/")) {}	/* Silence warn_unused_result warning */
	}
	return 0;
#endif
}
