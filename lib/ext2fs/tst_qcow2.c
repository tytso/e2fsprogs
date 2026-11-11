/*
 * tst_qcow2.c --- unit tests for qcow2.c
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Library
 * General Public License, version 2.
 * %End-Header%
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

#include "ext2_fs.h"
#include "ext2fs.h"
#include "qcow2.h"

static int test_count;
static int fail_count;

#define CHECK(cond, msg) do { \
	test_count++; \
	if (!(cond)) { \
		fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, msg); \
		fail_count++; \
	} \
} while (0)

static int make_temp_file(char *tmpl)
{
	int fd;

	strcpy(tmpl, "/tmp/tst_qcow2.XXXXXX");
	fd = mkstemp(tmpl);
	if (fd < 0) {
		perror("mkstemp");
		exit(1);
	}
	return fd;
}

/*
 * Suite 1: qcow2_read_header() and qcow2_write_raw_image() header validation
 */
static void test_qcow2_header_validation(void)
{
	char			qpath[64], rpath[64];
	int			qfd, rfd;
	struct ext2_qcow2_hdr	raw_hdr, *hdr;

	qfd = make_temp_file(qpath);
	rfd = make_temp_file(rpath);

	/* Empty / truncated file (< sizeof(struct ext2_qcow2_hdr)) */
	CHECK(pwrite(qfd, "QFI\xfb", 4, 0) == 4, "write short header");
	CHECK(qcow2_read_header(qfd) == NULL, "short header rejected");

	/* Bad magic */
	memset(&raw_hdr, 0, sizeof(raw_hdr));
	raw_hdr.magic = ext2fs_cpu_to_be32(0x12345678U);
	raw_hdr.version = ext2fs_cpu_to_be32(QCOW_VERSION);
	CHECK(pwrite(qfd, &raw_hdr, sizeof(raw_hdr), 0) ==
	      (ssize_t) sizeof(raw_hdr), "write bad magic");
	CHECK(qcow2_read_header(qfd) == NULL, "bad magic rejected");

	/* Unsupported versions (1 and 3) */
	raw_hdr.magic = ext2fs_cpu_to_be32(QCOW_MAGIC);
	raw_hdr.version = ext2fs_cpu_to_be32(1);
	CHECK(pwrite(qfd, &raw_hdr, sizeof(raw_hdr), 0) ==
	      (ssize_t) sizeof(raw_hdr), "write version 1");
	CHECK(qcow2_read_header(qfd) == NULL, "version 1 rejected");

	raw_hdr.version = ext2fs_cpu_to_be32(3);
	CHECK(pwrite(qfd, &raw_hdr, sizeof(raw_hdr), 0) ==
	      (ssize_t) sizeof(raw_hdr), "write version 3");
	CHECK(qcow2_read_header(qfd) == NULL, "version 3 rejected");

	/* Valid v2 header */
	raw_hdr.version = ext2fs_cpu_to_be32(QCOW_VERSION);
	raw_hdr.cluster_bits = ext2fs_cpu_to_be32(12);
	raw_hdr.size = ext2fs_cpu_to_be64(65536ULL);
	raw_hdr.l1_size = ext2fs_cpu_to_be32(1);
	raw_hdr.l1_table_offset = ext2fs_cpu_to_be64(4096ULL);
	CHECK(pwrite(qfd, &raw_hdr, sizeof(raw_hdr), 0) ==
	      (ssize_t) sizeof(raw_hdr), "write valid v2 header");

	hdr = qcow2_read_header(qfd);
	CHECK(hdr != NULL, "valid v2 header read");
	if (hdr) {
		/* Encrypted image rejected with -QCOW_ENCRYPTED */
		hdr->crypt_method = ext2fs_cpu_to_be32(1);
		CHECK(qcow2_write_raw_image(qfd, rfd, hdr) == -QCOW_ENCRYPTED,
		      "encrypted header rejected");
		hdr->crypt_method = 0;

		/* Out-of-range cluster_bits (< 9 or > 31) rejected */
		hdr->cluster_bits = ext2fs_cpu_to_be32(8);
		CHECK(qcow2_write_raw_image(qfd, rfd, hdr) == -QCOW_CORRUPTED,
		      "cluster_bits == 8 rejected");
		hdr->cluster_bits = ext2fs_cpu_to_be32(32);
		CHECK(qcow2_write_raw_image(qfd, rfd, hdr) == -QCOW_CORRUPTED,
		      "cluster_bits == 32 rejected");
		hdr->cluster_bits = ext2fs_cpu_to_be32(12);

		/* Unaligned l1_table_offset rejected */
		hdr->l1_table_offset = ext2fs_cpu_to_be64(4097ULL);
		CHECK(qcow2_write_raw_image(qfd, rfd, hdr) == -QCOW_CORRUPTED,
		      "unaligned l1_table_offset rejected");
		hdr->l1_table_offset = ext2fs_cpu_to_be64(4096ULL);

		/* Excessive l1_size > max_l1_size rejected */
		hdr->l1_size = ext2fs_cpu_to_be32(100000U);
		CHECK(qcow2_write_raw_image(qfd, rfd, hdr) == -QCOW_CORRUPTED,
		      "excessive l1_size rejected");

		ext2fs_free_mem(&hdr);
	}

	close(rfd);
	close(qfd);
	unlink(rpath);
	unlink(qpath);
	printf("  test_qcow2_header_validation: ok\n");
}

/*
 * Helper to synthesize and convert a multi-L1, sparse QCOW2 v2 image for a
 * given cluster_bits (e.g. 9 -> 512B clusters, 10 -> 1KiB clusters).
 *
 * For cluster_size = C, l2_size = C / 8 entries per L2 table:
 *   Virtual image spans 3 L1 entries: image_size = 3 * l2_size * C.
 *   QCOW2 layout (all offsets <= image_size):
 *     cluster 0: header
 *     cluster 1: L1 table (3 entries: L1[0] -> cl 2 | COPIED,
 *                                     L1[1] -> 0 (unallocated L1 hole),
 *                                     L1[2] -> cl 3)
 *     cluster 2: L2 table for L1[0] (slot 0 -> cl 4 | COPIED,
 *                                    slot 5 -> cl 5, rest 0)
 *     cluster 3: L2 table for L1[2] (slot 1 -> cl 6,
 *                                    slot (l2_size - 1) -> 0 (sparse tail))
 *     cluster 4: data pattern A
 *     cluster 5: data pattern B
 *     cluster 6: data pattern C
 */
static void run_qcow2_conversion_case(unsigned int cluster_bits)
{
	char			qpath[64], rpath[64];
	int			qfd, rfd;
	unsigned int		csz = 1U << cluster_bits;
	unsigned int		l2_entries = 1U << (cluster_bits - 3);
	__u64			image_size = (__u64) 3 * l2_entries * csz;
	struct ext2_qcow2_hdr	raw_hdr, *hdr;
	__u64			l1_tab[3];
	__u64			*l2_tab = NULL;
	unsigned char		*pat_a = NULL, *pat_b = NULL, *pat_c = NULL;
	unsigned char		*actual = NULL, *expected = NULL;
	struct stat		st;
	unsigned int		i;

	qfd = make_temp_file(qpath);
	rfd = make_temp_file(rpath);

	CHECK(ext2fs_get_memzero(csz, &l2_tab) == 0, "alloc l2_tab");
	CHECK(ext2fs_get_mem(csz, &pat_a) == 0, "alloc pat_a");
	CHECK(ext2fs_get_mem(csz, &pat_b) == 0, "alloc pat_b");
	CHECK(ext2fs_get_mem(csz, &pat_c) == 0, "alloc pat_c");
	CHECK(ext2fs_get_memzero(image_size, &expected) == 0, "alloc expected");
	CHECK(ext2fs_get_memzero(image_size, &actual) == 0, "alloc actual");

	for (i = 0; i < csz; i++) {
		pat_a[i] = (unsigned char) ((i * 3 + 0x11) & 0xff);
		pat_b[i] = (unsigned char) ((i * 7 + 0x5a) & 0xff);
		pat_c[i] = (unsigned char) ((i * 13 + 0xa5) & 0xff);
	}

	/* Cluster 0: QCOW2 header */
	memset(&raw_hdr, 0, sizeof(raw_hdr));
	raw_hdr.magic = ext2fs_cpu_to_be32(QCOW_MAGIC);
	raw_hdr.version = ext2fs_cpu_to_be32(QCOW_VERSION);
	raw_hdr.cluster_bits = ext2fs_cpu_to_be32(cluster_bits);
	raw_hdr.size = ext2fs_cpu_to_be64(image_size);
	raw_hdr.l1_size = ext2fs_cpu_to_be32(3);
	raw_hdr.l1_table_offset = ext2fs_cpu_to_be64((__u64) 1 * csz);
	CHECK(pwrite(qfd, &raw_hdr, sizeof(raw_hdr), 0) ==
	      (ssize_t) sizeof(raw_hdr), "write qcow2 hdr");

	/* Cluster 1: L1 table */
	l1_tab[0] = ext2fs_cpu_to_be64(((__u64) 2 * csz) | QCOW_OFLAG_COPIED);
	l1_tab[1] = 0;
	l1_tab[2] = ext2fs_cpu_to_be64((__u64) 3 * csz);
	CHECK(pwrite(qfd, l1_tab, sizeof(l1_tab), (off_t) (1 * csz)) ==
	      (ssize_t) sizeof(l1_tab), "write L1 table");

	/* Cluster 2: L2 table for L1[0] */
	memset(l2_tab, 0, csz);
	l2_tab[0] = ext2fs_cpu_to_be64(((__u64) 4 * csz) | QCOW_OFLAG_COPIED);
	l2_tab[5] = ext2fs_cpu_to_be64((__u64) 5 * csz);
	CHECK(pwrite(qfd, l2_tab, csz, (off_t) (2 * csz)) == (ssize_t) csz,
	      "write L2[0] table");

	/* Cluster 3: L2 table for L1[2] */
	memset(l2_tab, 0, csz);
	l2_tab[1] = ext2fs_cpu_to_be64((__u64) 6 * csz);
	CHECK(pwrite(qfd, l2_tab, csz, (off_t) (3 * csz)) == (ssize_t) csz,
	      "write L2[2] table");

	/* Clusters 4, 5, 6: out-of-order/packed data clusters */
	CHECK(pwrite(qfd, pat_a, csz, (off_t) (4 * csz)) == (ssize_t) csz,
	      "write data cluster 4");
	CHECK(pwrite(qfd, pat_b, csz, (off_t) (5 * csz)) == (ssize_t) csz,
	      "write data cluster 5");
	CHECK(pwrite(qfd, pat_c, csz, (off_t) (6 * csz)) == (ssize_t) csz,
	      "write data cluster 6");

	/* Populate expected raw image */
	memcpy(expected + (size_t) 0 * csz, pat_a, csz);
	memcpy(expected + (size_t) 5 * csz, pat_b, csz);
	memcpy(expected + ((size_t) (2 * l2_entries + 1) * csz), pat_c, csz);

	hdr = qcow2_read_header(qfd);
	CHECK(hdr != NULL, "qcow2_read_header for conversion");
	if (hdr) {
		CHECK(qcow2_write_raw_image(qfd, rfd, hdr) == 0,
		      "qcow2_write_raw_image succeeded");
		ext2fs_free_mem(&hdr);
	}

	CHECK(fstat(rfd, &st) == 0 && (__u64) st.st_size == image_size,
	      "raw output file size matches virtual image_size");
	CHECK(pread(rfd, actual, image_size, 0) == (ssize_t) image_size,
	      "read entire raw image");
	CHECK(memcmp(actual, expected, image_size) == 0,
	      "raw image matches expected data and sparse holes byte-for-byte");

	ext2fs_free_mem(&actual);
	ext2fs_free_mem(&expected);
	ext2fs_free_mem(&pat_c);
	ext2fs_free_mem(&pat_b);
	ext2fs_free_mem(&pat_a);
	ext2fs_free_mem(&l2_tab);
	close(rfd);
	close(qfd);
	unlink(rpath);
	unlink(qpath);
}

static void test_qcow2_raw_conversion(void)
{
	/* Test minimum cluster size (512B, cluster_bits=9) and 1KiB (10) */
	run_qcow2_conversion_case(9);
	run_qcow2_conversion_case(10);
	printf("  test_qcow2_raw_conversion: ok\n");
}

int main(int argc EXT2FS_ATTR((unused)), char *argv[] EXT2FS_ATTR((unused)))
{
	add_error_table(&et_ext2_error_table);
	printf("Running tst_qcow2 unit tests...\n");

	test_qcow2_header_validation();
	test_qcow2_raw_conversion();

	remove_error_table(&et_ext2_error_table);
	printf("tst_qcow2: %d checks run, %d failed\n",
	       test_count, fail_count);
	return fail_count ? 1 : 0;
}
