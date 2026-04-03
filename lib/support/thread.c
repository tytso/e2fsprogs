/*
 * thread.c - utility functions for Posix threads
 */

#include "config.h"
#ifdef HAVE_PTHREAD
#include <pthread.h>
#endif
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include "support/thread.h"

uint64_t get_thread_id(void)
{
#if defined(HAVE_GETTID)
	return gettid();
#elif defined(HAVE_PTHREAD_THREADID_NP)
	uint64_t tid;

	if (pthread_threadid_np(NULL, &tid))
		return tid;
#elif defined(HAVE_PTHREAD)
	return (__u64)(uintptr_t) pthread_self();
#endif
	return getpid();
}

#ifdef DEBUG_PROGRAM
int main(int argc, char **argv)
{
	printf("Thread id: %llu\n", get_thread_id());
	return 0;
}
#endif
