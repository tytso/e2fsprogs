#include "config.h"
#include "hashmap.h"
#include <string.h>

struct ext2fs_hashmap {
	uint32_t size;
	uint32_t(*hash)(const void *key, size_t len);
	void(*free)(void*);
	struct ext2fs_hashmap_entry *first;
	struct ext2fs_hashmap_entry *last;
#if __GNUC_PREREQ (4, 8)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
	struct ext2fs_hashmap_entry *entries[0];
#if __GNUC_PREREQ (4, 8)
#pragma GCC diagnostic pop
#endif
};

uint32_t ext2fs_djb2_hash(const void *str, size_t size)
{
	int c;
	const unsigned char *s = str;
	uint32_t hash = 5381;

	while (size-- > 0) {
		c = *s++;
		hash = ((hash << 5) + hash) + c;
	}
	return hash;
}

struct ext2fs_hashmap *ext2fs_hashmap_create(
				uint32_t(*hash_fct)(const void*, size_t),
				void(*free_fct)(void*), size_t size)
{
	struct ext2fs_hashmap *h;

	if (size == 0 || size > UINT32_MAX || !hash_fct ||
	    size > (SIZE_MAX - sizeof(struct ext2fs_hashmap)) /
		   sizeof(struct ext2fs_hashmap_entry *))
		return NULL;

	h = calloc(1, sizeof(struct ext2fs_hashmap) +
		      sizeof(struct ext2fs_hashmap_entry *) * size);
	if (!h)
		return NULL;

	h->size = size;
	h->free = free_fct;
	h->hash = hash_fct;
	h->first = h->last = NULL;
	return h;
}

int ext2fs_hashmap_add(struct ext2fs_hashmap *h,
		       void *data, const void *key, size_t key_len)
{
	uint32_t hash = h->hash(key, key_len) % h->size;
	struct ext2fs_hashmap_entry *e = malloc(sizeof(*e));

	if (!e)
		return -1;

	e->data = data;
	e->key = key;
	e->key_len = key_len;
	e->next = h->entries[hash];
	h->entries[hash] = e;

	e->list_prev = NULL;
	e->list_next = h->first;
	if (h->first)
		h->first->list_prev = e;
	h->first = e;
	if (!h->last)
		h->last = e;

	return 0;
}

void *ext2fs_hashmap_lookup(struct ext2fs_hashmap *h, const void *key,
			    size_t key_len)
{
	struct ext2fs_hashmap_entry *iter;
	uint32_t hash = h->hash(key, key_len) % h->size;

	for (iter = h->entries[hash]; iter; iter = iter->next)
		if (iter->key_len == key_len && !memcmp(iter->key, key, key_len))
			return iter->data;
	return NULL;
}

void *ext2fs_hashmap_iter_in_order(struct ext2fs_hashmap *h,
				   struct ext2fs_hashmap_entry **it)
{
	*it = *it ? (*it)->list_next : h->first;
	return *it ? (*it)->data : NULL;
}

void ext2fs_hashmap_del(struct ext2fs_hashmap *h,
			struct ext2fs_hashmap_entry *e)
{
	struct ext2fs_hashmap_entry **pp;
	uint32_t hash;

	if (!h || !e)
		return;

	hash = h->hash(e->key, e->key_len) % h->size;
	for (pp = &h->entries[hash]; *pp; pp = &(*pp)->next) {
		if (*pp == e)
			break;
	}
	if (!*pp)
		return;
	*pp = e->next;

	if (e->list_prev)
		e->list_prev->list_next = e->list_next;
	else
		h->first = e->list_next;

	if (e->list_next)
		e->list_next->list_prev = e->list_prev;
	else
		h->last = e->list_prev;

	if (h->free)
		h->free(e->data);
	free(e);
}

void ext2fs_hashmap_free(struct ext2fs_hashmap *h)
{
	size_t	i;

	if (!h)
		return;

	for (i = 0; i < h->size; ++i) {
		struct ext2fs_hashmap_entry *it = h->entries[i];
		while (it) {
			struct ext2fs_hashmap_entry *tmp = it->next;
			if (h->free)
				h->free(it->data);
			free(it);
			it = tmp;
		}
	}
	free(h);
}
