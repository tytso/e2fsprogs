#ifndef _JFS_USER_H
#define _JFS_USER_H

typedef unsigned short kdev_t;

#include <ext2fs/kernel-jbd.h>

#define JSB_HAS_INCOMPAT_FEATURE(jsb, mask)				\
	((jsb)->s_header.h_blocktype == ext2fs_cpu_to_be32(JFS_SUPERBLOCK_V2) &&	\
	 ((jsb)->s_feature_incompat & ext2fs_cpu_to_be32((mask))))
static inline size_t journal_super_tag_bytes(journal_superblock_t *jsb)
{
	size_t sz;

	if (JSB_HAS_INCOMPAT_FEATURE(jsb, JFS_FEATURE_INCOMPAT_CSUM_V3))
		return sizeof(journal_block_tag3_t);

	sz = sizeof(journal_block_tag_t);

	if (JSB_HAS_INCOMPAT_FEATURE(jsb, JFS_FEATURE_INCOMPAT_CSUM_V2))
		sz += sizeof(__u16);

	if (JSB_HAS_INCOMPAT_FEATURE(jsb, JFS_FEATURE_INCOMPAT_64BIT))
		return sz;

	return sz - sizeof(__u32);
}

#endif /* _JFS_USER_H */
