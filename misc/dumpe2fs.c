/*
 * dumpe2fs.c		- List the control structures of a second
 *			  extended filesystem
 *
 * Copyright (C) 1992, 1993, 1994  Remy Card <card@masi.ibp.fr>
 *                                 Laboratoire MASI, Institut Blaise Pascal
 *                                 Universite Pierre et Marie Curie (Paris VI)
 *
 * Copyright 1995, 1996, 1997 by Theodore Ts'o.
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 */

/*
 * History:
 * 94/01/09	- Creation
 * 94/02/27	- Ported to use the ext2fs library
 */

#ifdef HAVE_GETOPT_H
#include <getopt.h>
#else
extern char *optarg;
extern int optind;
#endif
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ext2fs/ext2_fs.h"

#include "ext2fs/ext2fs.h"
#include "e2p/e2p.h"
#include "jfs_user.h"

#include "../version.h"
#include "nls-enable.h"

#define in_use(m, x)	(ext2fs_test_bit ((x), (m)))

const char * program_name = "dumpe2fs";
char * device_name = NULL;
int opt_hex = 0;

static void usage(void)
{
	fprintf (stderr, _("Usage: %s [-bfhixV] [-ob superblock] "
		 "[-oB blocksize] device\n"), program_name);
	exit (1);
}

static void print_free (unsigned long group, char * bitmap,
			unsigned long nbytes, unsigned long offset)
{
	int p = 0;
	unsigned long i;
	unsigned long j;

	for (i = 0; i < nbytes; i++)
		if (!in_use (bitmap, i))
		{
			if (p)
				printf (", ");
			if (i == nbytes - 1 || in_use (bitmap, i + 1))
				printf (opt_hex ? "0x%04x" : "%lu",
					group * nbytes + i + offset);
			else
			{
				for (j = i; j < nbytes && !in_use (bitmap, j);
				     j++)
					;
				printf (opt_hex ? "0x%04lx-0x%04lx" :
					"%lu-%lu", group * nbytes + i + offset,
					group * nbytes + (j - 1) + offset);
				i = j - 1;
			}
			p = 1;
		}
}

static void list_desc (ext2_filsys fs)
{
	unsigned long i;
	blk_t	group_blk, next_blk;
	char * block_bitmap = fs->block_map->bitmap;
	char * inode_bitmap = fs->inode_map->bitmap;
	int inode_blocks_per_group;
	int group_desc_blocks;

	inode_blocks_per_group = ((fs->super->s_inodes_per_group *
				   EXT2_INODE_SIZE(fs->super)) +
				  EXT2_BLOCK_SIZE(fs->super) - 1) /
				 EXT2_BLOCK_SIZE(fs->super);
	group_desc_blocks = ((fs->super->s_blocks_count -
			      fs->super->s_first_data_block +
			      EXT2_BLOCKS_PER_GROUP(fs->super) - 1) /
			     EXT2_BLOCKS_PER_GROUP(fs->super) +
			     EXT2_DESC_PER_BLOCK(fs->super) - 1) /
			    EXT2_DESC_PER_BLOCK(fs->super);

	printf ("\n");
	group_blk = fs->super->s_first_data_block;
	for (i = 0; i < fs->group_desc_count; i++) {
		next_blk = group_blk + fs->super->s_blocks_per_group;
		if (next_blk > fs->super->s_blocks_count)
			next_blk = fs->super->s_blocks_count;
		printf (opt_hex ? _("Group %lu: (Blocks 0x%04x -- 0x%04x)\n"):
			 _("Group %lu: (Blocks %u -- %u)\n"), i,
			group_blk, next_blk -1 );
		if (ext2fs_bg_has_super (fs, i))
			printf (opt_hex ? _("  %s Superblock at 0x%04x,"
				"  Group Descriptors at 0x%04x-0x%04x\n"):
				_("  %s Superblock at %u,"
				"  Group Descriptors at %u-%u\n"),
				i == 0 ? _("Primary") : _("Backup"),
				group_blk, group_blk + 1,
				group_blk + group_desc_blocks);
		printf (opt_hex ? _("  Block bitmap at 0x%04x (+%d), "
			"Inode bitmap at 0x%04x (+%d)\n  "
			"Inode table at 0x%04x-0x%04x (+%d)\n"):
		        _("  Block bitmap at %u (+%d), "
			"Inode bitmap at %u (+%d)\n  "
			"Inode table at %u-%u (+%d)\n"),
			fs->group_desc[i].bg_block_bitmap,
			fs->group_desc[i].bg_block_bitmap - group_blk,
			fs->group_desc[i].bg_inode_bitmap,
			fs->group_desc[i].bg_inode_bitmap - group_blk,
			fs->group_desc[i].bg_inode_table,
			fs->group_desc[i].bg_inode_table +
				inode_blocks_per_group - 1,
			fs->group_desc[i].bg_inode_table - group_blk);
		printf (_("  %d free blocks, %d free inodes, %d directories\n"),
			fs->group_desc[i].bg_free_blocks_count,
			fs->group_desc[i].bg_free_inodes_count,
			fs->group_desc[i].bg_used_dirs_count);
		printf (_("  Free blocks: "));
		print_free (i, block_bitmap, fs->super->s_blocks_per_group,
			    fs->super->s_first_data_block);
		block_bitmap += fs->super->s_blocks_per_group / 8;
		printf ("\n");
		printf (_("  Free inodes: "));
		print_free (i, inode_bitmap, fs->super->s_inodes_per_group, 1);
		inode_bitmap += fs->super->s_inodes_per_group / 8;
		printf ("\n");
		group_blk = next_blk;
	}
}

static void list_bad_blocks(ext2_filsys fs)
{
	badblocks_list		bb_list = 0;
	badblocks_iterate	bb_iter;
	blk_t			blk;
	errcode_t		retval;

	retval = ext2fs_read_bb_inode(fs, &bb_list);
	if (retval) {
		com_err("ext2fs_read_bb_inode", retval, "");
		exit(1);
	}
	retval = ext2fs_badblocks_list_iterate_begin(bb_list, &bb_iter);
	if (retval) {
		com_err("ext2fs_badblocks_list_iterate_begin", retval,
			_("while printing bad block list"));
		exit(1);
	}
	if (ext2fs_badblocks_list_iterate(bb_iter, &blk))
		printf(_("Bad blocks: %d"), blk);
	while (ext2fs_badblocks_list_iterate(bb_iter, &blk))
		printf(", %d", blk);
	ext2fs_badblocks_list_iterate_end(bb_iter);
	printf("\n");
}

static void dump_bad_blocks(ext2_filsys fs)
{
	badblocks_list		bb_list = 0;
	badblocks_iterate	bb_iter;
	blk_t			blk;
	errcode_t		retval;

	retval = ext2fs_read_bb_inode(fs, &bb_list);
	if (retval) {
		com_err("ext2fs_read_bb_inode", retval, "");
		exit(1);
	}
	retval = ext2fs_badblocks_list_iterate_begin(bb_list, &bb_iter);
	if (retval) {
		com_err("ext2fs_badblocks_list_iterate_begin", retval,
			_("while printing bad block list"));
		exit(1);
	}
	while (ext2fs_badblocks_list_iterate(bb_iter, &blk))
		printf("%d\n", blk);
	ext2fs_badblocks_list_iterate_end(bb_iter);
}

static void print_journal_information(ext2_filsys fs)
{
	errcode_t	retval;
	char		buf[1024];
	char		str[80];
	int		i;
	journal_superblock_t	*jsb;

	/* Get the journal superblock */
	if ((retval = io_channel_read_blk(fs->io, fs->super->s_first_data_block+1, -1024, buf))) {
		com_err(program_name, retval,
			_("while reading journal superblock"));
		exit(1);
	}
	jsb = (journal_superblock_t *) buf;
	if ((jsb->s_header.h_magic != (unsigned) ntohl(JFS_MAGIC_NUMBER)) ||
	    (jsb->s_header.h_blocktype !=
	     (unsigned) ntohl(JFS_SUPERBLOCK_V2))) {
		com_err(program_name, 0,
			_("Couldn't find journal superblock magic numbers"));
		exit(1);
	}

	fputs("\n", stdout);
	printf("Journal block size:       %d\n", ntohl(jsb->s_blocksize));
	printf("Journal length:           %d\n", ntohl(jsb->s_maxlen));
	printf("Journal first block:      %d\n", ntohl(jsb->s_first));
	printf("Journal sequence:         0x%08x\n", ntohl(jsb->s_sequence));
	printf("Journal start:            %d\n", ntohl(jsb->s_start));
	printf("Journal number of users:  %d\n", ntohl(jsb->s_nr_users));
	for (i=0; i < ntohl(jsb->s_nr_users); i++) {
		if (i)
			printf("                          ");
		else
			printf("Journal users:            ");
		uuid_unparse(&jsb->s_users[i*16], str);
		printf("%s\n", str);
	}
}

int main (int argc, char ** argv)
{
	errcode_t	retval;
	ext2_filsys	fs;
	int		print_badblocks = 0;
	int		use_superblock = 0;
	int		use_blocksize = 0;
	int		image_dump = 0;
	int		force = 0;
	int		flags;
	int		header_only = 0;
	int		big_endian;
	int		c;

#ifdef ENABLE_NLS
	setlocale(LC_MESSAGES, "");
	bindtextdomain(NLS_CAT_NAME, LOCALEDIR);
	textdomain(NLS_CAT_NAME);
#endif
	initialize_ext2_error_table();
	fprintf (stderr, "dumpe2fs %s (%s)\n", E2FSPROGS_VERSION,
		 E2FSPROGS_DATE);
	if (argc && *argv)
		program_name = *argv;
	
	while ((c = getopt (argc, argv, "bfhixVo:")) != EOF) {
		switch (c) {
		case 'b':
			print_badblocks++;
			break;
		case 'f':
			force++;
			break;
		case 'h':
			header_only++;
			break;
		case 'i':
			image_dump++;
			break;
		case 'o':
			if (optarg[0] == 'b')
				use_superblock = atoi(optarg+1);
			else if (optarg[0] == 'B')
				use_blocksize = atoi(optarg+1);
			else
				usage();
			break;
		case 'V':
			/* Print version number and exit */
			fprintf(stderr, _("\tUsing %s\n"),
				error_message(EXT2_ET_BASE));
			exit(0);
		case 'x':
			opt_hex=1;
			break;
		default:
			usage();
		}
	}
	if (optind > argc - 1)
		usage();
	device_name = argv[optind++];
	if (use_superblock && !use_blocksize)
		use_blocksize = 1024;
	flags = EXT2_FLAG_JOURNAL_DEV_OK;
	if (force)
		flags |= EXT2_FLAG_FORCE;
	if (image_dump)
		flags |= EXT2_FLAG_IMAGE_FILE;
	
	retval = ext2fs_open (device_name, flags, use_superblock,
			      use_blocksize, unix_io_manager, &fs);
	if (retval) {
		com_err (program_name, retval, _("while trying to open %s"),
			 device_name);
		printf (_("Couldn't find valid filesystem superblock.\n"));
		exit (1);
	}
	if (print_badblocks) {
		dump_bad_blocks(fs);
	} else {
		big_endian = ((fs->flags & EXT2_FLAG_SWAP_BYTES) != 0);
#ifdef WORDS_BIGENDIAN
		big_endian = !big_endian;
#endif
		if (big_endian)
			printf(_("Note: This is a byte-swapped filesystem\n"));
		list_super (fs->super);
		if (fs->super->s_feature_incompat &
		      EXT3_FEATURE_INCOMPAT_JOURNAL_DEV) {
			print_journal_information(fs);
			ext2fs_close(fs);
			exit(0);
		}
		list_bad_blocks (fs);
		if (header_only) {
			ext2fs_close (fs);
			exit (0);
		}
		retval = ext2fs_read_bitmaps (fs);
		if (retval) {
			com_err (program_name, retval,
				 _("while trying to read the bitmaps"),
				 device_name);
			ext2fs_close (fs);
			exit (1);
		}
		list_desc (fs);
	}
	ext2fs_close (fs);
	exit (0);
}
