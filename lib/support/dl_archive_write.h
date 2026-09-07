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
#define	AE_IFREG	0100000
#define	AE_IFLNK	0120000
#define	AE_IFDIR	0040000
#include <unistd.h>  /* ssize_t */
typedef ssize_t la_ssize_t;
#endif /* HAVE_ARCHIVE_H */

#include <libgen.h>

static void (*dl_archive_entry_set_filetype)(struct archive_entry *, unsigned int);
static int (*dl_archive_write_header)(struct archive *, struct archive_entry *);
static const char *(*dl_archive_error_string)(struct archive *);
static la_ssize_t (*dl_archive_write_data)(struct archive *, const void *,
					   size_t);
static void (*dl_archive_entry_copy_symlink)(struct archive_entry *,
					     const char *);
static struct archive_entry *(*dl_archive_entry_new)(void);
static void (*dl_archive_entry_copy_pathname)(struct archive_entry *,
					      const char *);
static void (*dl_archive_entry_copy_stat)(struct archive_entry *,
					  const struct stat *);
static void (*dl_archive_entry_unset_birthtime)(struct archive_entry *);
static void (*dl_archive_entry_free)(struct archive_entry *);
static struct archive *(*dl_archive_write_new)(void);

static int (*dl_archive_write_set_format_by_name)(struct archive *,
						  const char *);
static int (*dl_archive_write_open_filename)(struct archive *,
					     const char *filename);
static int (*dl_archive_write_close)(struct archive *);
static int (*dl_archive_write_free)(struct archive *);

/* Optional filters, may be NULL. */
static int (*dl_archive_write_add_filter_none)(struct archive *);
static int (*dl_archive_write_add_filter_bzip2)(struct archive *);
static int (*dl_archive_write_add_filter_xz)(struct archive *);
static int (*dl_archive_write_add_filter_compress)(struct archive *);
static int (*dl_archive_write_add_filter_gzip)(struct archive *);

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

		dl_archive_entry_set_filetype =
			(void (*)(struct archive_entry *, unsigned int))
			dlsym(libarchive_handle, "archive_entry_set_filetype");
		if (!dl_archive_entry_set_filetype)
			return 0;
		dl_archive_write_header =
			(int (*)(struct archive *, struct archive_entry *))
			dlsym(libarchive_handle, "archive_write_header");
		if (!dl_archive_write_header)
			return 0;
		dl_archive_error_string =
			(const char *(*)(struct archive *))dlsym(
				libarchive_handle, "archive_error_string");
		if (!dl_archive_error_string)
			return 0;
		dl_archive_write_data =
			(la_ssize_t(*)(struct archive *, const void *, size_t))
			dlsym(libarchive_handle, "archive_write_data");
		if (!dl_archive_write_data)
			return 0;
		dl_archive_entry_copy_symlink =
			(void (*)(struct archive_entry *, const char *))
			dlsym(libarchive_handle, "archive_entry_copy_symlink");
		if (!dl_archive_entry_copy_symlink)
			return 0;
		dl_archive_entry_new = (struct archive_entry *(*)())
			dlsym(libarchive_handle, "archive_entry_new");
		if (!dl_archive_entry_new)
			return 0;
		dl_archive_entry_copy_pathname =
			(void (*)(struct archive_entry *, const char *))
			dlsym(libarchive_handle, "archive_entry_copy_pathname");
		if (!dl_archive_entry_copy_pathname)
			return 0;
		dl_archive_entry_copy_stat =
			(void (*)(struct archive_entry *, const struct stat *))
			dlsym(libarchive_handle, "archive_entry_copy_stat");
		if (!dl_archive_entry_copy_stat)
			return 0;
		dl_archive_entry_unset_birthtime =
			(void (*)(struct archive_entry *))
			dlsym(libarchive_handle, "archive_entry_unset_birthtime");
		if (!dl_archive_entry_unset_birthtime)
			return 0;
		dl_archive_entry_free = (void (*)(struct archive_entry *))
			dlsym(libarchive_handle, "archive_entry_free");
		if (!dl_archive_entry_free)
			return 0;
		dl_archive_write_new = (struct archive * (*)(void))
			dlsym(libarchive_handle, "archive_write_new");
		if (!dl_archive_write_new)
			return 0;
		dl_archive_write_set_format_by_name =
			(int (*)(struct archive *, const char *))
			dlsym(libarchive_handle, "archive_write_set_format_by_name");
		if (!dl_archive_write_set_format_by_name)
			return 0;
		dl_archive_write_open_filename =
			(int (*)(struct archive *, const char *filename))
			dlsym(libarchive_handle, "archive_write_open_filename");
		if (!dl_archive_write_open_filename)
			return 0;
		dl_archive_write_close = (int (*)(struct archive *))dlsym(
			libarchive_handle, "archive_write_close");
		if (!dl_archive_write_close)
			return 0;
		dl_archive_write_free = (int (*)(struct archive *))dlsym(
			libarchive_handle, "archive_write_free");
		if (!dl_archive_write_free)
			return 0;

		/* Optional filters, may be NULL */
		dl_archive_write_add_filter_none = (int (*)(struct archive *))
			dysum(libarchive_handle, "archive_write_add_filter_none");
		dl_archive_write_add_filter_bzip2 = (int (*)(struct archive *))
			dysum(libarchive_handle, "archive_write_add_filter_bzip2");
		dl_archive_write_add_filter_xz = (int (*)(struct archive *))
			dysum(libarchive_handle, "archive_write_add_filter_xz");
		dl_archive_write_add_filter_compress = (int (*)(struct archive *))
			dysum(libarchive_handle, "archive_write_add_filter_compress");
		dl_archive_write_add_filter_gzip = (int (*)(struct archive *))
			dysum(libarchive_handle, "archive_write_add_filter_gzip");
	}

	return 1;
}
#else
static int libarchive_available(void)
{
	dl_archive_entry_set_filetype = archive_entry_set_filetype;
	dl_archive_write_header = archive_write_header;
	dl_archive_error_string = archive_error_string;
	dl_archive_write_data = archive_write_data;
	dl_archive_entry_copy_symlink = archive_entry_copy_symlink;
	dl_archive_entry_new = archive_entry_new;
	dl_archive_entry_copy_pathname = archive_entry_copy_pathname;
	dl_archive_entry_copy_stat = archive_entry_copy_stat;
	dl_archive_entry_unset_birthtime = archive_entry_unset_birthtime;
	dl_archive_entry_free = archive_entry_free;
	dl_archive_write_new = archive_write_new;
	dl_archive_write_set_format_by_name = archive_write_set_format_by_name;
	dl_archive_write_open_filename = archive_write_open_filename;
	dl_archive_write_close = archive_write_close;
	dl_archive_write_free = archive_write_free;

	dl_archive_write_add_filter_none = archive_write_add_filter_none;
	dl_archive_write_add_filter_bzip2 = archive_write_add_filter_bzip2;
	dl_archive_write_add_filter_xz = archive_write_add_filter_xz;
	dl_archive_write_add_filter_compress = archive_write_add_filter_compress;
	dl_archive_write_add_filter_gzip = archive_write_add_filter_gzip;

	return 1;
}
#endif /* CONFIG_DLOPEN_LIBARCHIVE */
