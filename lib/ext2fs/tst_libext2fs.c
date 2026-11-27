/*
 * tst_libext2fs.c
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#if HAVE_ERRNO_H
#include <errno.h>
#endif
#if HAVE_GETOPT_H
#include <getopt.h>
#else
extern int optind;
extern char *optarg;
#endif
#if HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#include "ext2_fs.h"
#include "ext2fsP.h"

#include "ss/ss.h"
#include "debugfs.h"

/*
 * Hook in new commands into debugfs
 * Override debugfs's prompt
 */
const char *debug_prog_name = "tst_libext2fs";
extern ss_request_table libext2fs_cmds;
ss_request_table *extra_cmds = &libext2fs_cmds;

void do_block_iterate(int argc, ss_argv_t argv, int sci_idx, void *infop);
void do_setup_test_fs(int argc, ss_argv_t argv, int sci_idx, void *infop);
void do_teardown_test_fs(int argc, ss_argv_t argv, int sci_idx, void *infop);
void do_init_inode(int argc, ss_argv_t argv, int sci_idx, void *infop);
void do_check_inode_blocks(int argc, ss_argv_t argv, int sci_idx, void *infop);
void do_corrupt_extent_block(int argc, ss_argv_t argv, int sci_idx, void *infop);
void do_fix_extents_csum(int argc, ss_argv_t argv, int sci_idx, void *infop);
void do_test_extent_apis(int argc, ss_argv_t argv, int sci_idx, void *infop);

/*
 * Deterministic com_err hook that writes to stdout for unit test transcripts
 */
static void stdout_com_err_proc(const char *whoami, errcode_t code,
				const char *fmt, va_list args)
	COM_ERR_ATTR((format(printf, 3, 0)));

static void stdout_com_err_proc(const char *whoami, errcode_t code,
				const char *fmt, va_list args)
{
	if (whoami) {
		fputs(whoami, stdout);
		fputs(": ", stdout);
	}
	if (code) {
		fputs(error_message(code), stdout);
		fputs(" ", stdout);
	}
	if (fmt)
		vfprintf(stdout, fmt, args);
	fputc('\n', stdout);
	fflush(stdout);
}

/*
 * Lightweight in-memory io_manager for fast, self-contained unit tests
 */
static unsigned char *mem_disk;
static unsigned long long mem_disk_size;
static io_manager mem_io_manager;

static errcode_t mem_open(const char *name, int flags EXT2FS_ATTR((unused)),
			  io_channel *channel)
{
	io_channel	io = NULL;
	errcode_t	retval;

	if (!name)
		return EXT2_ET_BAD_DEVICE_NAME;
	retval = ext2fs_get_memzero(sizeof(struct struct_io_channel), &io);
	if (retval)
		return retval;
	io->magic = EXT2_ET_MAGIC_IO_CHANNEL;
	io->manager = mem_io_manager;
	retval = ext2fs_get_mem(strlen(name) + 1, &io->name);
	if (retval) {
		ext2fs_free_mem(&io);
		return retval;
	}
	strcpy(io->name, name);
	io->block_size = 1024;
	io->refcount = 1;
	*channel = io;
	return 0;
}

static errcode_t mem_close(io_channel channel)
{
	EXT2_CHECK_MAGIC(channel, EXT2_ET_MAGIC_IO_CHANNEL);
	if (--channel->refcount > 0)
		return 0;
	if (channel->name)
		ext2fs_free_mem(&channel->name);
	ext2fs_free_mem(&channel);
	return 0;
}

static errcode_t mem_set_blksize(io_channel channel, int blksize)
{
	EXT2_CHECK_MAGIC(channel, EXT2_ET_MAGIC_IO_CHANNEL);
	channel->block_size = blksize;
	return 0;
}

static errcode_t mem_read_blk64(io_channel channel, unsigned long long block,
				int count, void *buf)
{
	size_t			size;
	unsigned long long	offset;

	EXT2_CHECK_MAGIC(channel, EXT2_ET_MAGIC_IO_CHANNEL);
	size = (count < 0) ? (size_t) (-count) :
		(size_t) count * channel->block_size;
	offset = block * channel->block_size;
	if (!mem_disk || offset >= mem_disk_size) {
		memset(buf, 0, size);
		return 0;
	}
	if (offset + size > mem_disk_size) {
		size_t avail = (size_t) (mem_disk_size - offset);

		memcpy(buf, mem_disk + offset, avail);
		memset((char *) buf + avail, 0, size - avail);
		return 0;
	}
	memcpy(buf, mem_disk + offset, size);
	return 0;
}

static errcode_t mem_write_blk64(io_channel channel, unsigned long long block,
				 int count, const void *buf)
{
	size_t			size;
	unsigned long long	offset;

	EXT2_CHECK_MAGIC(channel, EXT2_ET_MAGIC_IO_CHANNEL);
	size = (count < 0) ? (size_t) (-count) :
		(size_t) count * channel->block_size;
	offset = block * channel->block_size;
	if (!mem_disk || offset + size > mem_disk_size)
		return EXT2_ET_LLSEEK_FAILED;
	memcpy(mem_disk + offset, buf, size);
	return 0;
}

static errcode_t mem_read_blk(io_channel channel, unsigned long block,
			      int count, void *buf)
{
	return mem_read_blk64(channel, block, count, buf);
}

static errcode_t mem_write_blk(io_channel channel, unsigned long block,
			       int count, const void *buf)
{
	return mem_write_blk64(channel, block, count, buf);
}

static errcode_t mem_flush(io_channel channel)
{
	EXT2_CHECK_MAGIC(channel, EXT2_ET_MAGIC_IO_CHANNEL);
	return 0;
}

static errcode_t mem_write_byte(io_channel channel, unsigned long offset,
				int count, const void *buf)
{
	EXT2_CHECK_MAGIC(channel, EXT2_ET_MAGIC_IO_CHANNEL);
	if (!mem_disk || offset + (unsigned long) count > mem_disk_size)
		return EXT2_ET_LLSEEK_FAILED;
	memcpy(mem_disk + offset, buf, count);
	return 0;
}

static errcode_t mem_zeroout(io_channel channel, unsigned long long block,
			     unsigned long long count)
{
	unsigned long long	offset, size;

	EXT2_CHECK_MAGIC(channel, EXT2_ET_MAGIC_IO_CHANNEL);
	offset = block * channel->block_size;
	size = count * channel->block_size;
	if (!mem_disk || offset + size > mem_disk_size)
		return 0;
	memset(mem_disk + offset, 0, (size_t) size);
	return 0;
}

static struct struct_io_manager struct_mem_manager = {
	.magic		= EXT2_ET_MAGIC_IO_MANAGER,
	.name		= "In-memory test I/O Manager",
	.open		= mem_open,
	.close		= mem_close,
	.set_blksize	= mem_set_blksize,
	.read_blk	= mem_read_blk,
	.write_blk	= mem_write_blk,
	.flush		= mem_flush,
	.write_byte	= mem_write_byte,
	.read_blk64	= mem_read_blk64,
	.write_blk64	= mem_write_blk64,
	.discard	= mem_zeroout,
	.zeroout	= mem_zeroout,
};

static io_manager mem_io_manager = &struct_mem_manager;

static int print_blocks_proc(ext2_filsys fs EXT2FS_ATTR((unused)),
			     blk64_t *blocknr, e2_blkcnt_t blockcnt,
			     blk64_t ref_block, int ref_offset,
			     void *private EXT2FS_ATTR((unused)))
{
	printf("%6lld %8llu (%d %llu)\n", (long long) blockcnt,
	       (unsigned long long) *blocknr, ref_offset,
	       (unsigned long long) ref_block);
	return 0;
}


void do_block_iterate(int argc, ss_argv_t argv, int sci_idx EXT2FS_ATTR((unused)),
		      void *infop EXT2FS_ATTR((unused)))
{
	const char	*usage = "block_iterate <file> <flags";
	ext2_ino_t	ino;
	int		err = 0;
	int		flags = 0;

	if (common_args_process(argc, argv, 2, 3, argv[0], usage, 0))
		return;

	ino = string_to_inode(argv[1]);
	if (!ino)
		return;

	if (argc > 2) {
		flags = parse_ulong(argv[2], argv[0], "flags", &err);
		if (err)
			return;
	}
	flags |= BLOCK_FLAG_READ_ONLY;

	ext2fs_block_iterate3(current_fs, ino, flags, NULL,
			      print_blocks_proc, NULL);
	putc('\n', stdout);
}

void do_setup_test_fs(int argc, ss_argv_t argv, int sci_idx EXT2FS_ATTR((unused)),
		      void *infop EXT2FS_ATTR((unused)))
{
	static const __u8 test_uuid[16] = {
		0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
		0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10
	};
	struct ext2_super_block	param;
	unsigned int		blocksize = 1024;
	unsigned int		clustersize = 0;
	unsigned int		blocks = 256;
	unsigned int		inodes = 64;
	int			csum_flag = 0;
	int			c, err;
	ext2_ino_t		i;
	errcode_t		retval;

	set_com_err_hook(stdout_com_err_proc);

	reset_getopt();
	while ((c = getopt(argc, argv, "b:C:cn:i:")) != EOF) {
		switch (c) {
		case 'b':
			blocksize = parse_ulong(optarg, argv[0],
						"blocksize", &err);
			if (err)
				return;
			break;
		case 'C':
			clustersize = parse_ulong(optarg, argv[0],
						  "clustersize", &err);
			if (err)
				return;
			break;
		case 'c':
			csum_flag = 1;
			break;
		case 'n':
			blocks = parse_ulong(optarg, argv[0],
					     "blocks", &err);
			if (err)
				return;
			break;
		case 'i':
			inodes = parse_ulong(optarg, argv[0],
					     "inodes", &err);
			if (err)
				return;
			break;
		default:
			com_err(argv[0], 0,
				"Usage: setup [-b blocksize] [-C clustersize] "
				"[-c] [-n blocks] [-i inodes]");
			return;
		}
	}

	if (current_fs)
		ext2fs_close_free(&current_fs);
	if (mem_disk)
		ext2fs_free_mem(&mem_disk);

	mem_disk_size = (unsigned long long) blocks * blocksize;
	retval = ext2fs_get_memzero((size_t) mem_disk_size, &mem_disk);
	if (retval) {
		com_err(argv[0], retval, "while allocating in-memory disk");
		return;
	}

	memset(&param, 0, sizeof(param));
	param.s_log_block_size =
		ext2fs_log2_u32(blocksize >> EXT2_MIN_BLOCK_LOG_SIZE);
	if (clustersize)
		param.s_log_cluster_size =
			ext2fs_log2_u32(clustersize >> EXT2_MIN_BLOCK_LOG_SIZE);
	else
		param.s_log_cluster_size = param.s_log_block_size;
	ext2fs_blocks_count_set(&param, blocks);
	param.s_inodes_count = inodes;
	param.s_inode_size = 256;
	param.s_desc_size = EXT2_MIN_DESC_SIZE_64BIT;
	param.s_feature_incompat = EXT3_FEATURE_INCOMPAT_EXTENTS |
				   EXT4_FEATURE_INCOMPAT_64BIT;
	if (csum_flag)
		param.s_feature_ro_compat |=
			EXT4_FEATURE_RO_COMPAT_METADATA_CSUM |
			EXT2_FEATURE_RO_COMPAT_SPARSE_SUPER;
	if (clustersize > blocksize)
		param.s_feature_ro_compat |= EXT4_FEATURE_RO_COMPAT_BIGALLOC;

	struct_mem_manager.open = mem_open;
	retval = ext2fs_initialize("test_fs",
				   EXT2_FLAG_RW | EXT2_FLAG_64BITS |
				   EXT2_FLAG_EXCLUSIVE,
				   &param, mem_io_manager, &current_fs);
	if (retval) {
		com_err(argv[0], retval, "while initializing test filesystem");
		return;
	}

	memcpy(current_fs->super->s_uuid, test_uuid, sizeof(test_uuid));
	ext2fs_init_csum_seed(current_fs);

	retval = ext2fs_allocate_tables(current_fs);
	if (retval) {
		com_err(argv[0], retval, "while allocating filesystem tables");
		ext2fs_close_free(&current_fs);
		return;
	}

	for (i = 1; i < EXT2_GOOD_OLD_FIRST_INO; i++)
		ext2fs_inode_alloc_stats2(current_fs, i, +1, 0);

	root = cwd = EXT2_ROOT_INO;
	printf("Test filesystem initialized: %u blocks, %u inodes, "
	       "blocksize %u%s%s\n", blocks, inodes, blocksize,
	       csum_flag ? ", metadata_csum" : "",
	       (clustersize > blocksize) ? ", bigalloc" : "");
}

void do_teardown_test_fs(int argc EXT2FS_ATTR((unused)),
			 ss_argv_t argv EXT2FS_ATTR((unused)),
			 int sci_idx EXT2FS_ATTR((unused)),
			 void *infop EXT2FS_ATTR((unused)))
{
	if (current_fs)
		ext2fs_close_free(&current_fs);
	if (mem_disk)
		ext2fs_free_mem(&mem_disk);
	mem_disk_size = 0;
}

void do_init_inode(int argc, ss_argv_t argv, int sci_idx EXT2FS_ATTR((unused)),
		   void *infop EXT2FS_ATTR((unused)))
{
	struct ext2_inode	inode;
	unsigned long long	size = 102400;
	int			non_extent = 0;
	int			bad_header = 0;
	int			c, err;
	ext2_ino_t		ino;
	errcode_t		retval;

	if (check_fs_open(argv[0]))
		return;

	reset_getopt();
	while ((c = getopt(argc, argv, "ns:x")) != EOF) {
		switch (c) {
		case 'n':
			non_extent = 1;
			break;
		case 's':
			size = parse_ulonglong(optarg, argv[0], "size", &err);
			if (err)
				return;
			break;
		case 'x':
			bad_header = 1;
			break;
		default:
			com_err(argv[0], 0,
				"Usage: init_inode [-s size] [-n] [-x] <ino>");
			return;
		}
	}
	if (optind != argc - 1) {
		com_err(argv[0], 0,
			"Usage: init_inode [-s size] [-n] [-x] <ino>");
		return;
	}
	ino = parse_ulong(argv[optind], argv[0], "inode number", &err);
	if (err)
		return;

	memset(&inode, 0, sizeof(inode));
	inode.i_mode = LINUX_S_IFREG | 0644;
	inode.i_links_count = 1;
	ext2fs_inode_size_set(current_fs, &inode, size);
	if (non_extent) {
		inode.i_block[0] = 50;
	} else if (bad_header) {
		inode.i_flags = EXT4_EXTENTS_FL;
		inode.i_block[0] = ext2fs_cpu_to_le32(0xdeadbeef);
	}

	retval = ext2fs_write_inode(current_fs, ino, &inode);
	if (retval) {
		com_err(argv[0], retval, "while writing inode %u", ino);
		return;
	}
	ext2fs_mark_inode_bitmap2(current_fs->inode_map, ino);
	printf("Initialized inode %u (size %llu)\n", ino, size);
}

void do_check_inode_blocks(int argc, ss_argv_t argv,
			   int sci_idx EXT2FS_ATTR((unused)),
			   void *infop EXT2FS_ATTR((unused)))
{
	struct ext2_inode	inode;
	ext2_ino_t		ino;
	blk64_t			pblk;
	int			err;
	errcode_t		retval;

	if (common_args_process(argc, argv, 2, 3, argv[0],
				"<ino> [pblk]", 0))
		return;

	ino = parse_ulong(argv[1], argv[0], "inode number", &err);
	if (err)
		return;

	retval = ext2fs_read_inode(current_fs, ino, &inode);
	if (retval) {
		com_err(argv[0], retval, "while reading inode %u", ino);
		return;
	}

	if (argc == 3) {
		if (strtoblk(argv[0], argv[2], "physical block", &pblk))
			return;
		printf("Inode %u: i_blocks=%llu, block %llu allocated=%d\n",
		       ino,
		       (unsigned long long) ext2fs_inode_i_blocks(current_fs,
								  &inode),
		       (unsigned long long) pblk,
		       ext2fs_test_block_bitmap2(current_fs->block_map,
						 pblk) ? 1 : 0);
	} else {
		printf("Inode %u: i_blocks=%llu\n", ino,
		       (unsigned long long) ext2fs_inode_i_blocks(current_fs,
								  &inode));
	}
}

void do_corrupt_extent_block(int argc, ss_argv_t argv,
			     int sci_idx EXT2FS_ATTR((unused)),
			     void *infop EXT2FS_ATTR((unused)))
{
	const char			*usage =
		"<--magic|--entries|--max|--csum> <blk> [ino]";
	char				*buf = NULL;
	struct ext3_extent_header	*eh;
	blk64_t				blk;
	ext2_ino_t			ino = 0;
	int				err;
	errcode_t			retval;

	if (common_args_process(argc, argv, 3, 4, argv[0], usage, CHECK_FS_RW))
		return;

	if (strtoblk(argv[0], argv[2], "block", &blk))
		return;
	if (argc == 4) {
		ino = parse_ulong(argv[3], argv[0], "inode", &err);
		if (err)
			return;
	}

	retval = ext2fs_get_mem(current_fs->blocksize, &buf);
	if (retval) {
		com_err(argv[0], retval, "while allocating block buffer");
		return;
	}
	retval = io_channel_read_blk64(current_fs->io, blk, 1, buf);
	if (retval) {
		com_err(argv[0], retval, "while reading block %llu",
			(unsigned long long) blk);
		goto out;
	}

	eh = (struct ext3_extent_header *) buf;
	if (!strcmp(argv[1], "--magic")) {
		eh->eh_magic = ext2fs_cpu_to_le16(0xdead);
	} else if (!strcmp(argv[1], "--entries")) {
		eh->eh_entries = 0;
		if (ino)
			ext2fs_extent_block_csum_set(current_fs, ino, eh);
	} else if (!strcmp(argv[1], "--max")) {
		eh->eh_max = ext2fs_cpu_to_le16(999);
	} else if (!strcmp(argv[1], "--csum")) {
		__u32 *csum = (__u32 *) (buf + current_fs->blocksize -
					 sizeof(__u32));
		*csum ^= 0xffffffff;
	} else {
		com_err(argv[0], 0, "Usage: %s %s", argv[0], usage);
		goto out;
	}

	retval = io_channel_write_blk64(current_fs->io, blk, 1, buf);
	if (retval) {
		com_err(argv[0], retval, "while writing block %llu",
			(unsigned long long) blk);
		goto out;
	}
	printf("Corrupted extent block %llu (%s)\n",
	       (unsigned long long) blk, argv[1]);
out:
	ext2fs_free_mem(&buf);
}

void do_fix_extents_csum(int argc, ss_argv_t argv,
			 int sci_idx EXT2FS_ATTR((unused)),
			 void *infop EXT2FS_ATTR((unused)))
{
	const char		*usage = "[--with-inode] <ino>";
	struct ext2_inode	inode;
	struct ext2_inode	*inode_ptr = NULL;
	ext2_ino_t		ino;
	int			with_inode = 0;
	int			err;
	errcode_t		retval;

	if (common_args_process(argc, argv, 2, 3, argv[0], usage, CHECK_FS_RW))
		return;

	if (argc == 3) {
		if (!strcmp(argv[1], "--with-inode")) {
			with_inode = 1;
			argc--;
			argv++;
		} else {
			com_err(argv[0], 0, "Usage: fix_extents_csum %s",
				usage);
			return;
		}
	}

	ino = parse_ulong(argv[1], argv[0], "inode", &err);
	if (err)
		return;

	if (with_inode) {
		retval = ext2fs_read_inode(current_fs, ino, &inode);
		if (retval) {
			com_err("fix_extents_csum", retval,
				"while reading inode %u", ino);
			return;
		}
		inode_ptr = &inode;
	}

	retval = ext2fs_fix_extents_checksums(current_fs, ino, inode_ptr);
	if (retval) {
		com_err("fix_extents_csum", retval,
			"while fixing extent checksums for inode %u", ino);
		return;
	}
	printf("ext2fs_fix_extents_checksums(ino %u%s): OK\n",
	       ino, with_inode ? ", with_inode" : "");
}

void do_test_extent_apis(int argc EXT2FS_ATTR((unused)),
			 ss_argv_t argv EXT2FS_ATTR((unused)),
			 int sci_idx EXT2FS_ATTR((unused)),
			 void *infop EXT2FS_ATTR((unused)))
{
	unsigned char			buf[1024];
	struct ext3_extent_header	*eh;
	struct ext3_extent		raw_ex;
	struct ext2fs_extent		decoded, extent;
	struct ext2_inode		inode;
	struct ext2_extent_info		info;
	ext2_extent_handle_t		handle = NULL;
	errcode_t			retval;
	int				i, save_flags;
	size_t				depth1, depth2;

	if (check_fs_open("test_extent_apis"))
		return;

	printf("--- Testing ext2fs_extent_header_verify ---\n");
	memset(buf, 0, sizeof(buf));
	eh = (struct ext3_extent_header *) buf;

	/* Valid inode extent header (size 60, max 4) */
	eh->eh_magic = ext2fs_cpu_to_le16(EXT3_EXT_MAGIC);
	eh->eh_entries = ext2fs_cpu_to_le16(2);
	eh->eh_max = ext2fs_cpu_to_le16(4);
	eh->eh_depth = 0;
	retval = ext2fs_extent_header_verify(eh, 60);
	printf("verify valid inode header: %s\n",
	       retval ? error_message(retval) : "OK");

	/* Bad magic */
	eh->eh_magic = ext2fs_cpu_to_le16(0x1234);
	retval = ext2fs_extent_header_verify(eh, 60);
	printf("verify bad magic: %s\n", error_message(retval));
	eh->eh_magic = ext2fs_cpu_to_le16(EXT3_EXT_MAGIC);

	/* eh_entries > eh_max */
	eh->eh_entries = ext2fs_cpu_to_le16(5);
	retval = ext2fs_extent_header_verify(eh, 60);
	printf("verify entries > max: %s\n", error_message(retval));
	eh->eh_entries = ext2fs_cpu_to_le16(2);

	/* eh_max > computed max (5 > 4 for size 60) */
	eh->eh_max = ext2fs_cpu_to_le16(5);
	retval = ext2fs_extent_header_verify(eh, 60);
	printf("verify eh_max too large: %s\n", error_message(retval));

	/* 1024-byte block: computed eh_max = (1024 - 12) / 12 = 84 */
	/* eh_max = 82 (eh_max - 2, allowed for checksum tail) */
	eh->eh_max = ext2fs_cpu_to_le16(82);
	eh->eh_depth = ext2fs_cpu_to_le16(1);
	retval = ext2fs_extent_header_verify(eh, 1024);
	printf("verify 1024B block eh_max=82 depth=1: %s\n",
	       retval ? error_message(retval) : "OK");

	/* eh_max = 81 (< eh_max - 2, invalid) */
	eh->eh_max = ext2fs_cpu_to_le16(81);
	retval = ext2fs_extent_header_verify(eh, 1024);
	printf("verify 1024B block eh_max=81 (too small): %s\n",
	       error_message(retval));

	printf("--- Testing ext2fs_decode_extent ---\n");
	memset(&raw_ex, 0, sizeof(raw_ex));
	retval = ext2fs_decode_extent(&decoded, &raw_ex,
				      sizeof(raw_ex) - 1);
	printf("decode_extent bad len: %s\n", error_message(retval));

	/* Initialized extent with 48-bit physical block */
	raw_ex.ee_block = ext2fs_cpu_to_le32(42);
	raw_ex.ee_len = ext2fs_cpu_to_le16(EXT_INIT_MAX_LEN);
	raw_ex.ee_start = ext2fs_cpu_to_le32(0x12345678);
	raw_ex.ee_start_hi = ext2fs_cpu_to_le16(0x0005);
	retval = ext2fs_decode_extent(&decoded, &raw_ex, sizeof(raw_ex));
	printf("decode_extent init: status=%s lblk=%llu len=%u "
	       "pblk=0x%llx flags=0x%x\n",
	       retval ? error_message(retval) : "OK",
	       (unsigned long long) decoded.e_lblk, decoded.e_len,
	       (unsigned long long) decoded.e_pblk, decoded.e_flags);

	/* Uninitialized extent with max uninit length (32767) */
	raw_ex.ee_block = ext2fs_cpu_to_le32(1000);
	raw_ex.ee_len = ext2fs_cpu_to_le16(EXT_INIT_MAX_LEN +
					   EXT_UNINIT_MAX_LEN);
	raw_ex.ee_start = ext2fs_cpu_to_le32(2000);
	raw_ex.ee_start_hi = 0;
	retval = ext2fs_decode_extent(&decoded, &raw_ex, sizeof(raw_ex));
	printf("decode_extent uninit: status=%s lblk=%llu len=%u "
	       "pblk=%llu flags=0x%x\n",
	       retval ? error_message(retval) : "OK",
	       (unsigned long long) decoded.e_lblk, decoded.e_len,
	       (unsigned long long) decoded.e_pblk, decoded.e_flags);

	printf("--- Testing ext2fs_extent_open2 (in-memory inode) & "
	       "RO_FILSYS / magic checks ---\n");
	ext2fs_extent_free(NULL);

	memset(&inode, 0, sizeof(inode));
	inode.i_mode = LINUX_S_IFREG | 0644;
	ext2fs_inode_size_set(current_fs, &inode, 40960);

	/* Open with explicit struct ext2_inode pointer */
	retval = ext2fs_extent_open2(current_fs, 14, &inode, &handle);
	printf("extent_open2(ino=14, &inode): %s, i_flags has EXTENTS_FL=%d\n",
	       retval ? error_message(retval) : "OK",
	       (inode.i_flags & EXT4_EXTENTS_FL) ? 1 : 0);

	depth1 = ext2fs_max_extent_depth(handle);
	depth2 = ext2fs_max_extent_depth(handle);
	printf("ext2fs_max_extent_depth: %zu (cached: %zu)\n", depth1, depth2);

	/* Insert an extent into the in-memory inode */
	memset(&extent, 0, sizeof(extent));
	extent.e_lblk = 0;
	extent.e_len = 10;
	extent.e_pblk = 100;
	extent.e_flags = EXT2_EXTENT_FLAGS_LEAF;
	retval = ext2fs_extent_insert(handle, 0, &extent);
	printf("insert into in-memory inode: %s\n",
	       retval ? error_message(retval) : "OK");

	/* Unsupported op in ext2fs_extent_get */
	retval = ext2fs_extent_get(handle, 0x00ff, &extent);
	printf("extent_get unsupported op: %s\n", error_message(retval));

	/* Read-only filesystem checks on all mutating extent APIs */
	save_flags = current_fs->flags;
	current_fs->flags &= ~EXT2_FLAG_RW;

	retval = ext2fs_extent_insert(handle, 0, &extent);
	printf("RO extent_insert: %s\n", error_message(retval));
	retval = ext2fs_extent_replace(handle, 0, &extent);
	printf("RO extent_replace: %s\n", error_message(retval));
	retval = ext2fs_extent_delete(handle, 0);
	printf("RO extent_delete: %s\n", error_message(retval));
	retval = ext2fs_extent_node_split(handle);
	printf("RO extent_node_split: %s\n", error_message(retval));
	retval = ext2fs_extent_set_bmap(handle, 0, 200, 0);
	printf("RO extent_set_bmap: %s\n", error_message(retval));
	retval = ext2fs_extent_fix_parents(handle);
	printf("RO extent_fix_parents: %s\n", error_message(retval));

	current_fs->flags = save_flags;

	/* Bad handle magic check */
	retval = ext2fs_extent_get((ext2_extent_handle_t) &inode,
				   EXT2_EXTENT_CURRENT, &extent);
	printf("bad magic extent_get: %s\n", error_message(retval));
	retval = ext2fs_extent_get_info((ext2_extent_handle_t) &inode, &info);
	printf("bad magic extent_get_info: %s\n", error_message(retval));

	ext2fs_extent_free(handle);

	printf("--- Testing full leaf block EOF split vs mid-tree split ---\n");
	memset(&inode, 0, sizeof(inode));
	inode.i_mode = LINUX_S_IFREG | 0644;
	ext2fs_inode_size_set(current_fs, &inode, 1024 * 1024);
	retval = ext2fs_write_inode(current_fs, 20, &inode);
	if (retval) {
		com_err("test_extent_apis", retval, "writing inode 20");
		return;
	}
	retval = ext2fs_extent_open(current_fs, 20, &handle);
	if (retval) {
		com_err("test_extent_apis", retval, "opening inode 20");
		return;
	}
	/*
	 * Insert 84 discontiguous extents at EOF:
	 * - After 4 extents, the 5th splits root into a level-1 leaf (max 84).
	 * - Once level-1 leaf reaches 84 entries, the 85th insert at EOF
	 *   triggers splitting_at_eof() -> tocopy = 1 (leaves 83 in leaf 0,
	 *   moves 1 to leaf 1, and inserts the 85th into leaf 1).
	 */
	for (i = 0; i < 85; i++) {
		memset(&extent, 0, sizeof(extent));
		extent.e_lblk = (blk64_t) i * 2 + 2;
		extent.e_len = 1;
		extent.e_pblk = (blk64_t) 500 + i * 2;
		extent.e_flags = EXT2_EXTENT_FLAGS_LEAF;
		retval = ext2fs_extent_insert(handle, EXT2_EXTENT_INSERT_AFTER,
					      &extent);
		if (retval) {
			com_err("test_extent_apis", retval,
				"inserting extent %d", i);
			ext2fs_extent_free(handle);
			return;
		}
	}
	/*
	 * Leaf 0 now has 83 entries (lblk 2..166). Navigate to lblk 2
	 * (in leaf 0, which is not at EOF since leaf 1 follows it),
	 * insert 1 entry to make leaf 0 full (84/84), then insert 1 more
	 * entry in leaf 0 to trigger a mid-tree 50/50 split (tocopy = 42).
	 */
	retval = ext2fs_extent_goto(handle, 2);
	if (retval == 0) {
		extent.e_lblk = 1;
		extent.e_len = 1;
		extent.e_pblk = 499;
		extent.e_flags = EXT2_EXTENT_FLAGS_LEAF;
		retval = ext2fs_extent_insert(handle, 0, &extent);
	}
	if (retval == 0) {
		extent.e_lblk = 0;
		extent.e_len = 1;
		extent.e_pblk = 498;
		extent.e_flags = EXT2_EXTENT_FLAGS_LEAF;
		retval = ext2fs_extent_insert(handle, 0, &extent);
	}
	if (retval == 0)
		retval = ext2fs_extent_get_info(handle, &info);
	printf("after mid-tree split: status=%s, leaf0 entries=%d/%d, "
	       "level=%d/%d\n",
	       retval ? error_message(retval) : "OK",
	       info.num_entries, info.max_entries,
	       info.curr_level, info.max_depth);

	ext2fs_extent_free(handle);
}
