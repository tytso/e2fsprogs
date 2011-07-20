/*
 * Implementation of new quotafile format
 *
 * Jan Kara <jack@suse.cz> - sponsored by SuSE CR
 */

#include <sys/types.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <asm/byteorder.h>

#include "common.h"
#include "quotaio_tree.h"
#include "quotaio.h"

typedef char *dqbuf_t;

#define getdqbuf()		smalloc(QT_BLKSIZE)
#define freedqbuf(buf)		free(buf)

/* Is given dquot empty? */
int qtree_entry_unused(struct qtree_mem_dqinfo *info, char *disk)
{
	int i;

	for (i = 0; i < info->dqi_entry_size; i++)
		if (disk[i])
			return 0;
	return 1;
}

int qtree_dqstr_in_blk(struct qtree_mem_dqinfo *info)
{
	return (QT_BLKSIZE - sizeof(struct qt_disk_dqdbheader)) /
		info->dqi_entry_size;
}

static int get_index(qid_t id, int depth)
{
	return (id >> ((QT_TREEDEPTH - depth - 1) * 8)) & 0xff;
}

/* Read given block */
static void read_blk(struct quota_handle *h, uint blk, dqbuf_t buf)
{
	int err;

	err = h->e2fs_read(&h->qh_qf, blk << QT_BLKSIZE_BITS, buf,
			QT_BLKSIZE);
	if (err < 0)
		log_fatal(2, "Cannot read block %u: %s", blk, strerror(errno));
	else if (err != QT_BLKSIZE)
		memset(buf + err, 0, QT_BLKSIZE - err);
}

/* Write block */
static int write_blk(struct quota_handle *h, uint blk, dqbuf_t buf)
{
	int err;

	err = h->e2fs_write(&h->qh_qf, blk << QT_BLKSIZE_BITS, buf,
			QT_BLKSIZE);
	if (err < 0 && errno != ENOSPC)
		log_fatal(2, "Cannot write block (%u): %s",
			  blk, strerror(errno));
	if (err != QT_BLKSIZE)
		return -ENOSPC;
	return 0;
}

/* Get free block in file (either from free list or create new one) */
static int get_free_dqblk(struct quota_handle *h)
{
	dqbuf_t buf = getdqbuf();
	struct qt_disk_dqdbheader *dh = (struct qt_disk_dqdbheader *)buf;
	struct qtree_mem_dqinfo *info = &h->qh_info.u.v2_mdqi.dqi_qtree;
	int blk;

	if (info->dqi_free_blk) {
		blk = info->dqi_free_blk;
		read_blk(h, blk, buf);
		info->dqi_free_blk = __le32_to_cpu(dh->dqdh_next_free);
	} else {
		memset(buf, 0, QT_BLKSIZE);
		/* Assure block allocation... */
		if (write_blk(h, info->dqi_blocks, buf) < 0) {
			freedqbuf(buf);
			log_err("Cannot allocate new quota block "
				"(out of disk space).", "");
			return -ENOSPC;
		}
		blk = info->dqi_blocks++;
	}
	mark_quotafile_info_dirty(h);
	freedqbuf(buf);
	return blk;
}

/* Put given block to free list */
static void put_free_dqblk(struct quota_handle *h, dqbuf_t buf, uint blk)
{
	struct qt_disk_dqdbheader *dh = (struct qt_disk_dqdbheader *)buf;
	struct qtree_mem_dqinfo *info = &h->qh_info.u.v2_mdqi.dqi_qtree;

	dh->dqdh_next_free = __cpu_to_le32(info->dqi_free_blk);
	dh->dqdh_prev_free = __cpu_to_le32(0);
	dh->dqdh_entries = __cpu_to_le16(0);
	info->dqi_free_blk = blk;
	mark_quotafile_info_dirty(h);
	write_blk(h, blk, buf);
}

/* Remove given block from the list of blocks with free entries */
static void remove_free_dqentry(struct quota_handle *h, dqbuf_t buf, uint blk)
{
	dqbuf_t tmpbuf = getdqbuf();
	struct qt_disk_dqdbheader *dh = (struct qt_disk_dqdbheader *)buf;
	uint nextblk = __le32_to_cpu(dh->dqdh_next_free), prevblk =

		__le32_to_cpu(dh->dqdh_prev_free);

	if (nextblk) {
		read_blk(h, nextblk, tmpbuf);
		((struct qt_disk_dqdbheader *)tmpbuf)->dqdh_prev_free =
				dh->dqdh_prev_free;
		write_blk(h, nextblk, tmpbuf);
	}
	if (prevblk) {
		read_blk(h, prevblk, tmpbuf);
		((struct qt_disk_dqdbheader *)tmpbuf)->dqdh_next_free =
				dh->dqdh_next_free;
		write_blk(h, prevblk, tmpbuf);
	} else {
		h->qh_info.u.v2_mdqi.dqi_qtree.dqi_free_entry = nextblk;
		mark_quotafile_info_dirty(h);
	}
	freedqbuf(tmpbuf);
	dh->dqdh_next_free = dh->dqdh_prev_free = __cpu_to_le32(0);
	write_blk(h, blk, buf);	/* No matter whether write succeeds
				 * block is out of list */
}

/* Insert given block to the beginning of list with free entries */
static void insert_free_dqentry(struct quota_handle *h, dqbuf_t buf, uint blk)
{
	dqbuf_t tmpbuf = getdqbuf();
	struct qt_disk_dqdbheader *dh = (struct qt_disk_dqdbheader *)buf;
	struct qtree_mem_dqinfo *info = &h->qh_info.u.v2_mdqi.dqi_qtree;

	dh->dqdh_next_free = __cpu_to_le32(info->dqi_free_entry);
	dh->dqdh_prev_free = __cpu_to_le32(0);
	write_blk(h, blk, buf);
	if (info->dqi_free_entry) {
		read_blk(h, info->dqi_free_entry, tmpbuf);
		((struct qt_disk_dqdbheader *)tmpbuf)->dqdh_prev_free =
				__cpu_to_le32(blk);
		write_blk(h, info->dqi_free_entry, tmpbuf);
	}
	freedqbuf(tmpbuf);
	info->dqi_free_entry = blk;
	mark_quotafile_info_dirty(h);
}

/* Find space for dquot */
static uint find_free_dqentry(struct quota_handle *h, struct dquot *dquot,
			      int *err)
{
	int blk, i;
	struct qt_disk_dqdbheader *dh;
	struct qtree_mem_dqinfo *info = &h->qh_info.u.v2_mdqi.dqi_qtree;
	char *ddquot;
	dqbuf_t buf;

	*err = 0;
	buf = getdqbuf();
	dh = (struct qt_disk_dqdbheader *)buf;
	if (info->dqi_free_entry) {
		blk = info->dqi_free_entry;
		read_blk(h, blk, buf);
	} else {
		blk = get_free_dqblk(h);
		if (blk < 0) {
			freedqbuf(buf);
			*err = blk;
			return 0;
		}
		memset(buf, 0, QT_BLKSIZE);
		info->dqi_free_entry = blk;
		mark_quotafile_info_dirty(h);
	}

	/* Block will be full? */
	if (__le16_to_cpu(dh->dqdh_entries) + 1 >= qtree_dqstr_in_blk(info))
		remove_free_dqentry(h, buf, blk);

	dh->dqdh_entries = __cpu_to_le16(__le16_to_cpu(dh->dqdh_entries) + 1);
	/* Find free structure in block */
	ddquot = buf + sizeof(struct qt_disk_dqdbheader);
	for (i = 0;
	     i < qtree_dqstr_in_blk(info) && !qtree_entry_unused(info, ddquot);
	     i++)
		ddquot += info->dqi_entry_size;

	if (i == qtree_dqstr_in_blk(info))
		log_fatal(2, "find_free_dqentry(): Data block full but it "
			     "shouldn't.", "");

	write_blk(h, blk, buf);
	dquot->dq_dqb.u.v2_mdqb.dqb_off =
		(blk << QT_BLKSIZE_BITS) + sizeof(struct qt_disk_dqdbheader) +
		i * info->dqi_entry_size;
	freedqbuf(buf);
	return blk;
}

/* Insert reference to structure into the trie */
static int do_insert_tree(struct quota_handle *h, struct dquot *dquot,
			  uint * treeblk, int depth)
{
	dqbuf_t buf;
	int newson = 0, newact = 0;
	u_int32_t *ref;
	uint newblk;
	int ret = 0;

	log_debug("inserting in tree: treeblk=%u, depth=%d", *treeblk, depth);
	buf = getdqbuf();
	if (!*treeblk) {
		ret = get_free_dqblk(h);
		if (ret < 0)
			goto out_buf;
		*treeblk = ret;
		memset(buf, 0, QT_BLKSIZE);
		newact = 1;
	} else {
		read_blk(h, *treeblk, buf);
	}

	ref = (u_int32_t *) buf;
	newblk = __le32_to_cpu(ref[get_index(dquot->dq_id, depth)]);
	if (!newblk)
		newson = 1;
	if (depth == QT_TREEDEPTH - 1) {
		if (newblk)
			log_fatal(2, "Inserting already present quota entry "
				     "(block %u).",
				  ref[get_index(dquot->dq_id, depth)]);
		newblk = find_free_dqentry(h, dquot, &ret);
	} else {
		ret = do_insert_tree(h, dquot, &newblk, depth + 1);
	}

	if (newson && ret >= 0) {
		ref[get_index(dquot->dq_id, depth)] = __cpu_to_le32(newblk);
		write_blk(h, *treeblk, buf);
	} else if (newact && ret < 0) {
		put_free_dqblk(h, buf, *treeblk);
	}

out_buf:
	freedqbuf(buf);
	return ret;
}

/* Wrapper for inserting quota structure into tree */
static void dq_insert_tree(struct quota_handle *h, struct dquot *dquot)
{
	uint tmp = QT_TREEOFF;

	if (do_insert_tree(h, dquot, &tmp, 0) < 0)
		log_fatal(2, "Cannot write quota (id %u): %s",
			  (uint) dquot->dq_id, strerror(errno));
}

/* Write dquot to file */
void qtree_write_dquot(struct dquot *dquot)
{
	ssize_t ret;
	struct quota_handle *h = dquot->dq_h;
	struct qtree_mem_dqinfo *info =
			&dquot->dq_h->qh_info.u.v2_mdqi.dqi_qtree;
	log_debug("writing ddquot 1: off=%llu, info->dqi_entry_size=%u",
			dquot->dq_dqb.u.v2_mdqb.dqb_off,
			info->dqi_entry_size);
	char *ddquot = (char *)smalloc(info->dqi_entry_size);

	if (!dquot->dq_dqb.u.v2_mdqb.dqb_off)
		dq_insert_tree(dquot->dq_h, dquot);
	info->dqi_ops->mem2disk_dqblk(ddquot, dquot);
	log_debug("writing ddquot 2: off=%llu, info->dqi_entry_size=%u",
			dquot->dq_dqb.u.v2_mdqb.dqb_off,
			info->dqi_entry_size);
	ret = h->e2fs_write(&h->qh_qf, dquot->dq_dqb.u.v2_mdqb.dqb_off, ddquot,
			info->dqi_entry_size);

	if (ret != info->dqi_entry_size) {
		if (ret > 0)
			errno = ENOSPC;
		log_fatal(2, "Quota write failed (id %u): %s",
			  (uint)dquot->dq_id, strerror(errno));
	}
}

/* Free dquot entry in data block */
static void free_dqentry(struct quota_handle *h, struct dquot *dquot, uint blk)
{
	struct qt_disk_dqdbheader *dh;
	struct qtree_mem_dqinfo *info = &h->qh_info.u.v2_mdqi.dqi_qtree;
	dqbuf_t buf = getdqbuf();

	if (dquot->dq_dqb.u.v2_mdqb.dqb_off >> QT_BLKSIZE_BITS != blk)
		log_fatal(2, "Quota structure has offset to other block (%u) "
			     "than it should (%u).", blk,
			  (uint) (dquot->dq_dqb.u.v2_mdqb.dqb_off >>
				  QT_BLKSIZE_BITS));

	read_blk(h, blk, buf);
	dh = (struct qt_disk_dqdbheader *)buf;
	dh->dqdh_entries = __cpu_to_le16(__le16_to_cpu(dh->dqdh_entries) - 1);

	if (!__le16_to_cpu(dh->dqdh_entries)) {	/* Block got free? */
		remove_free_dqentry(h, buf, blk);
		put_free_dqblk(h, buf, blk);
	} else {
		memset(buf + (dquot->dq_dqb.u.v2_mdqb.dqb_off &
			      ((1 << QT_BLKSIZE_BITS) - 1)),
		       0, info->dqi_entry_size);

		/* First free entry? */
		if (__le16_to_cpu(dh->dqdh_entries) ==
				qtree_dqstr_in_blk(info) - 1)
			/* This will also write data block */
			insert_free_dqentry(h, buf, blk);
		else
			write_blk(h, blk, buf);
	}
	dquot->dq_dqb.u.v2_mdqb.dqb_off = 0;
	freedqbuf(buf);
}

/* Remove reference to dquot from tree */
static void remove_tree(struct quota_handle *h, struct dquot *dquot,
			uint * blk, int depth)
{
	dqbuf_t buf = getdqbuf();
	uint newblk;
	u_int32_t *ref = (u_int32_t *) buf;

	read_blk(h, *blk, buf);
	newblk = __le32_to_cpu(ref[get_index(dquot->dq_id, depth)]);
	if (depth == QT_TREEDEPTH - 1) {
		free_dqentry(h, dquot, newblk);
		newblk = 0;
	} else {
		remove_tree(h, dquot, &newblk, depth + 1);
	}

	if (!newblk) {
		int i;

		ref[get_index(dquot->dq_id, depth)] = __cpu_to_le32(0);

		/* Block got empty? */
		for (i = 0; i < QT_BLKSIZE && !buf[i]; i++);

		/* Don't put the root block into the free block list */
		if (i == QT_BLKSIZE && *blk != QT_TREEOFF) {
			put_free_dqblk(h, buf, *blk);
			*blk = 0;
		} else {
			write_blk(h, *blk, buf);
		}
	}
	freedqbuf(buf);
}

/* Delete dquot from tree */
void qtree_delete_dquot(struct dquot *dquot)
{
	uint tmp = QT_TREEOFF;

	if (!dquot->dq_dqb.u.v2_mdqb.dqb_off)	/* Even not allocated? */
		return;
	remove_tree(dquot->dq_h, dquot, &tmp, 0);
}

/* Find entry in block */
static loff_t find_block_dqentry(struct quota_handle *h,
				 struct dquot *dquot, uint blk)
{
	struct qtree_mem_dqinfo *info = &h->qh_info.u.v2_mdqi.dqi_qtree;
	dqbuf_t buf = getdqbuf();
	int i;
	char *ddquot = buf + sizeof(struct qt_disk_dqdbheader);

	read_blk(h, blk, buf);
	for (i = 0;
	     i < qtree_dqstr_in_blk(info) && !info->dqi_ops->is_id(ddquot, dquot);
	     i++)
		ddquot += info->dqi_entry_size;

	if (i == qtree_dqstr_in_blk(info))
		log_fatal(2, "Quota for id %u referenced but not present.",
			  dquot->dq_id);
	freedqbuf(buf);
	return (blk << QT_BLKSIZE_BITS) + sizeof(struct qt_disk_dqdbheader) +
		i * info->dqi_entry_size;
}

/* Find entry for given id in the tree */
static loff_t find_tree_dqentry(struct quota_handle *h, struct dquot *dquot,
				uint blk, int depth)
{
	dqbuf_t buf = getdqbuf();
	loff_t ret = 0;
	u_int32_t *ref = (u_int32_t *) buf;

	read_blk(h, blk, buf);
	ret = 0;
	blk = __le32_to_cpu(ref[get_index(dquot->dq_id, depth)]);
	if (!blk)	/* No reference? */
		goto out_buf;
	if (depth < QT_TREEDEPTH - 1)
		ret = find_tree_dqentry(h, dquot, blk, depth + 1);
	else
		ret = find_block_dqentry(h, dquot, blk);
out_buf:
	freedqbuf(buf);
	return ret;
}

/* Find entry for given id in the tree - wrapper function */
static inline loff_t find_dqentry(struct quota_handle *h, struct dquot *dquot)
{
	return find_tree_dqentry(h, dquot, QT_TREEOFF, 0);
}

/*
 *  Read dquot (either from disk or from kernel)
 *  User can use errno to detect errstr when NULL is returned
 */
struct dquot *qtree_read_dquot(struct quota_handle *h, qid_t id)
{
	struct qtree_mem_dqinfo *info = &h->qh_info.u.v2_mdqi.dqi_qtree;
	loff_t offset;
	ssize_t ret;
	char *ddquot = smalloc(info->dqi_entry_size);
	struct dquot *dquot = get_empty_dquot();

	dquot->dq_id = id;
	dquot->dq_h = h;
	dquot->dq_dqb.u.v2_mdqb.dqb_off = 0;
	memset(&dquot->dq_dqb, 0, sizeof(struct util_dqblk));

	offset = find_dqentry(h, dquot);
	if (offset > 0) {
		dquot->dq_dqb.u.v2_mdqb.dqb_off = offset;
		ret = h->e2fs_read(&h->qh_qf, offset, ddquot,
			info->dqi_entry_size);
		if (ret != info->dqi_entry_size) {
			if (ret > 0)
				errno = EIO;
			log_fatal(2,
				"Cannot read quota structure for id %u: %s",
				dquot->dq_id, strerror(errno));
		}
		info->dqi_ops->disk2mem_dqblk(dquot, ddquot);
	}
	return dquot;
}

/*
 *	Scan all dquots in file and call callback on each
 */
#define set_bit(bmp, ind) ((bmp)[(ind) >> 3] |= (1 << ((ind) & 7)))
#define get_bit(bmp, ind) ((bmp)[(ind) >> 3] & (1 << ((ind) & 7)))

static int report_block(struct dquot *dquot, uint blk, char *bitmap,
			int (*process_dquot) (struct dquot *, char *))
{
	struct qtree_mem_dqinfo *info =
			&dquot->dq_h->qh_info.u.v2_mdqi.dqi_qtree;
	dqbuf_t buf = getdqbuf();
	struct qt_disk_dqdbheader *dh;
	char *ddata;
	int entries, i;

	set_bit(bitmap, blk);
	read_blk(dquot->dq_h, blk, buf);
	dh = (struct qt_disk_dqdbheader *)buf;
	ddata = buf + sizeof(struct qt_disk_dqdbheader);
	entries = __le16_to_cpu(dh->dqdh_entries);
	for (i = 0; i < qtree_dqstr_in_blk(info);
			i++, ddata += info->dqi_entry_size)
		if (!qtree_entry_unused(info, ddata)) {
			info->dqi_ops->disk2mem_dqblk(dquot, ddata);
			if (process_dquot(dquot, NULL) < 0)
				break;
		}
	freedqbuf(buf);
	return entries;
}

static void check_reference(struct quota_handle *h, uint blk)
{
	if (blk >= h->qh_info.u.v2_mdqi.dqi_qtree.dqi_blocks)
		log_fatal(2, "Illegal reference (%u >= %u) in %s quota file. "
			     "Quota file is probably corrupted.\n"
			     "Please run e2fsck (8) to fix it.",
			  blk,
			  h->qh_info.u.v2_mdqi.dqi_qtree.dqi_blocks,
			  type2name(h->qh_type));
}

static int report_tree(struct dquot *dquot, uint blk, int depth, char *bitmap,
		       int (*process_dquot) (struct dquot *, char *))
{
	int entries = 0, i;
	dqbuf_t buf = getdqbuf();
	u_int32_t *ref = (u_int32_t *) buf;

	read_blk(dquot->dq_h, blk, buf);
	if (depth == QT_TREEDEPTH - 1) {
		for (i = 0; i < QT_BLKSIZE >> 2; i++) {
			blk = __le32_to_cpu(ref[i]);
			check_reference(dquot->dq_h, blk);
			if (blk && !get_bit(bitmap, blk))
				entries += report_block(dquot, blk, bitmap,
							process_dquot);
		}
	} else {
		for (i = 0; i < QT_BLKSIZE >> 2; i++)
			blk = __le32_to_cpu(ref[i]);
			if (blk) {
				check_reference(dquot->dq_h, blk);
				entries += report_tree(dquot, blk, depth + 1,
						       bitmap, process_dquot);
			}
	}
	freedqbuf(buf);
	return entries;
}

static uint find_set_bits(char *bmp, int blocks)
{
	uint i, used = 0;

	for (i = 0; i < blocks; i++)
		if (get_bit(bmp, i))
			used++;
	return used;
}

int qtree_scan_dquots(struct quota_handle *h,
		      int (*process_dquot) (struct dquot *, char *))
{
	char *bitmap;
	struct v2_mem_dqinfo *v2info = &h->qh_info.u.v2_mdqi;
	struct qtree_mem_dqinfo *info = &v2info->dqi_qtree;
	struct dquot *dquot = get_empty_dquot();

	dquot->dq_h = h;
	bitmap = smalloc((info->dqi_blocks + 7) >> 3);
	memset(bitmap, 0, (info->dqi_blocks + 7) >> 3);
	v2info->dqi_used_entries = report_tree(dquot, QT_TREEOFF, 0, bitmap,
					       process_dquot);
	v2info->dqi_data_blocks = find_set_bits(bitmap, info->dqi_blocks);
	free(bitmap);
	free(dquot);
	return 0;
}
