/*
 * This testing program makes sure the badblocks implementation works.
 *
 * Copyright (C) 1996 by Theodore Ts'o.
 * 
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 */

#include <stdio.h>
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

blk_t test1[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 0 };
blk_t test2[] = { 11, 10, 9, 8, 7, 6, 5, 4, 3, 3, 2, 1 };
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

static int test_fail = 0;

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
		printf("%d ", blk);
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
		printf("\tblock %d is %s --- %s\n", vec[i],
		       match ? "present" : "absent",
		       ok ? "OK" : "NOT OK");
	}
}

int file_test(badblocks_list bb)
{
	char	tmp_filename[20] = "#testXXXXXX";
	badblocks_list new_bb = 0;
	errcode_t	retval;
	FILE	*f;

	mktemp(tmp_filename);

	unlink(tmp_filename);
	f = fopen(tmp_filename, "w");
	if (!f) {
		fprintf(stderr, "Error opening temp file %s: %s\n",
			tmp_filename, error_message(errno));
		return 1;
	}
	retval = ext2fs_write_bb_FILE(bb, 0, f);
	if (retval) {
		com_err("file_test", retval, "while writing bad blocks");
		return 1;
	}
	fclose(f);

	f = fopen(tmp_filename, "r");
	if (!f) {
		fprintf(stderr, "Error re-opening temp file %s: %s\n",
			tmp_filename, error_message(errno));
		return 1;
	}
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
	return 0;
}


int main(int argc, char **argv)
{
	badblocks_list bb1, bb2, bb3, bb4;
	int	equal;
	errcode_t	retval;

	bb1 = bb2 = bb3 = bb4 = 0;

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

	if (bb1 && bb2 && bb3 && bb4) {
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
		printf("\n");
	}
	
	if (test_fail == 0)
		printf("ext2fs library badblocks tests checks out OK!\n");

	file_test(bb4);
	
	if (bb1)
		ext2fs_badblocks_list_free(bb1);
	if (bb2)
		ext2fs_badblocks_list_free(bb2);
	if (bb3)
		ext2fs_badblocks_list_free(bb3);
	if (bb4)
		ext2fs_badblocks_list_free(bb4);

	return test_fail;

}
