/*
 * setsuper.c --- set a superblock value
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#include <fcntl.h>
#include <utime.h>

#include "debugfs.h"
#include "uuid/uuid.h"
#include "e2p/e2p.h"

static struct ext2_super_block set_sb;

struct super_set_info {
	const char	*name;
	void	*ptr;
	unsigned int	size;
	errcode_t (*func)(struct super_set_info *info, char *arg);
};

static errcode_t parse_uint(struct super_set_info *info, char *arg);
static errcode_t parse_int(struct super_set_info *info, char *arg);
static errcode_t parse_string(struct super_set_info *info, char *arg);
static errcode_t parse_uuid(struct super_set_info *info, char *arg);
static errcode_t parse_hashalg(struct super_set_info *info, char *arg);

static struct super_set_info super_fields[] = {
	{ "inodes_count", &set_sb.s_inodes_count, 4, parse_uint },
	{ "blocks_count", &set_sb.s_blocks_count, 4, parse_uint },
	{ "r_blocks_count", &set_sb.s_r_blocks_count, 4, parse_uint },
	{ "free_blocks_count", &set_sb.s_free_blocks_count, 4, parse_uint },
	{ "free_inodes_count", &set_sb.s_free_inodes_count, 4, parse_uint },
	{ "first_data_block", &set_sb.s_first_data_block, 4, parse_uint },
	{ "log_block_size", &set_sb.s_log_block_size, 4, parse_uint },
	{ "log_frag_size", &set_sb.s_log_frag_size, 4, parse_int },
	{ "blocks_per_group", &set_sb.s_blocks_per_group, 4, parse_uint },
	{ "frags_per_group", &set_sb.s_frags_per_group, 4, parse_uint },
	{ "inodes_per_group", &set_sb.s_inodes_per_group, 4, parse_uint },
	/* s_mtime (time_t) */
	/* s_wtime (time_t) */
	{ "mnt_count", &set_sb.s_mnt_count, 2, parse_uint },
	{ "max_mnt_count", &set_sb.s_max_mnt_count, 2, parse_int },
	/* s_magic */
	{ "state", &set_sb.s_state, 2, parse_uint },
	{ "errors", &set_sb.s_errors, 2, parse_uint },
	{ "minor_rev_level", &set_sb.s_minor_rev_level, 2, parse_uint },
	/* s_lastcheck (time_t) */
	{ "checkinterval", &set_sb.s_checkinterval, 4, parse_uint },
	{ "creator_os", &set_sb.s_creator_os, 4, parse_uint },
	{ "rev_level", &set_sb.s_rev_level, 4, parse_uint },
	{ "def_resuid", &set_sb.s_def_resuid, 2, parse_uint },
	{ "def_resgid", &set_sb.s_def_resgid, 2, parse_uint },
	{ "first_ino", &set_sb.s_first_ino, 4, parse_uint },
	{ "inode_size", &set_sb.s_inode_size, 2, parse_uint },
	{ "block_group_nr", &set_sb.s_block_group_nr, 2, parse_uint },
	{ "feature_compat", &set_sb.s_feature_compat, 4, parse_uint },
	{ "feature_incompat", &set_sb.s_feature_incompat, 4, parse_uint },
	{ "feature_ro_compat", &set_sb.s_feature_ro_compat, 4, parse_uint }, 
	{ "uuid", &set_sb.s_uuid, 16, parse_uuid },
	{ "volume_name",  &set_sb.s_volume_name, 16, parse_string },
	{ "last_mounted",  &set_sb.s_last_mounted, 64, parse_string },
	{ "lastcheck",  &set_sb.s_lastcheck, 4, parse_uint },
	{ "algorithm_usage_bitmap", &set_sb.s_algorithm_usage_bitmap, 
		  4, parse_uint },
	{ "prealloc_blocks", &set_sb.s_prealloc_blocks, 1, parse_uint },
	{ "prealloc_dir_blocks", &set_sb.s_prealloc_dir_blocks, 1,
		  parse_uint },
	/* s_padding1 */
	{ "journal_uuid", &set_sb.s_journal_uuid, 16, parse_uuid },
	{ "journal_inum", &set_sb.s_journal_inum, 4, parse_uint },
	{ "journal_dev", &set_sb.s_journal_dev, 4, parse_uint },
	{ "last_orphan", &set_sb.s_last_orphan, 4, parse_uint },
	{ "hash_seed", &set_sb.s_hash_seed, 16, parse_uuid },
	{ "def_hash_version", &set_sb.s_def_hash_version, 1, parse_hashalg },
	{ 0, 0, 0, 0 }
};

static struct super_set_info *find_field(char *field)
{
	struct super_set_info *ss;

	if (strncmp(field, "s_", 2) == 0)
		field += 2;
	for (ss = super_fields ; ss->name ; ss++) {
		if (strcmp(ss->name, field) == 0)
			return ss;
	}
	return NULL;
}

static errcode_t parse_uint(struct super_set_info *info, char *arg)
{
	unsigned long	num;
	char *tmp;
	__u32	*ptr32;
	__u16	*ptr16;
	__u8	*ptr8;

	num = strtoul(arg, &tmp, 0);
	if (*tmp) {
		fprintf(stderr, "Couldn't parse '%s' for field %s.\n",
			arg, info->name);
		return EINVAL;
	}
	switch (info->size) {
	case 4:
		ptr32 = (__u32 *) info->ptr;
		*ptr32 = num;
		break;
	case 2:
		ptr16 = (__u16 *) info->ptr;
		*ptr16 = num;
		break;
	case 1:
		ptr8 = (__u8 *) info->ptr;
		*ptr8 = num;
		break;
	}
	return 0;
}

static errcode_t parse_int(struct super_set_info *info, char *arg)
{
	long	num;
	char *tmp;
	__s32	*ptr32;
	__s16	*ptr16;
	__s8	*ptr8;

	num = strtol(arg, &tmp, 0);
	if (*tmp) {
		fprintf(stderr, "Couldn't parse '%s' for field %s.\n",
			arg, info->name);
		return EINVAL;
	}
	switch (info->size) {
	case 4:
		ptr32 = (__s32 *) info->ptr;
		*ptr32 = num;
		break;
	case 2:
		ptr16 = (__s16 *) info->ptr;
		*ptr16 = num;
		break;
	case 1:
		ptr8 = (__s8 *) info->ptr;
		*ptr8 = num;
		break;
	}
	return 0;
}

static errcode_t parse_string(struct super_set_info *info, char *arg)
{
	char	*cp = (char *) info->ptr;

	if (strlen(arg) >= info->size) {
		fprintf(stderr, "Error maximum size for %s is %d.\n",
			info->name, info->size);
		return EINVAL;
	}
	strcpy(cp, arg);
	return 0;
}

static errcode_t parse_uuid(struct super_set_info *info, char *arg)
{
	unsigned char *	p = (unsigned char *) info->ptr;
	
	if ((strcasecmp(arg, "null") == 0) ||
	    (strcasecmp(arg, "clear") == 0)) {
		uuid_clear(p);
	} else if (strcasecmp(arg, "time") == 0) {
		uuid_generate_time(p);
	} else if (strcasecmp(arg, "random") == 0) {
		uuid_generate(p);
	} else if (uuid_parse(arg, p)) {
		fprintf(stderr, "Invalid UUID format: %s\n", arg);
		return EINVAL;
	}
	return 0;
}

static errcode_t parse_hashalg(struct super_set_info *info, char *arg)
{
	int	hashv;
	unsigned char	*p = (unsigned char *) info->ptr;

	hashv = e2p_string2hash(arg);
	if (hashv < 0) {
		fprintf(stderr, "Invalid hash algorithm: %s\n", arg);
		return EINVAL;
	}
	*p = hashv;
	return 0;
}


static void print_possible_fields(void)
{
	struct super_set_info *ss;
	const char	*type;

	printf("Superblock fields supported by the set_super_value command:\n");
	for (ss = super_fields ; ss->name ; ss++) {
		type = "unknown";
		if (ss->func == parse_string)
			type = "string";
		else if (ss->func == parse_int)
			type = "integer";
		else if (ss->func == parse_uint)
			type = "unsigned integer";
		else if (ss->func == parse_uuid)
			type = "UUID";
		else if (ss->func == parse_hashalg)
			type = "hash algorithm";
		printf("\t%-20s\t%s\n", ss->name, type);
	}
}


void do_set_super(int argc, char *argv[])
{
	const char *usage = "<field> <value>\n"
		"\t\"set_super_value -l\" will list the names of "
		"superblock fields\n\twhich can be set.";
	static struct super_set_info *ss;
	
	if ((argc == 2) && !strcmp(argv[1], "-l")) {
		print_possible_fields();
		return;
	}

	if (common_args_process(argc, argv, 3, 3, "set_super_value",
				usage, CHECK_FS_RW))
		return;

	if ((ss = find_field(argv[1])) == 0) {
		com_err(argv[0], 0, "invalid field specifier: %s", argv[1]);
		return;
	}
	set_sb = *current_fs->super;
	if (ss->func(ss, argv[2]) == 0) {
		*current_fs->super = set_sb;
		ext2fs_mark_super_dirty(current_fs);
	}
}
