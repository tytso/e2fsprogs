/*
 * logsave.c --- A program which saves the output of a program until
 *	/var/log is mounted.
 *
 * Copyright (C) 2003 Theodore Ts'o.
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#ifdef HAVE_GETOPT_H
#include <getopt.h>
#else
extern char *optarg;
extern int optind;
#endif

int	outfd = -1;
int	outbufsize = 0;
void	*outbuf = 0;
int	verbose = 0;

static void usage(char *progname)
{
	printf("Usage: %s [-v] [-d dir] logfile program\n", progname);
	exit(1);
}

static void process_output(const char *buffer, int c)
{
	char	*n;
	
	if (c == 0)
		c = strlen(buffer);
	
	write(1, buffer, c);
	if (outfd > 0)
		write(outfd, buffer, c);
	else {
		n = realloc(outbuf, outbufsize + c);
		if (n) {
			outbuf = n;
			memcpy(((char *)outbuf)+outbufsize, buffer, c);
			outbufsize += c;
		}
	}
}

static void do_read(int fd)
{
	int	c;
	char	buffer[4096];

	c = read(fd, buffer, sizeof(buffer));
	process_output(buffer, c);
}

static int run_program(char **argv)
{
	int	fds[2];
	char	**cpp;
	int	status, rc, pid;
	char	buffer[80];
	time_t	t;

	if (pipe(fds) < 0) {
		perror("pipe");
		exit(1);
	}

	if (verbose) {
		process_output("Log of ", 0);
		for (cpp = argv; *cpp; cpp++) {
			process_output(*cpp, 0);
			process_output(" ", 0);
		}
		process_output("\n", 0);
		t = time(0);
		process_output(ctime(&t), 0);
		process_output("\n", 0);
	}
	
	pid = fork();
	if (pid < 0) {
		perror("vfork");
		exit(1);
	}
	if (pid == 0) {
		dup2(fds[1],1);		/* fds[1] replaces stdout */
		dup2(fds[1],2);  	/* fds[1] replaces stderr */
		close(fds[0]);	/* don't need this here */
		
		execvp(argv[0], argv);
		perror(argv[0]);
		exit(1);
	}
	close(fds[1]);

	while (!(waitpid(pid, &status, WNOHANG ))) {
		do_read(fds[0]);
	}
	do_read(fds[0]);
	close(fds[0]);

	if ( WIFEXITED(status) ) {
		rc = WEXITSTATUS(status);
		if (rc) {
			process_output(argv[0], 0);
			sprintf(buffer, " died with exit status %d", rc);
			process_output(buffer, 0);
		}
	} else {
		if (WIFSIGNALED(status)) {
			process_output(argv[0], 0);
			sprintf(buffer, "died with signal %d",
				WTERMSIG(status));
			process_output(buffer, 0);
			rc = 1;
		}
		rc = 0;
	}
	return rc;
}

static int copy_from_stdin(void)
{
	char	buffer[4096];
	int	c;
	int	bad_read = 0;

	while (1) {
		c = read(0, buffer, sizeof(buffer));
		if ((c == 0 ) ||
		    ((c < 0) && ((errno == EAGAIN) || (errno == EINTR)))) {
			if (bad_read++ > 3)
				break;
			continue;
		}
		if (c < 0) {
			perror("read");
			exit(1);
		}
		process_output(buffer, c);
		bad_read = 0;
	}
	return 0;
}	
	


int main(int argc, char **argv)
{
	int	c, pid, rc;
	char	*outfn;
	int	openflags = O_CREAT|O_WRONLY|O_TRUNC;
	
	while ((c = getopt(argc, argv, "+v")) != EOF) {
		switch (c) {
		case 'a':
			openflags &= ~O_TRUNC;
			openflags |= O_APPEND;
			break;
		case 'v':
			verbose++;
			break;
		}
	}
	if (optind == argc || optind+1 == argc)
		usage(argv[0]);
	outfn = argv[optind];
	optind++;
	argv += optind;
	argc -= optind;

	outfd = open(outfn, O_CREAT|O_WRONLY|O_TRUNC, 0644);

	if (strcmp(argv[0], "-"))
		rc = run_program(argv);
	else
		rc = copy_from_stdin();
	
	if (outbuf) {
		pid = fork();
		if (pid < 0) {
			perror("fork");
			exit(1);
		}
		if (pid) {
			if (verbose)
				printf("Backgrounding to save %s later\n",
				       outfn);
			exit(rc);
		}
		while (outfd < 0) {
			outfd = open(outfn, O_CREAT|O_WRONLY|O_TRUNC, 0644);
			sleep(1);
		} 
		write(outfd, outbuf, outbufsize);
		free(outbuf);
	}
	close(outfd);
	
	exit(rc);
}
