/*
 * mke2fs.c - Make a ext2fs filesystem.
 * 
 * Copyright (C) 1994, 1995, 1996, 1997 Theodore Ts'o.
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 */

/* Usage: mke2fs [options] device
 * 
 * The device may be a block device or a image of one, but this isn't
 * enforced (but it's not much fun on a character device :-). 
 */

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <ctype.h>
#include <time.h>
#ifdef linux
#include <sys/utsname.h>
#endif
#ifdef HAVE_GETOPT_H
#include <getopt.h>
#else
extern char *optarg;
extern int optind;
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#ifdef HAVE_MNTENT_H
#include <mntent.h>
#endif
#include <sys/ioctl.h>
#include <sys/types.h>

#include <linux/ext2_fs.h>
#ifdef HAVE_LINUX_MAJOR_H
#include <linux/major.h>
#include <sys/stat.h>		/* Only need sys/stat.h for major nr test */
#endif

#include "et/com_err.h"
#include "uuid/uuid.h"
#include "e2p/e2p.h"
#include "ext2fs/ext2fs.h"
#include "../version.h"
#include "nls-enable.h"

/* Everything is STDC, these days */
#define NOARGS void

#define STRIDE_LENGTH 8

#ifndef __sparc__
#define ZAP_BOOTBLOCK
#endif

extern int isatty(int);
extern FILE *fpopen(const char *cmd, const char *mode);

const char * program_name = "mke2fs";
const char * device_name = NULL;

/* Command line options */
int	cflag = 0;
int	verbose = 0;
int	quiet = 0;
int	super_only = 0;
int	force = 0;
int	noaction = 0;
char	*bad_blocks_filename = 0;
__u32	fs_stride = 0;

struct ext2_super_block param;
char *creator_os = NULL;
char *volume_label = NULL;
char *mount_dir = NULL;

static void usage(NOARGS), check_plausibility(NOARGS), check_mount(NOARGS);

static void usage(NOARGS)
{
	fprintf(stderr, _("Usage: %s [-c|-t|-l filename] [-b block-size] "
	"[-f fragment-size]\n\t[-i bytes-per-inode] "
	" [-N number-of-inodes]\n\t[-m reserved-blocks-percentage] "
	"[-o creator-os] [-g blocks-per-group]\n\t[-L volume-label] "
	"[-M last-mounted-directory] [-O feature[,...]]\n\t"
	"[-r fs-revision] [-R raid_opts] [-s sparse-super-flag]\n\t"
	"[-qvSV] device [blocks-count]\n"),
		program_name);
	exit(1);
}

static int int_log2(int arg)
{
	int	l = 0;

	arg >>= 1;
	while (arg) {
		l++;
		arg >>= 1;
	}
	return l;
}

static int int_log10(unsigned int arg)
{
	int	l;

	for (l=0; arg ; l++)
		arg = arg / 10;
	return l;
}

static void proceed_question(NOARGS)
{
	char buf[256];
	char *short_yes = _("yY");

	fflush(stdout);
	fflush(stderr);
	printf(_("Proceed anyway? (y,n) "));
	buf[0] = 0;
	fgets(buf, sizeof(buf), stdin);
	if (strchr(short_yes, buf[0]) == 0)
		exit(1);
}

#ifndef SCSI_BLK_MAJOR
#define SCSI_BLK_MAJOR(M)  ((M) == SCSI_DISK_MAJOR || (M) == SCSI_CDROM_MAJOR)
#endif

static void check_plausibility(NOARGS)
{
#ifdef HAVE_LINUX_MAJOR_H
#ifndef MAJOR
#define MAJOR(dev)	((dev)>>8)
#define MINOR(dev)	((dev) & 0xff)
#endif

	int val;
	struct stat s;
	
	val = stat(device_name, &s);
	
	if(val == -1) {
		fprintf(stderr, _("Could not stat %s --- %s\n"),
			device_name, error_message(errno));
		if (errno == ENOENT)
			fprintf(stderr, _("\nThe device apparently does "
			       "not exist; did you specify it correctly?\n"));
		exit(1);
	}
	if(!S_ISBLK(s.st_mode)) {
		printf(_("%s is not a block special device.\n"), device_name);
		proceed_question();
		return;
	} else if ((MAJOR(s.st_rdev) == HD_MAJOR &&
		    MINOR(s.st_rdev)%64 == 0) ||
		   (SCSI_BLK_MAJOR(MAJOR(s.st_rdev)) &&
		       MINOR(s.st_rdev)%16 == 0)) {
		printf(_("%s is entire device, not just one partition!\n"),
		       device_name);
		proceed_question();
	}
#endif
}

static void check_mount(NOARGS)
{
	errcode_t	retval;
	int		mount_flags;

	retval = ext2fs_check_if_mounted(device_name, &mount_flags);
	if (retval) {
		com_err("ext2fs_check_if_mount", retval,
			_("while determining whether %s is mounted."),
			device_name);
		return;
	}
	if (!(mount_flags & EXT2_MF_MOUNTED))
		return;

	fprintf(stderr, _("%s is mounted; "), device_name);
	if (force) {
		fprintf(stderr, _("mke2fs forced anyway.  "
			"Hope /etc/mtab is incorrect.\n"));
	} else {
		fprintf(stderr, _("will not make a filesystem here!\n"));
		exit(1);
	}
}

/*
 * This function sets the default parameters for a filesystem
 *
 * The type is specified by the user.  The size is the maximum size
 * (in megabytes) for which a set of parameters applies, with a size
 * of zero meaning that it is the default parameter for the type.
 * Note that order is important in the table below.
 */
static char default_str[] = "default";
struct mke2fs_defaults {
	const char	*type;
	int		size;
	int		blocksize;
	int		inode_ratio;
} settings[] = {
	{ default_str, 0, 4096, 8192 },
	{ default_str, 512, 1024, 4096 },
	{ default_str, 3, 1024, 8192 },
	{ "news", 0, 4096, 4096 },
	{ 0, 0, 0, 0},
};

static void set_fs_defaults(char *fs_type, struct ext2fs_sb *super,
			    int blocksize, int *inode_ratio)
{
	int	megs;
	int	ratio = 0;
	struct mke2fs_defaults *p;

	megs = (super->s_blocks_count * (EXT2_BLOCK_SIZE(super) / 1024) /
		1024);
	if (inode_ratio)
		ratio = *inode_ratio;
	if (!fs_type)
		fs_type = default_str;
	for (p = settings; p->type; p++) {
		if ((strcmp(p->type, fs_type) != 0) &&
		    (strcmp(p->type, default_str) != 0))
			continue;
		if ((p->size != 0) &&
		    (megs > p->size))
			continue;
		if (ratio == 0)
			*inode_ratio = p->inode_ratio;
		if (blocksize == 0) {
			super->s_log_frag_size = super->s_log_block_size =
				int_log2(p->blocksize >> EXT2_MIN_BLOCK_LOG_SIZE);
		}
	}
	if (blocksize == 0)
		super->s_blocks_count /= EXT2_BLOCK_SIZE(super) / 1024;
}

/*
 * Helper function for read_bb_file and test_disk
 */
static void invalid_block(ext2_filsys fs, blk_t blk)
{
	printf(_("Bad block %u out of range; ignored.\n"), blk);
	return;
}

/*
 * Reads the bad blocks list from a file
 */
static void read_bb_file(ext2_filsys fs, badblocks_list *bb_list,
			 const char *bad_blocks_file)
{
	FILE		*f;
	errcode_t	retval;

	f = fopen(bad_blocks_file, "r");
	if (!f) {
		com_err("read_bad_blocks_file", errno,
			_("while trying to open %s"), bad_blocks_file);
		exit(1);
	}
	retval = ext2fs_read_bb_FILE(fs, f, bb_list, invalid_block);
	fclose (f);
	if (retval) {
		com_err("ext2fs_read_bb_FILE", retval,
			_("while reading in list of bad blocks from file"));
		exit(1);
	}
}

/*
 * Runs the badblocks program to test the disk
 */
static void test_disk(ext2_filsys fs, badblocks_list *bb_list)
{
	FILE		*f;
	errcode_t	retval;
	char		buf[1024];

	sprintf(buf, "badblocks -b %d %s%s %d", fs->blocksize,
		quiet ? "" : "-s ", fs->device_name,
		fs->super->s_blocks_count);
	if (verbose)
		printf(_("Running command: %s\n"), buf);
	f = popen(buf, "r");
	if (!f) {
		com_err("popen", errno,
			_("while trying run '%s'"), buf);
		exit(1);
	}
	retval = ext2fs_read_bb_FILE(fs, f, bb_list, invalid_block);
	pclose(f);
	if (retval) {
		com_err("ext2fs_read_bb_FILE", retval,
			_("while processing list of bad blocks from program"));
		exit(1);
	}
}

static void handle_bad_blocks(ext2_filsys fs, badblocks_list bb_list)
{
	int			i, j;
	int			must_be_good;
	blk_t			blk;
	badblocks_iterate	bb_iter;
	errcode_t		retval;
	blk_t			group_block;
	int			group;
	int			group_bad;

	if (!bb_list)
		return;
	
	/*
	 * The primary superblock and group descriptors *must* be
	 * good; if not, abort.
	 */
	must_be_good = fs->super->s_first_data_block + 1 + fs->desc_blocks;
	for (i = fs->super->s_first_data_block; i <= must_be_good; i++) {
		if (badblocks_list_test(bb_list, i)) {
			fprintf(stderr, _("Block %d in primary "
				"superblock/group descriptor area bad.\n"), i);
			fprintf(stderr, _("Blocks %d through %d must be good "
				"in order to build a filesystem.\n"),
				fs->super->s_first_data_block, must_be_good);
			fprintf(stderr, _("Aborting....\n"));
			exit(1);
		}
	}

	/*
	 * See if any of the bad blocks are showing up in the backup
	 * superblocks and/or group descriptors.  If so, issue a
	 * warning and adjust the block counts appropriately.
	 */
	group_block = fs->super->s_first_data_block +
		fs->super->s_blocks_per_group;
	
	for (i = 1; i < fs->group_desc_count; i++) {
		group_bad = 0;
		for (j=0; j < fs->desc_blocks+1; j++) {
			if (badblocks_list_test(bb_list, group_block +
						j)) {
				if (!group_bad) 
					fprintf(stderr,
_("Warning: the backup superblock/group descriptors at block %d contain\n"
"	bad blocks.\n\n"),
						group_block);
				group_bad++;
				group = ext2fs_group_of_blk(fs, group_block+j);
				fs->group_desc[group].bg_free_blocks_count++;
				fs->super->s_free_blocks_count++;
			}
		}
		group_block += fs->super->s_blocks_per_group;
	}
	
	/*
	 * Mark all the bad blocks as used...
	 */
	retval = badblocks_list_iterate_begin(bb_list, &bb_iter);
	if (retval) {
		com_err("badblocks_list_iterate_begin", retval,
			_("while marking bad blocks as used"));
		exit(1);
	}
	while (badblocks_list_iterate(bb_iter, &blk)) 
		ext2fs_mark_block_bitmap(fs->block_map, blk);
	badblocks_list_iterate_end(bb_iter);
}

static void write_inode_tables(ext2_filsys fs)
{
	errcode_t	retval;
	blk_t		blk;
	int		i, j, num, count;
	char		*buf;
	char		format[20], backup[80];
	int		sync_kludge = 0;
	char		*mke2fs_sync;

	mke2fs_sync = getenv("MKE2FS_SYNC");
	if (mke2fs_sync)
		sync_kludge = atoi(mke2fs_sync);

	buf = malloc(fs->blocksize * STRIDE_LENGTH);
	if (!buf) {
		com_err("malloc", ENOMEM,
			_("while allocating zeroizing buffer"));
		exit(1);
	}
	memset(buf, 0, fs->blocksize * STRIDE_LENGTH);

	/*
	 * Figure out how many digits we need
	 */
	i = int_log10(fs->group_desc_count);
	sprintf(format, "%%%dd/%%%dld", i, i);
	memset(backup, '\b', sizeof(backup)-1);
	backup[sizeof(backup)-1] = 0;
	if ((2*i)+1 < sizeof(backup))
		backup[(2*i)+1] = 0;

	if (!quiet)
		printf(_("Writing inode tables: "));
	for (i = 0; i < fs->group_desc_count; i++) {
		if (!quiet)
			printf(format, i, fs->group_desc_count);
		
		blk = fs->group_desc[i].bg_inode_table;
		num = fs->inode_blocks_per_group;
		
		for (j=0; j < num; j += STRIDE_LENGTH, blk += STRIDE_LENGTH) {
			if (num-j > STRIDE_LENGTH)
				count = STRIDE_LENGTH;
			else
				count = num - j;
			retval = io_channel_write_blk(fs->io, blk, count, buf);
			if (retval)
				printf(_("Warning: could not write %d blocks "
				       "in inode table starting at %d: %s\n"),
				       count, blk, error_message(retval));
		}
		if (!quiet) 
			fputs(backup, stdout);
		if (sync_kludge) {
			if (sync_kludge == 1)
				sync();
			else if ((i % sync_kludge) == 0)
				sync();
		}
	}
	free(buf);
	if (!quiet)
		fputs(_("done                            \n"), stdout);
}

static void create_root_dir(ext2_filsys fs)
{
	errcode_t	retval;
	struct ext2_inode	inode;

	retval = ext2fs_mkdir(fs, EXT2_ROOT_INO, EXT2_ROOT_INO, 0);
	if (retval) {
		com_err("ext2fs_mkdir", retval, _("while creating root dir"));
		exit(1);
	}
	if (geteuid()) {
		retval = ext2fs_read_inode(fs, EXT2_ROOT_INO, &inode);
		if (retval) {
			com_err("ext2fs_read_inode", retval,
				_("while reading root inode"));
			exit(1);
		}
		inode.i_uid = getuid();
		if (inode.i_uid)
			inode.i_gid = getgid();
		retval = ext2fs_write_inode(fs, EXT2_ROOT_INO, &inode);
		if (retval) {
			com_err("ext2fs_write_inode", retval,
				_("while setting root inode ownership"));
			exit(1);
		}
	}
}

static void create_lost_and_found(ext2_filsys fs)
{
	errcode_t		retval;
	ino_t			ino;
	const char		*name = "lost+found";
	int			i;
	int			lpf_size = 0;

	retval = ext2fs_mkdir(fs, EXT2_ROOT_INO, 0, name);
	if (retval) {
		com_err("ext2fs_mkdir", retval,
			_("while creating /lost+found"));
		exit(1);
	}

	retval = ext2fs_lookup(fs, EXT2_ROOT_INO, name, strlen(name), 0, &ino);
	if (retval) {
		com_err("ext2_lookup", retval,
			_("while looking up /lost+found"));
		exit(1);
	}
	
	for (i=1; i < EXT2_NDIR_BLOCKS; i++) {
		if ((lpf_size += fs->blocksize) >= 16*1024)
			break;
		retval = ext2fs_expand_dir(fs, ino);
		if (retval) {
			com_err("ext2fs_expand_dir", retval,
				_("while expanding /lost+found"));
			exit(1);
		}
	}		
}

static void create_bad_block_inode(ext2_filsys fs, badblocks_list bb_list)
{
	errcode_t	retval;
	
	ext2fs_mark_inode_bitmap(fs->inode_map, EXT2_BAD_INO);
	fs->group_desc[0].bg_free_inodes_count--;
	fs->super->s_free_inodes_count--;
	retval = ext2fs_update_bb_inode(fs, bb_list);
	if (retval) {
		com_err("ext2fs_update_bb_inode", retval,
			_("while setting bad block inode"));
		exit(1);
	}

}

static void reserve_inodes(ext2_filsys fs)
{
	ino_t	i;
	int	group;

	for (i = EXT2_ROOT_INO + 1; i < EXT2_FIRST_INODE(fs->super); i++) {
		ext2fs_mark_inode_bitmap(fs->inode_map, i);
		group = ext2fs_group_of_ino(fs, i);
		fs->group_desc[group].bg_free_inodes_count--;
		fs->super->s_free_inodes_count--;
	}
	ext2fs_mark_ib_dirty(fs);
}

#ifdef ZAP_BOOTBLOCK
static void zap_bootblock(ext2_filsys fs)
{
	char buf[512];
	int retval;

	memset(buf, 0, 512);
	
	retval = io_channel_write_blk(fs->io, 0, -512, buf);
	if (retval)
		printf(_("Warning: could not erase block 0: %s\n"),
		       error_message(retval));
}
#endif
	

static void show_stats(ext2_filsys fs)
{
	struct ext2fs_sb 	*s = (struct ext2fs_sb *) fs->super;
	char 			buf[80];
	blk_t			group_block;
	int			i, need, col_left;
	
	if (param.s_blocks_count != s->s_blocks_count)
		printf(_("warning: %d blocks unused.\n\n"),
		       param.s_blocks_count - s->s_blocks_count);

	memset(buf, 0, sizeof(buf));
	strncpy(buf, s->s_volume_name, sizeof(s->s_volume_name));
	printf(_("Filesystem label=%s\n"), buf);
	printf(_("OS type: "));
	switch (fs->super->s_creator_os) {
	    case EXT2_OS_LINUX: printf ("Linux"); break;
	    case EXT2_OS_HURD:  printf ("GNU/Hurd");   break;
	    case EXT2_OS_MASIX: printf ("Masix"); break;
	    default:		printf (_("(unknown os)"));
        }
	printf("\n");
	printf(_("Block size=%u (log=%u)\n"), fs->blocksize,
		s->s_log_block_size);
	printf(_("Fragment size=%u (log=%u)\n"), fs->fragsize,
		s->s_log_frag_size);
	printf(_("%u inodes, %u blocks\n"), s->s_inodes_count,
	       s->s_blocks_count);
	printf(_("%u blocks (%2.2f%%) reserved for the super user\n"),
		s->s_r_blocks_count,
	       100.0 * s->s_r_blocks_count / s->s_blocks_count);
	printf(_("First data block=%u\n"), s->s_first_data_block);
	if (fs->group_desc_count > 1)
		printf(_("%lu block groups\n"), fs->group_desc_count);
	else
		printf(_("%lu block group\n"), fs->group_desc_count);
	printf(_("%u blocks per group, %u fragments per group\n"),
	       s->s_blocks_per_group, s->s_frags_per_group);
	printf(_("%u inodes per group\n"), s->s_inodes_per_group);

	if (fs->group_desc_count == 1) {
		printf("\n");
		return;
	}
	
	printf(_("Superblock backups stored on blocks: "));
	group_block = s->s_first_data_block;
	col_left = 0;
	for (i = 1; i < fs->group_desc_count; i++) {
		group_block += s->s_blocks_per_group;
		if (!ext2fs_bg_has_super(fs, i))
			continue;
		if (i != 1)
			printf(", ");
		need = int_log10(group_block) + 2;
		if (need > col_left) {
			printf("\n\t");
			col_left = 72;
		}
		col_left -= need;
		printf("%u", group_block);
	}
	printf("\n\n");
}

#ifndef HAVE_STRCASECMP
static int strcasecmp (char *s1, char *s2)
{
	while (*s1 && *s2) {
		int ch1 = *s1++, ch2 = *s2++;
		if (isupper (ch1))
			ch1 = tolower (ch1);
		if (isupper (ch2))
			ch2 = tolower (ch2);
		if (ch1 != ch2)
			return ch1 - ch2;
	}
	return *s1 ? 1 : *s2 ? -1 : 0;
}
#endif

/*
 * Set the S_CREATOR_OS field.  Return true if OS is known,
 * otherwise, 0.
 */
static int set_os(struct ext2_super_block *sb, char *os)
{
	if (isdigit (*os))
		sb->s_creator_os = atoi (os);
	else if (strcasecmp(os, "linux") == 0)
		sb->s_creator_os = EXT2_OS_LINUX;
	else if (strcasecmp(os, "GNU") == 0 || strcasecmp(os, "hurd") == 0)
		sb->s_creator_os = EXT2_OS_HURD;
	else if (strcasecmp(os, "masix") == 0)
		sb->s_creator_os = EXT2_OS_MASIX;
	else
		return 0;
	return 1;
}

#define PATH_SET "PATH=/sbin"

static void parse_raid_opts(const char *opts)
{
	char	*buf, *token, *next, *p, *arg;
	int	len;
	int	raid_usage = 0;

	len = strlen(opts);
	buf = malloc(len+1);
	if (!buf) {
		fprintf(stderr, _("Couldn't allocate memory to parse "
			"raid options!\n"));
		exit(1);
	}
	strcpy(buf, opts);
	for (token = buf; token && *token; token = next) {
		p = strchr(token, ',');
		next = 0;
		if (p) {
			*p = 0;
			next = p+1;
		} 
		arg = strchr(token, '=');
		if (arg) {
			*arg = 0;
			arg++;
		}
		if (strcmp(token, "stride") == 0) {
			if (!arg) {
				raid_usage++;
				continue;
			}
			fs_stride = strtoul(arg, &p, 0);
			if (*p || (fs_stride == 0)) {
				fprintf(stderr,
					_("Invalid stride parameter.\n"));
				raid_usage++;
				continue;
			}
		} else
			raid_usage++;
	}
	if (raid_usage) {
		fprintf(stderr, _("\nBad raid options specified.\n\n"
			"Raid options are separated by commas, "
			"and may take an argument which\n"
			"\tis set off by an equals ('=') sign.\n\n"
			"Valid raid options are:\n"
			"\tstride=<stride length in blocks>\n\n"));
		exit(1);
	}
}	

static __u32 ok_features[3] = {
	0,					/* Compat */
	EXT2_FEATURE_INCOMPAT_FILETYPE,		/* Incompat */
	EXT2_FEATURE_RO_COMPAT_SPARSE_SUPER	/* R/O compat */
};


static void PRS(int argc, char *argv[])
{
	int		c;
	int		size;
	char *		tmp;
	blk_t		max = 8192;
	int		blocksize = 0;
	int		inode_ratio = 0;
	int		reserved_ratio = 5;
	ino_t		num_inodes = 0;
	errcode_t	retval;
	int		sparse_option = 0;
	char *		oldpath = getenv("PATH");
	struct ext2fs_sb *param_ext2 = (struct ext2fs_sb *) &param;
	char *		raid_opts = 0;
	char *		fs_type = 0;
	const char *	feature_set = "filetype,sparse_super";
	blk_t		dev_size;
#ifdef linux
	struct 		utsname ut;

	if (uname(&ut)) {
		perror("uname");
		exit(1);
	}
	if ((ut.release[0] == '1') ||
	    (ut.release[0] == '2' && ut.release[1] == '.' &&
	     ut.release[2] < '2' && ut.release[3] == '.'))
		feature_set = 0;
#endif
	/* Update our PATH to include /sbin  */
	if (oldpath) {
		char *newpath;
		
		newpath = malloc(sizeof (PATH_SET) + 1 + strlen (oldpath));
		strcpy (newpath, PATH_SET);
		strcat (newpath, ":");
		strcat (newpath, oldpath);
		putenv (newpath);
	} else
		putenv (PATH_SET);

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);
	initialize_ext2_error_table();
	memset(&param, 0, sizeof(struct ext2_super_block));
	param.s_rev_level = 1;  /* Create revision 1 filesystems now */
	
	fprintf (stderr, _("mke2fs %s, %s for EXT2 FS %s, %s\n"),
		 E2FSPROGS_VERSION, E2FSPROGS_DATE,
		 EXT2FS_VERSION, EXT2FS_DATE);
	if (argc && *argv)
		program_name = *argv;
	while ((c = getopt (argc, argv,
		    "b:cf:g:i:l:m:no:qr:R:s:tvI:ST:FL:M:N:O:V")) != EOF)
		switch (c) {
		case 'b':
			blocksize = strtoul(optarg, &tmp, 0);
			if (blocksize < 1024 || blocksize > 4096 || *tmp) {
				com_err(program_name, 0,
					_("bad block size - %s"), optarg);
				exit(1);
			}
			param.s_log_block_size =
				int_log2(blocksize >> EXT2_MIN_BLOCK_LOG_SIZE);
			max = blocksize * 8;
			break;
		case 'c':
		case 't':	/* Check for bad blocks */
			cflag = 1;
			break;
		case 'f':
			size = strtoul(optarg, &tmp, 0);
			if (size < 1024 || size > 4096 || *tmp) {
				com_err(program_name, 0,
					_("bad fragment size - %s"),
					optarg);
				exit(1);
			}
			param.s_log_frag_size =
				int_log2(size >> EXT2_MIN_BLOCK_LOG_SIZE);
			printf(_("Warning: fragments not supported.  "
			       "Ignoring -f option\n"));
			break;
		case 'g':
			param.s_blocks_per_group = strtoul(optarg, &tmp, 0);
			if (*tmp) {
				com_err(program_name, 0,
					_("Illegal number for blocks per group"));
				exit(1);
			}
			if ((param.s_blocks_per_group % 8) != 0) {
				com_err(program_name, 0,
				_("blocks per group must be multiple of 8"));
				exit(1);
			}
			break;
		case 'i':
			inode_ratio = strtoul(optarg, &tmp, 0);
			if (inode_ratio < 1024 || inode_ratio > 256 * 1024 ||
			    *tmp) {
				com_err(program_name, 0,
					_("bad inode ratio - %s"), optarg);
				exit(1);
			}
			break;
		case 'l':
			bad_blocks_filename = malloc(strlen(optarg)+1);
			if (!bad_blocks_filename) {
				com_err(program_name, ENOMEM,
					_("in malloc for bad_blocks_filename"));
				exit(1);
			}
			strcpy(bad_blocks_filename, optarg);
			break;
		case 'm':
			reserved_ratio = strtoul(optarg, &tmp, 0);
			if (reserved_ratio > 50 || *tmp) {
				com_err(program_name, 0,
					_("bad reserved blocks percent - %s"),
					optarg);
				exit(1);
			}
			break;
		case 'n':
			noaction++;
			break;
		case 'o':
			creator_os = optarg;
			break;
		case 'r':
			param.s_rev_level = atoi(optarg);
			break;
		case 's':
			sparse_option = atoi(optarg);
			break;
#ifdef EXT2_DYNAMIC_REV
		case 'I':
			param.s_inode_size = atoi(optarg);
			break;
#endif
		case 'N':
			num_inodes = atoi(optarg);
			break;
		case 'v':
			verbose = 1;
			break;
		case 'q':
			quiet = 1;
			break;
		case 'F':
			force = 1;
			break;
		case 'L':
			volume_label = optarg;
			break;
		case 'M':
			mount_dir = optarg;
			break;
		case 'O':
			feature_set = optarg;
			break;
		case 'R':
			raid_opts = optarg;
			break;
		case 'S':
			super_only = 1;
			break;
		case 'T':
			fs_type = optarg;
			break;
		case 'V':
			/* Print version number and exit */
			fprintf(stderr, _("\tUsing %s\n"),
				error_message(EXT2_ET_BASE));
			exit(0);
		default:
			usage();
		}
	if (optind == argc)
		usage();
	device_name = argv[optind];
	optind++;
	if (optind < argc) {
		unsigned long tmp2  = strtoul(argv[optind++], &tmp, 0);

		if ((*tmp) || (tmp2 > 0xfffffffful)) {
			com_err(program_name, 0, _("bad blocks count - %s"),
				argv[optind - 1]);
			exit(1);
		}
		param.s_blocks_count = tmp2;
	}
	if (optind < argc)
		usage();

	if (raid_opts)
		parse_raid_opts(raid_opts);

	if (!force)
		check_plausibility();
	check_mount();

	param.s_log_frag_size = param.s_log_block_size;

	if (noaction && param.s_blocks_count) {
		dev_size = param.s_blocks_count;
		retval = 0;
	} else
		retval = ext2fs_get_device_size(device_name,
						EXT2_BLOCK_SIZE(&param),
						&dev_size);
	if (retval && (retval != EXT2_ET_UNIMPLEMENTED)) {
		com_err(program_name, retval,
			_("while trying to determine filesystem size"));
		exit(1);
	}
	if (!param.s_blocks_count) {
		if (retval == EXT2_ET_UNIMPLEMENTED) {
			com_err(program_name, 0,
				_("Couldn't determine device size; you "
				"must specify\nthe size of the "
				"filesystem\n"));
			exit(1);
		} else {
			if (dev_size == 0) {
				com_err(program_name, 0,
				_("Device size reported to be zero.  "
				  "Invalid partition specified, or\n\t"
				  "partition table wasn't reread "
				  "after running fdisk, due to\n\t"
				  "a modified partition being busy "
				  "and in use.  You may need to reboot\n\t"
				  "to re-read your partition table.\n"
				  ));
				exit(1);
			}
			param.s_blocks_count = dev_size;
		}
		
	} else if (!force && (param.s_blocks_count > dev_size)) {
		com_err(program_name, 0,
			_("Filesystem larger than apparent filesystem size."));
		proceed_question();
	}

	set_fs_defaults(fs_type, param_ext2, blocksize, &inode_ratio);

	if (param.s_blocks_per_group) {
		if (param.s_blocks_per_group < 256 ||
		    param.s_blocks_per_group > max || *tmp) {
			com_err(program_name, 0,
				_("blocks per group count out of range"));
			exit(1);
		}
	}

	/*
	 * Calculate number of inodes based on the inode ratio
	 */
	param.s_inodes_count = num_inodes ? num_inodes : 
		((__u64) param.s_blocks_count * EXT2_BLOCK_SIZE(&param))
			/ inode_ratio;

	/*
	 * Calculate number of blocks to reserve
	 */
	param.s_r_blocks_count = (param.s_blocks_count * reserved_ratio) / 100;

#ifdef EXT2_FEATURE_RO_COMPAT_SPARSE_SUPER
	if (sparse_option)
		param_ext2->s_feature_ro_compat |=
			EXT2_FEATURE_RO_COMPAT_SPARSE_SUPER;
#endif
	if (feature_set && !strncasecmp(feature_set, "none", 4))
		feature_set = 0;
	if (feature_set && e2p_edit_feature(feature_set,
					    &param_ext2->s_feature_compat,
					    ok_features)) {
		fprintf(stderr, _("Invalid filesystem option set: %s\n"),
			feature_set);
		exit(1);
	}
}
					
int main (int argc, char *argv[])
{
	errcode_t	retval = 0;
	ext2_filsys	fs;
	badblocks_list	bb_list = 0;
	struct ext2fs_sb *s;

#ifdef ENABLE_NLS
	setlocale(LC_MESSAGES, "");
	bindtextdomain(NLS_CAT_NAME, LOCALEDIR);
	textdomain(NLS_CAT_NAME);
#endif
	PRS(argc, argv);

	/*
	 * Initialize the superblock....
	 */
	retval = ext2fs_initialize(device_name, 0, &param,
				   unix_io_manager, &fs);
	if (retval) {
		com_err(device_name, retval, _("while setting up superblock"));
		exit(1);
	}

	/*
	 * Generate a UUID for it...
	 */
	s = (struct ext2fs_sb *) fs->super;
	uuid_generate(s->s_uuid);

	/*
	 * Override the creator OS, if applicable
	 */
	if (creator_os && !set_os(fs->super, creator_os)) {
		com_err (program_name, 0, _("unknown os - %s"), creator_os);
		exit(1);
	}

	/*
	 * For the Hurd, we will turn off filetype since it doesn't
	 * support it.
	 */
	if (fs->super->s_creator_os == EXT2_OS_HURD)
		fs->super->s_feature_incompat &=
			~EXT2_FEATURE_INCOMPAT_FILETYPE;

	/*
	 * Set the volume label...
	 */
	if (volume_label) {
		memset(s->s_volume_name, 0, sizeof(s->s_volume_name));
		strncpy(s->s_volume_name, volume_label,
			sizeof(s->s_volume_name));
	}

	/*
	 * Set the last mount directory
	 */
	if (mount_dir) {
		memset(s->s_last_mounted, 0, sizeof(s->s_last_mounted));
		strncpy(s->s_last_mounted, mount_dir,
			sizeof(s->s_last_mounted));
	}
	
	if (!quiet || noaction)
		show_stats(fs);

	if (noaction)
		exit(0);

	if (bad_blocks_filename)
		read_bb_file(fs, &bb_list, bad_blocks_filename);
	if (cflag)
		test_disk(fs, &bb_list);

	handle_bad_blocks(fs, bb_list);
	fs->stride = fs_stride;
	retval = ext2fs_allocate_tables(fs);
	if (retval) {
		com_err(program_name, retval,
			_("while trying to allocate filesystem tables"));
		exit(1);
	}
	if (super_only) {
		fs->super->s_state |= EXT2_ERROR_FS;
		fs->flags &= ~(EXT2_FLAG_IB_DIRTY|EXT2_FLAG_BB_DIRTY);
	} else {
		write_inode_tables(fs);
		create_root_dir(fs);
		create_lost_and_found(fs);
		reserve_inodes(fs);
		create_bad_block_inode(fs, bb_list);
#ifdef ZAP_BOOTBLOCK
		zap_bootblock(fs);
#endif
	}
	
	if (!quiet)
		printf(_("Writing superblocks and "
		       "filesystem accounting information: "));
	retval = ext2fs_flush(fs);
	if (retval) {
		printf(_("\nWarning, had trouble writing out superblocks."));
	}
	if (!quiet)
		printf(_("done\n"));
	ext2fs_close(fs);
	return 0;
}
