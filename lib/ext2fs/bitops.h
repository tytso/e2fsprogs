/*
 * bitops.h --- Bitmap frobbing code.
 * 
 * Copyright (C) 1993, 1994 Theodore Ts'o.  This file may be
 * redistributed under the terms of the GNU Public License.
 * 
 * Taken from <asm/bitops.h>, Copyright 1992, Linus Torvalds.
 */


extern int set_bit(int nr,void * addr);
extern int clear_bit(int nr, void * addr);
extern int test_bit(int nr, const void * addr);

/*
 * EXT2FS bitmap manipulation routines.
 */

/* Support for sending warning messages from the inline subroutines */
extern const char *ext2fs_block_string;
extern const char *ext2fs_inode_string;
extern const char *ext2fs_mark_string;
extern const char *ext2fs_unmark_string;
extern const char *ext2fs_test_string;
extern void ext2fs_warn_bitmap(ext2_filsys fs, const char *op,
			       const char *type, int arg);

extern void ext2fs_mark_block_bitmap(ext2_filsys fs, char *bitmap, int block);
extern void ext2fs_unmark_block_bitmap(ext2_filsys fs, char *bitmap,
				       int block);
extern int ext2fs_test_block_bitmap(ext2_filsys fs, const char *bitmap,
				    int block);
extern void ext2fs_mark_inode_bitmap(ext2_filsys fs, char *bitmap, int inode);
extern void ext2fs_unmark_inode_bitmap(ext2_filsys fs, char *bitmap,
				       int inode);
extern int ext2fs_test_inode_bitmap(ext2_filsys fs, const char *bitmap,
				    int inode);

/*
 * The inline routines themselves...
 * 
 * If NO_INLINE_FUNCS is defined, then we won't try to do inline
 * functions at all!
 */
#if (defined(INCLUDE_INLINE_FUNCS) || !defined(NO_INLINE_FUNCS))
#ifdef INCLUDE_INLINE_FUNCS
#define _INLINE_ extern
#else
#define _INLINE_ extern __inline__
#endif

#if (defined(__i386__) || defined(__i486__) || defined(__i586__))
/*
 * These are done by inline assembly for speed reasons.....
 *
 * All bitoperations return 0 if the bit was cleared before the
 * operation and != 0 if it was not.  Bit 0 is the LSB of addr; bit 32
 * is the LSB of (addr+1).
 */

/*
 * Some hacks to defeat gcc over-optimizations..
 */
struct __dummy_h { unsigned long a[100]; };
#define ADDR (*(struct __dummy_h *) addr)
#define CONST_ADDR (*(const struct __dummy_h *) addr)	

_INLINE_ int set_bit(int nr, void * addr)
{
	int oldbit;

	__asm__ __volatile__("btsl %2,%1\n\tsbbl %0,%0"
		:"=r" (oldbit),"=m" (ADDR)
		:"r" (nr));
	return oldbit;
}

_INLINE_ int clear_bit(int nr, void * addr)
{
	int oldbit;

	__asm__ __volatile__("btrl %2,%1\n\tsbbl %0,%0"
		:"=r" (oldbit),"=m" (ADDR)
		:"r" (nr));
	return oldbit;
}

_INLINE_ int test_bit(int nr, const void * addr)
{
	int oldbit;

	__asm__ __volatile__("btl %2,%1\n\tsbbl %0,%0"
		:"=r" (oldbit)
		:"m" (CONST_ADDR),"r" (nr));
	return oldbit;
}

#undef ADDR

#endif	/* i386 */

_INLINE_ void ext2fs_mark_block_bitmap(ext2_filsys fs, char *bitmap,
					    int block)
{
	if ((block < fs->super->s_first_data_block) ||
	    (block >= fs->super->s_blocks_count)) {
		ext2fs_warn_bitmap(fs, ext2fs_mark_string,
				   ext2fs_block_string, block);
		return;
	}
	set_bit(block - fs->super->s_first_data_block, bitmap);
}

_INLINE_ void ext2fs_unmark_block_bitmap(ext2_filsys fs, char *bitmap,
					      int block)
{
	if ((block < fs->super->s_first_data_block) ||
	    (block >= fs->super->s_blocks_count)) {
		ext2fs_warn_bitmap(fs, ext2fs_unmark_string,
				   ext2fs_block_string, block);
		return;
	}
	clear_bit(block - fs->super->s_first_data_block, bitmap);
}

_INLINE_ int ext2fs_test_block_bitmap(ext2_filsys fs, const char *bitmap,
				      int block)
{
	if ((block < fs->super->s_first_data_block) ||
	    (block >= fs->super->s_blocks_count)) {
		ext2fs_warn_bitmap(fs, ext2fs_test_string,
				   ext2fs_block_string, block);
		return 0;
	}
	return test_bit(block - fs->super->s_first_data_block, bitmap);
}

_INLINE_ void ext2fs_mark_inode_bitmap(ext2_filsys fs, char *bitmap,
					    int inode)
{
	if ((inode < 1) || (inode > fs->super->s_inodes_count)) {
		ext2fs_warn_bitmap(fs, ext2fs_mark_string,
				   ext2fs_inode_string, inode);
		return;
	}
	set_bit(inode - 1, bitmap);
}

_INLINE_ void ext2fs_unmark_inode_bitmap(ext2_filsys fs, char *bitmap,
					      int inode)
{
	if ((inode < 1) || (inode > fs->super->s_inodes_count)) {
		ext2fs_warn_bitmap(fs, ext2fs_unmark_string,
				   ext2fs_inode_string, inode);
		return;
	}
	clear_bit(inode - 1, bitmap);
}

_INLINE_ int ext2fs_test_inode_bitmap(ext2_filsys fs, const char *bitmap,
				      int inode)
{
	if ((inode < 1) || (inode > fs->super->s_inodes_count)) {
		ext2fs_warn_bitmap(fs, ext2fs_test_string,
				   ext2fs_inode_string, inode);
		return 0;
	}
	return test_bit(inode - 1, bitmap);
}

#undef _INLINE_
#endif
