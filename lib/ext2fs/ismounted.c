/*
 * ismounted.c --- Check to see if the filesystem was mounted
 * 
 * Copyright (C) 1995 Theodore Ts'o.
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 */

#include <stdio.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#if HAVE_ERRNO_H
#include <errno.h>
#endif
#include <fcntl.h>
#ifdef HAVE_LINUX_FD_H
#include <linux/fd.h>
#endif
#ifdef HAVE_MNTENT_H
#include <mntent.h>
#endif
#ifdef HAVE_GETMNTINFO
#include <paths.h>
#include <sys/param.h>
#include <sys/mount.h>
#endif /* HAVE_GETMNTINFO */

#if EXT2_FLAT_INCLUDES
#include "ext2_fs.h"
#else
#include <linux/ext2_fs.h>
#endif

#include "ext2fs.h"

#ifdef HAVE_MNTENT_H
/*
 * XXX we assume that /etc/mtab is located on the root filesystem, and
 * we only check to see if the mount is readonly for the root
 * filesystem.
 */
static errcode_t check_mntent(const char *file, int *mount_flags,
			      char *mtpt, int mtlen)
{
	FILE * f;
	struct mntent * mnt;
	int	fd;

	*mount_flags = 0;
	if ((f = setmntent (MOUNTED, "r")) == NULL)
		return errno;
	while ((mnt = getmntent (f)) != NULL)
		if (strcmp(file, mnt->mnt_fsname) == 0)
			break;
	endmntent (f);
	if (mnt == 0)
		return 0;
	*mount_flags = EXT2_MF_MOUNTED;
	
	if (!strcmp(mnt->mnt_dir, "/")) {
		*mount_flags |= EXT2_MF_ISROOT;
		fd = open(MOUNTED, O_RDWR);
		if (fd < 0) {
			if (errno == EROFS)
				*mount_flags |= EXT2_MF_READONLY;
		} else
			close(fd);
	}
	if (mtpt)
		strncpy(mtpt, mnt->mnt_dir, mtlen);
	return 0;
}
#endif

#ifdef HAVE_GETMNTINFO
static errcode_t check_getmntinfo(const char *file, int *mount_flags,
				  char *mtpt, int mtlen)
{
	struct statfs *mp;
        int    len, n;
        const  char   *s1;
	char	*s2;

        n = getmntinfo(&mp, MNT_NOWAIT);
        if (n == 0)
		return errno;

        len = sizeof(_PATH_DEV) - 1;
        s1 = file;
        if (strncmp(_PATH_DEV, s1, len) == 0)
                s1 += len;
 
	*mount_flags = 0;
        while (--n >= 0) {
                s2 = mp->f_mntfromname;
                if (strncmp(_PATH_DEV, s2, len) == 0) {
                        s2 += len - 1;
                        *s2 = 'r';
                }
                if (strcmp(s1, s2) == 0 || strcmp(s1, &s2[1]) == 0) {
			*mount_flags = EXT2_MF_MOUNTED;
			break;
		}
                ++mp;
	}
	if (mtpt)
		strncpy(mtpt, mp->f_mntonname, mtlen);
	return 0;
}
#endif /* HAVE_GETMNTINFO */

/*
 * ext2fs_check_mount_point() returns 1 if the device is mounted, 0
 * otherwise.  If mtpt is non-NULL, the directory where the device is
 * mounted is copied to where mtpt is pointing, up to mtlen
 * characters.
 */
#ifdef __TURBOC__
#pragma argsused
#endif
errcode_t ext2fs_check_mount_point(const char *device, int *mount_flags,
				  char *mtpt, int mtlen)
{
#ifdef HAVE_MNTENT_H
	return check_mntent(device, mount_flags, mtpt, mtlen);
#else 
#ifdef HAVE_GETMNTINFO
	return check_getmntinfo(device, mount_flags, mtpt, mtlen);
#else
	*mount_flags = 0;
	return 0;
#endif /* HAVE_GETMNTINFO */
#endif /* HAVE_MNTENT_H */
}

/*
 * ext2fs_check_if_mounted() sets the mount_flags EXT2_MF_MOUNTED and
 * EXT2_MF_READONLY
 * 
 */
#ifdef __TURBOC__
#pragma argsused
#endif
errcode_t ext2fs_check_if_mounted(const char *file, int *mount_flags)
{
#ifdef HAVE_MNTENT_H
	return check_mntent(file, mount_flags, NULL, 0);
#else 
#ifdef HAVE_GETMNTINFO
	return check_getmntinfo(file, mount_flags, NULL, 0);
#else
	*mount_flags = 0;
	return 0;
#endif /* HAVE_GETMNTINFO */
#endif /* HAVE_MNTENT_H */
}
