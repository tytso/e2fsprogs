/*
 * relocate.h
 * 
 * Copyright (C) 1996 Theodore Ts'o.  This file may be redistributed
 * under the terms of the GNU Public License.
 */

struct ext2_relocate_entry {
	__u32	new, old;
	__u32	owner;
}

struct ext2_relocate_struct {
	int	magic;
	int	count;
	int	size;
	int	max;
	struct ext2_relocate_entry *entries;
};

typedef struct ext2_relocate_struct  *ext2_relocate_table;
