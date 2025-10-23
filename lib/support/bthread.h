/*
 * bthread.h - Background thread manager
 *
 * Copyright (C) 2025-2026 Oracle.
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 */
#ifndef __BTHREAD_H__
#define __BTHREAD_H__

typedef void (*bthread_fn_t)(void *data);
struct bthread;

int bthread_create(const char *name, bthread_fn_t fn, void *data,
		   unsigned int period, struct bthread **btp);
void bthread_destroy(struct bthread **btp);

int bthread_start(struct bthread *bt);
void bthread_stop(struct bthread *bt);

int bthread_cancel(struct bthread *bt);
int bthread_cancelled(struct bthread *bt);

#endif /* __BTHREAD_H__ */
