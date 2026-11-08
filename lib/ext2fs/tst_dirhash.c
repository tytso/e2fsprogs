/*
 * tst_dirhash.c --- unit tests for dirhash.c and nls_utf8.c
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Library
 * General Public License, version 2.
 * %End-Header%
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif

#include "ext2_fs.h"
#include "ext2fsP.h"

static int test_fail = 0;

static void check_err(const char *label, errcode_t got, errcode_t expected)
{
	if (got != expected) {
		printf("FAIL [%s]: got errcode %ld (%s), expected %ld (%s)\n",
		       label, (long) got, error_message(got),
		       (long) expected, error_message(expected));
		test_fail++;
	}
}

static void check_u64(const char *label, unsigned long long got,
		      unsigned long long expected)
{
	if (got != expected) {
		printf("FAIL [%s]: got 0x%llx, expected 0x%llx\n",
		       label, got, expected);
		test_fail++;
	}
}

/*
 * Suite 1: ext2fs_dirhash() version probing, unsupported version error,
 * seed handling, multi-block names, and signed vs. unsigned char variants
 */
static void test_dirhash_versions_and_seeds(void)
{
	static const int versions[6] = {
		EXT2_HASH_LEGACY,
		EXT2_HASH_HALF_MD4,
		EXT2_HASH_TEA,
		EXT2_HASH_LEGACY_UNSIGNED,
		EXT2_HASH_HALF_MD4_UNSIGNED,
		EXT2_HASH_TEA_UNSIGNED,
	};
	static const __u32 zero_seed[4] = { 0, 0, 0, 0 };
	static const __u32 custom_seed[4] = {
		0x11223344, 0x55667788, 0x99aabbcc, 0xddeeff00
	};
	const char	*ascii_short = "hello_ext4.c";
	const char	*ascii_long =
		"0123456789abcdef0123456789abcdef0123456789abcdef"
		"0123456789abcdef_tail70";
	const char	high_bytes[] = "\xc3\xa9\xfe\x81\x90\xff";
	ext2_dirhash_t	h1 = 0, m1 = 0, h2 = 0, m2 = 0, h3 = 0, m3 = 0;
	errcode_t	retval;
	int		i;

	/* Version support probe (name == NULL, len == 0) */
	for (i = 0; i < 6; i++) {
		retval = ext2fs_dirhash(versions[i], NULL, 0, NULL, &h1, &m1);
		check_err("probe_supported_version", retval, 0);
		check_u64("probe_lsb_cleared", h1 & 1, 0);
	}

	/* Unsupported hash versions return EXT2_ET_DIRHASH_UNSUPP */
	h1 = 0xdeadbeef;
	retval = ext2fs_dirhash(-1, "a", 1, NULL, &h1, &m1);
	check_err("unsupp_neg1", retval, EXT2_ET_DIRHASH_UNSUPP);
	check_u64("unsupp_neg1_zero", h1, 0);

	h1 = 0xdeadbeef;
	retval = ext2fs_dirhash(99, "a", 1, NULL, &h1, NULL);
	check_err("unsupp_99", retval, EXT2_ET_DIRHASH_UNSUPP);
	check_u64("unsupp_99_zero", h1, 0);

	/* NULL ret_minor_hash is allowed across all versions */
	for (i = 0; i < 6; i++) {
		retval = ext2fs_dirhash(versions[i], ascii_short,
					strlen(ascii_short), NULL, &h1, NULL);
		check_err("null_minor_ptr", retval, 0);
		check_u64("hash_lsb_even", h1 & 1, 0);
	}

	/* Legacy hash produces minor_hash == 0 and ignores seed */
	m1 = 0x12345678;
	retval = ext2fs_dirhash(EXT2_HASH_LEGACY, ascii_short,
				strlen(ascii_short), custom_seed, &h1, &m1);
	check_err("legacy_seeded", retval, 0);
	check_u64("legacy_minor_zero", m1, 0);
	retval = ext2fs_dirhash(EXT2_HASH_LEGACY, ascii_short,
				strlen(ascii_short), NULL, &h2, &m2);
	check_err("legacy_unseeded", retval, 0);
	check_u64("legacy_seed_ignored", h1, h2);

	/* All-zero seed uses same default state as NULL seed; custom seed differs */
	retval = ext2fs_dirhash(EXT2_HASH_HALF_MD4, ascii_long,
				strlen(ascii_long), NULL, &h1, &m1);
	check_err("md4_null_seed", retval, 0);
	retval = ext2fs_dirhash(EXT2_HASH_HALF_MD4, ascii_long,
				strlen(ascii_long), zero_seed, &h2, &m2);
	check_err("md4_zero_seed", retval, 0);
	check_u64("md4_zero_eq_null_h", h1, h2);
	check_u64("md4_zero_eq_null_m", m1, m2);

	retval = ext2fs_dirhash(EXT2_HASH_HALF_MD4, ascii_long,
				strlen(ascii_long), custom_seed, &h3, &m3);
	check_err("md4_custom_seed", retval, 0);
	check_u64("md4_custom_differs", (h1 != h3) || (m1 != m3), 1);

	retval = ext2fs_dirhash(EXT2_HASH_TEA, ascii_long,
				strlen(ascii_long), NULL, &h1, &m1);
	check_err("tea_null_seed", retval, 0);
	retval = ext2fs_dirhash(EXT2_HASH_TEA, ascii_long,
				strlen(ascii_long), zero_seed, &h2, &m2);
	check_err("tea_zero_seed", retval, 0);
	check_u64("tea_zero_eq_null_h", h1, h2);
	check_u64("tea_zero_eq_null_m", m1, m2);
	retval = ext2fs_dirhash(EXT2_HASH_TEA, ascii_long,
				strlen(ascii_long), custom_seed, &h3, &m3);
	check_err("tea_custom_seed", retval, 0);
	check_u64("tea_custom_differs", (h1 != h3) || (m1 != m3), 1);

	/* Signed vs. unsigned variants match on 7-bit ASCII, differ on >= 0x80 */
	for (i = 0; i < 3; i++) {
		retval = ext2fs_dirhash(versions[i], ascii_short,
					strlen(ascii_short), NULL, &h1, &m1);
		check_err("ascii_signed", retval, 0);
		retval = ext2fs_dirhash(versions[i + 3], ascii_short,
					strlen(ascii_short), NULL, &h2, &m2);
		check_err("ascii_unsigned", retval, 0);
		check_u64("ascii_signed_eq_unsigned_h", h1, h2);
		check_u64("ascii_signed_eq_unsigned_m", m1, m2);

		retval = ext2fs_dirhash(versions[i], high_bytes,
					sizeof(high_bytes) - 1, NULL,
					&h1, &m1);
		check_err("hibyte_signed", retval, 0);
		retval = ext2fs_dirhash(versions[i + 3], high_bytes,
					sizeof(high_bytes) - 1, NULL,
					&h2, &m2);
		check_err("hibyte_unsigned", retval, 0);
		check_u64("hibyte_signed_ne_unsigned",
			  (h1 != h2) || (m1 != m2), 1);
	}

	printf("test_dirhash_versions_and_seeds: completed\n");
}

/*
 * Suite 2: NLS UTF-8 12.1 table loading and ext2fs_check_encoded_name()
 */
static void test_nls_utf8_validation(void)
{
	const struct ext2fs_nls_table	*tbl;
	char				*pos = NULL;
	char				valid_ascii[] = "normal_file-1.txt";
	char				valid_utf8[] =
		"Caf\xc3\xa9-\xe6\x97\xa5\xf0\x9f\x98\x80";
	char				bad_cont[] = "abc\x80xyz";
	char				bad_trunc[] = "ok\xe2\x82";
	char				bad_overlong[] = "\xc0\xaf";
	char				bad_surrogate[] = "\xed\xa0\x80";
	char				bad_maxcp[] = "\xf4\x90\x80\x80";
	int				rc;

	check_u64("load_nls_0_null",
		  ext2fs_load_nls_table(0) == NULL, 1);
	check_u64("load_nls_999_null",
		  ext2fs_load_nls_table(999) == NULL, 1);

	tbl = ext2fs_load_nls_table(EXT4_ENC_UTF8_12_1);
	check_u64("load_utf8_12_1_nonnull", tbl != NULL, 1);

	rc = ext2fs_check_encoded_name(tbl, valid_ascii,
				       strlen(valid_ascii), &pos);
	check_u64("validate_ascii_ok", rc, 0);

	rc = ext2fs_check_encoded_name(tbl, valid_utf8,
				       strlen(valid_utf8), &pos);
	check_u64("validate_multibyte_utf8_ok", rc, 0);

	rc = ext2fs_check_encoded_name(tbl, valid_ascii, 0, &pos);
	check_u64("validate_empty_ok", rc, 0);

	/* Lone continuation byte at offset 3 */
	pos = NULL;
	rc = ext2fs_check_encoded_name(tbl, bad_cont, strlen(bad_cont), &pos);
	check_u64("validate_bad_cont_rc", rc, 1);
	check_u64("validate_bad_cont_pos", pos == bad_cont + 3, 1);

	/* Truncated 3-byte sequence at offset 2 */
	pos = NULL;
	rc = ext2fs_check_encoded_name(tbl, bad_trunc, 4, &pos);
	check_u64("validate_bad_trunc_rc", rc, 1);
	check_u64("validate_bad_trunc_pos", pos == bad_trunc + 2, 1);

	/* Overlong 2-byte sequence */
	pos = NULL;
	rc = ext2fs_check_encoded_name(tbl, bad_overlong, 2, &pos);
	check_u64("validate_overlong_rc", rc, 1);
	check_u64("validate_overlong_pos", pos == bad_overlong, 1);

	/* UTF-16 surrogate U+D800 */
	pos = NULL;
	rc = ext2fs_check_encoded_name(tbl, bad_surrogate, 3, &pos);
	check_u64("validate_surrogate_rc", rc, 1);
	check_u64("validate_surrogate_pos", pos == bad_surrogate, 1);

	/* Code point U+110000 (> U+10FFFF) */
	pos = NULL;
	rc = ext2fs_check_encoded_name(tbl, bad_maxcp, 4, &pos);
	check_u64("validate_maxcp_rc", rc, 1);
	check_u64("validate_maxcp_pos", pos == bad_maxcp, 1);

	/* Unknown/future or zero table->version handled safely (no OOB) */
	{
		struct ext2fs_nls_table future_ver = {
			.version = (99 << 16),
			.ops = tbl->ops,
		};
		struct ext2fs_nls_table zero_ver = {
			.version = 0,
			.ops = tbl->ops,
		};
		unsigned char dest[16];

		pos = NULL;
		rc = ext2fs_check_encoded_name(&future_ver, valid_ascii,
					       strlen(valid_ascii), &pos);
		check_u64("validate_future_version_rc", rc == -1, 1);
		rc = future_ver.ops->casefold(&future_ver,
					      (const unsigned char *) "abc", 3,
					      dest, sizeof(dest));
		check_u64("casefold_future_version_einval", rc == -EINVAL, 1);
		rc = future_ver.ops->casefold(&future_ver,
					      (const unsigned char *) "abc", 3,
					      dest, 2);
		check_u64("casefold_future_short_enametoolong",
			  rc == -ENAMETOOLONG, 1);

		rc = ext2fs_check_encoded_name(&zero_ver, valid_ascii,
					       strlen(valid_ascii), &pos);
		check_u64("validate_zero_version_ok", rc, 0);
	}

	printf("test_nls_utf8_validation: completed\n");
}

/*
 * Suite 3: ext2fs_casefold_cmp() (ASCII/full casefolding, NFC vs NFD,
 * combining class canonical ordering, Hangul decomposition, and
 * Default_Ignorable_Code_Point removal)
 */
static void test_nls_utf8_casefold_cmp(void)
{
	const struct ext2fs_nls_table *tbl;
	const unsigned char *s1, *s2;

	tbl = ext2fs_load_nls_table(EXT4_ENC_UTF8_12_1);

	/* ASCII casefolding */
	s1 = (const unsigned char *) "ReadMe.TXT";
	s2 = (const unsigned char *) "readme.txt";
	check_u64("cf_ascii_eq",
		  ext2fs_casefold_cmp(tbl, s1, 10, s2, 10) == 0, 1);

	/* Full casefolding: German sharp S (U+00DF) == "SS" */
	s1 = (const unsigned char *) "stra\xc3\x9f" "e";
	s2 = (const unsigned char *) "STRASSE";
	check_u64("cf_eszett_eq_ss",
		  ext2fs_casefold_cmp(tbl, s1, strlen((const char *) s1),
				      s2, strlen((const char *) s2)) == 0, 1);

	/* NFC precomposed U+00C9 ('E' + acute) vs NFD 'e' + U+0301 */
	s1 = (const unsigned char *) "Caf\xc3\x89";
	s2 = (const unsigned char *) "cafe\xcc\x81";
	check_u64("cf_nfc_vs_nfd",
		  ext2fs_casefold_cmp(tbl, s1, strlen((const char *) s1),
				      s2, strlen((const char *) s2)) == 0, 1);

	/* Canonical Combining Class reordering: acute (230) + cedilla (202) */
	s1 = (const unsigned char *) "a\xcc\x81\xcc\xa7";
	s2 = (const unsigned char *) "A\xcc\xa7\xcc\x81";
	check_u64("cf_ccc_reorder",
		  ext2fs_casefold_cmp(tbl, s1, strlen((const char *) s1),
				      s2, strlen((const char *) s2)) == 0, 1);

	/* Algorithmic Hangul syllable decomposition: LV (U+AC00) and LVT (U+D55C) */
	s1 = (const unsigned char *) "\xea\xb0\x80\xed\x95\x9c";
	s2 = (const unsigned char *)
		"\xe1\x84\x80\xe1\x85\xa1\xe1\x84\x92\xe1\x85\xa1\xe1\x86\xab";
	check_u64("cf_hangul_lv_lvt",
		  ext2fs_casefold_cmp(tbl, s1, strlen((const char *) s1),
				      s2, strlen((const char *) s2)) == 0, 1);

	/* Default_Ignorable_Code_Point (U+00AD SOFT HYPHEN) stripped */
	s1 = (const unsigned char *) "co\xc2\xadop";
	s2 = (const unsigned char *) "COOP";
	check_u64("cf_default_ignorable_stripped",
		  ext2fs_casefold_cmp(tbl, s1, strlen((const char *) s1),
				      s2, strlen((const char *) s2)) == 0, 1);

	/* Distinct strings compare non-zero; invalid UTF-8 returns < 0 */
	s1 = (const unsigned char *) "alpha";
	s2 = (const unsigned char *) "beta";
	check_u64("cf_distinct_ne",
		  ext2fs_casefold_cmp(tbl, s1, 5, s2, 4) != 0, 1);

	s1 = (const unsigned char *) "\x80invalid";
	check_u64("cf_invalid_neg",
		  ext2fs_casefold_cmp(tbl, s1, 8, s2, 4) < 0, 1);

	printf("test_nls_utf8_casefold_cmp: completed\n");
}

/*
 * Suite 4: ext2fs_dirhash2() casefolded hashing and opaque fallback
 */
static void test_dirhash2_casefold(void)
{
	const struct ext2fs_nls_table	*tbl;
	const char			*nfc = "Caf\xc3\x89";
	const char			*nfd = "cafe\xcc\x81";
	const char			*eszett = "stra\xc3\x9f" "e";
	const char			*strasse = "STRASSE";
	ext2_dirhash_t			h1 = 0, m1 = 0, h2 = 0, m2 = 0;
	errcode_t			retval;

	tbl = ext2fs_load_nls_table(EXT4_ENC_UTF8_12_1);

	/* Without EXT4_CASEFOLD_FL, NFC and NFD produce different hashes */
	retval = ext2fs_dirhash2(EXT2_HASH_HALF_MD4, nfc, strlen(nfc),
				 tbl, 0, NULL, &h1, &m1);
	check_err("dirhash2_nocf_nfc", retval, 0);
	retval = ext2fs_dirhash2(EXT2_HASH_HALF_MD4, nfd, strlen(nfd),
				 tbl, 0, NULL, &h2, &m2);
	check_err("dirhash2_nocf_nfd", retval, 0);
	check_u64("dirhash2_nocf_differs", (h1 != h2) || (m1 != m2), 1);

	/* With EXT4_CASEFOLD_FL, NFC and NFD produce identical hashes */
	retval = ext2fs_dirhash2(EXT2_HASH_HALF_MD4, nfc, strlen(nfc),
				 tbl, EXT4_CASEFOLD_FL, NULL, &h1, &m1);
	check_err("dirhash2_cf_nfc", retval, 0);
	retval = ext2fs_dirhash2(EXT2_HASH_HALF_MD4, nfd, strlen(nfd),
				 tbl, EXT4_CASEFOLD_FL, NULL, &h2, &m2);
	check_err("dirhash2_cf_nfd", retval, 0);
	check_u64("dirhash2_cf_nfc_eq_nfd_h", h1, h2);
	check_u64("dirhash2_cf_nfc_eq_nfd_m", m1, m2);

	/* German sharp S and "STRASSE" hash identically under TEA as well */
	retval = ext2fs_dirhash2(EXT2_HASH_TEA, eszett, strlen(eszett),
				 tbl, EXT4_CASEFOLD_FL, NULL, &h1, &m1);
	check_err("dirhash2_cf_eszett", retval, 0);
	retval = ext2fs_dirhash2(EXT2_HASH_TEA, strasse, strlen(strasse),
				 tbl, EXT4_CASEFOLD_FL, NULL, &h2, &m2);
	check_err("dirhash2_cf_strasse", retval, 0);
	check_u64("dirhash2_cf_eszett_eq_h", h1, h2);
	check_u64("dirhash2_cf_eszett_eq_m", m1, m2);

	/*
	 * All invalid UTF-8 forms (leading continuation byte, trailing 0xff,
	 * overlong 2-byte, UTF-16 surrogate, truncated multibyte, > U+10FFFF)
	 * must return -EINVAL from utf8_casefold() so ext2fs_dirhash2()
	 * falls back to opaque byte hashing (matching Linux kernel ext4).
	 */
	{
		static const char * const invalid_names[] = {
			"\x80opaque",
			"foo\xff" "bar",
			"\xc0\xaf",
			"\xed\xa0\x80",
			"ok\xe2\x82",
			"\xf4\x90\x80\x80",
		};
		size_t k;

		for (k = 0; k < sizeof(invalid_names) / sizeof(invalid_names[0]);
		     k++) {
			const char *bad = invalid_names[k];
			int blen = (int) strlen(bad);

			retval = ext2fs_dirhash2(EXT2_HASH_HALF_MD4_UNSIGNED,
						 bad, blen, tbl,
						 EXT4_CASEFOLD_FL, NULL,
						 &h1, &m1);
			check_err("dirhash2_opaque_fallback", retval, 0);
			retval = ext2fs_dirhash(EXT2_HASH_HALF_MD4_UNSIGNED,
						bad, blen, NULL, &h2, &m2);
			check_err("dirhash_raw_bad", retval, 0);
			check_u64("dirhash2_fallback_matches_raw_h", h1, h2);
			check_u64("dirhash2_fallback_matches_raw_m", m1, m2);
		}
	}

	printf("test_dirhash2_casefold: completed\n");
}

int main(int argc EXT2FS_ATTR((unused)), char *argv[] EXT2FS_ATTR((unused)))
{
	add_error_table(&et_ext2_error_table);

	test_dirhash_versions_and_seeds();
	test_nls_utf8_validation();
	test_nls_utf8_casefold_cmp();
	test_dirhash2_casefold();

	if (test_fail == 0)
		printf("libext2fs dirhash and nls_utf8 unit tests passed!\n");
	else
		printf("libext2fs dirhash and nls_utf8 unit tests FAILED (%d errors)\n",
		       test_fail);

	return test_fail ? 1 : 0;
}
