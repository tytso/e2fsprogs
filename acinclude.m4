# AX_CHECK_MOUNT_OPT: an autoconf macro to check for generic filesystem-
# agnostic 'mount' options.  Originally written by Nicholas Clark, and
# modified by Theodore Ts'o to use more modern autoconf macros.
#
# Looks for constants in sys/mount.h to predict whether the 'mount'
# utility will support a specific mounting option.
#
# This macro can be used to check for the presence of 'nodev' (or other mount
# options), which isn't uniformly implemented in the BSD family at the time of
# this writing. Tested on FreeBSD, NetBSD, OpenBSD, and Linux.
#
# Usage:
#
# AX_CHECK_MOUNT_OPT(option)
#
# Defines HAVE_MOUNT_$OPTION (in uppercase) if the option exists, and sets
# ac_cv_mount_$option (in original case) otherwise.
#
# Copyright (c) 2018 Nicholas Clark <nicholas.clark@gmail.com>
# Copyright (c) 2025 Theodore Ts'o <tytso@alum.mit.edu>
#
# Copying and distribution of this file, with or without modification, are
# permitted in any medium without royalty or attribution requirement.

AC_DEFUN([AX_CHECK_MOUNT_OPT], [__AX_CHECK_MOUNT_OPT(m4_tolower([$1]),m4_toupper([$1]))])
AC_DEFUN([__AX_CHECK_MOUNT_OPT],
[
	AC_MSG_CHECKING([for mount '$1' option])
	AC_COMPILE_IFELSE([AC_LANG_SOURCE([[
		#include <sys/mount.h>
		int main(int argc, char **argv) {
			void *temp = (void *)(MS_$2);
			(void) temp;
			return 0;
		}
		]])],[
		   AC_DEFINE(HAVE_MOUNT_$2, 1, [Define to 1 if mount supports $1.])
		   AS_VAR_SET(ac_cv_mount_$1, yes)
		   AC_MSG_RESULT([yes])],[
			AC_COMPILE_IFELSE([AC_LANG_SOURCE([[
				#include <sys/mount.h>
				int main(int argc, char **argv) {
					void *temp = (void *)(MNT_$2);
					(void) temp;
					return 0;
				}
				]])],[
				   AC_DEFINE(HAVE_MOUNT_$2, 1, [Define to 1 if mount supports $1.])
				   AS_VAR_SET(ac_cv_mount_$1, yes)
				   AC_MSG_RESULT([yes])],[
				   AC_MSG_RESULT([no])])
			   ])
])
