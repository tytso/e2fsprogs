/*
 * e2image.c --- Program which writes an image file backing up
 * critical metadata for the filesystem.
 *
 * Copyright 2000, 2001 by Theodore Ts'o.
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 */

#ifndef _LARGEFILE_SOURCE
#define _LARGEFILE_SOURCE
#endif
#ifndef _LARGEFILE64_SOURCE
#define _LARGEFILE64_SOURCE
#endif

#include "config.h"
#include <fcntl.h>
#include <grp.h>
#ifdef HAVE_GETOPT_H
#include <getopt.h>
#else
extern char *optarg;
extern int optind;
#endif
#include <pwd.h>
#include <stdio.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <assert.h>
#include <signal.h>

#include "ext2fs/ext2_fs.h"
#include "ext2fs/ext2fs.h"
#include "ext2fs/ext2fsP.h"
#include "et/com_err.h"
#include "uuid/uuid.h"
#include "e2p/e2p.h"
#include "ext2fs/e2image.h"
#include "ext2fs/qcow2.h"

#include "support/nls-enable.h"
#include "support/plausible.h"
#include "support/quotaio.h"
#include "../version.h"

#define QCOW_OFLAG_COPIED     (1ULL << 63)
#define NO_BLK ((blk64_t) -1)

/* Image types */
#define E2IMAGE_RAW	1
#define E2IMAGE_QCOW2	2

/* Image flags */
#define E2IMAGE_INSTALL_FLAG	1
#define E2IMAGE_SCRAMBLE_FLAG	2
#define E2IMAGE_IS_QCOW2_FLAG	4
#define E2IMAGE_CHECK_ZERO_FLAG	8

static const char * program_name = "e2image";
static char * device_name = NULL;
static char all_data;
static char output_is_blk;
static char nop_flag;
/* writing to blk device: don't skip zeroed blocks */
static blk64_t source_offset, dest_offset;
static char move_mode;
static char show_progress;
static char *check_buf;
static int skipped_blocks;

static blk64_t align_offset(blk64_t offset, unsigned int n)
{
	return (offset + n - 1) & ~((blk64_t) n - 1);
}

static int get_bits_from_size(size_t size)
{
	int res = 0;

	if (size == 0)
		return -1;

	while (size != 1) {
		/* Not a power of two */
		if (size & 1)
			return -1;

		size >>= 1;
		res++;
	}
	return res;
}

static void usage(void)
{
	fprintf(stderr, _("Usage: %s [ -r|-Q ] [ -f ] [ -b superblock ] [ -B blocksize ] "
			  "device image-file\n"),
		program_name);
	fprintf(stderr, _("       %s -I device image-file\n"), program_name);
	fprintf(stderr, _("       %s -ra [ -cfnp ] [ -o src_offset ] "
			  "[ -O dest_offset ] src_fs [ dest_fs ]\n"),
		program_name);
	exit (1);
}

static ext2_loff_t seek_relative(int fd, int offset)
{
	ext2_loff_t ret = ext2fs_llseek(fd, offset, SEEK_CUR);
	if (ret < 0) {
		perror("seek_relative");
		exit(1);
	}
	return ret;
}

static ext2_loff_t seek_set(int fd, ext2_loff_t offset)
{
	ext2_loff_t ret = ext2fs_llseek(fd, offset, SEEK_SET);
	if (ret < 0) {
		perror("seek_set");
		exit(1);
	}
	return ret;
}

/*
 * Returns true if the block we are about to write is identical to
 * what is already on the disk.
 */
static int check_block(int fd, void *buf, void *cbuf, int blocksize)
{
	char *cp = cbuf;
	int count = blocksize, ret;

	if (cbuf == NULL)
		return 0;

	while (count > 0) {
		ret = read(fd, cp, count);
		if (ret < 0) {
			perror("check_block");
			exit(1);
		}
		count -= ret;
		cp += ret;
	}
	ret = memcmp(buf, cbuf, blocksize);
	seek_relative(fd, -blocksize);
	return (ret == 0) ? 1 : 0;
}

static void generic_write(int fd, void *buf, int blocksize, blk64_t block)
{
	int count, free_buf = 0;
	errcode_t err;

	if (!blocksize)
		return;

	if (!buf) {
		free_buf = 1;
		err = ext2fs_get_arrayzero(1, blocksize, &buf);
		if (err) {
			com_err(program_name, err, "%s",
				_("while allocating buffer"));
			exit(1);
		}
	}
	if (nop_flag) {
		printf(_("Writing block %llu\n"), (unsigned long long) block);
		if (fd != 1)
			seek_relative(fd, blocksize);
		goto free_and_return;
	}
	count = write(fd, buf, blocksize);
	if (count != blocksize) {
		if (count == -1)
			err = errno;
		else
			err = 0;

		if (block)
			com_err(program_name, err,
				_("error writing block %llu"),
				(unsigned long long) block);
		else
			com_err(program_name, err, "%s",
				_("error in generic_write()"));

		exit(1);
	}
free_and_return:
	if (free_buf)
		ext2fs_free_mem(&buf);
}

static void write_header(int fd, void *hdr, int hdr_size, int wrt_size)
{
	char *header_buf;
	int ret;

	/* Sanity check */
	if (hdr_size > wrt_size) {
		fprintf(stderr, "%s",
			_("Error: header size is bigger than wrt_size\n"));
	}

	ret = ext2fs_get_mem(wrt_size, &header_buf);
	if (ret) {
		fputs(_("Couldn't allocate header buffer\n"), stderr);
		exit(1);
	}

	seek_set(fd, 0);
	memset(header_buf, 0, wrt_size);

	if (hdr)
		memcpy(header_buf, hdr, hdr_size);

	generic_write(fd, header_buf, wrt_size, NO_BLK);

	ext2fs_free_mem(&header_buf);
}

static void write_image_file(ext2_filsys fs, int fd)
{
	struct ext2_image_hdr	hdr;
	struct stat		st;
	errcode_t		retval;
	time_t			now = time(0);

	write_header(fd, NULL, sizeof(struct ext2_image_hdr), fs->blocksize);
	memset(&hdr, 0, sizeof(struct ext2_image_hdr));

	hdr.offset_super = ext2fs_cpu_to_le32(seek_relative(fd, 0));
	retval = ext2fs_image_super_write(fs, fd, 0);
	if (retval) {
		com_err(program_name, retval, "%s",
			_("while writing superblock"));
		exit(1);
	}

	hdr.offset_inode = ext2fs_cpu_to_le32(seek_relative(fd, 0));
	retval = ext2fs_image_inode_write(fs, fd,
				  (fd != 1) ? IMAGER_FLAG_SPARSEWRITE : 0);
	if (retval) {
		com_err(program_name, retval, "%s",
			_("while writing inode table"));
		exit(1);
	}

	hdr.offset_blockmap = ext2fs_cpu_to_le32(seek_relative(fd, 0));
	retval = ext2fs_image_bitmap_write(fs, fd, 0);
	if (retval) {
		com_err(program_name, retval, "%s",
			_("while writing block bitmap"));
		exit(1);
	}

	hdr.offset_inodemap = ext2fs_cpu_to_le32(seek_relative(fd, 0));
	retval = ext2fs_image_bitmap_write(fs, fd, IMAGER_FLAG_INODEMAP);
	if (retval) {
		com_err(program_name, retval, "%s",
			_("while writing inode bitmap"));
		exit(1);
	}

	hdr.magic_number = ext2fs_cpu_to_le32(EXT2_ET_MAGIC_E2IMAGE);
	strcpy(hdr.magic_descriptor, "Ext2 Image 1.0");
	gethostname(hdr.fs_hostname, sizeof(hdr.fs_hostname));
	strncpy(hdr.fs_device_name, device_name, sizeof(hdr.fs_device_name)-1);
	hdr.fs_device_name[sizeof(hdr.fs_device_name) - 1] = 0;
	hdr.fs_blocksize = ext2fs_cpu_to_le32(fs->blocksize);

	if (stat(device_name, &st) == 0)
		hdr.fs_device = ext2fs_cpu_to_le32(st.st_rdev);

	if (fstat(fd, &st) == 0) {
		hdr.image_device = ext2fs_cpu_to_le32(st.st_dev);
		hdr.image_inode = ext2fs_cpu_to_le32(st.st_ino);
	}
	memcpy(hdr.fs_uuid, fs->super->s_uuid, sizeof(hdr.fs_uuid));

	hdr.image_time_lo = ext2fs_cpu_to_le32(now & 0xFFFFFFFF);
#if (SIZEOF_TIME_T > 4)
	hdr.image_time_hi = ext2fs_cpu_to_le32(now >> 32);
#else
	hdr.image_time_hi = 0;
#endif
	write_header(fd, &hdr, sizeof(struct ext2_image_hdr), fs->blocksize);
}

/*
 * These set of functions are used to write a RAW image file.
 */
static ext2fs_block_bitmap meta_block_map;
static ext2fs_block_bitmap scramble_block_map;	/* Directory blocks to be scrambled */
static blk64_t meta_blocks_count;

struct process_block_struct {
	ext2_ino_t	ino;
	int		is_dir;
};

/*
 * These subroutines short circuits ext2fs_get_blocks and
 * ext2fs_check_directory; we use them since we already have the inode
 * structure, so there's no point in letting the ext2fs library read
 * the inode again.
 */
static ext2_ino_t stashed_ino = 0;
static struct ext2_inode *stashed_inode;

static errcode_t meta_get_blocks(ext2_filsys fs EXT2FS_ATTR((unused)),
				 ext2_ino_t ino,
				 blk_t *blocks)
{
	int	i;

	if ((ino != stashed_ino) || !stashed_inode)
		return EXT2_ET_CALLBACK_NOTHANDLED;

	for (i=0; i < EXT2_N_BLOCKS; i++)
		blocks[i] = stashed_inode->i_block[i];
	return 0;
}

static errcode_t meta_check_directory(ext2_filsys fs EXT2FS_ATTR((unused)),
				      ext2_ino_t ino)
{
	if ((ino != stashed_ino) || !stashed_inode)
		return EXT2_ET_CALLBACK_NOTHANDLED;

	if (!LINUX_S_ISDIR(stashed_inode->i_mode))
		return EXT2_ET_NO_DIRECTORY;
	return 0;
}

static errcode_t meta_read_inode(ext2_filsys fs EXT2FS_ATTR((unused)),
				 ext2_ino_t ino,
				 struct ext2_inode *inode)
{
	if ((ino != stashed_ino) || !stashed_inode)
		return EXT2_ET_CALLBACK_NOTHANDLED;
	*inode = *stashed_inode;
	return 0;
}

static void use_inode_shortcuts(ext2_filsys fs, int use_shortcuts)
{
	if (use_shortcuts) {
		fs->get_blocks = meta_get_blocks;
		fs->check_directory = meta_check_directory;
		fs->read_inode = meta_read_inode;
		stashed_ino = 0;
	} else {
		fs->get_blocks = 0;
		fs->check_directory = 0;
		fs->read_inode = 0;
	}
}

static int process_dir_block(ext2_filsys fs EXT2FS_ATTR((unused)),
			     blk64_t *block_nr,
			     e2_blkcnt_t blockcnt EXT2FS_ATTR((unused)),
			     blk64_t ref_block EXT2FS_ATTR((unused)),
			     int ref_offset EXT2FS_ATTR((unused)),
			     void *priv_data EXT2FS_ATTR((unused)))
{
	struct process_block_struct *p;

	p = (struct process_block_struct *) priv_data;

	ext2fs_mark_block_bitmap2(meta_block_map, *block_nr);
	meta_blocks_count++;
	if (scramble_block_map && p->is_dir && blockcnt >= 0)
		ext2fs_mark_block_bitmap2(scramble_block_map, *block_nr);
	return 0;
}

static int process_file_block(ext2_filsys fs EXT2FS_ATTR((unused)),
			      blk64_t *block_nr,
			      e2_blkcnt_t blockcnt,
			      blk64_t ref_block EXT2FS_ATTR((unused)),
			      int ref_offset EXT2FS_ATTR((unused)),
			      void *priv_data EXT2FS_ATTR((unused)))
{
	if (blockcnt < 0 || all_data) {
		ext2fs_mark_block_bitmap2(meta_block_map, *block_nr);
		meta_blocks_count++;
	}
	return 0;
}

static void mark_table_blocks(ext2_filsys fs)
{
	blk64_t	first_block, b;
	unsigned int	i,j;

	first_block = fs->super->s_first_data_block;
	/*
	 * Mark primary superblock
	 */
	ext2fs_mark_block_bitmap2(meta_block_map, first_block);
	meta_blocks_count++;

	/*
	 * Mark the primary superblock descriptors
	 */
	for (j = 0; j < fs->desc_blocks; j++) {
		ext2fs_mark_block_bitmap2(meta_block_map,
			 ext2fs_descriptor_block_loc2(fs, first_block, j));
	}
	meta_blocks_count += fs->desc_blocks;

	/*
	 *  Mark MMP block
	 */
	if (fs->super->s_feature_incompat & EXT4_FEATURE_INCOMPAT_MMP) {
		ext2fs_mark_block_bitmap2(meta_block_map, fs->super->s_mmp_block);
		meta_blocks_count++;
	}

	for (i = 0; i < fs->group_desc_count; i++) {
		/*
		 * Mark the blocks used for the inode table
		 */
		if ((output_is_blk ||
		     !ext2fs_bg_flags_test(fs, i, EXT2_BG_INODE_UNINIT)) &&
		    ext2fs_inode_table_loc(fs, i)) {
			unsigned int end = (unsigned) fs->inode_blocks_per_group;
			/* skip unused blocks */
			if (!output_is_blk && ext2fs_has_group_desc_csum(fs))
				end -= (ext2fs_bg_itable_unused(fs, i) /
					EXT2_INODES_PER_BLOCK(fs->super));
			for (j = 0, b = ext2fs_inode_table_loc(fs, i);
			     j < end;
			     j++, b++) {
				ext2fs_mark_block_bitmap2(meta_block_map, b);
				meta_blocks_count++;
			}
		}

		/*
		 * Mark block used for the block bitmap
		 */
		if (!ext2fs_bg_flags_test(fs, i, EXT2_BG_BLOCK_UNINIT) &&
		    ext2fs_block_bitmap_loc(fs, i)) {
			ext2fs_mark_block_bitmap2(meta_block_map,
				     ext2fs_block_bitmap_loc(fs, i));
			meta_blocks_count++;
		}

		/*
		 * Mark block used for the inode bitmap
		 */
		if (!ext2fs_bg_flags_test(fs, i, EXT2_BG_INODE_UNINIT) &&
		    ext2fs_inode_bitmap_loc(fs, i)) {
			ext2fs_mark_block_bitmap2(meta_block_map,
				 ext2fs_inode_bitmap_loc(fs, i));
			meta_blocks_count++;
		}
	}
}

/*
 * This function returns 1 if the specified block is all zeros
 */
static int check_zero_block(char *buf, int blocksize)
{
	char	*cp = buf;
	int	left = blocksize;

	if (output_is_blk)
		return 0;
	while (left > 0) {
		if (*cp++)
			return 0;
		left--;
	}
	return 1;
}

static int name_id[256];

#define EXT4_MAX_REC_LEN		((1<<16)-1)

static void scramble_dir_block(ext2_filsys fs, blk64_t blk, char *buf)
{
	char *p, *end, *cp;
	struct ext2_dir_entry_2 *dirent;
	unsigned int rec_len;
	int id, len;

	end = buf + fs->blocksize;
	for (p = buf; p < end-8; p += rec_len) {
		dirent = (struct ext2_dir_entry_2 *) p;
		rec_len = dirent->rec_len;
#ifdef WORDS_BIGENDIAN
		rec_len = ext2fs_swab16(rec_len);
#endif
		if (rec_len == EXT4_MAX_REC_LEN || rec_len == 0)
			rec_len = fs->blocksize;
		else 
			rec_len = (rec_len & 65532) | ((rec_len & 3) << 16);
#if 0
		printf("rec_len = %d, name_len = %d\n", rec_len, dirent->name_len);
#endif
		if (rec_len < 8 || (rec_len % 4) ||
		    (p+rec_len > end)) {
			printf(_("Corrupt directory block %llu: "
				 "bad rec_len (%d)\n"),
			       (unsigned long long) blk, rec_len);
			rec_len = end - p;
			(void) ext2fs_set_rec_len(fs, rec_len,
					(struct ext2_dir_entry *) dirent);
#ifdef WORDS_BIGENDIAN
			dirent->rec_len = ext2fs_swab16(dirent->rec_len);
#endif
			continue;
		}
		if (dirent->name_len + 8U > rec_len) {
			printf(_("Corrupt directory block %llu: "
				 "bad name_len (%d)\n"),
			       (unsigned long long) blk, dirent->name_len);
			dirent->name_len = rec_len - 8;
			continue;
		}
		cp = p+8;
		len = rec_len - dirent->name_len - 8;
		if (len > 0)
			memset(cp+dirent->name_len, 0, len);
		if (dirent->name_len==1 && cp[0] == '.')
			continue;
		if (dirent->name_len==2 && cp[0] == '.' && cp[1] == '.')
			continue;

		memset(cp, 'A', dirent->name_len);
		len = dirent->name_len;
		id = name_id[len]++;
		while ((len > 0) && (id > 0)) {
			*cp += id % 26;
			id = id / 26;
			cp++;
			len--;
		}
	}
}

static char got_sigint;

static void sigint_handler(int unused EXT2FS_ATTR((unused)))
{
	got_sigint = 1;
	signal (SIGINT, SIG_DFL);
}

#define calc_percent(a, b) ((int) ((100.0 * (((float) (a)) / \
					     ((float) (b)))) + 0.5))
#define calc_rate(t, b, d) (((float)(t) / ((float)(1024 * 1024) / (b))) / (d))

static int print_progress(blk64_t num, blk64_t total)
{
	return fprintf(stderr, _("%llu / %llu blocks (%d%%)"),
		       (unsigned long long) num,
		       (unsigned long long) total,
		       calc_percent(num, total));
}

static void output_meta_data_blocks(ext2_filsys fs, int fd, int flags)
{
	errcode_t	retval;
	blk64_t		blk;
	char		*buf, *zero_buf;
	int		sparse = 0;
	blk64_t		start = 0;
	blk64_t		distance = 0;
	blk64_t		end = ext2fs_blocks_count(fs->super);
	time_t		last_update = 0;
	time_t		start_time = 0;
	blk64_t		total_written = 0;
	int		bscount = 0;

	retval = ext2fs_get_mem(fs->blocksize, &buf);
	if (retval) {
		com_err(program_name, retval, "%s",
			_("while allocating buffer"));
		exit(1);
	}
	retval = ext2fs_get_memzero(fs->blocksize, &zero_buf);
	if (retval) {
		com_err(program_name, retval, "%s",
			_("while allocating buffer"));
		exit(1);
	}
	if (show_progress) {
		fprintf(stderr, "%s", _("Copying "));
		bscount = print_progress(total_written, meta_blocks_count);
		fflush(stderr);
		last_update = time(NULL);
		start_time = time(NULL);
	}
	/* when doing an in place move to the right, you can't start
	   at the beginning or you will overwrite data, so instead
	   divide the fs up into distance size chunks and write them
	   in reverse. */
	if (move_mode && dest_offset > source_offset) {
		distance = (dest_offset - source_offset) / fs->blocksize;
		if (distance < ext2fs_blocks_count(fs->super))
			start = ext2fs_blocks_count(fs->super) - distance;
	}
	if (move_mode)
		signal (SIGINT, sigint_handler);
more_blocks:
	if (distance)
		seek_set(fd, (start * fs->blocksize) + dest_offset);
	for (blk = start; blk < end; blk++) {
		if (got_sigint) {
			if (distance) {
				/* moving to the right */
				if (distance >= ext2fs_blocks_count(fs->super)||
				    start == ext2fs_blocks_count(fs->super) -
						distance)
					kill(getpid(), SIGINT);
			} else {
				/* moving to the left */
				if (blk < (source_offset - dest_offset) /
				    fs->blocksize)
					kill(getpid(), SIGINT);
			}
			if (show_progress)
				fputc('\r', stderr);
			fprintf(stderr, "%s",
				_("Stopping now will destroy the filesystem, "
				 "interrupt again if you are sure\n"));
			if (show_progress) {
				fprintf(stderr, "%s", _("Copying "));
				bscount = print_progress(total_written,
							 meta_blocks_count);
				fflush(stderr);
			}

			got_sigint = 0;
		}
		if (show_progress && last_update != time(NULL)) {
			time_t duration;
			last_update = time(NULL);
			while (bscount--)
				fputc('\b', stderr);
			bscount = print_progress(total_written,
						 meta_blocks_count);
			duration = time(NULL) - start_time;
			if (duration > 5 && total_written) {
				time_t est = (duration * meta_blocks_count /
					      total_written) - duration;
				char buff[30];
				strftime(buff, 30, "%T", gmtime(&est));
				bscount +=
					fprintf(stderr,
						_(" %s remaining at %.2f MB/s"),
						buff, calc_rate(total_written,
								fs->blocksize,
								duration));
			}
			fflush (stderr);
		}
		if ((blk >= fs->super->s_first_data_block) &&
		    ext2fs_test_block_bitmap2(meta_block_map, blk)) {
			retval = io_channel_read_blk64(fs->io, blk, 1, buf);
			if (retval) {
				com_err(program_name, retval,
					_("error reading block %llu"),
					(unsigned long long) blk);
			}
			total_written++;
			if (scramble_block_map &&
			    ext2fs_test_block_bitmap2(scramble_block_map, blk))
				scramble_dir_block(fs, blk, buf);
			if ((flags & E2IMAGE_CHECK_ZERO_FLAG) &&
			    check_zero_block(buf, fs->blocksize))
				goto sparse_write;
			if (sparse)
				seek_relative(fd, sparse);
			sparse = 0;
			if (check_block(fd, buf, check_buf, fs->blocksize)) {
				seek_relative(fd, fs->blocksize);
				skipped_blocks++;
			} else
				generic_write(fd, buf, fs->blocksize, blk);
		} else {
		sparse_write:
			if (fd == 1) {
				if (!nop_flag)
					generic_write(fd, zero_buf,
						      fs->blocksize, blk);
				continue;
			}
			sparse += fs->blocksize;
			if (sparse > 1024*1024) {
				seek_relative(fd, 1024*1024);
				sparse -= 1024*1024;
			}
		}
	}
	if (distance && start) {
		if (start < distance) {
			end = start;
			start = 0;
		} else {
			end -= distance;
			start -= distance;
			if (end < distance) {
				/* past overlap, do rest in one go */
				end = start;
				start = 0;
			}
		}
		sparse = 0;
		goto more_blocks;
	}
	signal (SIGINT, SIG_DFL);
	if (show_progress) {
		time_t duration = time(NULL) - start_time;
		char buff[30];
		fputc('\r', stderr);
		strftime(buff, 30, "%T", gmtime(&duration));
		fprintf(stderr, _("Copied %llu / %llu blocks (%d%%) in %s "),
			(unsigned long long) total_written,
			(unsigned long long) meta_blocks_count,
			calc_percent(total_written, meta_blocks_count), buff);
		if (duration)
			fprintf(stderr, _("at %.2f MB/s"),
				calc_rate(total_written, fs->blocksize, duration));
		fputs("       \n", stderr);
	}
#ifdef HAVE_FTRUNCATE64
	if (sparse) {
		ext2_loff_t offset;
		if (distance)
			offset = seek_set(fd,
					  fs->blocksize * ext2fs_blocks_count(fs->super) + dest_offset);
		else
			offset = seek_relative(fd, sparse);

		if (ftruncate64(fd, offset) < 0) {
			seek_relative(fd, -1);
			generic_write(fd, zero_buf, 1, NO_BLK);
		}
	}
#else
	if (sparse && !distance) {
		seek_relative(fd, sparse-1);
		generic_write(fd, zero_buf, 1, NO_BLK);
	}
#endif
	ext2fs_free_mem(&zero_buf);
	ext2fs_free_mem(&buf);
}

static void init_l1_table(struct ext2_qcow2_image *image)
{
	__u64 *l1_table;
	errcode_t ret;

	ret = ext2fs_get_arrayzero(image->l1_size, sizeof(__u64), &l1_table);
	if (ret) {
		com_err(program_name, ret, "%s",
			_("while allocating l1 table"));
		exit(1);
	}

	image->l1_table = l1_table;
}

static void init_l2_cache(struct ext2_qcow2_image *image)
{
	unsigned int count, i;
	struct ext2_qcow2_l2_cache *cache;
	struct ext2_qcow2_l2_table *table;
	errcode_t ret;

	ret = ext2fs_get_arrayzero(1, sizeof(struct ext2_qcow2_l2_cache),
				   &cache);
	if (ret)
		goto alloc_err;

	count = (image->l1_size > L2_CACHE_PREALLOC) ? L2_CACHE_PREALLOC :
		 image->l1_size;

	cache->count = count;
	cache->free = count;
	cache->next_offset = image->l2_offset;

	for (i = 0; i < count; i++) {
		ret = ext2fs_get_arrayzero(1,
				sizeof(struct ext2_qcow2_l2_table), &table);
		if (ret)
			goto alloc_err;

		ret = ext2fs_get_arrayzero(image->l2_size,
						   sizeof(__u64), &table->data);
		if (ret)
			goto alloc_err;

		table->next = cache->free_head;
		cache->free_head = table;
	}

	image->l2_cache = cache;
	return;

alloc_err:
	com_err(program_name, ret, "%s", _("while allocating l2 cache"));
	exit(1);
}

static void put_l2_cache(struct ext2_qcow2_image *image)
{
	struct ext2_qcow2_l2_cache *cache = image->l2_cache;
	struct ext2_qcow2_l2_table *tmp, *table;

	if (!cache)
		return;

	table = cache->free_head;
	cache->free_head = NULL;
again:
	while (table) {
		tmp = table;
		table = table->next;
		ext2fs_free_mem(&tmp->data);
		ext2fs_free_mem(&tmp);
	}

	if (cache->free != cache->count) {
		fprintf(stderr, "%s", _("Warning: There are still tables in "
					"the cache while putting the cache, "
					"data will be lost so the image may "
					"not be valid.\n"));
		table = cache->used_head;
		cache->used_head = NULL;
		goto again;
	}

	ext2fs_free_mem(&cache);
}

static int init_refcount(struct ext2_qcow2_image *img, blk64_t table_offset)
{
	struct	ext2_qcow2_refcount	*ref;
	blk64_t table_clusters;
	errcode_t ret;

	ref = &(img->refcount);

	/*
	 * One refcount block addresses 2048 clusters, one refcount table
	 * addresses cluster/sizeof(__u64) refcount blocks, and we need
	 * to address meta_blocks_count clusters + qcow2 metadata clusters
	 * in the worst case.
	 */
	table_clusters = meta_blocks_count + (table_offset >>
					      img->cluster_bits);
	table_clusters >>= (img->cluster_bits + 6 - 1);
	table_clusters = (table_clusters == 0) ? 1 : table_clusters;

	ref->refcount_table_offset = table_offset;
	ref->refcount_table_clusters = table_clusters;
	ref->refcount_table_index = 0;
	ref->refcount_block_index = 0;

	/* Allocate refcount table */
	ret = ext2fs_get_arrayzero(ref->refcount_table_clusters,
				   img->cluster_size, &ref->refcount_table);
	if (ret)
		return ret;

	/* Allocate refcount block */
	ret = ext2fs_get_arrayzero(1, img->cluster_size, &ref->refcount_block);
	if (ret)
		ext2fs_free_mem(&ref->refcount_table);

	return ret;
}

static errcode_t initialize_qcow2_image(int fd, ext2_filsys fs,
					struct ext2_qcow2_image *image)
{
	struct ext2_qcow2_hdr *header;
	blk64_t total_size, offset;
	int shift, l2_bits, header_size, l1_size, ret;
	int cluster_bits = get_bits_from_size(fs->blocksize);
	struct ext2_super_block *sb = fs->super;

	/* Sbould never happen, but just in case... */
	if (cluster_bits < 0)
		return EXT2_FILSYS_CORRUPTED;

	/* Allocate header */
	ret = ext2fs_get_memzero(sizeof(struct ext2_qcow2_hdr), &header);
	if (ret)
		return ret;

	total_size = ext2fs_blocks_count(sb) << cluster_bits;
	image->cluster_size = fs->blocksize;
	image->l2_size = 1 << (cluster_bits - 3);
	image->cluster_bits = cluster_bits;
	image->fd = fd;

	header->magic = ext2fs_cpu_to_be32(QCOW_MAGIC);
	header->version = ext2fs_cpu_to_be32(QCOW_VERSION);
	header->size = ext2fs_cpu_to_be64(total_size);
	header->cluster_bits = ext2fs_cpu_to_be32(cluster_bits);

	header_size = (sizeof(struct ext2_qcow2_hdr) + 7) & ~7;
	offset = align_offset(header_size, image->cluster_size);

	header->l1_table_offset = ext2fs_cpu_to_be64(offset);
	image->l1_offset = offset;

	l2_bits = cluster_bits - 3;
	shift = cluster_bits + l2_bits;
	l1_size = ((total_size + (1LL << shift) - 1) >> shift);
	header->l1_size = ext2fs_cpu_to_be32(l1_size);
	image->l1_size = l1_size;

	/* Make space for L1 table */
	offset += align_offset(l1_size * sizeof(blk64_t), image->cluster_size);

	/* Initialize refcounting */
	ret = init_refcount(image, offset);
	if (ret) {
		ext2fs_free_mem(&header);
		return ret;
	}
	header->refcount_table_offset = ext2fs_cpu_to_be64(offset);
	header->refcount_table_clusters =
		ext2fs_cpu_to_be32(image->refcount.refcount_table_clusters);
	offset += image->cluster_size;
	offset += (blk64_t) image->refcount.refcount_table_clusters <<
		image->cluster_bits;

	/* Make space for L2 tables */
	image->l2_offset = offset;
	offset += image->cluster_size;

	/* Make space for first refcount block */
	image->refcount.refcount_block_offset = offset;

	image->hdr = header;
	/* Initialize l1 and l2 tables */
	init_l1_table(image);
	init_l2_cache(image);

	return 0;
}

static void free_qcow2_image(struct ext2_qcow2_image *img)
{
	if (!img)
		return;

	if (img->hdr)
		ext2fs_free_mem(&img->hdr);

	if (img->l1_table)
		ext2fs_free_mem(&img->l1_table);

	if (img->refcount.refcount_table)
		ext2fs_free_mem(&img->refcount.refcount_table);
	if (img->refcount.refcount_block)
		ext2fs_free_mem(&img->refcount.refcount_block);

	put_l2_cache(img);

	ext2fs_free_mem(&img);
}

/**
 * Put table from used list (used_head) into free list (free_head).
 * l2_table is used to return pointer to the next used table (used_head).
 */
static void put_used_table(struct ext2_qcow2_image *img,
			  struct ext2_qcow2_l2_table **l2_table)
{
	struct ext2_qcow2_l2_cache *cache = img->l2_cache;
	struct ext2_qcow2_l2_table *table;

	table = cache->used_head;
	cache->used_head = table->next;

	assert(table);
	if (!table->next)
		cache->used_tail = NULL;

	/* Clean the table for case we will need to use it again */
	memset(table->data, 0, img->cluster_size);
	table->next = cache->free_head;
	cache->free_head = table;

	cache->free++;

	*l2_table = cache->used_head;
}

static void flush_l2_cache(struct ext2_qcow2_image *image)
{
	blk64_t seek = 0;
	ext2_loff_t offset;
	struct ext2_qcow2_l2_cache *cache = image->l2_cache;
	struct ext2_qcow2_l2_table *table = cache->used_head;
	int fd = image->fd;

	/* Store current position */
	offset = seek_relative(fd, 0);

	assert(table);
	while (cache->free < cache->count) {
		if (seek != table->offset) {
			seek_set(fd, table->offset);
			seek = table->offset;
		}

		generic_write(fd, (char *)table->data, image->cluster_size,
			      NO_BLK);
		put_used_table(image, &table);
		seek += image->cluster_size;
	}

	/* Restore previous position */
	seek_set(fd, offset);
}

/**
 * Get first free table (from free_head) and put it into tail of used list
 * (to used_tail).
 * l2_table is used to return pointer to moved table.
 * Returns 1 if the cache is full, 0 otherwise.
 */
static void get_free_table(struct ext2_qcow2_image *image,
			  struct ext2_qcow2_l2_table **l2_table)
{
	struct ext2_qcow2_l2_table *table;
	struct ext2_qcow2_l2_cache *cache = image->l2_cache;

	if (0 == cache->free)
		flush_l2_cache(image);

	table = cache->free_head;
	assert(table);
	cache->free_head = table->next;

	if (cache->used_tail)
		cache->used_tail->next = table;
	else
		/* First item in the used list */
		cache->used_head = table;

	cache->used_tail = table;
	cache->free--;

	*l2_table = table;
}

static int add_l2_item(struct ext2_qcow2_image *img, blk64_t blk,
		       blk64_t data, blk64_t next)
{
	struct ext2_qcow2_l2_cache *cache = img->l2_cache;
	struct ext2_qcow2_l2_table *table = cache->used_tail;
	blk64_t l1_index = blk / img->l2_size;
	blk64_t l2_index = blk & (img->l2_size - 1);
	int ret = 0;

	/*
	 * Need to create new table if it does not exist,
	 * or if it is full
	 */
	if (!table || (table->l1_index != l1_index)) {
		get_free_table(img, &table);
		table->l1_index = l1_index;
		table->offset = cache->next_offset;
		cache->next_offset = next;
		img->l1_table[l1_index] =
			ext2fs_cpu_to_be64(table->offset | QCOW_OFLAG_COPIED);
		ret++;
	}

	table->data[l2_index] = ext2fs_cpu_to_be64(data | QCOW_OFLAG_COPIED);
	return ret;
}

static int update_refcount(int fd, struct ext2_qcow2_image *img,
			   blk64_t offset, blk64_t rfblk_pos)
{
	struct	ext2_qcow2_refcount	*ref;
	__u32	table_index;
	int ret = 0;

	ref = &(img->refcount);
	table_index = offset >> (2 * img->cluster_bits - 1);

	/*
	 * Need to create new refcount block when the offset addresses
	 * another item in the refcount table
	 */
	if (table_index != ref->refcount_table_index) {

		seek_set(fd, ref->refcount_block_offset);

		generic_write(fd, (char *)ref->refcount_block,
			      img->cluster_size, NO_BLK);
		memset(ref->refcount_block, 0, img->cluster_size);

		ref->refcount_table[ref->refcount_table_index] =
			ext2fs_cpu_to_be64(ref->refcount_block_offset);
		ref->refcount_block_offset = rfblk_pos;
		ref->refcount_block_index = 0;
		ref->refcount_table_index = table_index;
		ret++;
	}

	/*
	 * We are relying on the fact that we are creating the qcow2
	 * image sequentially, hence we will always allocate refcount
	 * block items sequentially.
	 */
	ref->refcount_block[ref->refcount_block_index] = ext2fs_cpu_to_be16(1);
	ref->refcount_block_index++;
	return ret;
}

static int sync_refcount(int fd, struct ext2_qcow2_image *img)
{
	struct	ext2_qcow2_refcount	*ref;

	ref = &(img->refcount);

	ref->refcount_table[ref->refcount_table_index] =
		ext2fs_cpu_to_be64(ref->refcount_block_offset);
	seek_set(fd, ref->refcount_table_offset);
	generic_write(fd, (char *)ref->refcount_table,
		ref->refcount_table_clusters << img->cluster_bits, NO_BLK);

	seek_set(fd, ref->refcount_block_offset);
	generic_write(fd, (char *)ref->refcount_block, img->cluster_size,
		      NO_BLK);
	return 0;
}

static void output_qcow2_meta_data_blocks(ext2_filsys fs, int fd)
{
	errcode_t		retval;
	blk64_t			blk, offset, size, end;
	char			*buf;
	struct ext2_qcow2_image	*img;
	unsigned int		header_size;

	/* allocate  struct ext2_qcow2_image */
	retval = ext2fs_get_mem(sizeof(struct ext2_qcow2_image), &img);
	if (retval) {
		com_err(program_name, retval, "%s",
			_("while allocating ext2_qcow2_image"));
		exit(1);
	}

	retval = initialize_qcow2_image(fd, fs, img);
	if (retval) {
		com_err(program_name, retval, "%s",
			_("while initializing ext2_qcow2_image"));
		exit(1);
	}
	header_size = align_offset(sizeof(struct ext2_qcow2_hdr),
				   img->cluster_size);
	write_header(fd, img->hdr, sizeof(struct ext2_qcow2_hdr), header_size);

	/* Refcount all qcow2 related metadata up to refcount_block_offset */
	end = img->refcount.refcount_block_offset;
	seek_set(fd, end);
	blk = end + img->cluster_size;
	for (offset = 0; offset <= end; offset += img->cluster_size) {
		if (update_refcount(fd, img, offset, blk)) {
			blk += img->cluster_size;
			/*
			 * If we create new refcount block, we need to refcount
			 * it as well.
			 */
			end += img->cluster_size;
		}
	}
	seek_set(fd, offset);

	retval = ext2fs_get_mem(fs->blocksize, &buf);
	if (retval) {
		com_err(program_name, retval, "%s",
			_("while allocating buffer"));
		exit(1);
	}
	/* Write qcow2 data blocks */
	for (blk = 0; blk < ext2fs_blocks_count(fs->super); blk++) {
		if ((blk >= fs->super->s_first_data_block) &&
		    ext2fs_test_block_bitmap2(meta_block_map, blk)) {
			retval = io_channel_read_blk64(fs->io, blk, 1, buf);
			if (retval) {
				com_err(program_name, retval,
					_("error reading block %llu"),
					(unsigned long long) blk);
				continue;
			}
			if (scramble_block_map &&
			    ext2fs_test_block_bitmap2(scramble_block_map, blk))
				scramble_dir_block(fs, blk, buf);
			if (check_zero_block(buf, fs->blocksize))
				continue;

			if (update_refcount(fd, img, offset, offset)) {
				/* Make space for another refcount block */
				offset += img->cluster_size;
				seek_set(fd, offset);
				/*
				 * We have created the new refcount block, this
				 * means that we need to refcount it as well.
				 * So the previous update_refcount refcounted
				 * the block itself and now we are going to
				 * create refcount for data. New refcount
				 * block should not be created!
				 */
				if (update_refcount(fd, img, offset, offset)) {
					fprintf(stderr, "%s",
						_("Programming error: multiple "
						  "sequential refcount blocks "
						  "created!\n"));
					exit(1);
				}
			}

			generic_write(fd, buf, fs->blocksize, blk);

			if (add_l2_item(img, blk, offset,
					offset + img->cluster_size)) {
				offset += img->cluster_size;
				if (update_refcount(fd, img, offset,
					offset + img->cluster_size)) {
					offset += img->cluster_size;
					if (update_refcount(fd, img, offset,
							    offset)) {
						fprintf(stderr, "%s",
			_("Programming error: multiple sequential refcount "
			  "blocks created!\n"));
						exit(1);
					}
				}
				offset += img->cluster_size;
				seek_set(fd, offset);
				continue;
			}

			offset += img->cluster_size;
		}
	}
	(void) update_refcount(fd, img, offset, offset);
	flush_l2_cache(img);
	sync_refcount(fd, img);

	/* Write l1_table*/
	seek_set(fd, img->l1_offset);
	size = img->l1_size * sizeof(__u64);
	generic_write(fd, (char *)img->l1_table, size, NO_BLK);

	ext2fs_free_mem(&buf);
	free_qcow2_image(img);
}

static void write_raw_image_file(ext2_filsys fs, int fd, int type, int flags,
				 blk64_t superblock)
{
	struct process_block_struct	pb;
	struct ext2_inode		inode;
	ext2_inode_scan			scan;
	ext2_ino_t			ino;
	errcode_t			retval;
	char *				block_buf;

	meta_blocks_count = 0;
	retval = ext2fs_allocate_block_bitmap(fs, _("in-use block map"),
					      &meta_block_map);
	if (retval) {
		com_err(program_name, retval, "%s",
			_("while allocating block bitmap"));
		exit(1);
	}

	if (flags & E2IMAGE_SCRAMBLE_FLAG) {
		retval = ext2fs_allocate_block_bitmap(fs, "scramble block map",
						      &scramble_block_map);
		if (retval) {
			com_err(program_name, retval, "%s",
				_("while allocating scramble block bitmap"));
			exit(1);
		}
	}

	if (superblock) {
		unsigned int j;

		ext2fs_mark_block_bitmap2(meta_block_map, superblock);
		meta_blocks_count++;

		/*
		 * Mark the backup superblock descriptors
		 */
		for (j = 0; j < fs->desc_blocks; j++) {
			ext2fs_mark_block_bitmap2(meta_block_map,
			ext2fs_descriptor_block_loc2(fs, superblock, j));
		}
		meta_blocks_count += fs->desc_blocks;
	}

	mark_table_blocks(fs);
	if (show_progress)
		fprintf(stderr, "%s", _("Scanning inodes...\n"));

	retval = ext2fs_open_inode_scan(fs, 0, &scan);
	if (retval) {
		com_err(program_name, retval, "%s",
			_("while opening inode scan"));
		exit(1);
	}

	retval = ext2fs_get_mem(fs->blocksize * 3, &block_buf);
	if (retval) {
		com_err(program_name, 0, "%s",
			_("Can't allocate block buffer"));
		exit(1);
	}

	use_inode_shortcuts(fs, 1);
	stashed_inode = &inode;
	while (1) {
		retval = ext2fs_get_next_inode(scan, &ino, &inode);
		if (retval == EXT2_ET_BAD_BLOCK_IN_INODE_TABLE)
			continue;
		if (retval) {
			com_err(program_name, retval, "%s",
				_("while getting next inode"));
			exit(1);
		}
		if (ino == 0)
			break;
		if (!inode.i_links_count)
			continue;
		if (ext2fs_file_acl_block(fs, &inode)) {
			ext2fs_mark_block_bitmap2(meta_block_map,
					ext2fs_file_acl_block(fs, &inode));
			meta_blocks_count++;
		}
		if (!ext2fs_inode_has_valid_blocks2(fs, &inode))
			continue;

		stashed_ino = ino;
		pb.ino = ino;
		pb.is_dir = LINUX_S_ISDIR(inode.i_mode);
		if (LINUX_S_ISDIR(inode.i_mode) ||
		    LINUX_S_ISLNK(inode.i_mode) ||
		    ino == fs->super->s_journal_inum ||
		    ino == quota_type2inum(USRQUOTA, fs->super) ||
		    ino == quota_type2inum(GRPQUOTA, fs->super) ||
		    ino == quota_type2inum(PRJQUOTA, fs->super) ||
		    ino == fs->super->s_orphan_file_inum) {
			retval = ext2fs_block_iterate3(fs, ino,
					BLOCK_FLAG_READ_ONLY, block_buf,
					process_dir_block, &pb);
			if (retval) {
				com_err(program_name, retval,
					_("while iterating over inode %u"),
					ino);
				exit(1);
			}
		} else {
			if ((inode.i_flags & EXT4_EXTENTS_FL) ||
			    inode.i_block[EXT2_IND_BLOCK] ||
			    inode.i_block[EXT2_DIND_BLOCK] ||
			    inode.i_block[EXT2_TIND_BLOCK] || all_data) {
				retval = ext2fs_block_iterate3(fs,
				       ino, BLOCK_FLAG_READ_ONLY, block_buf,
				       process_file_block, &pb);
				if (retval) {
					com_err(program_name, retval,
					_("while iterating over inode %u"), ino);
					exit(1);
				}
			}
		}
	}
	use_inode_shortcuts(fs, 0);

	if (type & E2IMAGE_QCOW2)
		output_qcow2_meta_data_blocks(fs, fd);
	else
		output_meta_data_blocks(fs, fd, flags);

	ext2fs_free_mem(&block_buf);
	ext2fs_close_inode_scan(scan);
	ext2fs_free_block_bitmap(meta_block_map);
	if (type & E2IMAGE_SCRAMBLE_FLAG)
		ext2fs_free_block_bitmap(scramble_block_map);
}

static void install_image(char *device, char *image_fn, int type)
{
	errcode_t retval;
	ext2_filsys fs;
	int open_flag = EXT2_FLAG_IMAGE_FILE | EXT2_FLAG_64BITS |
			EXT2_FLAG_IGNORE_CSUM_ERRORS;
	int fd = 0;
	io_manager	io_ptr;
	io_channel	io;

	if (type) {
		com_err(program_name, 0, "%s",
			_("Raw and qcow2 images cannot be installed"));
		exit(1);
	}

#ifdef CONFIG_TESTIO_DEBUG
	if (getenv("TEST_IO_FLAGS") || getenv("TEST_IO_BLOCK")) {
		io_ptr = test_io_manager;
		test_io_backing_manager = unix_io_manager;
	} else
#endif
		io_ptr = unix_io_manager;

	retval = ext2fs_open (image_fn, open_flag, 0, 0,
			      io_ptr, &fs);
        if (retval) {
		com_err(program_name, retval, _("while trying to open %s"),
			image_fn);
		exit(1);
	}

	retval = ext2fs_read_bitmaps (fs);
	if (retval) {
		com_err(program_name, retval, "%s", _("error reading bitmaps"));
		exit(1);
	}

	fd = ext2fs_open_file(image_fn, O_RDONLY, 0);
	if (fd < 0) {
		perror(image_fn);
		exit(1);
	}

	retval = io_ptr->open(device, IO_FLAG_RW, &io);
	if (retval) {
		com_err(device, 0, "%s", _("while opening device file"));
		exit(1);
	}

	ext2fs_rewrite_to_io(fs, io);

	seek_set(fd, ext2fs_le32_to_cpu(fs->image_header->offset_inode));

	retval = ext2fs_image_inode_read(fs, fd, 0);
	if (retval) {
		com_err(image_fn, 0, "%s",
			_("while restoring the image table"));
		exit(1);
	}

	close(fd);
	ext2fs_close_free(&fs);
}

static struct ext2_qcow2_hdr *check_qcow2_image(int *fd, char *name)
{

	*fd = ext2fs_open_file(name, O_RDONLY, 0600);
	if (*fd < 0)
		return NULL;

	return qcow2_read_header(*fd);
}

int main (int argc, char ** argv)
{
	int c;
	errcode_t retval;
	ext2_filsys fs;
	char *image_fn, offset_opt[64];
	struct ext2_qcow2_hdr *header = NULL;
	int open_flag = EXT2_FLAG_64BITS | EXT2_FLAG_THREADS |
		EXT2_FLAG_IGNORE_CSUM_ERRORS;
	int img_type = 0;
	int flags = 0;
	int mount_flags = 0;
	int qcow2_fd = 0;
	int fd = 0;
	int ret = 0;
	int ignore_rw_mount = 0;
	int check = 0;
	struct stat st;
	blk64_t superblock = 0;
	int blocksize = 0;

#ifdef ENABLE_NLS
	setlocale(LC_MESSAGES, "");
	setlocale(LC_CTYPE, "");
	bindtextdomain(NLS_CAT_NAME, LOCALEDIR);
	textdomain(NLS_CAT_NAME);
	set_com_err_gettext(gettext);
#endif
	fprintf (stderr, "e2image %s (%s)\n", E2FSPROGS_VERSION,
		 E2FSPROGS_DATE);
	if (argc && *argv)
		program_name = *argv;
	else
		usage();
	add_error_table(&et_ext2_error_table);
	while ((c = getopt(argc, argv, "b:B:nrsIQafo:O:pc")) != EOF)
		switch (c) {
		case 'b':
			superblock = strtoull(optarg, NULL, 0);
			break;
		case 'B':
			blocksize = strtoul(optarg, NULL, 0);
			break;
		case 'I':
			flags |= E2IMAGE_INSTALL_FLAG;
			break;
		case 'Q':
			if (img_type)
				usage();
			img_type |= E2IMAGE_QCOW2;
			break;
		case 'r':
			if (img_type)
				usage();
			img_type |= E2IMAGE_RAW;
			break;
		case 's':
			flags |= E2IMAGE_SCRAMBLE_FLAG;
			break;
		case 'a':
			all_data = 1;
			break;
		case 'f':
			ignore_rw_mount = 1;
			break;
		case 'n':
			nop_flag = 1;
			break;
		case 'o':
			source_offset = strtoull(optarg, NULL, 0);
			break;
		case 'O':
			dest_offset = strtoull(optarg, NULL, 0);
			break;
		case 'p':
			show_progress = 1;
			break;
		case 'c':
			check = 1;
			break;
		default:
			usage();
		}
	if (optind == argc - 1 &&
	    (source_offset || dest_offset))
		    move_mode = 1;
	else if (optind != argc - 2 )
		usage();

	if (all_data && !img_type) {
		com_err(program_name, 0, "%s", _("-a option can only be used "
						 "with raw or QCOW2 images."));
		exit(1);
	}
	if (superblock && !img_type) {
		com_err(program_name, 0, "%s", _("-b option can only be used "
						 "with raw or QCOW2 images."));
		exit(1);
	}
	if ((source_offset || dest_offset) && img_type != E2IMAGE_RAW) {
		com_err(program_name, 0, "%s",
			_("Offsets are only allowed with raw images."));
		exit(1);
	}
	if (move_mode && img_type != E2IMAGE_RAW) {
		com_err(program_name, 0, "%s",
			_("Move mode is only allowed with raw images."));
		exit(1);
	}
	if (move_mode && !all_data) {
		com_err(program_name, 0, "%s",
			_("Move mode requires all data mode."));
		exit(1);
	}
	device_name = argv[optind];
	if (move_mode)
		image_fn = device_name;
	else image_fn = argv[optind+1];

	retval = ext2fs_check_if_mounted(device_name, &mount_flags);
	if (retval) {
		com_err(program_name, retval, "%s", _("checking if mounted"));
		exit(1);
	}

	if (img_type && !ignore_rw_mount &&
	    (mount_flags & EXT2_MF_MOUNTED) &&
	   !(mount_flags & EXT2_MF_READONLY)) {
		fprintf(stderr, "%s", _("\nRunning e2image on a R/W mounted "
			"filesystem can result in an\n"
			"inconsistent image which will not be useful "
			"for debugging purposes.\n"
			"Use -f option if you really want to do that.\n"));
		exit(1);
	}

	if (flags & E2IMAGE_INSTALL_FLAG) {
		install_image(device_name, image_fn, img_type);
		exit (0);
	}

	if (img_type & E2IMAGE_RAW) {
		header = check_qcow2_image(&qcow2_fd, device_name);
		if (header) {
			flags |= E2IMAGE_IS_QCOW2_FLAG;
			goto skip_device;
		}
	}
	sprintf(offset_opt, "offset=%llu", (unsigned long long) source_offset);
	retval = ext2fs_open2(device_name, offset_opt, open_flag,
			      superblock, blocksize, unix_io_manager, &fs);
        if (retval) {
		com_err (program_name, retval, _("while trying to open %s"),
			 device_name);
		fputs(_("Couldn't find valid filesystem superblock.\n"), stdout);
		if (retval == EXT2_ET_BAD_MAGIC)
			check_plausibility(device_name, CHECK_FS_EXIST, NULL);
		exit(1);
	}

skip_device:
	if (strcmp(image_fn, "-") == 0)
		fd = 1;
	else {
		int o_flags = O_CREAT|O_RDWR;

		if (img_type != E2IMAGE_RAW)
			o_flags |= O_TRUNC;
		if (access(image_fn, F_OK) != 0)
			flags |= E2IMAGE_CHECK_ZERO_FLAG;
		fd = ext2fs_open_file(image_fn, o_flags, 0600);
		if (fd < 0) {
			com_err(program_name, errno,
				_("while trying to open %s"), image_fn);
			exit(1);
		}
	}
	if (dest_offset)
		seek_set(fd, dest_offset);

	if ((img_type & E2IMAGE_QCOW2) && (fd == 1)) {
		com_err(program_name, 0, "%s",
			_("QCOW2 image can not be written to the stdout!\n"));
		exit(1);
	}
	if (fd != 1) {
		if (fstat(fd, &st)) {
			com_err(program_name, 0, "%s",
				_("Can not stat output\n"));
			exit(1);
		}
		if (ext2fsP_is_disk_device(st.st_mode))
			output_is_blk = 1;
	}
	if (flags & E2IMAGE_IS_QCOW2_FLAG) {
		ret = qcow2_write_raw_image(qcow2_fd, fd, header);
		if (ret) {
			if (ret == -QCOW_COMPRESSED)
				fprintf(stderr, _("Image (%s) is compressed\n"),
					image_fn);
			else if (ret == -QCOW_ENCRYPTED)
				fprintf(stderr, _("Image (%s) is encrypted\n"),
					image_fn);
			else if (ret == -QCOW_CORRUPTED)
				fprintf(stderr, _("Image (%s) is corrupted\n"),
					image_fn);
			else
				com_err(program_name, ret,
					_("while trying to convert qcow2 image"
					  " (%s) into raw image (%s)"),
					image_fn, device_name);
			ret = 1;
		}
		goto out;
	}

	if (check) {
		if (img_type != E2IMAGE_RAW) {
			fprintf(stderr, "%s", _("The -c option only supported "
						"in raw mode\n"));
			exit(1);
		}
		if (fd == 1) {
			fprintf(stderr, "%s", _("The -c option not supported "
						"when writing to stdout\n"));
			exit(1);
		}
		retval = ext2fs_get_mem(fs->blocksize, &check_buf);
		if (retval) {
			com_err(program_name, retval, "%s",
				_("while allocating check_buf"));
			exit(1);
		}
	}
	if (show_progress && (img_type != E2IMAGE_RAW)) {
		fprintf(stderr, "%s",
			_("The -p option only supported in raw mode\n"));
		exit(1);
	}
	if (img_type)
		write_raw_image_file(fs, fd, img_type, flags, superblock);
	else
		write_image_file(fs, fd);

	ext2fs_close_free(&fs);
	if (check)
		printf(_("%d blocks already contained the data to be copied\n"),
		       skipped_blocks);

out:
	if (header)
		free(header);
	if (qcow2_fd)
		close(qcow2_fd);
	remove_error_table(&et_ext2_error_table);
	return ret;
}
