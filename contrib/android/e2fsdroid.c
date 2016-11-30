#include <stdio.h>
#include <getopt.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <ext2fs/ext2fs.h>

#include "block_list.h"

const char *prog_name = "e2fsdroid";
const char *in_file;
const char *block_list;
const char *mountpoint = "";
int android_sparse_file = 1;

static void usage(int ret)
{
	fprintf(stderr, "%s [-B block_list] [-e] image\n", prog_name);
	exit(ret);
}

static char *absolute_path(const char *file)
{
	char *ret;
	char cwd[PATH_MAX];

	if (file[0] != '/') {
		getcwd(cwd, PATH_MAX);
		ret = malloc(strlen(cwd) + 1 + strlen(file) + 1);
		if (ret)
			sprintf(ret, "%s/%s", cwd, file);
	} else
		ret = strdup(file);
	return ret;
}

int main(int argc, char *argv[])
{
	int c;
	int flags = EXT2_FLAG_RW;
	errcode_t retval;
	io_manager io_mgr;
	ext2_filsys fs = NULL;

	add_error_table(&et_ext2_error_table);

	while ((c = getopt (argc, argv, "B:e")) != EOF) {
		switch (c) {
		case 'B':
			block_list = absolute_path(optarg);
			break;
		case 'e':
			android_sparse_file = 0;
			break;
		default:
			usage(EXIT_FAILURE);
		}
	}
	if (optind >= argc) {
		fprintf(stderr, "Expected filename after options\n");
		exit(EXIT_FAILURE);
	}
	in_file = strdup(argv[optind]);

	io_mgr = android_sparse_file ? sparse_io_manager: unix_io_manager;
	retval = ext2fs_open(in_file, flags, 0, 0, io_mgr, &fs);
	if (retval) {
		com_err(prog_name, retval, "while opening file %s\n", in_file);
		return retval;
	}

	if (block_list) {
		retval = fsmap_iter_filsys(fs, &block_list_format, block_list,
					   mountpoint);
		if (retval) {
			com_err(prog_name, retval, "%s",
				"while creating block_list");
			exit(1);
		}
	}

	retval = ext2fs_close_free(&fs);
	if (retval) {
		com_err(prog_name, retval, "%s",
				"while writing superblocks");
		exit(1);
	}

	remove_error_table(&et_ext2_error_table);
	return 0;
}
