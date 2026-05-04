// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2006 Silicon Graphics, Inc.
 * All Rights Reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <errno.h>

#include "config.h"
#include "list.h"
#include "cache.h"

#undef CACHE_DEBUG
/* #define CACHE_DEBUG 1 */
#undef CACHE_ABORT
/* #define CACHE_ABORT 1 */

#define CACHE_SHAKE_COUNT	64

#ifdef CACHE_DEBUG
# include <assert.h>
# define ASSERT(x)		assert(x)
#else
# define ASSERT(x)		do { } while (0)
#endif

#ifndef HAVE_FLS
static inline int fls(int x)
{
	int r = 32;

	if (!x)
		return 0;
	if (!(x & 0xffff0000u)) {
		x = (x & 0xffffu) << 16;
		r -= 16;
	}
	if (!(x & 0xff000000u)) {
		x = (x & 0xffffffu) << 8;
		r -= 8;
	}
	if (!(x & 0xf0000000u)) {
		x = (x & 0xfffffffu) << 4;
		r -= 4;
	}
	if (!(x & 0xc0000000u)) {
		x = (x & 0x3fffffffu) << 2;
		r -= 2;
	}
	if (!(x & 0x80000000u)) {
		r -= 1;
	}
	return r;
}
#endif

static unsigned int cache_generic_bulkrelse(struct cache *, struct list_head *);

int
cache_init(
	int			flags,
	unsigned int		hashsize,
	const struct cache_operations	*cache_operations,
	struct cache		*cache)
{
	unsigned int		i, maxcount;

	maxcount = hashsize * HASH_CACHE_RATIO;

	memset(cache, 0, sizeof(*cache));

	cache->c_flags = flags;
	cache->c_count = 0;
	cache->c_max = 0;
	cache->c_hits = 0;
	cache->c_misses = 0;
	cache->c_maxcount = maxcount;
	cache->c_orig_max = maxcount;
	cache->hash = cache_operations->hash;
	cache->alloc = cache_operations->alloc;
	cache->flush = cache_operations->flush;
	cache->relse = cache_operations->relse;
	cache->compare = cache_operations->compare;
	cache->bulkrelse = cache_operations->bulkrelse ?
		cache_operations->bulkrelse : cache_generic_bulkrelse;
	cache->get = cache_operations->get;
	cache->put = cache_operations->put;
	cache->resize = cache_operations->resize;
	pthread_mutex_init(&cache->c_mutex, NULL);

	for (i = 0; i <= CACHE_DIRTY_PRIORITY; i++) {
		list_head_init(&cache->c_mrus[i].cm_list);
		cache->c_mrus[i].cm_count = 0;
		pthread_mutex_init(&cache->c_mrus[i].cm_mutex, NULL);
	}

	cache->c_hash = calloc(hashsize, sizeof(struct cache_hash));
	if (!cache->c_hash)
		return ENOMEM;

	cache->c_hashsize = hashsize;
	cache->c_hashshift = fls(hashsize) - 1;

	for (i = 0; i < hashsize; i++) {
		list_head_init(&cache->c_hash[i].ch_list);
		cache->c_hash[i].ch_count = 0;
		pthread_mutex_init(&cache->c_hash[i].ch_mutex, NULL);
	}

	return 0;
}

void
cache_set_maxcount(
	struct cache		*cache,
	unsigned int		maxcount)
{
	pthread_mutex_lock(&cache->c_mutex);
	cache->c_orig_max = maxcount;
	cache->c_maxcount = maxcount;
	pthread_mutex_unlock(&cache->c_mutex);
}

int
cache_set_flag(
	struct cache		*cache,
	int			flags)
{
	cache->c_flags |= (flags & CACHE_FLAGS_ALL);
	return 0;
}

int
cache_clear_flag(
	struct cache		*cache,
	int			flags)
{
	cache->c_flags &= ~flags;
	return 0;
}

static void
cache_expand(
	struct cache *		cache)
{
	unsigned int		new_size = 0;

	pthread_mutex_lock(&cache->c_mutex);
	if (cache->resize)
		new_size = cache->resize(cache, cache->c_maxcount, 1);
	if (new_size <= cache->c_maxcount)
		new_size = cache->c_maxcount * 2;
#ifdef CACHE_DEBUG
	fprintf(stderr, "increasing cache max size from %u to %u\n",
			cache->c_maxcount, new_size);
#endif
	cache->c_maxcount = new_size;
	pthread_mutex_unlock(&cache->c_mutex);
}

void
cache_walk(
	struct cache		*cache,
	cache_walk_t		visit,
	void			*data)
{
	struct cache_hash	*hash;
	struct cache_node	*pos;
	unsigned int		i;

	for (i = 0; i < cache->c_hashsize; i++) {
		hash = &cache->c_hash[i];
		pthread_mutex_lock(&hash->ch_mutex);
		list_for_each_entry(pos, &hash->ch_list, cn_hash)
			visit(cache, pos, data);
		pthread_mutex_unlock(&hash->ch_mutex);
	}
}

#ifdef CACHE_ABORT
#define cache_abort()	abort()
#else
#define cache_abort()	do { } while (0)
#endif

#ifdef CACHE_DEBUG
static void
cache_zero_check(
	struct cache		*cache,
	struct cache_node	*node,
	void			*data)
{
	if (node->cn_count > 0) {
		fprintf(stderr, "%s: refcount is %u, not zero (node=%p)\n",
			__FUNCTION__, node->cn_count, node);
		cache_abort();
	}
}
#define cache_destroy_check(c)	cache_walk((c), cache_zero_check, NULL)
#else
#define cache_destroy_check(c)	do { } while (0)
#endif

void
cache_destroy(
	struct cache *		cache)
{
	unsigned int		i;

	cache_destroy_check(cache);
	for (i = 0; i < cache->c_hashsize; i++) {
		list_head_destroy(&cache->c_hash[i].ch_list);
		pthread_mutex_destroy(&cache->c_hash[i].ch_mutex);
	}
	for (i = 0; i <= CACHE_DIRTY_PRIORITY; i++) {
		list_head_destroy(&cache->c_mrus[i].cm_list);
		pthread_mutex_destroy(&cache->c_mrus[i].cm_mutex);
	}
	pthread_mutex_destroy(&cache->c_mutex);
	free(cache->c_hash);
	memset(cache, 0, sizeof(*cache));
}

static unsigned int
cache_generic_bulkrelse(
	struct cache *		cache,
	struct list_head *	list)
{
	struct cache_node *	node;
	unsigned int		count = 0;

	while (!list_empty(list)) {
		node = list_entry(list->next, struct cache_node, cn_mru);
		pthread_mutex_destroy(&node->cn_mutex);
		list_del_init(&node->cn_mru);
		cache->relse(cache, node);
		count++;
	}

	return count;
}

/*
 * Park unflushable nodes on their own special MRU so that cache_shake() doesn't
 * end up repeatedly scanning them in the futile attempt to clean them before
 * reclaim.
 */
static void
cache_add_to_dirty_mru(
	struct cache		*cache,
	struct cache_node	*node)
{
	struct cache_mru	*mru = &cache->c_mrus[CACHE_DIRTY_PRIORITY];

	pthread_mutex_lock(&mru->cm_mutex);
	node->cn_old_priority = node->cn_priority;
	node->cn_priority = CACHE_DIRTY_PRIORITY;
	list_add(&node->cn_mru, &mru->cm_list);
	mru->cm_count++;
	pthread_mutex_unlock(&mru->cm_mutex);
}

/*
 * We've hit the limit on cache size, so we need to start reclaiming nodes we've
 * used. The MRU specified by the priority is shaken.  Returns new priority at
 * end of the call (in case we call again). We are not allowed to reclaim dirty
 * objects, so we have to flush them first. If flushing fails, we move them to
 * the "dirty, unreclaimable" list.
 *
 * Hence we skip priorities > CACHE_MAX_PRIORITY unless "purge" is set as we
 * park unflushable (and hence unreclaimable) buffers at these priorities.
 * Trying to shake unreclaimable buffer lists when there is memory pressure is a
 * waste of time and CPU and greatly slows down cache node recycling operations.
 * Hence we only try to free them if we are being asked to purge the cache of
 * all entries.
 */
static unsigned int
cache_shake(
	struct cache *		cache,
	unsigned int		priority,
	bool			purge,
	unsigned int		nr_to_shake)
{
	struct cache_mru	*mru;
	struct cache_hash	*hash;
	struct list_head	temp;
	struct cache_node	*node, *n;
	unsigned int		count;

	ASSERT(priority <= CACHE_DIRTY_PRIORITY);
	if (priority > CACHE_MAX_PRIORITY && !purge)
		priority = 0;

	mru = &cache->c_mrus[priority];
	count = 0;
	list_head_init(&temp);

	pthread_mutex_lock(&mru->cm_mutex);
	list_for_each_entry_safe_reverse(node, n, &mru->cm_list, cn_mru) {
		if (pthread_mutex_trylock(&node->cn_mutex) != 0)
			continue;

		/* memory pressure is not allowed to release dirty objects */
		if (cache->flush(cache, node) && !purge) {
			list_del(&node->cn_mru);
			mru->cm_count--;
			node->cn_priority = -1;
			pthread_mutex_unlock(&node->cn_mutex);
			cache_add_to_dirty_mru(cache, node);
			continue;
		}

		hash = cache->c_hash + node->cn_hashidx;
		if (pthread_mutex_trylock(&hash->ch_mutex) != 0) {
			pthread_mutex_unlock(&node->cn_mutex);
			continue;
		}
		ASSERT(node->cn_count == 0);
		ASSERT(node->cn_priority == priority);
		node->cn_priority = -1;

		list_move(&node->cn_mru, &temp);
		list_del_init(&node->cn_hash);
		hash->ch_count--;
		mru->cm_count--;
		pthread_mutex_unlock(&hash->ch_mutex);
		pthread_mutex_unlock(&node->cn_mutex);

		count++;
		if (!purge && count == nr_to_shake)
			break;
	}
	pthread_mutex_unlock(&mru->cm_mutex);

	if (count > 0) {
		cache->bulkrelse(cache, &temp);

		pthread_mutex_lock(&cache->c_mutex);
		cache->c_count -= count;
		pthread_mutex_unlock(&cache->c_mutex);
	}

	return (count == nr_to_shake) ? priority : ++priority;
}

/*
 * Allocate a new hash node (updating atomic counter in the process),
 * unless doing so will push us over the maximum cache size.
 */
static struct cache_node *
cache_node_allocate(
	struct cache *		cache,
	cache_key_t		key)
{
	unsigned int		nodesfree;
	struct cache_node *	node;

	pthread_mutex_lock(&cache->c_mutex);
	nodesfree = (cache->c_count < cache->c_maxcount);
	if (nodesfree) {
		cache->c_count++;
		if (cache->c_count > cache->c_max)
			cache->c_max = cache->c_count;
	}
	cache->c_misses++;
	pthread_mutex_unlock(&cache->c_mutex);
	if (!nodesfree)
		return NULL;
	node = cache->alloc(cache, key);
	if (node == NULL) {	/* uh-oh */
		pthread_mutex_lock(&cache->c_mutex);
		cache->c_count--;
		pthread_mutex_unlock(&cache->c_mutex);
		return NULL;
	}
	pthread_mutex_init(&node->cn_mutex, NULL);
	list_head_init(&node->cn_mru);
	node->cn_count = 1;
	node->cn_priority = 0;
	node->cn_old_priority = -1;
	return node;
}

int
cache_overflowed(
	struct cache *		cache)
{
	return cache->c_maxcount == cache->c_max;
}


static int
__cache_node_purge(
	struct cache *		cache,
	struct cache_node *	node)
{
	int			count;
	struct cache_mru *	mru;

	pthread_mutex_lock(&node->cn_mutex);
	count = node->cn_count;
	if (count != 0) {
		pthread_mutex_unlock(&node->cn_mutex);
		return count;
	}

	/* can't purge dirty objects */
	if (cache->flush(cache, node)) {
		pthread_mutex_unlock(&node->cn_mutex);
		return 1;
	}

	mru = &cache->c_mrus[node->cn_priority];
	pthread_mutex_lock(&mru->cm_mutex);
	list_del_init(&node->cn_mru);
	mru->cm_count--;
	pthread_mutex_unlock(&mru->cm_mutex);

	pthread_mutex_unlock(&node->cn_mutex);
	pthread_mutex_destroy(&node->cn_mutex);
	list_del_init(&node->cn_hash);
	cache->relse(cache, node);
	return 0;
}

/* Grab a new refcount to the cache node object.  Caller must hold cn_mutex. */
struct cache_node *cache_node_grab(struct cache *cache, struct cache_node *node)
{
	struct cache_mru *mru;

	if (node->cn_count == 0 && cache->get) {
		int err = cache->get(cache, node);
		if (err)
			return NULL;
	}
	if (node->cn_count == 0) {
		ASSERT(node->cn_priority >= 0);
		ASSERT(!list_empty(&node->cn_mru));
		mru = &cache->c_mrus[node->cn_priority];
		pthread_mutex_lock(&mru->cm_mutex);
		mru->cm_count--;
		list_del_init(&node->cn_mru);
		pthread_mutex_unlock(&mru->cm_mutex);
		if (node->cn_old_priority != -1) {
			ASSERT(node->cn_priority ==
					CACHE_DIRTY_PRIORITY);
			node->cn_priority = node->cn_old_priority;
			node->cn_old_priority = -1;
		}
	}
	node->cn_count++;
	return node;
}

/*
 * Lookup in the cache hash table.  With any luck we'll get a cache
 * hit, in which case this will all be over quickly and painlessly.
 * Otherwise, we allocate a new node, taking care not to expand the
 * cache beyond the requested maximum size (shrink it if it would).
 * Returns one if hit in cache, otherwise zero.  A node is _always_
 * returned, however.
 */
int
cache_node_get(
	struct cache		*cache,
	cache_key_t		key,
	unsigned int		cgflags,
	struct cache_node	**nodep)
{
	struct cache_hash	*hash;
	struct cache_node	*node = NULL, *n;
	unsigned int		hashidx;
	int			priority = 0;
	int			purged = 0;

	hashidx = cache->hash(key, cache->c_hashsize, cache->c_hashshift);
	hash = cache->c_hash + hashidx;

	for (;;) {
		pthread_mutex_lock(&hash->ch_mutex);
		list_for_each_entry_safe(node, n, &hash->ch_list, cn_hash) {
			int result;

			result = cache->compare(node, key);
			switch (result) {
			case CACHE_HIT:
				break;
			case CACHE_PURGE:
				if ((cache->c_flags & CACHE_MISCOMPARE_PURGE) &&
				    !__cache_node_purge(cache, node)) {
					purged++;
					hash->ch_count--;
				}
				/* FALL THROUGH */
			case CACHE_MISS:
				goto next_object;
			}

			/*
			 * node found, bump node's reference count, remove it
			 * from its MRU list, and update stats.
			 */
			pthread_mutex_lock(&node->cn_mutex);
			if (!cache_node_grab(cache, node)) {
				pthread_mutex_unlock(&node->cn_mutex);
				goto next_object;
			}
			pthread_mutex_unlock(&node->cn_mutex);
			pthread_mutex_unlock(&hash->ch_mutex);

			pthread_mutex_lock(&cache->c_mutex);
			cache->c_hits++;
			pthread_mutex_unlock(&cache->c_mutex);

			*nodep = node;
			return 0;
next_object:
			continue;	/* what the hell, gcc? */
		}
		pthread_mutex_unlock(&hash->ch_mutex);

		if (cgflags & CACHE_GET_INCORE) {
			*nodep = NULL;
			return 0;
		}

		/*
		 * not found, allocate a new entry
		 */
		node = cache_node_allocate(cache, key);
		if (node)
			break;
		priority = cache_shake(cache, priority, false, CACHE_SHAKE_COUNT);
		/*
		 * We start at 0; if we free CACHE_SHAKE_COUNT we get
		 * back the same priority, if not we get back priority+1.
		 * If we exceed CACHE_MAX_PRIORITY all slots are full; grow it.
		 */
		if (priority > CACHE_MAX_PRIORITY) {
			priority = 0;
			cache_expand(cache);
		}
	}

	node->cn_hashidx = hashidx;

	/* add new node to appropriate hash */
	pthread_mutex_lock(&hash->ch_mutex);
	hash->ch_count++;
	list_add(&node->cn_hash, &hash->ch_list);
	pthread_mutex_unlock(&hash->ch_mutex);

	if (purged) {
		pthread_mutex_lock(&cache->c_mutex);
		cache->c_count -= purged;
		pthread_mutex_unlock(&cache->c_mutex);
	}

	*nodep = node;
	return 1;
}

static unsigned int cache_mru_count(const struct cache *cache)
{
	const struct cache_mru	*mru = cache->c_mrus;
	unsigned int		mru_count = 0;
	unsigned int		i;

	for (i = 0; i < CACHE_NR_PRIORITIES; i++, mru++)
		mru_count += mru->cm_count;

	return mru_count;
}


void cache_shrink(struct cache *cache)
{
	unsigned int		mru_count = 0;
	unsigned int		threshold = 0;
	unsigned int		priority = 0;
	unsigned int		new_size;

	pthread_mutex_lock(&cache->c_mutex);
	/* Don't shrink below the original cache size */
	if (cache->c_maxcount <= cache->c_orig_max)
		goto out_unlock;

	mru_count = cache_mru_count(cache);

	/*
	 * If there's not even a batch of nodes on the MRU to try to free,
	 * don't bother with the rest.
	 */
	if (mru_count < CACHE_SHAKE_COUNT)
		goto out_unlock;

	/*
	 * Figure out the next step down in size, but don't go below the
	 * original size.
	 */
	if (cache->resize)
		new_size = cache->resize(cache, cache->c_maxcount, -1);
	else
		new_size = cache->c_maxcount / 2;
	if (new_size >= cache->c_maxcount)
		goto out_unlock;
	if (new_size < cache->c_orig_max)
		new_size = cache->c_orig_max;

	/*
	 * If we can't purge enough nodes to get the node count below new_size,
	 * don't resize the cache.
	 */
	if (cache->c_count - mru_count >= new_size)
		goto out_unlock;

#ifdef CACHE_DEBUG
	fprintf(stderr, "decreasing cache max size from %u to %u (currently %u)\n",
		cache->c_maxcount, new_size, cache->c_count);
#endif
	cache->c_maxcount = new_size;

	/* Try to reduce the number of cached objects. */
	do {
		unsigned int new_priority;

		/*
		 * The threshold is the amount we need to purge to get c_count
		 * below the new maxcount.  Try to free some objects off the
		 * MRU.  Drop c_mutex because cache_shake will take it.
		 */
		threshold = cache->c_count - new_size;
		pthread_mutex_unlock(&cache->c_mutex);

		new_priority = cache_shake(cache, priority, false, threshold);

		/* Either we made no progress or we ran out of MRU levels */
		if (new_priority == priority ||
		    new_priority > CACHE_MAX_PRIORITY)
			return;
		priority = new_priority;

		pthread_mutex_lock(&cache->c_mutex);
		/*
		 * Someone could have walked in and changed the cache maxsize
		 * again while we had the lock dropped.  If that happened, stop
		 * clearing.
		 */
		if (cache->c_maxcount != new_size)
			goto out_unlock;

		mru_count = cache_mru_count(cache);
		if (cache->c_count - mru_count >= new_size)
			goto out_unlock;
	} while (1);

out_unlock:
	pthread_mutex_unlock(&cache->c_mutex);
	return;
}

void
cache_node_put(
	struct cache *		cache,
	struct cache_node *	node)
{
	struct cache_mru *	mru;
	bool was_put = false;

	pthread_mutex_lock(&node->cn_mutex);
#ifdef CACHE_DEBUG
	if (node->cn_count < 1) {
		fprintf(stderr, "%s: node put on refcount %u (node=%p)\n",
				__FUNCTION__, node->cn_count, node);
		cache_abort();
	}
	if (!list_empty(&node->cn_mru)) {
		fprintf(stderr, "%s: node put on node (%p) in MRU list\n",
				__FUNCTION__, node);
		cache_abort();
	}
#endif
	node->cn_count--;
	was_put = (node->cn_count == 0);

	if (node->cn_count == 0 && cache->put)
		cache->put(cache, node);
	if (node->cn_count == 0) {
		/* add unreferenced node to appropriate MRU for shaker */
		mru = &cache->c_mrus[node->cn_priority];
		pthread_mutex_lock(&mru->cm_mutex);
		mru->cm_count++;
		list_add(&node->cn_mru, &mru->cm_list);
		pthread_mutex_unlock(&mru->cm_mutex);
	}

	pthread_mutex_unlock(&node->cn_mutex);

	if (was_put && (cache->c_flags & CACHE_AUTO_SHRINK))
		cache_shrink(cache);
}

void
cache_node_set_priority(
	struct cache *		cache,
	struct cache_node *	node,
	int			priority)
{
	if (priority < 0)
		priority = 0;
	else if (priority > CACHE_MAX_PRIORITY)
		priority = CACHE_MAX_PRIORITY;

	pthread_mutex_lock(&node->cn_mutex);
	ASSERT(node->cn_count > 0);
	node->cn_priority = priority;
	node->cn_old_priority = -1;
	pthread_mutex_unlock(&node->cn_mutex);
}

int
cache_node_get_priority(
	struct cache_node *	node)
{
	int			priority;

	pthread_mutex_lock(&node->cn_mutex);
	priority = node->cn_priority;
	pthread_mutex_unlock(&node->cn_mutex);

	return priority;
}


/*
 * Purge a specific node from the cache.  Reference count must be zero.
 */
int
cache_node_purge(
	struct cache		*cache,
	cache_key_t		key,
	struct cache_node	*node)
{
	struct cache_node	*pos, *n;
	struct cache_hash	*hash;
	int			count = -1;

	hash = cache->c_hash + cache->hash(key, cache->c_hashsize,
					   cache->c_hashshift);
	pthread_mutex_lock(&hash->ch_mutex);
	list_for_each_entry_safe(pos, n, &hash->ch_list, cn_hash) {
		if (pos != node)
			continue;

		count = __cache_node_purge(cache, node);
		if (!count)
			hash->ch_count--;
		break;
	}
	pthread_mutex_unlock(&hash->ch_mutex);

	if (count == 0) {
		pthread_mutex_lock(&cache->c_mutex);
		cache->c_count--;
		pthread_mutex_unlock(&cache->c_mutex);
	}
#ifdef CACHE_DEBUG
	if (count >= 1) {
		fprintf(stderr, "%s: refcount was %u, not zero (node=%p)\n",
				__FUNCTION__, count, node);
		cache_abort();
	}
	if (count == -1) {
		fprintf(stderr, "%s: purge node not found! (node=%p)\n",
			__FUNCTION__, node);
		cache_abort();
	}
#endif
	return count == 0;
}

/*
 * Purge all nodes from the cache.  All reference counts must be zero.
 */
void
cache_purge(
	struct cache *		cache)
{
	int			i;

	for (i = 0; i <= CACHE_DIRTY_PRIORITY; i++)
		cache_shake(cache, i, true, CACHE_SHAKE_COUNT);

#ifdef CACHE_DEBUG
	if (cache->c_count != 0) {
		/* flush referenced nodes to disk */
		cache_flush(cache);
		fprintf(stderr, "%s: shake on cache %p left %u nodes!?\n",
				__FUNCTION__, cache, cache->c_count);
		cache_abort();
	}
#endif
}

/*
 * Flush all nodes in the cache to disk.  Returns true if the flush succeeded.
 */
bool
cache_flush(
	struct cache		*cache)
{
	struct cache_hash	*hash;
	struct cache_node	*node;
	int			i;
	bool			still_dirty = false;

	if (!cache->flush)
		return true;

	for (i = 0; i < cache->c_hashsize; i++) {
		hash = &cache->c_hash[i];

		pthread_mutex_lock(&hash->ch_mutex);
		list_for_each_entry(node, &hash->ch_list, cn_hash) {
			pthread_mutex_lock(&node->cn_mutex);
			still_dirty |= cache->flush(cache, node);
			pthread_mutex_unlock(&node->cn_mutex);
		}
		pthread_mutex_unlock(&hash->ch_mutex);
	}

	return !still_dirty;
}

#define	HASH_REPORT	(3 * HASH_CACHE_RATIO)
void
cache_report(
	FILE		*fp,
	const char	*name,
	struct cache	*cache)
{
	int		i;
	unsigned long	count, index, total;
	unsigned long	hash_bucket_lengths[HASH_REPORT + 2] = { 0 };

	if ((cache->c_hits + cache->c_misses) == 0)
		return;

	/* report cache summary */
	fprintf(fp, "%s: %p\n"
			"Max supported entries = %u\n"
			"Max utilized entries = %u\n"
			"Active entries = %u\n"
			"Hash table size = %u\n"
			"Hits = %llu\n"
			"Misses = %llu\n"
			"Hit ratio = %5.2f\n",
			name, cache,
			cache->c_maxcount,
			cache->c_max,
			cache->c_count,
			cache->c_hashsize,
			cache->c_hits,
			cache->c_misses,
			(double)cache->c_hits * 100 /
				(cache->c_hits + cache->c_misses)
	);

	for (i = 0; i <= CACHE_MAX_PRIORITY; i++)
		fprintf(fp, "MRU %d entries = %6u (%3u%%)\n",
			i, cache->c_mrus[i].cm_count,
			cache->c_mrus[i].cm_count * 100 / cache->c_count);

	i = CACHE_DIRTY_PRIORITY;
	fprintf(fp, "Dirty MRU %d entries = %6u (%3u%%)\n",
		i, cache->c_mrus[i].cm_count,
		cache->c_mrus[i].cm_count * 100 / cache->c_count);

	/* report hash bucket lengths */
	for (i = 0; i < cache->c_hashsize; i++) {
		count = cache->c_hash[i].ch_count;
		if (count > HASH_REPORT)
			index = HASH_REPORT + 1;
		else
			index = count;
		hash_bucket_lengths[index]++;
	}

	total = 0;
	for (i = 0; i < HASH_REPORT + 1; i++) {
		total += i * hash_bucket_lengths[i];
		if (hash_bucket_lengths[i] == 0)
			continue;
		fprintf(fp, "Hash buckets with  %2d entries %6ld (%3ld%%)\n",
			i, hash_bucket_lengths[i],
			(i * hash_bucket_lengths[i] * 100) / cache->c_count);
	}
	if (hash_bucket_lengths[i])	/* last report bucket is the overflow bucket */
		fprintf(fp, "Hash buckets with >%2d entries %6ld (%3ld%%)\n",
			i - 1, hash_bucket_lengths[i],
			((cache->c_count - total) * 100) / cache->c_count);
}
