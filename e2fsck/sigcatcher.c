/*
 * sigcatcher.c --- print a backtrace on a SIGSEGV, et. al
 *
 * Copyright (C) 2011 Theodore Ts'o.
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#ifdef HAVE_EXECINFO_H
#include <execinfo.h>
#endif

#include "e2fsck.h"

struct str_table {
	int	num;
	const char	*name;
};

#define DEFINE_ENTRY(SYM)	{ SYM, #SYM },
#define END_TABLE		{ 0, 0 }

static struct str_table sig_table[] = {
	DEFINE_ENTRY(SIGHUP)
	DEFINE_ENTRY(SIGINT)
	DEFINE_ENTRY(SIGQUIT)
	DEFINE_ENTRY(SIGILL)
	DEFINE_ENTRY(SIGTRAP)
	DEFINE_ENTRY(SIGABRT)
	DEFINE_ENTRY(SIGIOT)
	DEFINE_ENTRY(SIGBUS)
	DEFINE_ENTRY(SIGFPE)
	DEFINE_ENTRY(SIGKILL)
	DEFINE_ENTRY(SIGUSR1)
	DEFINE_ENTRY(SIGSEGV)
	DEFINE_ENTRY(SIGUSR2)
	DEFINE_ENTRY(SIGPIPE)
	DEFINE_ENTRY(SIGALRM)
	DEFINE_ENTRY(SIGTERM)
	DEFINE_ENTRY(SIGSTKFLT)
	DEFINE_ENTRY(SIGCHLD)
	DEFINE_ENTRY(SIGCONT)
	DEFINE_ENTRY(SIGSTOP)
	DEFINE_ENTRY(SIGTSTP)
	DEFINE_ENTRY(SIGTTIN)
	DEFINE_ENTRY(SIGTTOU)
	DEFINE_ENTRY(SIGURG)
	DEFINE_ENTRY(SIGXCPU)
	DEFINE_ENTRY(SIGXFSZ)
	DEFINE_ENTRY(SIGVTALRM)
	DEFINE_ENTRY(SIGPROF)
	DEFINE_ENTRY(SIGWINCH)
	DEFINE_ENTRY(SIGIO)
	DEFINE_ENTRY(SIGPOLL)
	DEFINE_ENTRY(SIGPWR)
	DEFINE_ENTRY(SIGSYS)
	END_TABLE
};

static struct str_table generic_code_table[] = {
	DEFINE_ENTRY(SI_ASYNCNL)
	DEFINE_ENTRY(SI_TKILL)
	DEFINE_ENTRY(SI_SIGIO)
	DEFINE_ENTRY(SI_ASYNCIO)
	DEFINE_ENTRY(SI_MESGQ)
	DEFINE_ENTRY(SI_TIMER)
	DEFINE_ENTRY(SI_QUEUE)
	DEFINE_ENTRY(SI_USER)
	DEFINE_ENTRY(SI_KERNEL)
	END_TABLE
};

static struct str_table sigill_code_table[] = {
	DEFINE_ENTRY(ILL_ILLOPC)
	DEFINE_ENTRY(ILL_ILLOPN)
	DEFINE_ENTRY(ILL_ILLADR)
	DEFINE_ENTRY(ILL_ILLTRP)
	DEFINE_ENTRY(ILL_PRVOPC)
	DEFINE_ENTRY(ILL_PRVREG)
	DEFINE_ENTRY(ILL_COPROC)
	DEFINE_ENTRY(ILL_BADSTK)
	DEFINE_ENTRY(BUS_ADRALN)
	DEFINE_ENTRY(BUS_ADRERR)
	DEFINE_ENTRY(BUS_OBJERR)
	END_TABLE
};

static struct str_table sigfpe_code_table[] = {
	DEFINE_ENTRY(FPE_INTDIV)
	DEFINE_ENTRY(FPE_INTOVF)
	DEFINE_ENTRY(FPE_FLTDIV)
	DEFINE_ENTRY(FPE_FLTOVF)
	DEFINE_ENTRY(FPE_FLTUND)
	DEFINE_ENTRY(FPE_FLTRES)
	DEFINE_ENTRY(FPE_FLTINV)
	DEFINE_ENTRY(FPE_FLTSUB)
	END_TABLE
};

static struct str_table sigsegv_code_table[] = {
	DEFINE_ENTRY(SEGV_MAPERR)
	DEFINE_ENTRY(SEGV_ACCERR)
	END_TABLE
};


static struct str_table sigbus_code_table[] = {
	DEFINE_ENTRY(BUS_ADRALN)
	DEFINE_ENTRY(BUS_ADRERR)
	DEFINE_ENTRY(BUS_OBJERR)
	END_TABLE
};

static struct str_table sigstrap_code_table[] = {
	DEFINE_ENTRY(TRAP_BRKPT)
	DEFINE_ENTRY(TRAP_TRACE)
	END_TABLE
};

static struct str_table sigcld_code_table[] = {
	DEFINE_ENTRY(CLD_EXITED)
	DEFINE_ENTRY(CLD_KILLED)
	DEFINE_ENTRY(CLD_DUMPED)
	DEFINE_ENTRY(CLD_TRAPPED)
	DEFINE_ENTRY(CLD_STOPPED)
	DEFINE_ENTRY(CLD_CONTINUED)
	END_TABLE
};

static struct str_table sigpoll_code_table[] = {
	DEFINE_ENTRY(POLL_IN)
	DEFINE_ENTRY(POLL_OUT)
	DEFINE_ENTRY(POLL_MSG)
	DEFINE_ENTRY(POLL_ERR)
	DEFINE_ENTRY(POLL_PRI)
	DEFINE_ENTRY(POLL_HUP)
	END_TABLE
};

static const char *lookup_table(int num, struct str_table *table)
{
	struct str_table *p;

	for (p=table; p->name; p++)
		if (num == p->num)
			return(p->name);
	return NULL;
}

static const char *lookup_table_fallback(int num, struct str_table *table)
{
	static char buf[32];
	const char *ret = lookup_table(num, table);

	if (ret)
		return ret;
	snprintf(buf, sizeof(buf), "%d", num);
	buf[sizeof(buf)-1] = 0;
	return buf;
}

static void die_signal_handler(int signum, siginfo_t *siginfo, void *context)
{
       void *stack_syms[32];
       int frames;
       const char *cp;

       fprintf(stderr, "Signal (%d) %s ", signum,
	       lookup_table_fallback(signum, sig_table));
       if (siginfo->si_code == SI_USER)
	       fprintf(stderr, "(sent from pid %u) ", siginfo->si_pid);
       cp = lookup_table(siginfo->si_code, generic_code_table);
       if (cp)
	       fprintf(stderr, "si_code=%s ", cp);
       else if (signum == SIGILL)
	       fprintf(stderr, "si_code=%s ",
		       lookup_table_fallback(siginfo->si_code,
					     sigill_code_table));
       else if (signum == SIGFPE)
	       fprintf(stderr, "si_code=%s ",
		       lookup_table_fallback(siginfo->si_code,
					     sigfpe_code_table));
       else if (signum == SIGSEGV)
	       fprintf(stderr, "si_code=%s ",
		       lookup_table_fallback(siginfo->si_code,
					     sigsegv_code_table));
       else if (signum == SIGBUS)
	       fprintf(stderr, "si_code=%s ",
		       lookup_table_fallback(siginfo->si_code,
					     sigbus_code_table));
       else if (signum == SIGCLD)
	       fprintf(stderr, "si_code=%s ",
		       lookup_table_fallback(siginfo->si_code,
					     sigcld_code_table));
       else
	       fprintf(stderr, "si code=%d ", siginfo->si_code);
       if ((siginfo->si_code != SI_USER) &&
	   (signum == SIGILL || signum == SIGFPE ||
	    signum == SIGSEGV || signum == SIGBUS))
	       fprintf(stderr, "fault addr=%p", siginfo->si_addr);
       fprintf(stderr, "\n");

#ifdef HAVE_BACKTRACE
       frames = backtrace(stack_syms, 32);
       backtrace_symbols_fd(stack_syms, frames, 2);
#endif
       exit(FSCK_ERROR);
}

void sigcatcher_setup(void)
{
	struct sigaction	sa;
	
	memset(&sa, 0, sizeof(struct sigaction));
	sa.sa_sigaction = die_signal_handler;
	sa.sa_flags = SA_SIGINFO;

	sigaction(SIGFPE, &sa, 0);
	sigaction(SIGILL, &sa, 0);
	sigaction(SIGBUS, &sa, 0);
	sigaction(SIGSEGV, &sa, 0);
}	


#ifdef DEBUG
#include <getopt.h>

void usage(void)
{
	fprintf(stderr, "tst_sigcatcher: [-akfn]\n");
	exit(1);
}

int main(int argc, char** argv)
{
	struct sigaction	sa;
	char			*p = 0;
	int 			i, c;
	volatile		x=0;

	memset(&sa, 0, sizeof(struct sigaction));
	sa.sa_sigaction = die_signal_handler;
	sa.sa_flags = SA_SIGINFO;
	for (i=1; i < 31; i++)
		sigaction(i, &sa, 0);

	while ((c = getopt (argc, argv, "afkn")) != EOF)
		switch (c) {
		case 'a':
			abort();
			break;
		case 'f':
			printf("%d\n", 42/x);
		case 'k':
			kill(getpid(), SIGTERM);
			break;
		case 'n':
			*p = 42;
		default:
			usage ();
		}

	printf("Sleeping for 10 seconds, send kill signal to pid %u...\n",
	       getpid());
	fflush(stdout);
	sleep(10);
	exit(0);
}
#endif
