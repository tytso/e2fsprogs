/*
 * findsuper --- quick hacked up program to find ext2 superblocks.
 *
 * This is a hack, and really shouldn't be installed anywhere.  If you
 * need a program which does this sort of functionality, please try
 * using gpart program.
 *
 * Portions Copyright 1998-2000, Theodore Ts'o.
 * 
 * This program may be used under the provisions of the GNU Public
 * License, *EXCEPT* that a binary copy of the executable may not be
 * packaged as a part of binary package which is distributed as part
 * of a Linux distribution.  (Yes, this violates the Debian Free
 * Software Guidelines in terms of restricting its field of use.
 * That's the point.  I don't want this program being distributed in
 * Debian, because I don't care to support it, and the maintainer,
 * Yann Dirson, doesn't seem to pay attention to my wishes on this
 * matter.  So I'm delibiately adding this clause so it violates the
 * Debian Free Software Guidelines to force him to take it out.  If
 * this doesn't work, I'll have to remove it from the upstream source
 * distribution at the next release.  End of Rant.  :-)
 * 
 *
 * Well, here's my linux version of findsuper.
 * I'm sure you coulda done it faster.  :)
 * IMHO there isn't as much interesting data to print in the
 * linux superblock as there is in the SunOS superblock--disk geometry is
 * not there...and linux seems to update the dates in all the superblocks.
 * SunOS doesn't ever touch the backup superblocks after the fs is created,
 * as far as I can tell, so the date is more interesting IMHO and certainly
 * marks which superblocks are backup ones.
 *
 * This still doesn't handle disks >2G.
 *
 * I wanted to add msdos support, but I couldn't make heads or tails
 * of the kernel include files to find anything I could look for in msdos.
 * 
 * Reading every block of a Sun partition is fairly quick.  Doing the
 * same under linux (slower hardware I suppose) just isn't the same.
 * It might be more useful to default to reading the first (second?) block
 * on each cyl; however, if the disk geometry is wrong, this is useless.
 * But ya could still get the cyl size to print the numbers as cyls instead
 * of blocks...
 *
 * run this as (for example)
 *   findsuper /dev/hda
 *   findsuper /dev/hda 437760 1024   (my disk has cyls of 855*512)
 *
 * I suppose the next step is to figgure out a way to determine if
 * the block found is the first superblock somehow, and if so, build
 * a partition table from the superblocks found... but this is still
 * useful as is.
 *
 *		Steve
 * ssd@nevets.oau.org
 * ssd@mae.engr.ucf.edu
 * 
 */

/*
 * Documentation addendum added by Andreas dwguest@win.tue.nl/aeb@cwi.nl
 * 
 * The program findsuper is a utility that scans a disk and finds
 * copies of ext2 superblocks (by checking for the ext2 signature; it
 * will occasionally find other blocks that by coincidence have this
 * signature - often these can be recognised by their ridiculous
 * dates).
 * 
 * For each superblock found, it prints the offset in bytes, the
 * offset in 1024-byte blocks, the size of ext2 partition in 1024-byte
 * blocks, the filesystem blocksize (given as log(blocksize)-10, so
 * that 0 means 1024), the block group number (0 for older ext2
 * systems), and a timestamp (s_mtime).
 * 
 * This program can be used to retrieve partitions that have been
 * lost.  The superblock for block group 0 is found 1 block (2
 * sectors) after the partition start.
 * 
 * For new systems that have a block group number in the superblock it
 * is immediately clear which superblock is the first of a partition.
 * For old systems where no group numbers are given, the first
 * superblock can be recognised by the timestamp: all superblock
 * copies have the creation time in s_mtime, except the first, which
 * has the last time e2fsck or tune2fs wrote to the filesystem.
 * 
 */


#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "ext2fs/ext2_fs.h"
#include "nls-enable.h"


main(int argc, char *argv[])
{
	int i;
	int skiprate=512;		/* one sector */
	long sk=0;			/* limited to 2G filesystems!! */
	FILE *f;
	char *s;
	time_t tm;

	struct ext2_super_block ext2;
	/* interesting fields: EXT2_SUPER_MAGIC
	 *      s_blocks_count s_log_block_size s_mtime s_magic s_lastcheck */

#ifdef ENABLE_NLS
	setlocale(LC_MESSAGES, "");
	bindtextdomain(NLS_CAT_NAME, LOCALEDIR);
	textdomain(NLS_CAT_NAME);
#endif
	if (argc<2) {
		fprintf(stderr,
			_("Usage:  findsuper device [skiprate [start]]\n"));
		exit(1);
	}
	if (argc>2)
		skiprate=atoi(argv[2]);
	if (skiprate<512) {
		fprintf(stderr,
			_("Do you really want to skip less than a sector??\n"));
		exit(2);
	}
	if (argc>3)
		sk=atol(argv[3]);
	if (sk<0) {
		fprintf(stderr,_("Have to start at 0 or greater,not %ld\n"),sk);
		exit(1);
	}
	f=fopen(argv[1],"r");
	if (!f) {
		perror(argv[1]);
		exit(1);
	}
 
	/* Now, go looking for the superblock ! */
	printf("  thisoff     block fs_blk_sz  blksz grp last_mount\n");
	for (;!feof(f) &&  (i=fseek(f,sk,SEEK_SET))!= -1; sk+=skiprate){
		if (i=fread(&ext2,sizeof(ext2),1, f)!=1) {
			perror(_("read failed"));
		}
		if (ext2.s_magic != EXT2_SUPER_MAGIC)
			continue;
		
		tm = ext2.s_mtime;
		s=ctime(&tm);
		s[24]=0;
		printf("%9ld %9ld %9ld %5ld %4d %s\n", sk,
		       sk/1024, ext2.s_blocks_count,
		       ext2.s_log_block_size,
		       ext2.s_block_group_nr, s);
	}
	printf(_("Failed on %d at %ld\n"), i, sk);
	fclose(f);
}
