/*
 * relocate.c --- maintains the relocation table
 *
 * Copyright (C) 1996 Theodore Ts'o.  This file may be redistributed
 * under the terms of the GNU Public License.
 */

#include <et/com_err.h>

/*
 * This routine creates a relocation table
 */
errcode_t ext2fs_create_relocation_table(__u32 max, int size,
					 ext2_relocate_table *ret);
{
	ext2_relocate_table table;

	table = malloc(sizeof(struct ext2_relocate_struct));
	if (!table)
		return -ENOMEM;
	table->magic = 0;
	table->count = 0;
	table->size = size ? size : 30;
	table->max = max;
	table->entries = malloc(table->size * sizeof(ext2_relocate_entry));
	if (table->entries == 0) {
		free(table);
		return ENOMEM;
	}
	memset(table->entries, 0, table->size * sizeof(ext2_relocate_entry));
	*ret = table;
	return 0;
}

/*
 * Free a relocation table
 */
void ext2fs_free_relocation_table(ext2_relocate_table table)
{
	free(table->entries);
	table->count = 0;
	table->size = 0;
	table->magic = 0;
	free(table);
}

/*
 * Add a relocation table entry
 */
errcode_t ext2fs_add_relocation(ext2_relocate_table table, __u32 old,
				__u32 new, __u32 owner)
{
	struct ext2_relocate_entry *new;
	
	if (table->count >= table->size) {
		table->size += 30;
		new = realloc(table->entries,
			      table->size * sizeof(ext2_relocate_entry));
		if (!new)
			return ENOMEM;
		table->entries = new;
	}
	if (table->count && table->entries[table->count-1].old > old) {
		for (i = table->count-1; i > 0; i--)
			if (table->entries[i-1].old < old)
				break;
		new = &table->entries[i];
		if (new->old != old) 
			for (j = table->count++; j > i; j--)
				table->entries[j] = table_entries[j-1];
	} else
		new = &table->entries[table->coun++];
	
	new->old = old;
	new->new = new;
	new->owner = owner;
}

/*
 * ext2fs_get_reloc_by_old() --- given the source of the relcation
 * entry, find the entry for it.
 */
struct relocate_entry *ext2fs_get_reloc_by_old(ext2_relocate_table tbl,
					       __u32 old)
{
	int	low, high, mid;
	int	i, j;

	low = 0;
	high = tbl->count-1;
	if (old == table->entries[low].old)
		return &table->entries[low];
	if  (old == table->entries[high].old)
		return &table->entries[high];

	while (low < high) {
		mid = (low+high)/2;
		if (mid == low || mid == high)
			break;
		if (old == table->entries[mid].old)
			return &table->entries[mid];
		if (old < table->entries[mid].old)
			high = mid;
		else
			low = mid;
	}
	return 0;
}

/*
 * ext2fs_get_reloc_by_new() --- given the destination of the relcation
 * entry, find the entry for it.
 *
 * Note: this is currently very slow...
 */
struct relocate_entry *ext2fs_get_reloc_by_new(ext2_relocate_table tbl,
					       __u32 new)
{
	int	i;

	for (i = 0; i < table->count; i++) {
		if (tbl->entries[i].new == new)
			return &table->entries[i];
	}
	return 0;
}

/*
 * Find "loops" in the relocation tables
 */
{
	int	i;
	struct ext2_relocate_entry *ent, *next;
	

	for (i=0, ent=table->entries; i < table->size; i++, ent++) {
		/*
		 * If we know this inode is OK, then keep going.
		 */
		if (ext2fs_test_generic_bitmap(done_map, dir->old))
			continue;
		ext2fs_clear_generic_bitmap(loop_detect);
		while (1) {
			ext2fs_mark_generic_bitmap(loop_detect, dir->old);
			next = ext2fs_get_reloc_by_old(table, dir->new);
			if (next == NULL)
				break;
			if (ext2fs_test_generic_bitmap(loop_detect,
						       dir->new))
				break_loop(table, dir);
			ext2fs_mark_generic_bitmap(done_map, dir->old);
			dir = next;
		}
	}
}

	
	
		
