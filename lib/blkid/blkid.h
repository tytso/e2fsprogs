/*
 * blkid.h - Interface for libblkid, a library to identify block devices
 *
 * Copyright (C) 2001 Andreas Dilger
 * Copyright (C) 2003 Theodore Ts'o
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 * %End-Header%
 */

#ifndef _BLKID_BLKID_H
#define _BLKID_BLKID_H

#ifdef __cplusplus
extern "C" {
#endif

#define BLKID_VERSION	"1.2.0"
#define BLKID_DATE	"22-Nov-2001"

typedef struct blkid_struct_dev *blkid_dev;
typedef struct blkid_struct_tag *blkid_tag;
typedef struct blkid_struct_cache *blkid_cache;

/* cache.c */
extern void blkid_free_cache(blkid_cache cache);

/* dev.c */
extern const char *blkid_devname_name(blkid_dev dev);

typedef struct blkid_struct_dev_iterate *blkid_dev_iterate;
extern blkid_dev_iterate blkid_dev_iterate_begin(blkid_cache cache);
extern int blkid_dev_next(blkid_dev_iterate iterate, blkid_dev *dev);
extern void blkid_dev_iterate_end(blkid_dev_iterate iterate);

/* devname.c */
extern int blkid_probe_all(blkid_cache *cache);
extern blkid_dev blkid_get_devname(blkid_cache cache, const char *devname);

/* read.c */
int blkid_read_cache(blkid_cache *cache, const char *filename);

/* resolve.c */
extern char *blkid_get_tagname_devname(blkid_cache cache, const char *tagname,
				       const char *devname);
extern char *blkid_get_token(blkid_cache cache, const char *token,
			     const char *value);

/* save.c */
extern int blkid_save_cache(blkid_cache cache, const char *filename);

/* tag.c */
typedef struct blkid_struct_tag_iterate *blkid_tag_iterate;
extern blkid_tag_iterate blkid_tag_iterate_begin(blkid_dev dev);
extern int blkid_tag_next(blkid_tag_iterate iterate,
			      const char **type, const char **value);
extern void blkid_tag_iterate_end(blkid_tag_iterate iterate);
extern blkid_dev blkid_find_dev_with_tag(blkid_cache cache,
					 const char *type,
					 const char *value);
extern int blkid_parse_tag_string(const char *token, char **ret_type,
				  char **ret_val);

#ifdef __cplusplus
}
#endif

#endif /* _BLKID_BLKID_H */
