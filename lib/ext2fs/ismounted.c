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
#include <string.h>

#if EXT2_FLAT_INCLUDES
#include "ext2_fs.h"
#else
#include <linux/ext2_fs.h>
#endif

#include "ext2fs.h"

#ifdef HAVE_MNTENT_H
/*
 * Helper function which checks a file in /etc/mtab format to see if a
 * filesystem is mounted.  Returns an error if the file doesn't exist
 * or can't be opened.  
 */
static errcode_t check_mntent_file(const char *mtab_file, const char *file, 
				   int *mount_flags, char *mtpt, int mtlen)
{
	FILE * f;
	struct mntent * mnt;
	int	fd;

	*mount_flags = 0;
	if ((f = setmntent (mtab_file, "r")) == NULL)
		return errno;
	while ((mnt = getmntent (f)) != NULL)
		if (strcmp(file, mnt->mnt_fsname) == 0)
			break;
	endmntent (f);
	if (mnt == 0)
		return 0;
	*mount_flags = EXT2_MF_MOUNTED;
	
	/* Check to see if the ro option is set */
	if (hasmntopt(mnt, MNTOPT_RO))
		*mount_flags |= EXT2_MF_READONLY;

	/*
	 * Check to see if we're referring to the root filesystem.
	 * If so, do a manual check to see if we can open /etc/mtab
	 * read/write, since if the root is mounted read/only,
	 * /etc/mtab may not be accurate.
	 */
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

static errcode_t check_mntent(const char *file, int *mount_flags,
			      char *mtpt, int mtlen)
{
	errcode_t	retval;

#ifdef __linux__
	retval = check_mntent_file("/proc/mounts", file, mount_flags,
				   mtpt, mtlen);
	if (retval == 0)
		return 0;
#endif
	retval = check_mntent_file(MOUNTED, file, mount_flags, mtpt, mtlen);
	return retval;
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

#ifdef DEBUG
int main(int argc, char **argv)
{
	blk_t	blocks;
	int	retval, mount_flags;
	
	if (argc < 2) {
		fprintf(stderr, "Usage: %s device\n", argv[0]);
		exit(1);
	}

	retval = ext2fs_check_if_mounted(argv[1], &mount_flags);
	if (retval) {
		com_err(argv[0], retval,
			"while calling ext2fs_check_if_mounted");
		exit(1);
	}
	printf("Device %s reports flags %02x\n", argv[1], mount_flags);
	if (mount_flags & EXT2_MF_MOUNTED)
		printf("\t%s is mounted.\n");
	
	if (mount_flags & EXT2_MF_READONLY)
		printf("\t%s is read-only.\n");
	
	if (mount_flags & EXT2_MF_ISROOT)
		printf("\t%s is the root filesystem.\n");
	
	exit(0);
}
#endif
