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
#include <uuid/uuid.h>

#include "../version.h"
#include "nls-enable.h"

#define in_use(m, x)	(ext2fs_test_bit ((x), (m)))

const char * program_name = "dumpe2fs";
char * device_name = NULL;
const char *num_format = "%lu";
char range_format[16];

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

	offset += group * nbytes;
	for (i = 0; i < nbytes; i++)
		if (!in_use (bitmap, i))
		{
			if (p)
				printf (", ");
			printf (num_format, i + offset);
			for (j = i; j < nbytes && !in_use (bitmap, j); j++)
				;
			if (--j != i) {
				fputc('-', stdout);
				printf(num_format, j + offset);
				i = j;
			}
			p = 1;
		}
}

static void list_desc (ext2_filsys fs)
{
	unsigned long i;
	long diff;
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

	fputc('\n', stdout);
	group_blk = fs->super->s_first_data_block;
	for (i = 0; i < fs->group_desc_count; i++) {
		next_blk = group_blk + fs->super->s_blocks_per_group;
		if (next_blk > fs->super->s_blocks_count)
			next_blk = fs->super->s_blocks_count;
		printf (_("Group %lu: (Blocks "), i);
		printf(range_format, group_blk, next_blk - 1);
		fputs(")\n", stdout);
		if (ext2fs_bg_has_super (fs, i)) {
			printf (_("  %s Superblock at "),
				i == 0 ? _("Primary") : _("Backup"));
			printf(num_format, group_blk);
			printf(_(",  Group Descriptors at "));
			printf(range_format, group_blk+1,
			       group_blk + group_desc_blocks);
			fputc('\n', stdout);
		}
		fputs(_("  Block bitmap at "), stdout);
		printf(num_format, fs->group_desc[i].bg_block_bitmap);
		diff = fs->group_desc[i].bg_block_bitmap - group_blk;
		if (diff >= 0)
			printf(" (+%ld)", diff);
		fputs(_(", Inode bitmap at "), stdout);
		printf(num_format, fs->group_desc[i].bg_inode_bitmap);
		diff = fs->group_desc[i].bg_inode_bitmap - group_blk;
		if (diff >= 0)
			printf(" (+%ld)", diff);
		fputs(_("\n  Inode table at "), stdout);
		printf(range_format, fs->group_desc[i].bg_inode_table,
		       fs->group_desc[i].bg_inode_table +
		       inode_blocks_per_group - 1);
		diff = fs->group_desc[i].bg_inode_table - group_blk;
		if (diff > 0)
			printf(" (+%ld)", diff);
		printf (_("\n  %d free blocks, %d free inodes, "
			  "%d directories\n  Free blocks: "),
			fs->group_desc[i].bg_free_blocks_count,
			fs->group_desc[i].bg_free_inodes_count,
			fs->group_desc[i].bg_used_dirs_count);
		print_free (i, block_bitmap, fs->super->s_blocks_per_group,
			    fs->super->s_first_data_block);
		fputs(_("\n  Free inodes: "), stdout);
		print_free (i, inode_bitmap, fs->super->s_inodes_per_group, 1);
		fputc('\n', stdout);
		block_bitmap += fs->super->s_blocks_per_group / 8;
		inode_bitmap += fs->super->s_inodes_per_group / 8;
		group_blk = next_blk;
	}
}

static void list_bad_blocks(ext2_filsys fs, int dump)
{
	badblocks_list		bb_list = 0;
	badblocks_iterate	bb_iter;
	blk_t			blk;
	errcode_t		retval;
	const char		*header, *fmt;

	retval = ext2fs_read_bb_inode(fs, &bb_list);
	if (retval) {
		com_err("ext2fs_read_bb_inode", retval, "");
		return;
	}
	retval = ext2fs_badblocks_list_iterate_begin(bb_list, &bb_iter);
	if (retval) {
		com_err("ext2fs_badblocks_list_iterate_begin", retval,
			_("while printing bad block list"));
		return;
	}
	if (dump) {
		header = fmt = "%d\n";
	} else {
		header =  _("Bad blocks: %d");
		fmt = ", %d";
	}
	while (ext2fs_badblocks_list_iterate(bb_iter, &blk)) {
		printf(header ? header : fmt, blk);
		header = 0;
	}
	ext2fs_badblocks_list_iterate_end(bb_iter);
	if (!dump)
		fputc('\n', stdout);
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

	printf(_("\nJournal block size:       %d\n"
		 "Journal length:           %d\n"
		 "Journal first block:      %d\n"
		 "Journal sequence:         0x%08x\n"
		 "Journal start:            %d\n"
		 "Journal number of users:  %d\n"),
	       ntohl(jsb->s_blocksize),  ntohl(jsb->s_maxlen),
	       ntohl(jsb->s_first), ntohl(jsb->s_sequence),
	       ntohl(jsb->s_start), ntohl(jsb->s_nr_users));

	for (i=0; i < ntohl(jsb->s_nr_users); i++) {
		uuid_unparse(&jsb->s_users[i*16], str);
		printf(i ? "                          %s\n"
		       : "Journal users:            %s\n",
		       str);
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
	setlocale(LC_CTYPE, "");
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
			num_format = "0x%04x";
			break;
		default:
			usage();
		}
	}
	if (optind > argc - 1)
		usage();
	sprintf(range_format, "%s-%s", num_format, num_format);
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
		list_bad_blocks(fs, 1);
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
		list_bad_blocks(fs, 0);
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
