/*
 * dl_archive.h --- allow compilation without archive.h present
 *
 * Copyright (C) 2023 Johannes Schauer Marin Rodrigues <josch@debian.org>
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU library
 * General Public License, version 2.
 * %End-Header%
 */

#include "config.h"

/* If archive.h was found, include it as usual. To support easier
 * bootstrapping, also allow compilation without archive.h present by
 * declaring the necessary opaque structs and preprocessor definitions. */
#ifdef HAVE_ARCHIVE_H
#include <archive.h>
#include <archive_entry.h>
#else
struct archive;
struct archive_entry;
#define	ARCHIVE_EOF	  1	/* Found end of archive. */
#define	ARCHIVE_OK	  0	/* Operation was successful. */
#include <unistd.h>  /* ssize_t */
typedef ssize_t la_ssize_t;
#endif /* HAVE_ARCHIVE_H */

#include <libgen.h>

static const char *(*dl_archive_entry_hardlink)(struct archive_entry *);
static const char *(*dl_archive_entry_pathname)(struct archive_entry *);
static const struct stat *(*dl_archive_entry_stat)(struct archive_entry *);
static const char *(*dl_archive_entry_symlink)(struct archive_entry *);
static int (*dl_archive_entry_xattr_count)(struct archive_entry *);
static int (*dl_archive_entry_xattr_next)(struct archive_entry *, const char **,
					  const void **, size_t *);
static int (*dl_archive_entry_xattr_reset)(struct archive_entry *);
static const char *(*dl_archive_error_string)(struct archive *);
static int (*dl_archive_read_close)(struct archive *);
static la_ssize_t (*dl_archive_read_data)(struct archive *, void *, size_t);
static int (*dl_archive_read_free)(struct archive *);
static struct archive *(*dl_archive_read_new)(void);
static int (*dl_archive_read_next_header)(struct archive *,
					  struct archive_entry **);
static int (*dl_archive_read_open_filename)(struct archive *,
					    const char *filename, size_t);
static int (*dl_archive_read_support_filter_all)(struct archive *);
static int (*dl_archive_read_support_format_all)(struct archive *);

#ifdef CONFIG_DLOPEN_LIBARCHIVE
#include <dlfcn.h>

static void *libarchive_handle;

#if defined(__FreeBSD__)
#define LIBARCHIVE_SO "libarchive.so.7"
#elif defined(__APPLE__)
#define LIBARCHIVE_SO "libarchive.13.dylib"
#else
#define LIBARCHIVE_SO "libarchive.so.13"
#endif

static int libarchive_available(void)
{
	if (!libarchive_handle) {
		libarchive_handle = dlopen(LIBARCHIVE_SO, RTLD_NOW);
		if (!libarchive_handle)
			return 0;

		dl_archive_entry_hardlink =
			(const char *(*)(struct archive_entry *))dlsym(
				libarchive_handle, "archive_entry_hardlink");
		if (!dl_archive_entry_hardlink)
			return 0;
		dl_archive_entry_pathname =
			(const char *(*)(struct archive_entry *))dlsym(
				libarchive_handle, "archive_entry_pathname");
		if (!dl_archive_entry_pathname)
			return 0;
		dl_archive_entry_stat =
			(const struct stat *(*)(struct archive_entry *))dlsym(
				libarchive_handle, "archive_entry_stat");
		if (!dl_archive_entry_stat)
			return 0;
		dl_archive_entry_symlink =
			(const char *(*)(struct archive_entry *))dlsym(
				libarchive_handle, "archive_entry_symlink");
		if (!dl_archive_entry_symlink)
			return 0;
		dl_archive_entry_xattr_count =
			(int (*)(struct archive_entry *))dlsym(
				libarchive_handle, "archive_entry_xattr_count");
		if (!dl_archive_entry_xattr_count)
			return 0;
		dl_archive_entry_xattr_next = (int (*)(
			struct archive_entry *, const char **, const void **,
			size_t *))dlsym(libarchive_handle,
					"archive_entry_xattr_next");
		if (!dl_archive_entry_xattr_next)
			return 0;
		dl_archive_entry_xattr_reset =
			(int (*)(struct archive_entry *))dlsym(
				libarchive_handle, "archive_entry_xattr_reset");
		if (!dl_archive_entry_xattr_reset)
			return 0;
		dl_archive_error_string =
			(const char *(*)(struct archive *))dlsym(
				libarchive_handle, "archive_error_string");
		if (!dl_archive_error_string)
			return 0;
		dl_archive_read_close = (int (*)(struct archive *))dlsym(
			libarchive_handle, "archive_read_close");
		if (!dl_archive_read_close)
			return 0;
		dl_archive_read_data =
			(la_ssize_t(*)(struct archive *, void *, size_t))dlsym(
				libarchive_handle, "archive_read_data");
		if (!dl_archive_read_data)
			return 0;
		dl_archive_read_free = (int (*)(struct archive *))dlsym(
			libarchive_handle, "archive_read_free");
		if (!dl_archive_read_free)
			return 0;
		dl_archive_read_new = (struct archive * (*)(void))
			dlsym(libarchive_handle, "archive_read_new");
		if (!dl_archive_read_new)
			return 0;
		dl_archive_read_next_header = (int (*)(struct archive *,
						       struct archive_entry **))
			dlsym(libarchive_handle, "archive_read_next_header");
		if (!dl_archive_read_next_header)
			return 0;
		dl_archive_read_open_filename =
			(int (*)(struct archive *, const char *filename,
				 size_t))dlsym(libarchive_handle,
					       "archive_read_open_filename");
		if (!dl_archive_read_open_filename)
			return 0;
		dl_archive_read_support_filter_all =
			(int (*)(struct archive *))dlsym(
				libarchive_handle,
				"archive_read_support_filter_all");
		if (!dl_archive_read_support_filter_all)
			return 0;
		dl_archive_read_support_format_all =
			(int (*)(struct archive *))dlsym(
				libarchive_handle,
				"archive_read_support_format_all");
		if (!dl_archive_read_support_format_all)
			return 0;
	}

	return 1;
}
#else
static int libarchive_available(void)
{
	dl_archive_entry_hardlink = archive_entry_hardlink;
	dl_archive_entry_pathname = archive_entry_pathname;
	dl_archive_entry_stat = archive_entry_stat;
	dl_archive_entry_symlink = archive_entry_symlink;
	dl_archive_entry_xattr_count = archive_entry_xattr_count;
	dl_archive_entry_xattr_next = archive_entry_xattr_next;
	dl_archive_entry_xattr_reset = archive_entry_xattr_reset;
	dl_archive_error_string = archive_error_string;
	dl_archive_read_close = archive_read_close;
	dl_archive_read_data = archive_read_data;
	dl_archive_read_free = archive_read_free;
	dl_archive_read_new = archive_read_new;
	dl_archive_read_next_header = archive_read_next_header;
	dl_archive_read_open_filename = archive_read_open_filename;
	dl_archive_read_support_filter_all = archive_read_support_filter_all;
	dl_archive_read_support_format_all = archive_read_support_format_all;

	return 1;
}
#endif /* CONFIG_DLOPEN_LIBARCHIVE */
