/*
 * This testing program makes sure the badblocks implementation works.
 *
 * Copyright (C) 1996 by Theodore Ts'o.
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
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#if HAVE_ERRNO_H
#include <errno.h>
#endif

#include "ext2_fs.h"
#include "ext2fs.h"

#define ADD_BLK	0x0001
#define DEL_BLK	0x0002

blk_t test1[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 0 };
blk_t test2[] = { 11, 10, 9, 8, 7, 6, 5, 4, 3, 3, 2, 1, 0 };
blk_t test3[] = { 3, 1, 4, 5, 9, 2, 7, 10, 5, 6, 10, 8, 0 };
blk_t test4[] = { 20, 50, 12, 17, 13, 2, 66, 23, 56, 0 };
blk_t test4a[] = {
	20, 1,
	50, 1,
	3, 0,
	17, 1,
	18, 0,
	16, 0,
	11, 0,
	12, 1,
	13, 1,
	14, 0,
	80, 0,
	45, 0,
	66, 1,
	0 };
blk_t test5[] = { 31, 20, 17, 51, 23, 1, 56, 57, 0 };
blk_t test5a[] = {
	50, ADD_BLK,
	51, DEL_BLK,
	57, DEL_BLK,
	66, ADD_BLK,
	31, DEL_BLK,
	12, ADD_BLK,
	2, ADD_BLK,
	13, ADD_BLK,
	1, DEL_BLK,
	0
	};


static int test_fail = 0;
static int test_expected_fail = 0;

static errcode_t create_test_list(blk_t *vec, badblocks_list *ret)
{
	errcode_t	retval;
	badblocks_list	bb;
	int		i;

	retval = ext2fs_badblocks_list_create(&bb, 5);
	if (retval) {
		com_err("create_test_list", retval, "while creating list");
		return retval;
	}
	for (i=0; vec[i]; i++) {
		retval = ext2fs_badblocks_list_add(bb, vec[i]);
		if (retval) {
			com_err("create_test_list", retval,
				"while adding test vector %d", i);
			ext2fs_badblocks_list_free(bb);
			return retval;
		}
	}
	*ret = bb;
	return 0;
}

static void print_list(badblocks_list bb, int verify)
{
	errcode_t	retval;
	badblocks_iterate	iter;
	blk_t			blk;
	int			i, ok;

	retval = ext2fs_badblocks_list_iterate_begin(bb, &iter);
	if (retval) {
		com_err("print_list", retval, "while setting up iterator");
		return;
	}
	ok = i = 1;
	while (ext2fs_badblocks_list_iterate(iter, &blk)) {
		printf("%u ", blk);
		if (i++ != blk)
			ok = 0;
	}
	ext2fs_badblocks_list_iterate_end(iter);
	if (verify) {
		if (ok)
			printf("--- OK");
		else {
			printf("--- NOT OK");
			test_fail++;
		}
	}
}

static void validate_test_seq(badblocks_list bb, blk_t *vec)
{
	int	i, match, ok;

	for (i = 0; vec[i]; i += 2) {
		match = ext2fs_badblocks_list_test(bb, vec[i]);
		if (match == vec[i+1])
			ok = 1;
		else {
			ok = 0;
			test_fail++;
		}
		printf("\tblock %u is %s --- %s\n", vec[i],
		       match ? "present" : "absent",
		       ok ? "OK" : "NOT OK");
	}
}

static void do_test_seq(badblocks_list bb, blk_t *vec)
{
	int	i, match;

	for (i = 0; vec[i]; i += 2) {
		switch (vec[i+1]) {
		case ADD_BLK:
			ext2fs_badblocks_list_add(bb, vec[i]);
			match = ext2fs_badblocks_list_test(bb, vec[i]);
			printf("Adding block %u --- now %s\n", vec[i],
			       match ? "present" : "absent");
			if (!match) {
				printf("FAILURE!\n");
				test_fail++;
			}
			break;
		case DEL_BLK:
			ext2fs_badblocks_list_del(bb, vec[i]);
			match = ext2fs_badblocks_list_test(bb, vec[i]);
			printf("Removing block %u --- now %s\n", vec[i],
			       ext2fs_badblocks_list_test(bb, vec[i]) ?
			       "present" : "absent");
			if (match) {
				printf("FAILURE!\n");
				test_fail++;
			}
			break;
		}
	}
}


static int file_test(badblocks_list bb)
{
	badblocks_list new_bb = 0;
	errcode_t	retval;
	FILE	*f;

	f = tmpfile();
	if (!f) {
		fprintf(stderr, "Error opening temp file: %s\n",
			error_message(errno));
		return 1;
	}
	retval = ext2fs_write_bb_FILE(bb, 0, f);
	if (retval) {
		com_err("file_test", retval, "while writing bad blocks");
		return 1;
	}

	rewind(f);
	retval = ext2fs_read_bb_FILE2(0, f, &new_bb, 0, 0);
	if (retval) {
		com_err("file_test", retval, "while reading bad blocks");
		return 1;
	}
	fclose(f);

	if (ext2fs_badblocks_equal(bb, new_bb)) {
		printf("Block bitmap matched after reading and writing.\n");
	} else {
		printf("Block bitmap NOT matched.\n");
		test_fail++;
	}
	ext2fs_badblocks_list_free(new_bb);
	return 0;
}

static void invalid_proc(ext2_filsys fs EXT2FS_ATTR((unused)), blk_t blk)
{
	if (blk == 34500) {
		printf("Expected invalid block\n");
		test_expected_fail++;
	} else {
		printf("Invalid block #: %u\n", blk);
		test_fail++;
	}
}

static void file_test_invalid(badblocks_list bb)
{
	badblocks_list new_bb = 0;
	errcode_t	retval;
	ext2_filsys 	fs;
	FILE	*f;

	fs = malloc(sizeof(struct struct_ext2_filsys));
	memset(fs, 0, sizeof(struct struct_ext2_filsys));
	fs->magic = EXT2_ET_MAGIC_EXT2FS_FILSYS;
	fs->super = malloc(SUPERBLOCK_SIZE);
	memset(fs->super, 0, SUPERBLOCK_SIZE);
	fs->super->s_first_data_block = 1;
	ext2fs_blocks_count_set(fs->super, 100);

	f = tmpfile();
	if (!f) {
		fprintf(stderr, "Error opening temp file: %s\n",
			error_message(errno));
		test_fail++;
		goto out;
	}
	retval = ext2fs_write_bb_FILE(bb, 0, f);
	if (retval) {
		com_err("file_test", retval, "while writing bad blocks");
		test_fail++;
		goto out;
	}
	fprintf(f, "34500\n");

	rewind(f);
	test_expected_fail = 0;
	retval = ext2fs_read_bb_FILE(fs, f, &new_bb, invalid_proc);
	if (retval) {
		com_err("file_test", retval, "while reading bad blocks");
		test_fail++;
		goto out;
	}
	fclose(f);
	if (!test_expected_fail) {
		printf("Expected test failure didn't happen!\n");
		test_fail++;
	}


	if (ext2fs_badblocks_equal(bb, new_bb)) {
		printf("Block bitmap matched after reading and writing.\n");
	} else {
		printf("Block bitmap NOT matched.\n");
		test_fail++;
	}
	ext2fs_badblocks_list_free(new_bb);
out:
	free(fs->super);
	free(fs);
}

#define CHECK_BB(cond, msg) do { \
	if (!(cond)) { \
		fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, msg); \
		test_fail++; \
	} \
} while (0)

static void bb_inode_test(badblocks_list sample)
{
	struct ext2_super_block		param;
	struct struct_ext2_filsys	bad_fs;
	struct ext2_inode		inode;
	ext2fs_block_bitmap		saved_bmap;
	ext2_filsys			fs = NULL;
	ext2_badblocks_list		bb_copy = NULL, bb_direct = NULL;
	ext2_badblocks_list		bb_ind = NULL, bb_ind2 = NULL;
	ext2_badblocks_list		bb_first = NULL, bb_ind_bad = NULL;
	ext2_badblocks_list		read_bb = NULL, pre_bb = NULL;
	blk64_t				initial_free, first_free, cand;
	blk_t				ind_blk, new_ind_blk;
	char				path[64];
	int				fd, i, added;

	/* 1. Copy and count helpers */
	CHECK_BB(ext2fs_badblocks_copy(sample, &bb_copy) == 0,
		 "ext2fs_badblocks_copy");
	CHECK_BB(ext2fs_badblocks_equal(sample, bb_copy) == 1,
		 "copied badblocks list equals original");
	CHECK_BB(ext2fs_u32_list_count((ext2_u32_list) bb_copy) ==
		 ext2fs_u32_list_count((ext2_u32_list) sample),
		 "ext2fs_u32_list_count matches");
	CHECK_BB(ext2fs_badblocks_list_add(bb_copy, 999) == 0,
		 "mutate copied list");
	CHECK_BB(ext2fs_badblocks_equal(sample, bb_copy) == 0,
		 "mutated copy leaves original unchanged");
	ext2fs_badblocks_list_free(bb_copy);

	/* 2. Set up a 1024-block (1K blocksize) filesystem backed by /tmp */
	strcpy(path, "/tmp/tst_badblocks.XXXXXX");
	fd = mkstemp(path);
	if (fd < 0) {
		perror("mkstemp");
		test_fail++;
		return;
	}
	if (ftruncate(fd, 1024 * 1024) < 0) {
		perror("ftruncate");
		close(fd);
		unlink(path);
		test_fail++;
		return;
	}

	memset(&param, 0, sizeof(param));
	ext2fs_blocks_count_set(&param, 1024);
	param.s_inodes_count = 128;
	param.s_rev_level = EXT2_DYNAMIC_REV;
	param.s_inode_size = 128;
	if (ext2fs_initialize(path, EXT2_FLAG_RW | EXT2_FLAG_64BITS,
			      &param, unix_io_manager, &fs) ||
	    ext2fs_allocate_tables(fs)) {
		fprintf(stderr, "Failed to initialize test filesystem\n");
		close(fd);
		unlink(path);
		test_fail++;
		return;
	}
	fs->now = 1700000000;
	initial_free = ext2fs_free_blocks_count(fs->super);

	/* 3. Error paths: bad magic and missing block_map */
	memset(&bad_fs, 0, sizeof(bad_fs));
	CHECK_BB(ext2fs_update_bb_inode(&bad_fs, NULL) ==
		 EXT2_ET_MAGIC_EXT2FS_FILSYS, "update_bb_inode bad magic");
	CHECK_BB(ext2fs_read_bb_inode(&bad_fs, &read_bb) ==
		 EXT2_ET_MAGIC_EXT2FS_FILSYS, "read_bb_inode bad magic");

	saved_bmap = fs->block_map;
	fs->block_map = NULL;
	CHECK_BB(ext2fs_update_bb_inode(fs, NULL) == EXT2_ET_NO_BLOCK_BITMAP,
		 "update_bb_inode without block_map");
	fs->block_map = saved_bmap;

	/* 4. Direct bad blocks (<= 12) */
	CHECK_BB(ext2fs_badblocks_list_create(&bb_direct, 8) == 0,
		 "create bb_direct");
	for (i = 0; i < 5; i++)
		CHECK_BB(ext2fs_badblocks_list_add(bb_direct, 100 + i * 5) == 0,
			 "add direct bad block");

	CHECK_BB(ext2fs_update_bb_inode(fs, bb_direct) == 0,
		 "update_bb_inode with 5 direct blocks");
	for (i = 0; i < 5; i++)
		CHECK_BB(ext2fs_test_block_bitmap2(fs->block_map,
						   100 + i * 5) != 0,
			 "direct bad block marked in block_map");
	CHECK_BB(ext2fs_free_blocks_count(fs->super) == initial_free - 5,
		 "free_blocks_count decremented by 5");
	CHECK_BB(ext2fs_read_inode(fs, EXT2_BAD_INO, &inode) == 0 &&
		 EXT2_I_SIZE(&inode) == 5 * fs->blocksize &&
		 inode.i_block[EXT2_IND_BLOCK] == 0,
		 "EXT2_BAD_INO size and no IND block");

	read_bb = NULL;
	CHECK_BB(ext2fs_read_bb_inode(fs, &read_bb) == 0,
		 "read_bb_inode direct");
	CHECK_BB(ext2fs_badblocks_equal(bb_direct, read_bb) == 1,
		 "read_bb_inode matches direct list");
	ext2fs_badblocks_list_free(read_bb);

	/* 5. Indirect bad blocks (> 12, early free blocks available first) */
	CHECK_BB(ext2fs_badblocks_list_create(&bb_ind, 24) == 0,
		 "create bb_ind");
	for (i = 0; i < 20; i++)
		CHECK_BB(ext2fs_badblocks_list_add(bb_ind, 200 + i * 2) == 0,
			 "add indirect bad block");

	CHECK_BB(ext2fs_update_bb_inode(fs, bb_ind) == 0,
		 "update_bb_inode with 20 bad blocks (indirect)");
	for (i = 0; i < 5; i++)
		CHECK_BB(ext2fs_test_block_bitmap2(fs->block_map,
						   100 + i * 5) == 0,
			 "old direct bad block freed");
	for (i = 0; i < 20; i++)
		CHECK_BB(ext2fs_test_block_bitmap2(fs->block_map,
						   200 + i * 2) != 0,
			 "new bad block marked");
	CHECK_BB(ext2fs_read_inode(fs, EXT2_BAD_INO, &inode) == 0,
		 "read EXT2_BAD_INO after indirect update");
	ind_blk = inode.i_block[EXT2_IND_BLOCK];
	CHECK_BB(ind_blk != 0 &&
		 ext2fs_test_block_bitmap2(fs->block_map, ind_blk) != 0,
		 "indirect block allocated and marked");
	CHECK_BB(EXT2_I_SIZE(&inode) == 20 * fs->blocksize,
		 "EXT2_BAD_INO size == 20 blocks");
	CHECK_BB(inode.i_blocks == 21 * (fs->blocksize / 512),
		 "EXT2_BAD_INO i_blocks includes indirect block (21 blocks)");
	CHECK_BB(ext2fs_free_blocks_count(fs->super) == initial_free - 21,
		 "free_blocks_count reflects 20 bad + 1 indirect");

	read_bb = NULL;
	CHECK_BB(ext2fs_read_bb_inode(fs, &read_bb) == 0,
		 "read_bb_inode indirect (auto-create)");
	CHECK_BB(ext2fs_badblocks_equal(bb_ind, read_bb) == 1,
		 "read_bb_inode matches 20 bad blocks without ind_blk");
	ext2fs_badblocks_list_free(read_bb);

	/* Merge into pre-allocated badblocks list */
	CHECK_BB(ext2fs_badblocks_list_create(&pre_bb, 4) == 0 &&
		 ext2fs_badblocks_list_add(pre_bb, 90) == 0,
		 "create pre-populated badblocks list");
	CHECK_BB(ext2fs_read_bb_inode(fs, &pre_bb) == 0 &&
		 ext2fs_u32_list_count((ext2_u32_list) pre_bb) == 21 &&
		 ext2fs_badblocks_list_test(pre_bb, 90) == 1 &&
		 ext2fs_badblocks_list_test(pre_bb, 238) == 1,
		 "read_bb_inode merges into existing list");
	ext2fs_badblocks_list_free(pre_bb);

	/* 6. Replace indirect list -> reuses saved indirect block */
	CHECK_BB(ext2fs_badblocks_list_create(&bb_ind2, 20) == 0,
		 "create bb_ind2");
	for (i = 0; i < 16; i++)
		CHECK_BB(ext2fs_badblocks_list_add(bb_ind2, 300 + i * 3) == 0,
			 "add replacement bad block");
	CHECK_BB(ext2fs_update_bb_inode(fs, bb_ind2) == 0,
		 "replace indirect badblocks list");
	CHECK_BB(ext2fs_read_inode(fs, EXT2_BAD_INO, &inode) == 0 &&
		 inode.i_block[EXT2_IND_BLOCK] == ind_blk,
		 "saved indirect block reused");
	CHECK_BB(ext2fs_free_blocks_count(fs->super) == initial_free - 17,
		 "free_blocks_count reflects 16 bad + 1 indirect");
	read_bb = NULL;
	CHECK_BB(ext2fs_read_bb_inode(fs, &read_bb) == 0 &&
		 ext2fs_badblocks_equal(bb_ind2, read_bb) == 1,
		 "replacement indirect list round-trip matches");
	ext2fs_badblocks_list_free(read_bb);

	/* 7. Clear badblocks inode (bb_list == NULL) */
	CHECK_BB(ext2fs_update_bb_inode(fs, NULL) == 0,
		 "clear EXT2_BAD_INO with NULL list");
	CHECK_BB(ext2fs_test_block_bitmap2(fs->block_map, ind_blk) == 0,
		 "indirect block freed on clear");
	CHECK_BB(ext2fs_free_blocks_count(fs->super) == initial_free,
		 "all bad and indirect blocks returned to free count");
	CHECK_BB(ext2fs_read_inode(fs, EXT2_BAD_INO, &inode) == 0 &&
		 EXT2_I_SIZE(&inode) == 0 && inode.i_blocks == 0 &&
		 inode.i_block[0] == 0 && inode.i_block[EXT2_IND_BLOCK] == 0,
		 "EXT2_BAD_INO completely cleared");

	/*
	 * 8. Bad blocks occupying the very first free blocks on the
	 * filesystem: indirect block allocation must NOT pick a block
	 * from bb_first, nor reuse a saved indirect block that itself
	 * went bad in a subsequent update.
	 */
	first_free = 0;
	CHECK_BB(ext2fs_new_block2(fs, 0, fs->block_map, &first_free) == 0,
		 "locate first free block");
	CHECK_BB(ext2fs_badblocks_list_create(&bb_first, 24) == 0,
		 "create bb_first");
	added = 0;
	for (cand = first_free;
	     cand < ext2fs_blocks_count(fs->super) && added < 20; cand++) {
		if (!ext2fs_test_block_bitmap2(fs->block_map, cand)) {
			CHECK_BB(ext2fs_badblocks_list_add(bb_first,
							   (blk_t) cand) == 0,
				 "add first-free bad block");
			added++;
		}
	}
	CHECK_BB(added == 20, "collected 20 lowest free blocks");
	CHECK_BB(ext2fs_update_bb_inode(fs, bb_first) == 0,
		 "update_bb_inode with lowest 20 free blocks");
	CHECK_BB(ext2fs_read_inode(fs, EXT2_BAD_INO, &inode) == 0,
		 "read EXT2_BAD_INO after bb_first");
	ind_blk = inode.i_block[EXT2_IND_BLOCK];
	CHECK_BB(ind_blk != 0 &&
		 ext2fs_badblocks_list_test(bb_first, ind_blk) == 0,
		 "indirect block not allocated from badblocks list");
	CHECK_BB(inode.i_blocks == 21 * (fs->blocksize / 512),
		 "bb_first i_blocks == 21 blocks");
	CHECK_BB(ext2fs_free_blocks_count(fs->super) == initial_free - 21,
		 "bb_first free_blocks_count == initial_free - 21");
	read_bb = NULL;
	CHECK_BB(ext2fs_read_bb_inode(fs, &read_bb) == 0 &&
		 ext2fs_badblocks_equal(bb_first, read_bb) == 1,
		 "bb_first round-trip matches exactly");
	ext2fs_badblocks_list_free(read_bb);

	/* Saved indirect block itself goes bad in replacement list */
	CHECK_BB(ext2fs_badblocks_copy(bb_first, &bb_ind_bad) == 0 &&
		 ext2fs_badblocks_list_add(bb_ind_bad, ind_blk) == 0,
		 "mark previous indirect block bad");
	CHECK_BB(ext2fs_update_bb_inode(fs, bb_ind_bad) == 0,
		 "update_bb_inode when old indirect block went bad");
	CHECK_BB(ext2fs_read_inode(fs, EXT2_BAD_INO, &inode) == 0,
		 "read EXT2_BAD_INO after old ind_blk marked bad");
	new_ind_blk = inode.i_block[EXT2_IND_BLOCK];
	CHECK_BB(new_ind_blk != 0 && new_ind_blk != ind_blk &&
		 ext2fs_badblocks_list_test(bb_ind_bad, new_ind_blk) == 0,
		 "old bad indirect block not reused as indirect block");
	CHECK_BB(inode.i_blocks == 22 * (fs->blocksize / 512) &&
		 ext2fs_free_blocks_count(fs->super) == initial_free - 22,
		 "accounting reflects 21 bad + 1 new indirect block");
	read_bb = NULL;
	CHECK_BB(ext2fs_read_bb_inode(fs, &read_bb) == 0 &&
		 ext2fs_badblocks_equal(bb_ind_bad, read_bb) == 1,
		 "bb_ind_bad round-trip matches");
	ext2fs_badblocks_list_free(read_bb);
	CHECK_BB(ext2fs_update_bb_inode(fs, NULL) == 0 &&
		 ext2fs_free_blocks_count(fs->super) == initial_free &&
		 ext2fs_read_inode(fs, EXT2_BAD_INO, &inode) == 0,
		 "clear bb_ind_bad restores all free blocks");

	/* 9. Illegal block number in EXT2_BAD_INO ignored/cleared safely */
	inode.i_block[0] = 5000;
	CHECK_BB(ext2fs_write_inode(fs, EXT2_BAD_INO, &inode) == 0,
		 "plant out-of-range block in EXT2_BAD_INO");
	read_bb = NULL;
	CHECK_BB(ext2fs_read_bb_inode(fs, &read_bb) == 0 &&
		 ext2fs_u32_list_count((ext2_u32_list) read_bb) == 0,
		 "read_bb_inode ignores out-of-range block");
	ext2fs_badblocks_list_free(read_bb);
	CHECK_BB(ext2fs_update_bb_inode(fs, NULL) == 0 &&
		 ext2fs_free_blocks_count(fs->super) == initial_free,
		 "update_bb_inode clears out-of-range block safely");

	ext2fs_badblocks_list_free(bb_direct);
	ext2fs_badblocks_list_free(bb_ind);
	ext2fs_badblocks_list_free(bb_ind2);
	ext2fs_badblocks_list_free(bb_first);
	ext2fs_badblocks_list_free(bb_ind_bad);
	CHECK_BB(ext2fs_close_free(&fs) == 0, "close test filesystem");
	close(fd);
	unlink(path);
	printf("Badblocks inode update and read tests checked out OK!\n");
}

int main(int argc EXT2FS_ATTR((unused)), char **argv EXT2FS_ATTR((unused)))
{
	badblocks_list bb1, bb2, bb3, bb4, bb5;
	int	equal;
	errcode_t	retval;

	add_error_table(&et_ext2_error_table);

	bb1 = bb2 = bb3 = bb4 = bb5 = 0;

	printf("test1: ");
	retval = create_test_list(test1, &bb1);
	if (retval == 0)
		print_list(bb1, 1);
	printf("\n");

	printf("test2: ");
	retval = create_test_list(test2, &bb2);
	if (retval == 0)
		print_list(bb2, 1);
	printf("\n");

	printf("test3: ");
	retval = create_test_list(test3, &bb3);
	if (retval == 0)
		print_list(bb3, 1);
	printf("\n");

	printf("test4: ");
	retval = create_test_list(test4, &bb4);
	if (retval == 0) {
		print_list(bb4, 0);
		printf("\n");
		validate_test_seq(bb4, test4a);
	}
	printf("\n");

	printf("test5: ");
	retval = create_test_list(test5, &bb5);
	if (retval == 0) {
		print_list(bb5, 0);
		printf("\n");
		do_test_seq(bb5, test5a);
		printf("After test5 sequence: ");
		print_list(bb5, 0);
		printf("\n");
	}
	printf("\n");

	if (bb1 && bb2 && bb3 && bb4 && bb5) {
		printf("Comparison tests:\n");
		equal = ext2fs_badblocks_equal(bb1, bb2);
		printf("bb1 and bb2 are %sequal.\n", equal ? "" : "NOT ");
		if (equal)
			test_fail++;

		equal = ext2fs_badblocks_equal(bb1, bb3);
		printf("bb1 and bb3 are %sequal.\n", equal ? "" : "NOT ");
		if (!equal)
			test_fail++;

		equal = ext2fs_badblocks_equal(bb1, bb4);
		printf("bb1 and bb4 are %sequal.\n", equal ? "" : "NOT ");
		if (equal)
			test_fail++;

		equal = ext2fs_badblocks_equal(bb4, bb5);
		printf("bb4 and bb5 are %sequal.\n", equal ? "" : "NOT ");
		if (!equal)
			test_fail++;
		printf("\n");
	}

	file_test(bb4);

	file_test_invalid(bb4);

	bb_inode_test(bb4);

	if (test_fail == 0)
		printf("ext2fs library badblocks tests checks out OK!\n");

	if (bb1)
		ext2fs_badblocks_list_free(bb1);
	if (bb2)
		ext2fs_badblocks_list_free(bb2);
	if (bb3)
		ext2fs_badblocks_list_free(bb3);
	if (bb4)
		ext2fs_badblocks_list_free(bb4);
	if (bb5)
		ext2fs_badblocks_list_free(bb5);

	return test_fail;

}
