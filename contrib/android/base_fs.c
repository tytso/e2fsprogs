#include "base_fs.h"
#include <stdio.h>

#define BASE_FS_VERSION "Base EXT4 version 1.0"

struct base_fs {
	FILE *file;
	const char *mountpoint;
	struct basefs_entry entry;
};

static void *init(const char *file, const char *mountpoint)
{
	struct base_fs *params = malloc(sizeof(*params));

	if (!params)
		return NULL;
	params->mountpoint = mountpoint;
	params->file = fopen(file, "w+");
	if (!params->file) {
		free(params);
		return NULL;
	}
	if (fwrite(BASE_FS_VERSION"\n", 1, strlen(BASE_FS_VERSION"\n"),
		   params->file) != strlen(BASE_FS_VERSION"\n")) {
		fclose(params->file);
		free(params);
		return NULL;
	}
	return params;
}

static int start_new_file(char *path, ext2_ino_t ino EXT2FS_ATTR((unused)),
			  struct ext2_inode *inode, void *data)
{
	struct base_fs *params = data;

	params->entry.head = params->entry.tail = NULL;
	params->entry.path = LINUX_S_ISREG(inode->i_mode) ? path : NULL;
	return 0;
}

static int add_block(ext2_filsys fs EXT2FS_ATTR((unused)), blk64_t blocknr,
		     int metadata, void *data)
{
	struct base_fs *params = data;

	if (params->entry.path && !metadata)
		add_blocks_to_range(&params->entry.head, &params->entry.tail,
				    blocknr, blocknr);
	return 0;
}

static int inline_data(void *inline_data EXT2FS_ATTR((unused)),
		       void *data EXT2FS_ATTR((unused)))
{
	return 0;
}

static int end_new_file(void *data)
{
	struct base_fs *params = data;

	if (!params->entry.path)
		return 0;
	if (fprintf(params->file, "%s%s ", params->mountpoint,
		    params->entry.path) < 0
	    || write_block_ranges(params->file, params->entry.head, ",")
	    || fwrite("\n", 1, 1, params->file) != 1)
		return -1;

	delete_block_ranges(params->entry.head);
	return 0;
}

static int cleanup(void *data)
{
	struct base_fs *params = data;

	fclose(params->file);
	free(params);
	return 0;
}

struct fsmap_format base_fs_format = {
	.init = init,
	.start_new_file = start_new_file,
	.add_block = add_block,
	.inline_data = inline_data,
	.end_new_file = end_new_file,
	.cleanup = cleanup,
};
