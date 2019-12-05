#include <sys/types.h>
#include <sys/stat.h>
#include "basefs_allocator.h"
#include "block_range.h"
#include "hashmap.h"
#include "base_fs.h"

struct base_fs_allocator {
	struct ext2fs_hashmap *entries;
	struct basefs_entry *cur_entry;
};

static errcode_t basefs_block_allocator(ext2_filsys, blk64_t, blk64_t *,
					struct blk_alloc_ctx *ctx);

static void fs_free_blocks_range(ext2_filsys fs,
				 struct block_range_list *list)
{
	struct block_range *blocks = list->head;

	while (blocks) {
		ext2fs_unmark_block_bitmap_range2(fs->block_map, blocks->start,
			blocks->end - blocks->start + 1);
		blocks = blocks->next;
	}
}

static void basefs_allocator_free(ext2_filsys fs,
				  struct base_fs_allocator *allocator)
{
	struct basefs_entry *e;
	struct ext2fs_hashmap_entry *it = NULL;
	struct ext2fs_hashmap *entries = allocator->entries;

	if (entries) {
		while ((e = ext2fs_hashmap_iter_in_order(entries, &it))) {
			fs_free_blocks_range(fs, &e->blocks);
			delete_block_ranges(&e->blocks);
		}
		ext2fs_hashmap_free(entries);
	}
	free(allocator);
}

static void fs_reserve_blocks_range(ext2_filsys fs,
				    struct block_range_list *list)
{
	struct block_range *blocks = list->head;
	while (blocks) {
		ext2fs_mark_block_bitmap_range2(fs->block_map,
			blocks->start, blocks->end - blocks->start + 1);
		blocks = blocks->next;
	}
}

/*
 * For each file in the base FS map, ensure that its blocks are reserved in
 * the actual block map. This prevents libext2fs from allocating them for
 * general purpose use, and ensures that if the file needs data blocks, they
 * can be re-acquired exclusively for that file.
 */
static void fs_reserve_blocks(ext2_filsys fs,
			      struct base_fs_allocator *allocator)
{
	struct basefs_entry *e;
	struct ext2fs_hashmap_entry *it = NULL;
	struct ext2fs_hashmap *entries = allocator->entries;

	while ((e = ext2fs_hashmap_iter_in_order(entries, &it)))
		fs_reserve_blocks_range(fs, &e->blocks);
}

errcode_t base_fs_alloc_load(ext2_filsys fs, const char *file,
			     const char *mountpoint)
{
	errcode_t retval = 0;
	struct base_fs_allocator *allocator;

	allocator = calloc(1, sizeof(*allocator));
	if (!allocator) {
		retval = ENOMEM;
		goto out;
	}

	retval = ext2fs_read_bitmaps(fs);
	if (retval)
		goto err_alloc;

	allocator->cur_entry = NULL;
	allocator->entries = basefs_parse(file, mountpoint);
	if (!allocator->entries) {
		retval = EIO;
		goto err_alloc;
	}

	fs_reserve_blocks(fs, allocator);

	/* Override the default allocator */
	fs->get_alloc_block2 = basefs_block_allocator;
	fs->priv_data = allocator;

	goto out;

err_alloc:
	basefs_allocator_free(fs, allocator);
out:
	return retval;
}

static errcode_t basefs_block_allocator(ext2_filsys fs, blk64_t goal,
					blk64_t *ret, struct blk_alloc_ctx *ctx)
{
	errcode_t retval;
	struct base_fs_allocator *allocator = fs->priv_data;
	struct basefs_entry *e = allocator->cur_entry;

	/* Try to get a block from the base_fs */
	if (e && e->blocks.head && ctx && (ctx->flags & BLOCK_ALLOC_DATA)) {
		*ret = consume_next_block(&e->blocks);
	} else { /* Allocate a new block */
		retval = ext2fs_new_block2(fs, goal, fs->block_map, ret);
		if (retval)
			return retval;
		ext2fs_mark_block_bitmap2(fs->block_map, *ret);
	}
	return 0;
}

void base_fs_alloc_cleanup(ext2_filsys fs)
{
	basefs_allocator_free(fs, fs->priv_data);
	fs->priv_data = NULL;
	fs->get_alloc_block2 = NULL;
}

errcode_t base_fs_alloc_set_target(ext2_filsys fs, const char *target_path,
	const char *name EXT2FS_ATTR((unused)),
	ext2_ino_t parent_ino EXT2FS_ATTR((unused)),
	ext2_ino_t root EXT2FS_ATTR((unused)), mode_t mode)
{
	struct base_fs_allocator *allocator = fs->priv_data;

	if (mode != S_IFREG)
		return 0;

	if (allocator)
		allocator->cur_entry = ext2fs_hashmap_lookup(allocator->entries,
						      target_path,
						      strlen(target_path));
	return 0;
}

errcode_t base_fs_alloc_unset_target(ext2_filsys fs,
        const char *target_path EXT2FS_ATTR((unused)),
	const char *name EXT2FS_ATTR((unused)),
	ext2_ino_t parent_ino EXT2FS_ATTR((unused)),
	ext2_ino_t root EXT2FS_ATTR((unused)), mode_t mode)
{
	struct base_fs_allocator *allocator = fs->priv_data;

	if (!allocator || !allocator->cur_entry || mode != S_IFREG)
		return 0;

	fs_free_blocks_range(fs, &allocator->cur_entry->blocks);
	delete_block_ranges(&allocator->cur_entry->blocks);
	return 0;
}
