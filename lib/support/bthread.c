/*
 * bthread.c - Background thread manager
 *
 * Copyright (C) 2025-2026 Oracle.
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 */
#include "config.h"
#ifdef HAVE_PTHREAD
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include <string.h>

#include "support/bthread.h"

enum bthread_state {
	/* waiting to be put in the running state */
	BT_WAITING,
	/* running */
	BT_RUNNING,
	/* cancelled */
	BT_CANCELLED,
};

struct bthread {
	enum bthread_state state;
	pthread_t thread;
	pthread_mutex_t lock;
	pthread_cond_t cond;
	bthread_fn_t fn;
	void *data;
	unsigned int period; /* seconds */
	unsigned int can_join:1;
};

/* Wait for a signal or for the periodic interval */
static inline int bthread_wait(struct bthread *bt)
{
	struct timespec ts;

	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_sec += bt->period;
	return pthread_cond_timedwait(&bt->cond, &bt->lock, &ts);
}

static void *bthread_run(void *arg)
{
	struct bthread *bt = arg;
	int ret;

	while (1) {
		pthread_mutex_lock(&bt->lock);
		ret = bthread_wait(bt);
		switch (bt->state) {
		case BT_WAITING:
			/* waiting to be runnable, go around again */
			pthread_mutex_unlock(&bt->lock);
			break;
		case BT_RUNNING:
			/* running; call our function if we timed out */
			pthread_mutex_unlock(&bt->lock);
			if (ret == ETIMEDOUT)
				bt->fn(bt->data);
			break;
		case BT_CANCELLED:
			/* exit if we're cancelled */
			pthread_mutex_unlock(&bt->lock);
			return NULL;
		}
	}

	return NULL;
}

/* Create background thread and have it wait to be started */
int bthread_create(const char *name,  bthread_fn_t fn, void *data,
		   unsigned int period, struct bthread **btp)
{
	struct bthread *bt;
	int error;

	if (!period)
		return EINVAL;

	bt = calloc(1, sizeof(struct bthread));
	if (!bt)
		return ENOMEM;
	bt->state = BT_WAITING;
	bt->fn = fn;
	bt->data = data;
	bt->period = period;
	bt->can_join = 1;

	bt->lock = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
	bt->cond = (pthread_cond_t)PTHREAD_COND_INITIALIZER;

	error = pthread_create(&bt->thread, NULL, bthread_run, bt);
	if (error)
		goto out_cond;

#ifdef HAVE_PTHREAD_SETNAME_NP
#ifdef __APPLE__
	pthread_setname_np(name);
#else
	pthread_setname_np(bt->thread, name);
#endif
#endif

	*btp = bt;
	return 0;

out_cond:
	pthread_cond_destroy(&bt->cond);
	pthread_mutex_destroy(&bt->lock);
	free(bt);
	return error;
}

/* Stop the thread (if running) and tear everything down */
void bthread_destroy(struct bthread **btp)
{
	struct bthread *bt = *btp;

	if (bt) {
		bthread_stop(bt);

		pthread_cond_destroy(&bt->cond);
		pthread_mutex_destroy(&bt->lock);

		free(bt);
	}

	*btp = NULL;
}

/* Start background thread, put it in waiting state */
int bthread_start(struct bthread *bt)
{
	int err;

	pthread_mutex_lock(&bt->lock);
	bt->state = BT_RUNNING;
	err = pthread_cond_signal(&bt->cond);
	pthread_mutex_unlock(&bt->lock);

	return err;
}

/* Has this thread been cancelled? */
int bthread_cancelled(struct bthread *bt)
{
	int ret;

	pthread_mutex_lock(&bt->lock);
	ret = bt->state == BT_CANCELLED;
	pthread_mutex_unlock(&bt->lock);

	return ret;
}

/* Ask the thread to cancel itself, but don't wait */
int bthread_cancel(struct bthread *bt)
{
	int err = 0;

	pthread_mutex_lock(&bt->lock);
	switch (bt->state) {
	case BT_CANCELLED:
		break;
	case BT_WAITING:
	case BT_RUNNING:
		bt->state = BT_CANCELLED;
		err = pthread_cond_signal(&bt->cond);
		break;
	}
	pthread_mutex_unlock(&bt->lock);

	return err;
}

/* Ask the thread to cancel itself and wait for it */
void bthread_stop(struct bthread *bt)
{
	unsigned int need_join = 0;

	pthread_mutex_lock(&bt->lock);
	switch (bt->state) {
	case BT_CANCELLED:
		need_join = bt->can_join;
		break;
	case BT_WAITING:
	case BT_RUNNING:
		bt->state = BT_CANCELLED;
		need_join = 1;
		pthread_cond_signal(&bt->cond);
		break;
	}
	if (need_join)
		bt->can_join = 0;
	pthread_mutex_unlock(&bt->lock);

	if (need_join)
		pthread_join(bt->thread, NULL);
}
#endif /* HAVE_PTHREAD */
