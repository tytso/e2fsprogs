/*
 * tst_hashmap.c --- unit tests for hashmap.c
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Library
 * General Public License, version 2.
 * %End-Header%
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ext2_fs.h"
#include "ext2fs.h"
#include "hashmap.h"

static int test_count;
static int fail_count;
static int freed_items;

#define CHECK(cond, msg) do { \
	test_count++; \
	if (!(cond)) { \
		fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, msg); \
		fail_count++; \
	} \
} while (0)

static uint32_t const_collision_hash(const void *key EXT2FS_ATTR((unused)),
				     size_t len EXT2FS_ATTR((unused)))
{
	return 7;
}

static void record_free_cb(void *data)
{
	freed_items++;
	free(data);
}

static void test_djb2_hash(void)
{
	CHECK(ext2fs_djb2_hash("", 0) == 5381U,
	      "djb2 empty key == 5381");
	CHECK(ext2fs_djb2_hash("a", 1) == 177670U,
	      "djb2('a') == 177670");
	CHECK(ext2fs_djb2_hash("ext4", 4) == 0x7c967f6aU,
	      "djb2('ext4') == 0x7c967f6a");
	CHECK(ext2fs_djb2_hash("ext4_ignored_suffix", 4) == 0x7c967f6aU,
	      "djb2 honors explicit size parameter");
	CHECK(ext2fs_djb2_hash("ab\0cd", 5) != ext2fs_djb2_hash("ab\0ef", 5),
	      "djb2 hashes past embedded NUL bytes");
	/* High-bit bytes 0x80..0xff must hash as unsigned across all arches */
	CHECK(ext2fs_djb2_hash("\x80\xff", 2) ==
	      ((5381U * 33U + 0x80U) * 33U + 0xffU),
	      "djb2 treats bytes >= 0x80 as unsigned");
}

static void test_lookup_and_binary_keys(void)
{
	struct ext2fs_hashmap		*h;
	struct ext2fs_hashmap_entry	*it = NULL;
	int				v1 = 101, v2 = 202, v3 = 303;
	int				v4 = 404, v5 = 505;
	const char			bin1[5] = { 'a', 'b', '\0', 'c', 'd' };
	const char			bin2[5] = { 'a', 'b', '\0', 'e', 'f' };

	CHECK(ext2fs_hashmap_create(ext2fs_djb2_hash, NULL, 0) == NULL,
	      "hashmap_create rejects size == 0");
	CHECK(ext2fs_hashmap_create(NULL, NULL, 8) == NULL,
	      "hashmap_create rejects NULL hash_fct");
	CHECK(ext2fs_hashmap_create(ext2fs_djb2_hash, NULL, SIZE_MAX) == NULL,
	      "hashmap_create rejects overflowing size");
#if SIZE_MAX > UINT32_MAX
	CHECK(ext2fs_hashmap_create(ext2fs_djb2_hash, NULL,
				    (size_t) UINT32_MAX + 1) == NULL,
	      "hashmap_create rejects size > UINT32_MAX");
#endif

	h = ext2fs_hashmap_create(ext2fs_djb2_hash, NULL, 16);
	CHECK(h != NULL, "hashmap_create(size=16)");

	/* Empty lookup and empty iterator */
	CHECK(ext2fs_hashmap_lookup(h, "missing", 7) == NULL,
	      "lookup on empty map returns NULL");
	CHECK(ext2fs_hashmap_iter_in_order(h, &it) == NULL && it == NULL,
	      "iter on empty map returns NULL");

	/* Prefix vs full key and binary keys with embedded NUL */
	CHECK(ext2fs_hashmap_add(h, &v1, "dir", 3) == 0, "add 'dir'");
	CHECK(ext2fs_hashmap_add(h, &v2, "dir_entry", 9) == 0,
	      "add 'dir_entry'");
	CHECK(ext2fs_hashmap_add(h, &v3, bin1, 5) == 0, "add bin1");
	CHECK(ext2fs_hashmap_add(h, &v4, bin2, 5) == 0, "add bin2");

	CHECK(ext2fs_hashmap_lookup(h, "dir", 3) == &v1, "lookup 'dir'");
	CHECK(ext2fs_hashmap_lookup(h, "dir_entry", 9) == &v2,
	      "lookup 'dir_entry'");
	CHECK(ext2fs_hashmap_lookup(h, "dir_entry", 3) == &v1,
	      "lookup prefix with key_len=3 matches 'dir'");
	CHECK(ext2fs_hashmap_lookup(h, "dir", 2) == NULL,
	      "lookup shorter prefix 'di' misses");
	CHECK(ext2fs_hashmap_lookup(h, bin1, 5) == &v3, "lookup bin1");
	CHECK(ext2fs_hashmap_lookup(h, bin2, 5) == &v4, "lookup bin2");

	/* Duplicate key shadows older entry at head of bucket */
	CHECK(ext2fs_hashmap_add(h, &v5, "dir", 3) == 0,
	      "add duplicate 'dir'");
	CHECK(ext2fs_hashmap_lookup(h, "dir", 3) == &v5,
	      "duplicate key lookup returns newest entry");

	ext2fs_hashmap_free(h);
}

static void test_collisions_iter_and_free_cb(void)
{
	struct ext2fs_hashmap		*h;
	struct ext2fs_hashmap_entry	*it = NULL;
	static const char * const	keys[5] = {
		"k0", "k1", "k2", "k3", "k4"
	};
	int				*vals[5];
	void				*got;
	int				i, step;

	/* Single-bucket + constant hash forces all entries into one chain */
	freed_items = 0;
	h = ext2fs_hashmap_create(const_collision_hash, record_free_cb, 1);
	CHECK(h != NULL, "hashmap_create(size=1) with free_fct");

	for (i = 0; i < 5; i++) {
		vals[i] = malloc(sizeof(int));
		if (!vals[i]) {
			CHECK(0, "malloc val");
			ext2fs_hashmap_free(h);
			return;
		}
		*vals[i] = (i + 1) * 10;
		CHECK(ext2fs_hashmap_add(h, vals[i], keys[i], 2) == 0,
		      "add colliding entry");
	}

	/* Verify lookup hits head, middle, and tail of collision chain */
	for (i = 0; i < 5; i++)
		CHECK(ext2fs_hashmap_lookup(h, keys[i], 2) == vals[i],
		      "lookup colliding key");
	CHECK(ext2fs_hashmap_lookup(h, "zz", 2) == NULL,
	      "lookup miss after full collision chain walk");

	/* Iterate list from h->first (most-recent to oldest insertion) */
	it = NULL;
	step = 0;
	while ((got = ext2fs_hashmap_iter_in_order(h, &it)) != NULL) {
		int expected_idx = 4 - step;

		if (step < 5) {
			CHECK(got == vals[expected_idx] &&
			      it->key == keys[expected_idx] &&
			      it->key_len == 2,
			      "iter_in_order entry matches expected order");
		}
		step++;
	}
	CHECK(step == 5 && it == NULL,
	      "iter_in_order visited all 5 entries and terminated cleanly");

	/*
	 * Delete middle ("k2"), head ("k4" == h->first), and tail ("k0" ==
	 * h->last) entries via ext2fs_hashmap_del(), verifying bucket chain,
	 * doubly-linked iteration list, and free_fct on each removal.
	 */
	ext2fs_hashmap_del(NULL, NULL);
	ext2fs_hashmap_del(h, NULL);
	it = NULL;
	while (ext2fs_hashmap_iter_in_order(h, &it) != NULL) {
		if (strcmp(it->key, "k2") == 0) {
			ext2fs_hashmap_del(h, it);
			break;
		}
	}
	CHECK(freed_items == 1 && ext2fs_hashmap_lookup(h, "k2", 2) == NULL,
	      "hashmap_del unlinks and frees middle entry 'k2'");

	it = NULL;
	CHECK(ext2fs_hashmap_iter_in_order(h, &it) == vals[4] && it != NULL,
	      "h->first is still 'k4'");
	ext2fs_hashmap_del(h, it);
	CHECK(freed_items == 2 && ext2fs_hashmap_lookup(h, "k4", 2) == NULL,
	      "hashmap_del unlinks and frees head entry 'k4'");

	it = NULL;
	while (ext2fs_hashmap_iter_in_order(h, &it) != NULL) {
		if (strcmp(it->key, "k0") == 0) {
			ext2fs_hashmap_del(h, it);
			break;
		}
	}
	CHECK(freed_items == 3 && ext2fs_hashmap_lookup(h, "k0", 2) == NULL,
	      "hashmap_del unlinks and frees tail entry 'k0'");

	/* Remaining entries must be ["k3", "k1"] in order */
	CHECK(ext2fs_hashmap_lookup(h, "k3", 2) == vals[3] &&
	      ext2fs_hashmap_lookup(h, "k1", 2) == vals[1],
	      "remaining entries 'k3' and 'k1' still found by lookup");
	it = NULL;
	CHECK(ext2fs_hashmap_iter_in_order(h, &it) == vals[3] &&
	      ext2fs_hashmap_iter_in_order(h, &it) == vals[1] &&
	      ext2fs_hashmap_iter_in_order(h, &it) == NULL,
	      "iter_in_order walks ['k3', 'k1'] cleanly after deletions");

	ext2fs_hashmap_free(h);
	ext2fs_hashmap_free(NULL);
	CHECK(freed_items == 5,
	      "hashmap_del + hashmap_free invoked free_fct on all 5 entries");
}

int main(int argc EXT2FS_ATTR((unused)), char **argv EXT2FS_ATTR((unused)))
{
	test_djb2_hash();
	test_lookup_and_binary_keys();
	test_collisions_iter_and_free_cb();

	if (fail_count) {
		fprintf(stderr, "tst_hashmap: %d/%d checks FAILED\n",
			fail_count, test_count);
		return 1;
	}
	printf("tst_hashmap: all %d checks passed\n", test_count);
	return 0;
}
