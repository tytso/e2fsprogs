/*
 * bitops.c --- Bitmap frobbing code.  See bitops.h for the inlined
 * 	routines.
 * 
 * Copyright (C) 1993, 1994 Theodore Ts'o.  This file may be
 * redistributed under the terms of the GNU Public License.
 * 
 * Taken from <asm/bitops.h>, Copyright 1992, Linus Torvalds.
 */

#include <stdio.h>
#include <sys/types.h>
#include <linux/fs.h>
#include <linux/ext2_fs.h>

#include "ext2fs.h"

#if (!defined(__i386__) && !defined(__i486__) && !defined(__i586__))

/*
 * For the benefit of those who are trying to port Linux to another
 * architecture, here are some C-language equivalents.  You should
 * recode these in the native assmebly language, if at all possible.
 * To guarantee atomicity, these routines call cli() and sti() to
 * disable interrupts while they operate.  (You have to provide inline
 * routines to cli() and sti().)
 *
 * Also note, these routines assume that you have 32 bit integers.
 * You will have to change this if you are trying to port Linux to the
 * Alpha architecture or to a Cray.  :-)
 * 
 * C language equivalents written by Theodore Ts'o, 9/26/92
 */

int set_bit(int nr,void * addr)
{
	int	mask, retval;
	int	*ADDR = (int *) addr;

	ADDR += nr >> 5;
	mask = 1 << (nr & 0x1f);
	cli();
	retval = (mask & *ADDR) != 0;
	*ADDR |= mask;
	sti();
	return retval;
}

int clear_bit(int nr, void * addr)
{
	int	mask, retval;
	int	*ADDR = (int *) addr;

	ADDR += nr >> 5;
	mask = 1 << (nr & 0x1f);
	cli();
	retval = (mask & *ADDR) != 0;
	*ADDR &= ~mask;
	sti();
	return retval;
}

int test_bit(int nr, const void * addr)
{
	int		mask;
	const int	*ADDR = (const int *) addr;

	ADDR += nr >> 5;
	mask = 1 << (nr & 0x1f);
	return ((mask & *ADDR) != 0);
}
#endif	/* !i386 */

/*
 * These are routines print warning messages; they are called by
 * inline routines.
 */
const char *ext2fs_block_string = "block";
const char *ext2fs_inode_string = "inode";
const char *ext2fs_mark_string = "mark";
const char *ext2fs_unmark_string = "unmark";
const char *ext2fs_test_string = "test";

void ext2fs_warn_bitmap(ext2_filsys fs, const char *op, const char *type,
			int arg)
{
	char	func[80];

	sprintf(func, "ext2fs_%s_%s_bitmap", op, type);
	com_err(func, 0, "INTERNAL ERROR: illegal %s #%d for %s",
		type, arg, fs->device_name);
}



