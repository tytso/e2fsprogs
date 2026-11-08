/*
 * tst_dblist.c --- unit tests for dblist.c and dblist_dir.c
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Library
 * General Public License, version 2.
 * %End-Header%
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif

#include "ext2_fs.h"
#include "ext2fs.h"
#include "ext2fsP.h"

static int test_count;
static int fail_count;

#define CHECK(cond, msg) do { \
	test_count++; \
	if (!(cond)) { \
		fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, msg); \
		fail_count++; \
	} \
} while (0)

#define CHECK_RC(rc, expected, msg) do { \
	errcode_t _rc = (rc); \
	errcode_t _exp = (expected); \
	test_count++; \
	if (_rc != _exp) { \
		fprintf(stderr, "FAIL [%s:%d]: %s (got %ld, expected %ld)\n", \
			__FILE__, __LINE__, msg, (long) _rc, (long) _exp); \
		fail_count++; \
	} \
} while (0)

struct iter_log {
	struct ext2_db_entry2	entries[16];
	int			count;
	int			abort_at;
};

static int record_db2_cb(ext2_filsys fs EXT2FS_ATTR((unused)),
			 struct ext2_db_entry2 *db_info, void *priv_data)
{
	struct iter_log *log = (struct iter_log *) priv_data;

	if (log->count < 16)
		log->entries[log->count] = *db_info;
	log->count++;
	if (log->abort_at > 0 && log->count >= log->abort_at)
		return DBLIST_ABORT;
	return 0;
}

static int mutate_db32_cb(ext2_filsys fs EXT2FS_ATTR((unused)),
			  struct ext2_db_entry *db_info, void *priv_data)
{
	int *seen = (int *) priv_data;

	(*seen)++;
	db_info->blk += 1000;
	return 0;
}

static EXT2_QSORT_TYPE rev_ino_cmp2(const void *a, const void *b)
{
	const struct ext2_db_entry2 *da = (const struct ext2_db_entry2 *) a;
	const struct ext2_db_entry2 *db = (const struct ext2_db_entry2 *) b;

	if (da->ino != db->ino)
		return (da->ino < db->ino) ? 1 : -1;
	return (int) (da->blockcnt - db->blockcnt);
}

static EXT2_QSORT_TYPE rev_blk_cmp32(const void *a, const void *b)
{
	const struct ext2_db_entry *da = (const struct ext2_db_entry *) a;
	const struct ext2_db_entry *db = (const struct ext2_db_entry *) b;

	if (da->blk != db->blk)
		return (da->blk < db->blk) ? 1 : -1;
	return 0;
}

static void test_dblist_core(ext2_filsys fs)
{
	struct struct_ext2_filsys	bad_fs;
	struct ext2_struct_dblist	bad_db;
	ext2_dblist			db = NULL, copy = NULL;
	struct ext2_db_entry2		*last2 = NULL;
	struct ext2_db_entry		*last32 = NULL;
	struct iter_log			log;
	int				i, seen32;

	memset(&bad_fs, 0, sizeof(bad_fs));
	memset(&bad_db, 0, sizeof(bad_db));

	/* 1. Magic and empty-list error checks */
	CHECK_RC(ext2fs_init_dblist(&bad_fs, &db), EXT2_ET_MAGIC_EXT2FS_FILSYS,
		 "init_dblist bad fs magic");
	CHECK_RC(ext2fs_add_dir_block2(&bad_db, 2, 10, 0),
		 EXT2_ET_MAGIC_DBLIST, "add_dir_block2 bad magic");
	CHECK_RC(ext2fs_set_dir_block2(&bad_db, 2, 10, 0),
		 EXT2_ET_MAGIC_DBLIST, "set_dir_block2 bad magic");
	CHECK_RC(ext2fs_dblist_iterate2(&bad_db, record_db2_cb, &log),
		 EXT2_ET_MAGIC_DBLIST, "dblist_iterate2 bad magic");
	CHECK_RC(ext2fs_dblist_iterate(&bad_db, mutate_db32_cb, &seen32),
		 EXT2_ET_MAGIC_DBLIST, "dblist_iterate bad magic");
	CHECK_RC(ext2fs_dblist_get_last2(&bad_db, &last2),
		 EXT2_ET_MAGIC_DBLIST, "dblist_get_last2 bad magic");
	CHECK_RC(ext2fs_dblist_get_last(&bad_db, &last32),
		 EXT2_ET_MAGIC_DBLIST, "dblist_get_last bad magic");
	CHECK_RC(ext2fs_dblist_drop_last(&bad_db),
		 EXT2_ET_MAGIC_DBLIST, "dblist_drop_last bad magic");

	/* Attach to fs->dblist when ret_dblist == NULL */
	CHECK_RC(ext2fs_init_dblist(fs, NULL), 0, "init fs->dblist");
	CHECK(fs->dblist != NULL && ext2fs_dblist_count2(fs->dblist) == 0,
	      "fs->dblist created empty");

	/* Independent dblist */
	CHECK_RC(ext2fs_init_dblist(fs, &db), 0, "init standalone dblist");
	CHECK_RC(ext2fs_dblist_get_last2(db, &last2), EXT2_ET_DBLIST_EMPTY,
		 "get_last2 on empty dblist");
	CHECK_RC(ext2fs_dblist_get_last(db, &last32), EXT2_ET_DBLIST_EMPTY,
		 "get_last on empty dblist");
	CHECK_RC(ext2fs_dblist_drop_last(db), EXT2_ET_DBLIST_EMPTY,
		 "drop_last on empty dblist");

	/* 2. Dynamic array growth past both <=200 and >200 thresholds */
	for (i = 0; i < 250; i++) {
		if (ext2fs_add_dir_block2(db, 100 + i, 500 + i, 0)) {
			CHECK(0, "add_dir_block2 during 250-entry growth");
			break;
		}
	}
	CHECK(ext2fs_dblist_count2(db) == 250 && ext2fs_dblist_count(db) == 250,
	      "dblist_count2 and dblist_count == 250");
	CHECK_RC(ext2fs_dblist_get_last2(db, NULL), 0,
		 "get_last2 with NULL entry");
	CHECK_RC(ext2fs_dblist_get_last(db, NULL), 0,
		 "get_last with NULL entry");
	CHECK_RC(ext2fs_dblist_get_last2(db, &last2), 0, "get_last2");
	CHECK(last2->ino == 349 && last2->blk == 749, "last2 values match");
	CHECK_RC(ext2fs_dblist_get_last(db, &last32), 0, "get_last 32-bit");
	CHECK(last32->ino == 349 && last32->blk == 749, "last32 values match");
	CHECK_RC(ext2fs_dblist_drop_last(db), 0, "drop_last");
	CHECK(ext2fs_dblist_count2(db) == 249, "count decremented to 249");
	ext2fs_free_dblist(db);

	/* 3. Sorting, tie-breaking, set_dir_block, copy, subrange & 32-bit */
	CHECK_RC(ext2fs_init_dblist(fs, &db), 0, "re-init dblist");
	CHECK_RC(ext2fs_add_dir_block2(db, 20, 300, 1), 0, "add (20,300,1)");
	CHECK_RC(ext2fs_add_dir_block(db, 12, 100, 0), 0, "add32 (12,100,0)");
	CHECK_RC(ext2fs_add_dir_block2(db, 20, 200, 1), 0, "add (20,200,1)");
	CHECK_RC(ext2fs_add_dir_block2(db, 20, 200, 0), 0, "add (20,200,0)");
	CHECK_RC(ext2fs_add_dir_block2(db, 15, 200, 0), 0, "add (15,200,0)");

	CHECK_RC(ext2fs_set_dir_block2(db, 99, 400, 0), EXT2_ET_DB_NOT_FOUND,
		 "set_dir_block2 missing entry returns DB_NOT_FOUND");
	CHECK_RC(ext2fs_set_dir_block(db, 20, 50, 1), 0,
		 "set_dir_block updates first matching (20, *, 1) -> blk 50");

	/* Copy dblist and mutate copy independently */
	CHECK_RC(ext2fs_copy_dblist(db, &copy), 0, "copy_dblist");
	CHECK(ext2fs_dblist_count2(copy) == 5, "copied count == 5");
	CHECK_RC(ext2fs_dblist_drop_last(copy), 0, "drop_last on copy");
	CHECK(ext2fs_dblist_count2(copy) == 4 && ext2fs_dblist_count2(db) == 5,
	      "copy mutation leaves original count == 5");
	ext2fs_free_dblist(copy);

	/* Iterate auto-sorts by (blk, ino, blockcnt):
	 * (20,50,1), (12,100,0), (15,200,0), (20,200,0), (20,200,1)
	 */
	memset(&log, 0, sizeof(log));
	CHECK_RC(ext2fs_dblist_iterate2(db, record_db2_cb, &log), 0,
		 "dblist_iterate2 auto-sorts");
	CHECK(log.count == 5 &&
	      log.entries[0].blk == 50 && log.entries[0].ino == 20 &&
	      log.entries[1].blk == 100 && log.entries[1].ino == 12 &&
	      log.entries[2].blk == 200 && log.entries[2].ino == 15 &&
	      log.entries[3].blk == 200 && log.entries[3].ino == 20 &&
	      log.entries[3].blockcnt == 0 &&
	      log.entries[4].blk == 200 && log.entries[4].ino == 20 &&
	      log.entries[4].blockcnt == 1,
	      "default sort order (blk, ino, blockcnt) verified");

	/* Subrange iterate3 + end clamping + DBLIST_ABORT */
	memset(&log, 0, sizeof(log));
	CHECK_RC(ext2fs_dblist_iterate3(db, record_db2_cb, 3, 10, &log), 0,
		 "dblist_iterate3 clamps end to count");
	CHECK(log.count == 2 && log.entries[0].ino == 20 &&
	      log.entries[0].blockcnt == 0 && log.entries[1].blockcnt == 1,
	      "subrange iterate3 visited indices 3..4");

	memset(&log, 0, sizeof(log));
	log.abort_at = 2;
	CHECK_RC(ext2fs_dblist_iterate2(db, record_db2_cb, &log), 0,
		 "dblist_iterate2 with DBLIST_ABORT");
	CHECK(log.count == 2, "DBLIST_ABORT stopped after 2 entries");

	/* Custom comparators for sort2 and legacy 32-bit sort */
	ext2fs_dblist_sort2(db, rev_ino_cmp2);
	memset(&log, 0, sizeof(log));
	CHECK_RC(ext2fs_dblist_iterate2(db, record_db2_cb, &log), 0,
		 "iterate after custom sort2");
	CHECK(log.entries[0].ino == 20 && log.entries[4].ino == 12,
	      "custom sort2 descending by ino");

	ext2fs_dblist_sort(db, rev_blk_cmp32);
	memset(&log, 0, sizeof(log));
	CHECK_RC(ext2fs_dblist_iterate2(db, record_db2_cb, &log), 0,
		 "iterate after legacy 32-bit custom sort");
	CHECK(log.entries[0].blk == 200 && log.entries[4].blk == 50,
	      "legacy 32-bit sort descending by blk");

	/* Legacy 32-bit iterate writes back modified fields */
	seen32 = 0;
	CHECK_RC(ext2fs_dblist_iterate(db, mutate_db32_cb, &seen32), 0,
		 "legacy 32-bit dblist_iterate");
	CHECK(seen32 == 5, "32-bit iterate visited all 5 entries");
	memset(&log, 0, sizeof(log));
	CHECK_RC(ext2fs_dblist_iterate2(db, record_db2_cb, &log), 0,
		 "verify 32-bit writeback");
	CHECK(log.entries[0].blk == 1200 && log.entries[4].blk == 1050,
	      "32-bit passthrough wrote modified blk back to entries");

	ext2fs_free_dblist(db);

	/*
	 * 4. 64-bit blk, high-bit ext2_ino_t, and 64-bit e2_blkcnt_t sort
	 *    ordering without 32-bit int subtraction truncation/wraparound.
	 */
	CHECK_RC(ext2fs_init_dblist(fs, &db), 0, "init 64-bit sort dblist");
	CHECK_RC(ext2fs_add_dir_block2(db, 10, (1ULL << 32) + 100, 0), 0,
		 "add blk 2^32+100");
	CHECK_RC(ext2fs_add_dir_block2(db, 10, 0x90000000ULL, 0), 0,
		 "add blk 0x90000000");
	CHECK_RC(ext2fs_add_dir_block2(db, 10, 100ULL, 0), 0,
		 "add blk 100");
	CHECK_RC(ext2fs_add_dir_block2(db, 10, 0x50000000ULL, 0), 0,
		 "add blk 0x50000000");
	CHECK_RC(ext2fs_add_dir_block2(db, 10, 1ULL, 0), 0,
		 "add blk 1");
	CHECK_RC(ext2fs_add_dir_block2(db, 0x90000000U, (1ULL << 33),
				       (1LL << 32) + 5), 0,
		 "add high ino + high blockcnt");
	CHECK_RC(ext2fs_add_dir_block2(db, 0x90000000U, (1ULL << 33), 5), 0,
		 "add high ino + low blockcnt");
	CHECK_RC(ext2fs_add_dir_block2(db, 0x90000000U, (1ULL << 33), -1), 0,
		 "add high ino + negative blockcnt");
	CHECK_RC(ext2fs_add_dir_block2(db, 12U, (1ULL << 33), 0), 0,
		 "add low ino at 2^33");
	CHECK_RC(ext2fs_add_dir_block2(db, 0x50000000U, (1ULL << 33), 0), 0,
		 "add mid ino at 2^33");

	memset(&log, 0, sizeof(log));
	CHECK_RC(ext2fs_dblist_iterate2(db, record_db2_cb, &log), 0,
		 "dblist_iterate2 sorts 64-bit values");
	CHECK(log.count == 10 &&
	      log.entries[0].blk == 1ULL &&
	      log.entries[1].blk == 100ULL &&
	      log.entries[2].blk == 0x50000000ULL &&
	      log.entries[3].blk == 0x90000000ULL &&
	      log.entries[4].blk == (1ULL << 32) + 100,
	      "64-bit blk values sorted monotonically");
	CHECK(log.entries[5].ino == 12U &&
	      log.entries[6].ino == 0x50000000U &&
	      log.entries[7].ino == 0x90000000U &&
	      log.entries[7].blockcnt == -1 &&
	      log.entries[8].ino == 0x90000000U &&
	      log.entries[8].blockcnt == 5 &&
	      log.entries[9].ino == 0x90000000U &&
	      log.entries[9].blockcnt == (1LL << 32) + 5,
	      "high-bit ino and 64-bit blockcnt sorted monotonically");
	ext2fs_free_dblist(db);
}

struct dir_visit {
	int	seen_blk_sub;
	int	seen_inl_sub;
	int	seen_inl_dot;
	int	seen_blk_sub_dot;
	int	total;
	int	abort_on_root;
};

static int dir_iter_cb(ext2_ino_t dir, int entry EXT2FS_ATTR((unused)),
		       struct ext2_dir_entry *dirent,
		       int offset EXT2FS_ATTR((unused)),
		       int blocksize EXT2FS_ATTR((unused)),
		       char *buf EXT2FS_ATTR((unused)), void *priv_data)
{
	struct dir_visit	*v = (struct dir_visit *) priv_data;
	int			len = ext2fs_dirent_name_len(dirent);

	v->total++;
	if (dir == EXT2_ROOT_INO && len == 7 &&
	    memcmp(dirent->name, "blk_sub", 7) == 0)
		v->seen_blk_sub = 1;
	if (dir == EXT2_ROOT_INO && len == 7 &&
	    memcmp(dirent->name, "inl_sub", 7) == 0)
		v->seen_inl_sub = 1;
	if (dir == 11 && len == 1 && dirent->name[0] == '.')
		v->seen_blk_sub_dot++;
	if (dir == 12 && len == 1 && dirent->name[0] == '.')
		v->seen_inl_dot++;
	if (v->abort_on_root && dir == EXT2_ROOT_INO)
		return DIRENT_ABORT;
	return 0;
}

static void test_dblist_dir_iterate(ext2_filsys fs)
{
	struct ext2_struct_dblist	bad_db;
	struct ext2_inode		inode;
	ext2_ino_t			blk_sub = 0, inl_sub = 0;
	blk64_t				root_blk = 0, sub_blk = 0;
	struct dir_visit		visit;
	char				*caller_buf = NULL;

	memset(&bad_db, 0, sizeof(bad_db));
	CHECK_RC(ext2fs_dblist_dir_iterate(&bad_db, 0, NULL, dir_iter_cb,
					   &visit),
		 EXT2_ET_MAGIC_DBLIST, "dblist_dir_iterate bad magic");

	/* Create block-backed root dir, block-backed subdir, and inline dir */
	CHECK_RC(ext2fs_mkdir(fs, EXT2_ROOT_INO, EXT2_ROOT_INO, NULL), 0,
		 "mkdir root (block-backed)");
	ext2fs_clear_feature_inline_data(fs->super);
	CHECK_RC(ext2fs_mkdir2(fs, EXT2_ROOT_INO, 0, 0, 0, "blk_sub",
			       &blk_sub), 0, "mkdir blk_sub (block-backed)");
	ext2fs_set_feature_inline_data(fs->super);
	CHECK_RC(ext2fs_mkdir2(fs, EXT2_ROOT_INO, 0, 0, 0, "inl_sub",
			       &inl_sub), 0, "mkdir inl_sub (inline_data)");

	CHECK_RC(ext2fs_read_inode(fs, inl_sub, &inode), 0, "read inl_sub");
	CHECK((inode.i_flags & EXT4_INLINE_DATA_FL) != 0,
	      "inl_sub has EXT4_INLINE_DATA_FL");

	CHECK_RC(ext2fs_bmap2(fs, EXT2_ROOT_INO, NULL, NULL, 0, 0, NULL,
			      &root_blk), 0, "bmap root_blk");
	CHECK_RC(ext2fs_bmap2(fs, blk_sub, NULL, NULL, 0, 0, NULL, &sub_blk),
		 0, "bmap sub_blk");

	CHECK_RC(ext2fs_add_dir_block2(fs->dblist, EXT2_ROOT_INO, root_blk, 0),
		 0, "add root_blk to fs->dblist");
	CHECK_RC(ext2fs_add_dir_block2(fs->dblist, blk_sub, sub_blk, 0),
		 0, "add sub_blk to fs->dblist");
	CHECK_RC(ext2fs_add_dir_block2(fs->dblist, inl_sub, 0, 0),
		 0, "add inl_sub to fs->dblist");

	/* Iterate with NULL block_buf (internal buffer allocation) */
	memset(&visit, 0, sizeof(visit));
	CHECK_RC(ext2fs_dblist_dir_iterate(fs->dblist, 0, NULL, dir_iter_cb,
					   &visit),
		 0, "dblist_dir_iterate with internal buf");
	CHECK(visit.seen_blk_sub && visit.seen_inl_sub &&
	      visit.seen_blk_sub_dot == 1 && visit.seen_inl_dot == 1 &&
	      visit.total >= 8,
	      "visited both block-backed and inline-data directories");

	/* Iterate with caller-supplied block_buf and DIRENT_ABORT */
	CHECK_RC(ext2fs_get_mem(fs->blocksize, &caller_buf), 0,
		 "alloc caller_buf");
	memset(&visit, 0, sizeof(visit));
	visit.abort_on_root = 1;
	CHECK_RC(ext2fs_dblist_dir_iterate(fs->dblist, 0, caller_buf,
					   dir_iter_cb, &visit),
		 0, "dblist_dir_iterate with caller_buf + DIRENT_ABORT");
	CHECK(!visit.seen_blk_sub && visit.seen_blk_sub_dot == 0,
	      "DIRENT_ABORT stopped dblist_dir_iterate before blk_sub");
	ext2fs_free_mem(&caller_buf);

	/* Invalid inode number in dblist propagates read_inode error */
	CHECK_RC(ext2fs_add_dir_block2(fs->dblist, 9999, root_blk, 0), 0,
		 "add invalid ino 9999");
	memset(&visit, 0, sizeof(visit));
	CHECK_RC(ext2fs_dblist_dir_iterate(fs->dblist, 0, NULL, dir_iter_cb,
					   &visit),
		 EXT2_ET_BAD_INODE_NUM,
		 "dblist_dir_iterate propagates read_inode error");
}

int main(int argc EXT2FS_ATTR((unused)), char **argv EXT2FS_ATTR((unused)))
{
	struct ext2_super_block	param;
	ext2_filsys		fs = NULL;
	char			path[64];
	int			fd;

	add_error_table(&et_ext2_error_table);

	strcpy(path, "/tmp/tst_dblist.XXXXXX");
	fd = mkstemp(path);
	if (fd < 0) {
		perror("mkstemp");
		return 1;
	}
	if (ftruncate(fd, 512 * 1024) < 0) {
		perror("ftruncate");
		close(fd);
		unlink(path);
		return 1;
	}

	memset(&param, 0, sizeof(param));
	ext2fs_blocks_count_set(&param, 512);
	param.s_inodes_count = 64;
	param.s_rev_level = EXT2_DYNAMIC_REV;
	param.s_inode_size = 256;
	ext2fs_set_feature_extents(&param);
	ext2fs_set_feature_inline_data(&param);
	ext2fs_set_feature_filetype(&param);

	if (ext2fs_initialize(path, EXT2_FLAG_RW | EXT2_FLAG_64BITS,
			      &param, unix_io_manager, &fs) ||
	    ext2fs_allocate_tables(fs)) {
		fprintf(stderr, "Failed to initialize test filesystem\n");
		close(fd);
		unlink(path);
		return 1;
	}

	test_dblist_core(fs);
	test_dblist_dir_iterate(fs);

	ext2fs_close_free(&fs);
	close(fd);
	unlink(path);

	if (fail_count) {
		fprintf(stderr, "tst_dblist: %d/%d checks FAILED\n",
			fail_count, test_count);
		return 1;
	}
	printf("tst_dblist: all %d checks passed\n", test_count);
	return 0;
}
