/*
 * probe.c - identify a block device by its contents, and return a dev
 *           struct with the details
 *
 * Copyright (C) 1999 by Andries Brouwer
 * Copyright (C) 1999, 2000 by Theodore Ts'o
 * Copyright (C) 2001 by Andreas Dilger
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 * %End-Header%
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#ifdef HAVE_SYS_MKDEV_H
#include <sys/mkdev.h>
#endif
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#include "blkidP.h"
#include "uuid/uuid.h"
#include "probe.h"

/* #define DEBUG_PROBE */
#ifdef DEBUG_PROBE
#define DBG(x)	x
#else
#define DBG(x)
#endif

/*
 * Do the required things for instantiating a new device.  This is called if
 * there is nor a probe handler for a filesystem type, and is also called by
 * the filesystem-specific types to do common initialization tasks.
 *
 * The devname, dev_p, and id fields are required.  The buf is
 * a buffer to return superblock data in.
 */
static int probe_default(int fd, blkid_dev *dev_p, const char *devname,
			 struct blkid_magic *id, unsigned char *buf,
			 blkid_loff_t size)
{
	blkid_loff_t offset;
	blkid_dev dev;
	struct stat st;
	int ret;

	if (!devname || !dev_p || !id || !buf || fd < 0)
		return -BLKID_ERR_PARAM;

	if (fstat(fd, &st) < 0 || !S_ISBLK(st.st_mode))
		return -BLKID_ERR_DEV;

	offset = (blkid_loff_t)id->bim_kboff << 10;
	if (id->bim_kboff < 0)
		offset += (size & ~((blkid_loff_t)(id->bim_align - 1)));

	if (blkid_llseek(fd, offset, 0) < 0 ||
	    read(fd, buf, id->bim_kbsize << 10) != id->bim_kbsize << 10)
		return -BLKID_ERR_IO;

	/* Revalidate magic for blkid_validate_devname */
	if (memcmp(id->bim_magic, buf + id->bim_sboff, id->bim_len))
		return -BLKID_ERR_PARAM;

	dev = blkid_new_dev();
	if (!dev)
		return -BLKID_ERR_MEM;

	dev->bid_name = string_copy(devname);
	if (!dev->bid_name) {
		ret = -BLKID_ERR_MEM;
		goto exit_dev;
	}

	/* Don't set this until there is no chance of error */
	*dev_p = dev;
	dev->bid_devno = st.st_rdev;
	dev->bid_devsize = size;
	dev->bid_time = time(0);
	dev->bid_flags |= BLKID_BID_FL_VERIFIED;

	if (id->bim_type)
		blkid_create_tag(dev, "TYPE", id->bim_type,
				 strlen(id->bim_type));

	DBG(printf("%s: devno 0x%04Lx, type %s\n", devname,
		   st.st_rdev, id->bim_type));

	return 0;
exit_dev:
	blkid_free_dev(dev);
	return ret;
}

static int probe_ext2(int fd, blkid_dev *dev_p, const char *devname,
		      struct blkid_magic *id, unsigned char *buf,
		      blkid_loff_t size)
{
	blkid_dev dev;
	struct ext2_super_block *es;
	int ret;

	if ((ret = probe_default(fd, &dev, devname, id, buf, size)) < 0)
		return ret;

	es = (struct ext2_super_block *)buf;

	DBG(printf("size = %Ld, ext2_sb.compat = %08X:%08X:%08X\n", size,
		   blkid_le32(es->s_feature_compat),
		   blkid_le32(es->s_feature_incompat),
		   blkid_le32(es->s_feature_ro_compat)));

	/* Make sure we don't keep re-probing as ext2 for a journaled fs */
	if (!strcmp(id->bim_type, "ext2") &&
	    (blkid_le32(es->s_feature_compat) &
	     EXT3_FEATURE_COMPAT_HAS_JOURNAL ||
	     blkid_le32(es->s_feature_incompat) &
	     EXT3_FEATURE_INCOMPAT_JOURNAL_DEV)) {
		blkid_free_dev(dev);
		return -BLKID_ERR_PARAM;
	}

	/* Don't set this until there is no chance of error */
	*dev_p = dev;

	dev->bid_size = (blkid_loff_t)blkid_le32(es->s_blocks_count) <<
		(blkid_le32(es->s_log_block_size) + 10);

	if (strlen(es->s_volume_name)) {
		blkid_create_tag(dev, "LABEL", es->s_volume_name,
				 sizeof(es->s_volume_name));
	}

	if (!uuid_is_null(es->s_uuid)) {
		char uuid[37];
		uuid_unparse(es->s_uuid, uuid);
		blkid_create_tag(dev, "UUID", uuid, sizeof(uuid));
	}

	return 0;
}

static int probe_jbd(int fd, blkid_dev *dev_p, const char *devname,
		     struct blkid_magic *id, unsigned char *buf,
		     blkid_loff_t size)
{
	blkid_dev dev;
	struct ext2_super_block *es;
	int ret;

	if ((ret = probe_ext2(fd, &dev, devname, id, buf, size)) < 0)
		return ret;

	es = (struct ext2_super_block *)buf;

	if (!(blkid_le32(es->s_feature_incompat) &
	      EXT3_FEATURE_INCOMPAT_JOURNAL_DEV)) {
		blkid_free_dev(dev);
		return -BLKID_ERR_PARAM;
	}

	/* Don't set this until there is no chance of error */
	*dev_p = dev;
	return 0;
}

static int probe_ext3(int fd, blkid_dev *dev_p, const char *devname,
		     struct blkid_magic *id, unsigned char *buf,
		     blkid_loff_t size)
{
	blkid_dev dev;
	struct ext2_super_block *es;
	int ret;

	if ((ret = probe_ext2(fd, &dev, devname, id, buf, size)) < 0)
		return ret;

	es = (struct ext2_super_block *)buf;

	if (!(blkid_le32(es->s_feature_compat) &
	      EXT3_FEATURE_COMPAT_HAS_JOURNAL)) {
		blkid_free_dev(dev);
		*dev_p = NULL;
		return -BLKID_ERR_PARAM;
	}
	/* Don't set this until there is no chance of error */
	*dev_p = dev;

	if (!(blkid_le32(es->s_feature_incompat) &
	      EXT3_FEATURE_INCOMPAT_RECOVER)) {
		blkid_create_tag(dev, "TYPE", "ext2", 4);
		dev->bid_flags |= BLKID_BID_FL_MTYPE;
	}

	return 0;
}

static int probe_vfat(int fd, blkid_dev *dev_p, const char *devname,
		      struct blkid_magic *id, unsigned char *buf,
		      blkid_loff_t size)
{
	blkid_dev dev;
	struct vfat_super_block *vs;
	char serno[10];
	blkid_loff_t sectors;
	int cluster_size;
	int ret;

	if ((ret = probe_default(fd, &dev, devname, id, buf, size)) < 0)
		return ret;

	vs = (struct vfat_super_block *)buf;

	/* Don't set this until there is no chance of error */
	*dev_p = dev;

	sectors = ((vs->vs_sectors[1] << 8) | vs->vs_sectors[0]);
	if (sectors == 0)
		sectors = vs->vs_total_sect;
	cluster_size = ((vs->vs_sector_size[1] << 8) | vs->vs_sector_size[0]);
	dev->bid_size = sectors * cluster_size;
	DBG(printf("%lld %d byte sectors\n", sectors, cluster_size));

	if (strncmp(vs->vs_label, "NO NAME", 7)) {
		char *end = vs->vs_label + sizeof(vs->vs_label) - 1;

		while (*end == ' ' && end >= vs->vs_label)
			--end;
		if (end >= vs->vs_label)
			blkid_create_tag(dev, "LABEL", vs->vs_label,
					 end - vs->vs_label + 1);
	}

	/* We can't just print them as %04X, because they are unaligned */
	sprintf(serno, "%02X%02X-%02X%02X", vs->vs_serno[3], vs->vs_serno[2],
		vs->vs_serno[1], vs->vs_serno[0]);
	blkid_create_tag(dev, "UUID", serno, sizeof(serno));

	return 0;
}

static int probe_msdos(int fd, blkid_dev *dev_p, const char *devname,
		       struct blkid_magic *id, unsigned char *buf,
		       blkid_loff_t size)
{
	blkid_dev dev;
	struct msdos_super_block *ms;
	char serno[10];
	int cluster_size;
	blkid_loff_t sectors;
	int ret;

	if ((ret = probe_default(fd, &dev, devname, id, buf, size)) < 0)
		return ret;

	ms = (struct msdos_super_block *)buf;

	/* Don't set this until there is no chance of error */
	*dev_p = dev;

	sectors = ((ms->ms_sectors[1] << 8) | ms->ms_sectors[0]);
	if (sectors == 0)
		sectors = ms->ms_total_sect;
	cluster_size = ((ms->ms_sector_size[1] << 8) | ms->ms_sector_size[0]);
	dev->bid_size = sectors * cluster_size;
	DBG(printf("%Ld %d byte sectors\n", sectors, cluster_size));

	if (strncmp(ms->ms_label, "NO NAME", 7)) {
		char *end = ms->ms_label + sizeof(ms->ms_label) - 1;

		while (*end == ' ' && end >= ms->ms_label)
			--end;
		if (end >= ms->ms_label)
			blkid_create_tag(dev, "LABEL", ms->ms_label,
					 end - ms->ms_label + 1);
	}

	/* We can't just print them as %04X, because they are unaligned */
	sprintf(serno, "%02X%02X-%02X%02X", ms->ms_serno[3], ms->ms_serno[2],
		ms->ms_serno[1], ms->ms_serno[0]);
	blkid_create_tag(dev, "UUID", serno, sizeof(serno));

	return 0;
}

static int probe_xfs(int fd, blkid_dev *dev_p, const char *devname,
		     struct blkid_magic *id, unsigned char *buf,
		     blkid_loff_t size)
{
	blkid_dev dev;
	struct xfs_super_block *xs;
	int ret;

	if ((ret = probe_default(fd, &dev, devname, id, buf, size)) < 0)
		return ret;

	xs = (struct xfs_super_block *)buf;

	/* Don't set this until there is no chance of error */
	*dev_p = dev;
	/* If the filesystem size is larger than the device, this is bad */
	dev->bid_size = blkid_be64(xs->xs_dblocks) *
		blkid_be32(xs->xs_blocksize);

	if (strlen(xs->xs_fname))
		blkid_create_tag(dev, "LABEL", xs->xs_fname,
				 sizeof(xs->xs_fname));

	if (!uuid_is_null(xs->xs_uuid)) {
		char uuid[37];
		uuid_unparse(xs->xs_uuid, uuid);
		blkid_create_tag(dev, "UUID", uuid, sizeof(uuid));
	}
	return 0;
}

static int probe_reiserfs(int fd, blkid_dev *dev_p, const char *devname,
			  struct blkid_magic *id, unsigned char *buf,
			  blkid_loff_t size)
{
	blkid_dev dev;
	struct reiserfs_super_block *rs;
	unsigned int blocksize;
	int ret;

	if ((ret = probe_default(fd, &dev, devname, id, buf, size)) < 0)
		return ret;

	rs = (struct reiserfs_super_block *)buf;

	blocksize = blkid_le16(rs->rs_blocksize);

	/* If the superblock is inside the journal, we have the wrong one */
	if (id->bim_kboff/(blocksize>>10) > blkid_le32(rs->rs_journal_block)) {
		blkid_free_dev(dev);
		return -BLKID_ERR_BIG;
	}

	/* Don't set this until there is no chance of error */
	*dev_p = dev;

	/* If the filesystem size is larger than the device, this is bad */
	dev->bid_size = blkid_le32(rs->rs_blocks_count) * blocksize;

	/* LABEL/UUID are only valid for later versions of Reiserfs v3.6. */
	if (!strcmp(id->bim_magic, "ReIsEr2Fs") ||
	    !strcmp(id->bim_magic, "ReIsEr3Fs")) {
		if (strlen(rs->rs_label)) {
			blkid_create_tag(dev, "LABEL", rs->rs_label,
					 sizeof(rs->rs_label));
		}

		if (!uuid_is_null(rs->rs_uuid)) {
			char uuid[37];
			uuid_unparse(rs->rs_uuid, uuid);
			blkid_create_tag(dev, "UUID", uuid, sizeof(uuid));
		}
	}

	return 0;
}

static int probe_minix(int fd, blkid_dev *dev_p, const char *devname,
		       struct blkid_magic *id, unsigned char *buf,
		       blkid_loff_t size)
{
	blkid_dev dev;
	struct minix_super_block *ms;
	int ret;

	if ((ret = probe_default(fd, &dev, devname, id, buf, size)) < 0)
		return ret;

	ms = (struct minix_super_block *)buf;

	/* Don't set this until there is no chance of error */
	*dev_p = dev;
	dev->bid_size = ms->ms_nzones << ms->ms_log_zone_size;
	return 0;
}

static int probe_swap(int fd, blkid_dev *dev_p, const char *devname,
		      struct blkid_magic *id, unsigned char *buf,
		      blkid_loff_t size)
{
	blkid_dev dev;
	struct swap_header *sh;
	int psize;
	int ret;

	if ((ret = probe_default(fd, &dev, devname, id, buf, size)) < 0)
		return ret;

	/* PAGE_SIZE can be found by where the magic is located */
	psize = (id->bim_kboff << 10) + (id->bim_sboff + 10);

	/* Don't set this until there is no chance of error */
	*dev_p = dev;

	sh = (struct swap_header *)buf;
	/* Is swap data in local endian format? */
	dev->bid_size = (blkid_loff_t)(sh->sh_last_page + 1) * psize;

	/* A label can not exist on the old (128MB max) swap format */
	if (!strcmp(id->bim_magic, "SWAPSPACE2") && sh->sh_label[0]) {
		blkid_create_tag(dev, "LABEL", sh->sh_label,
				 sizeof(sh->sh_label));
	}

	return 0;
}

static int probe_mdraid(int fd, blkid_dev *dev_p, const char *devname,
			struct blkid_magic *id, unsigned char *buf,
			blkid_loff_t size)
{
	blkid_dev dev;
	struct mdp_superblock_s *md;
	int ret;

	if ((ret = probe_default(fd, &dev, devname, id, buf, size)) < 0)
		return ret;

	/* Don't set this until there is no chance of error */
	*dev_p = dev;

	md = (struct mdp_superblock_s *)buf;
	/* What units is md->size in?  Assume 512-byte sectors? */
	dev->bid_size = md->size * 512;

	/* The MD UUID is not contiguous in the superblock, make it so */
	if (md->set_uuid0 || md->set_uuid1 || md->set_uuid2 || md->set_uuid3) {
		unsigned char md_uuid[16];
		char uuid[37];

		memcpy(md_uuid, &md->set_uuid0, 4);
		memcpy(md_uuid + 4, &md->set_uuid1, 12);

		uuid_unparse(md_uuid, uuid);
		blkid_create_tag(dev, "UUID", uuid, sizeof(uuid));
	}
	return 0;
}

static int probe_hfs(int fd, blkid_dev *dev_p, const char *devname,
		     struct blkid_magic *id, unsigned char *buf,
		     blkid_loff_t size)
{
	blkid_dev dev;
	struct hfs_super_block *hfs;
	int ret;

	if ((ret = probe_default(fd, &dev, devname, id, buf, size)) < 0)
		return ret;

	hfs = (struct hfs_super_block *)buf;

	if (blkid_be32(hfs->h_blksize) != 512)
		return -BLKID_ERR_PARAM;

	/* Don't set this until there is no chance of error */
	*dev_p = dev;

	return 0;
}


/*
 * BLKID_BLK_OFFS is at least as large as the highest bim_kboff defined
 * in the type_array table below + bim_kbalign.  If we ever start looking for magics
 * relative to the end of a device, we can start using negative offsets
 * in type_array.
 */
#define BLKID_BLK_BITS	(10)
#define BLKID_BLK_KBITS	(BLKID_BLK_BITS - 10)
#define BLKID_BLK_SIZE	(1024 << BLKID_BLK_KBITS)
#define BLKID_BLK_MASK  (BLKID_BLK_SIZE - 1)
#define BLKID_BLK_OFFS	128	/* currently MDRAID kboff + align */

/*
 * Various filesystem magics that we can check for.  Note that kboff and
 * sboff are in kilobytes and bytes respectively.  All magics are in
 * byte strings so we don't worry about endian issues.
 */
struct blkid_magic type_array[] = {
/*  type     kboff   sboff len  magic           align kbsize probe */
  { "MDRAID",  -64,      0,  4, "\251+N\374",   65536,  4, probe_mdraid },
/*{ "LVM",       0,      0,  4, "HM\001\000",       1,  4, probe_lvm },*/
  { "jbd",       1,   0x38,  2, "\123\357",         1,  1, probe_jbd },
  { "ext3",      1,   0x38,  2, "\123\357",         1,  1, probe_ext3 },
  { "ext2",      1,   0x38,  2, "\123\357",         1,  1, probe_ext2 },
  { "reiserfs",  8,   0x34,  8, "ReIsErFs",         1,  1, probe_reiserfs },
  { "reiserfs", 64,   0x34,  9, "ReIsEr2Fs",        1,  1, probe_reiserfs },
  { "reiserfs", 64,   0x34,  9, "ReIsEr3Fs",        1,  1, probe_reiserfs },
  { "reiserfs", 64,   0x34,  8, "ReIsErFs",         1,  1, probe_reiserfs },
  { "reiserfs",  8,     20,  8, "ReIsErFs",         1,  1, probe_reiserfs },
  { "ntfs",      0,      3,  8, "NTFS    ",         1,  1, probe_default },
  { "vfat",      0,   0x52,  5, "MSWIN",            1,  1, probe_vfat },
  { "vfat",      0,   0x52,  8, "FAT32   ",         1,  1, probe_vfat },
  { "msdos",     0,   0x36,  5, "MSDOS",            1,  1, probe_msdos },
  { "msdos",     0,   0x36,  8, "FAT16   ",         1,  1, probe_msdos },
  { "msdos",     0,   0x36,  8, "FAT12   ",         1,  1, probe_msdos },
  { "minix",     1,   0x10,  2, "\177\023",         1,  1, probe_minix },
  { "minix",     1,   0x10,  2, "\217\023",         1,  1, probe_minix },
  { "minix",     1,   0x10,  2, "\150\044",         1,  1, probe_minix },
  { "minix",     1,   0x10,  2, "\170\044",         1,  1, probe_minix },
  { "vxfs",      1,      0,  4, "\365\374\001\245", 1,  1, probe_default },
  { "xfs",       0,      0,  4, "XFSB",             1,  1, probe_xfs },
  { "romfs",     0,      0,  8, "-rom1fs-",         1,  1, probe_default },
  { "bfs",       0,      0,  4, "\316\372\173\033", 1,  1, probe_default },
  { "cramfs",    0,      0,  4, "E=\315\034",       1,  1, probe_default },
  { "qnx4",      0,      4,  6, "QNX4FS",           1,  1, probe_default },
  { "iso9660",  32,      1,  5, "CD001",            1,  1, probe_default },
  { "iso9660",  32,      9,  5, "CDROM",            1,  1, probe_default },
  { "udf",      32,      1,  5, "BEA01",            1,  1, probe_default },
  { "udf",      32,      1,  5, "BOOT2",            1,  1, probe_default },
  { "udf",      32,      1,  5, "CD001",            1,  1, probe_default },
  { "udf",      32,      1,  5, "CDW02",            1,  1, probe_default },
  { "udf",      32,      1,  5, "NSR02",            1,  1, probe_default },
  { "udf",      32,      1,  5, "NSR03",            1,  1, probe_default },
  { "udf",      32,      1,  5, "TEA01",            1,  1, probe_default },
  { "jfs",      32,      0,  4, "JFS1",             1,  1, probe_default },
  { "hfs",       1,      0,  2, "BD",               1,  1, probe_hfs },
  { "ufs",       8,  0x55c,  4, "T\031\001\000",    1,  1, probe_default },
  { "hpfs",      8,      0,  4, "I\350\225\371",    1,  1, probe_default },
  { "sysv",      0,  0x3f8,  4, "\020~\030\375",    1,  1, probe_default },
  { "swap",      0,  0xff6, 10, "SWAP-SPACE",       1,  4, probe_swap },
  { "swap",      0,  0xff6, 10, "SWAPSPACE2",       1,  4, probe_swap },
  { "swap",      0, 0x1ff6, 10, "SWAP-SPACE",       1,  8, probe_swap },
  { "swap",      0, 0x1ff6, 10, "SWAPSPACE2",       1,  8, probe_swap },
  { "swap",      0, 0x3ff6, 10, "SWAP-SPACE",       1, 16, probe_swap },
  { "swap",      0, 0x3ff6, 10, "SWAPSPACE2",       1, 16, probe_swap },
  {   NULL,      0,      0,  0, NULL,               1,  0, NULL }
};


/*
 * When probing for a lot of magics, we handle everything in 1kB buffers so
 * that we don't have to worry about reading each combination of block sizes.
 */
static unsigned char *read_one_buf(int fd, blkid_loff_t offset)
{
	unsigned char *buf;

	if (lseek(fd, offset, SEEK_SET) < 0)
		return NULL;

	if (!(buf = (unsigned char *)malloc(BLKID_BLK_SIZE)))
		return NULL;

	if (read(fd, buf, BLKID_BLK_SIZE) != BLKID_BLK_SIZE) {
		free(buf);
		return NULL;
	}

	return buf;
}

static unsigned char *read_sb_buf(int fd, unsigned char **bufs, int kboff,
				  blkid_loff_t start)
{
	int idx = kboff >> BLKID_BLK_KBITS;
	unsigned char **buf;

	if (idx > BLKID_BLK_OFFS || idx < -BLKID_BLK_OFFS) {
		fprintf(stderr, "reading from invalid offset %d (%d)!\n",
			kboff, idx);
		return NULL;
	}

	buf = bufs + idx;
	if (!*buf)
		*buf = read_one_buf(fd, start);

	return *buf;
}

static struct blkid_magic *devname_to_magic(const char *devname, int fd,
					    unsigned char **bufs,
					    struct blkid_magic *id,
					    blkid_loff_t size)
{
	struct blkid_magic *ret = NULL;

	if (!bufs || fd < 0)
		return NULL;

	if (id >= type_array + sizeof(type_array) / sizeof(*id))
		return NULL;

	for (id = id < type_array ? type_array : id + 1; id->bim_type; ++id) {
		unsigned char *buf;
		blkid_loff_t start = 0LL;
		blkid_loff_t offset = 0LL;
		int kboff;

		offset = ((blkid_loff_t)id->bim_kboff << 10) +
			(id->bim_sboff & ~0x3ffULL);
		/*
		 * We index negative buffers by their actual offset (including
		 * superblock offsets > 1kB, not the aligned offset, so that
		 * we correctly access negative buffers with different
		 * alignment requirements.
		 */
		if (id->bim_kboff < 0) {
			start = (size & ~((blkid_loff_t)(id->bim_align - 1))) +
				offset;
			if (start < 0) /* Device too small for alignment */
				continue;
			kboff = (start - size) >> 10;
		} else {
			start = offset;
			kboff = offset >> 10;
		}

		if ((buf =
		     read_sb_buf(fd, bufs, kboff, start)) &&
		    !memcmp(id->bim_magic, buf + (id->bim_sboff&0x3ffULL),
			    id->bim_len)) {
			ret = id;
			break;
		}
	}

	return ret;
}

/*
 * Get data from a single block special device.
 *
 * Return a blkid_dev with at least the device type and size set.
 * If the passed-in size is zero, then we get the device size here.
 */
blkid_dev blkid_devname_to_dev(const char *devname, blkid_loff_t size)
{
	unsigned char *buf_array[BLKID_BLK_OFFS * 2 + 1];
	unsigned char **bufs = buf_array + BLKID_BLK_OFFS;
	blkid_dev dev = NULL, last = NULL;
	unsigned char *sb_buf = NULL;
	int sb_size = 0;
	struct blkid_magic *id = NULL;
	blkid_loff_t diff_last = 0xf000000000000000ULL;
	int fd;

	if (!devname)
		return NULL;

	fd = open(devname, O_RDONLY);
	if (fd < 0)
		return NULL;

	if (!size)
		size = blkid_get_dev_size(fd);
	if (size < 1024)
		goto exit_fd;

	memset(buf_array, 0, sizeof(buf_array));

	while ((id = devname_to_magic(devname, fd, bufs, id, size)) &&
	       diff_last) {
		int new_sb;
		blkid_loff_t diff_dev;

		DBG(printf("found type %s (#%d) on %s, probing\n",
			   id->bim_type, id - type_array, devname));

		new_sb = id->bim_kbsize << 10;
		if (sb_size < new_sb) {
			unsigned char *sav = sb_buf;
			if (!(sb_buf = realloc(sb_buf, new_sb))) {
				sb_buf = sav;
				continue;
			}
			sb_size = new_sb;
		}

		if (id->bim_probe(fd, &dev, devname, id, sb_buf, size) < 0)
			continue;

		diff_dev = size - dev->bid_size;
		DBG(printf("size = %Lu, fs size = %Lu\n", size, dev->bid_size));
		DBG(printf("checking best match: old %Ld, new %Ld\n",
			   diff_last, diff_dev));
		/* See which type is a better match by checking size */
		if ((diff_last < 0 && diff_dev > diff_last) ||
		    (diff_last > 0 && diff_dev >= 0 && diff_dev < diff_last)) {
			if (last)
				blkid_free_dev(last);
			last = dev;
			diff_last = diff_dev;
		} else
			blkid_free_dev(dev);
	}

	if (!last)
		DBG(printf("unknown device type on %s\n", devname));
	else
		DBG(printf(last));

	/* Free up any buffers we allocated */
	for (bufs = buf_array; bufs - buf_array < sizeof(buf_array) /
						sizeof(buf_array[0]); bufs++) {
		if (*bufs)
			free(*bufs);
	}

	if (sb_buf)
		free(sb_buf);
exit_fd:
	close(fd);
	return last;
}

/*
 * Verify that the data in dev is consistent with what is on the actual
 * block device (using the devname field only).  Normally this will be
 * called when finding items in the cache, but for long running processes
 * is also desirable to revalidate an item before use.
 *
 * If we are unable to revalidate the data, we return the old data and
 * do not set the BLKID_BID_FL_VERIFIED flag on it.
 */
blkid_dev blkid_verify_devname(blkid_cache cache, blkid_dev dev)
{
	blkid_loff_t size;
	struct blkid_magic *id;
	blkid_dev new = NULL;
	unsigned char *sb_buf = NULL;
	int sb_size = 0;
	time_t diff;
	int fd;

	if (!dev)
		return NULL;

	diff = time(0) - dev->bid_time;

	if (diff < BLKID_PROBE_MIN || (dev->bid_flags & BLKID_BID_FL_VERIFIED &&
				       diff < BLKID_PROBE_INTERVAL))
		return dev;

	DBG(printf("need to revalidate %s\n", dev->bid_name));

	if ((fd = open(dev->bid_name, O_RDONLY)) < 0) {
		if (errno == ENXIO || errno == ENODEV) {
			fprintf(stderr, "unable to open %s for revalidation\n",
				dev->bid_name);
			blkid_free_dev(dev);
			return NULL;
		}
		/* We don't have read permission, just return cache data. */
		DBG(printf("returning unverified data for %s\n", dev->bid_name));
		return dev;
	}

	size = blkid_get_dev_size(fd);

	/* See if we can probe this device by its existing type directly */
	for (id = type_array; id->bim_type; id++) {
		if (!strcmp(id->bim_type, dev->bid_type)) {
			int new_sb = id->bim_kbsize << 10;
			/* See if we need to allocate a larger sb buffer */
			if (sb_size < new_sb) {
				unsigned char *sav = sb_buf;

				/* We can't revalidate, return old dev */
				if (!(sb_buf = realloc(sb_buf, new_sb))) {
					fprintf(stderr, "not enough memory for "
						"%s revalidation\n",
						dev->bid_name);
					free(sav);
					goto exit_fd;
				}
				sb_size = new_sb;
			}

			if (id->bim_probe(fd, &new, dev->bid_name, id, sb_buf,
					  size) == 0)
				break;
		}
	}

	if (sb_buf)
		free(sb_buf);

	/* Otherwise we need to determine the device type first */
	if (new || (new = blkid_devname_to_dev(dev->bid_name, size))) {
		new->bid_id = dev->bid_id; /* save old id for cache */
		blkid_free_dev(dev);
		dev = blkid_add_dev_to_cache(cache, new);
	}

exit_fd:
	close(fd);

	/* In case the cache is missing the device size */
	if (dev->bid_devsize == 0)
		dev->bid_devsize = size;
	return dev;

}

#ifdef TEST_PROGRAM
int main(int argc, char **argv)
{
	blkid_dev dev;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s device\n"
			"Probe a single device to determine type\n", argv[0]);
		exit(1);
	}
	dev = blkid_devname_to_dev(argv[1], 0);
	if (dev)
		blkid_free_dev(dev);
	else
		printf("%s: %s has an unsupported type\n", argv[0], argv[1]);
	return (0);
}
#endif
