#include <sys/types.h>
#include <sys/stat.h>
#include "basefs_allocator.h"
#include "block_range.h"
#include "hashmap.h"
#include "base_fs.h"

struct base_fs_allocator {
	struct ext2fs_hashmap *entries;
	struct basefs_entry *cur_entry;
	/* Blocks which are definitely owned by a single inode in BaseFS. */
	ext2fs_block_bitmap exclusive_block_map;
	/* Blocks which are available to the first inode that requests it. */
	ext2fs_block_bitmap dedup_block_map;
};

static errcode_t basefs_block_allocator(ext2_filsys, blk64_t, blk64_t *,
					struct blk_alloc_ctx *ctx);

/*
 * Free any reserved, but unconsumed block ranges in the allocator. This both
 * frees the block_range_list data structure and unreserves exclusive blocks
 * from the block map.
 */
static void fs_free_blocks_range(ext2_filsys fs,
				 struct base_fs_allocator *allocator,
				 struct block_range_list *list)
{
	ext2fs_block_bitmap exclusive_map = allocator->exclusive_block_map;

	blk64_t block;
	while (list->head) {
		block = consume_next_block(list);
		if (ext2fs_test_block_bitmap2(exclusive_map, block)) {
			ext2fs_unmark_block_bitmap2(fs->block_map, block);
			ext2fs_unmark_block_bitmap2(exclusive_map, block);
		}
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
			fs_free_blocks_range(fs, allocator, &e->blocks);
			delete_block_ranges(&e->blocks);
		}
		ext2fs_hashmap_free(entries);
	}
	ext2fs_free_block_bitmap(allocator->exclusive_block_map);
	ext2fs_free_block_bitmap(allocator->dedup_block_map);
	free(allocator);
}

/*
 * Build a bitmap of which blocks are definitely owned by exactly one file in
 * Base FS. Blocks which are not valid or are de-duplicated are skipped. This
 * is called during allocator initialization, to ensure that libext2fs does
 * not allocate which we want to re-use.
 *
 * If a block was allocated in the initial filesystem, it can never be re-used,
 * so it will appear in neither the exclusive or dedup set. If a block is used
 * by multiple files, it will be removed from the owned set and instead added
 * to the dedup set.
 *
 * The dedup set is not removed from fs->block_map. This allows us to re-use
 * dedup blocks separately and not have them be allocated outside of file data.
 */
static void fs_reserve_block(ext2_filsys fs,
			     struct base_fs_allocator *allocator,
			     blk64_t block)
{
	ext2fs_block_bitmap exclusive_map = allocator->exclusive_block_map;
	ext2fs_block_bitmap dedup_map = allocator->dedup_block_map;

	if (block >= ext2fs_blocks_count(fs->super))
		return;

	if (ext2fs_test_block_bitmap2(fs->block_map, block)) {
		if (!ext2fs_test_block_bitmap2(exclusive_map, block))
			return;
		ext2fs_unmark_block_bitmap2(exclusive_map, block);
		ext2fs_mark_block_bitmap2(dedup_map, block);
	} else {
		ext2fs_mark_block_bitmap2(fs->block_map, block);
		ext2fs_mark_block_bitmap2(exclusive_map, block);
	}
}

static void fs_reserve_blocks_range(ext2_filsys fs,
				    struct base_fs_allocator *allocator,
				    struct block_range_list *list)
{
	blk64_t block;
	struct block_range *blocks = list->head;

	while (blocks) {
		for (block = blocks->start; block <= blocks->end; block++)
			fs_reserve_block(fs, allocator, block);
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
		fs_reserve_blocks_range(fs, allocator, &e->blocks);
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
		goto err_load;

	allocator->cur_entry = NULL;
	allocator->entries = basefs_parse(file, mountpoint);
	if (!allocator->entries) {
		retval = EIO;
		goto err_load;
	}
	retval = ext2fs_allocate_block_bitmap(fs, "exclusive map",
		&allocator->exclusive_block_map);
	if (retval)
		goto err_load;
	retval = ext2fs_allocate_block_bitmap(fs, "dedup map",
		&allocator->dedup_block_map);
	if (retval)
		goto err_load;

	fs_reserve_blocks(fs, allocator);

	/* Override the default allocator */
	fs->get_alloc_block2 = basefs_block_allocator;
	fs->priv_data = allocator;

	goto out;

err_load:
	basefs_allocator_free(fs, allocator);
out:
	return retval;
}

/* Try and acquire the next usable block from the Base FS map. */
static int get_next_block(ext2_filsys fs, struct base_fs_allocator *allocator,
			  struct block_range_list* list, blk64_t *ret)
{
	blk64_t block;
	ext2fs_block_bitmap exclusive_map = allocator->exclusive_block_map;
	ext2fs_block_bitmap dedup_map = allocator->dedup_block_map;

	while (list->head) {
		block = consume_next_block(list);
		if (block >= ext2fs_blocks_count(fs->super))
			continue;
		if (ext2fs_test_block_bitmap2(exclusive_map, block)) {
			ext2fs_unmark_block_bitmap2(exclusive_map, block);
			*ret = block;
			return 0;
		}
		if (ext2fs_test_block_bitmap2(dedup_map, block)) {
			ext2fs_unmark_block_bitmap2(dedup_map, block);
			*ret = block;
			return 0;
		}
	}
	return -1;
}

static errcode_t basefs_block_allocator(ext2_filsys fs, blk64_t goal,
					blk64_t *ret, struct blk_alloc_ctx *ctx)
{
	errcode_t retval;
	struct base_fs_allocator *allocator = fs->priv_data;
	struct basefs_entry *e = allocator->cur_entry;

	if (e && ctx && (ctx->flags & BLOCK_ALLOC_DATA)) {
		if (!get_next_block(fs, allocator, &e->blocks, ret))
			return 0;
	}

	retval = ext2fs_new_block2(fs, goal, fs->block_map, ret);
	if (retval)
		return retval;
	ext2fs_mark_block_bitmap2(fs->block_map, *ret);
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

	fs_free_blocks_range(fs, allocator, &allocator->cur_entry->blocks);
	delete_block_ranges(&allocator->cur_entry->blocks);
	return 0;
}
