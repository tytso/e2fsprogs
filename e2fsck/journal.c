/*
 * journal.c --- code for handling the "ext3" journal
 */

#include <errno.h>

#include "e2fsck.h"

/*
 * This is a list of directories to try.  The first element may get
 * replaced by a mktemp'ed generated temp directory if possible.
 */
static char *dirlist[] = { "/mnt", "/tmp", "/root", "/boot", 0 };

/*
 * This function attempts to mount and unmount an ext3 filesystem,
 * which is a cheap way to force the kernel to run the journal and
 * handle the recovery for us.
 */
int e2fsck_run_ext3_journal(const char *device)
{
	int	ret = 0;
	char	**cpp, *dir;
	char	template[] = "/tmp/ext3.XXXXXX";
	char	*tmpdir;

	/*
	 * First try to make a temporary directory.  This may fail if
	 * the root partition is still mounted read-only.
	 */
	tmpdir = mktemp(template);
	if (tmpdir) {
		ret = mkdir(template, 0700);
		if (ret)
			tmpdir = 0;
	}
	if (tmpdir) {
		ret = mount(device, tmpdir, "ext3", 0xC0ED, NULL);
		if (ret) {
			ret = errno;
			rmdir(tmpdir);
			return (ret);
		}
	} else {
		/*
		 * OK, creating a temporary directory didn't work.
		 * Let's try a list of possible temporary mountpoints.
		 */
		for (cpp = dirlist; dir = *cpp; cpp++) {
			ret = mount(device, dir, "ext3", 0xC0ED, NULL);
			if (ret == 0)
				break;
		}
		if (!dir)
			return errno;
	}

	/*
	 * Now that it mounted cleanly, the filesystem will have been
	 * recovered, so we can now unmount it.
	 */
	ret = umount(device);
	if (ret)
		return errno;
	/*
	 * Remove the temporary directory, if it was created.
	 */
	if (tmpdir)
		rmdir(tmpdir);
	return 0;
}

