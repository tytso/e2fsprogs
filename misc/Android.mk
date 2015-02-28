LOCAL_PATH := $(call my-dir)

#########################################################################
# Build mke2fs
mke2fs_src_files := \
	mke2fs.c \
	util.c \
	mk_hugefiles.c \
	default_profile.c \
	create_inode.c \
	plausible.c

mke2fs_c_includes := \
	external/e2fsprogs/lib \
	external/e2fsprogs/e2fsck

mke2fs_cflags := -O2 -g -W -Wall \
	-DHAVE_UNISTD_H \
	-DHAVE_ERRNO_H \
	-DHAVE_NETINET_IN_H \
	-DHAVE_SYS_IOCTL_H \
	-DHAVE_SYS_MMAN_H \
	-DHAVE_SYS_MOUNT_H \
	-DHAVE_SYS_RESOURCE_H \
	-DHAVE_SYS_SELECT_H \
	-DHAVE_SYS_STAT_H \
	-DHAVE_SYS_TYPES_H \
	-DHAVE_STDLIB_H \
	-DHAVE_STRCASECMP \
	-DHAVE_STRDUP \
	-DHAVE_MMAP \
	-DHAVE_UTIME_H \
	-DHAVE_GETPAGESIZE \
	-DHAVE_EXT2_IOCTLS \
	-DHAVE_TYPE_SSIZE_T \
	-DHAVE_GETOPT_H \
	-DHAVE_SYS_TIME_H \
        -DHAVE_SYS_PARAM_H \
	-DHAVE_SYSCONF

mke2fs_cflags_linux := \
	-DHAVE_LINUX_FD_H \
	-DHAVE_SYS_PRCTL_H \
	-DHAVE_LSEEK64 \
	-DHAVE_LSEEK64_PROTOTYPE

mke2fs_cflags += -DNO_CHECK_BB

mke2fs_shared_libraries := \
	libext2fs \
	libext2_blkid \
	libext2_uuid \
	libext2_profile \
	libext2_quota \
	libext2_com_err \
	libext2_e2p

mke2fs_system_shared_libraries := libc

include $(CLEAR_VARS)

LOCAL_SRC_FILES := $(mke2fs_src_files)
LOCAL_C_INCLUDES := $(mke2fs_c_includes)
LOCAL_CFLAGS := $(mke2fs_cflags) $(mke2fs_cflags_linux)
LOCAL_SYSTEM_SHARED_LIBRARIES := $(mke2fs_system_shared_libraries)
LOCAL_SHARED_LIBRARIES := $(mke2fs_shared_libraries)
LOCAL_MODULE := mke2fs
LOCAL_MODULE_TAGS := optional
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := $(mke2fs_src_files)
LOCAL_C_INCLUDES := $(mke2fs_c_includes)
ifeq ($(HOST_OS),linux)
LOCAL_CFLAGS := $(mke2fs_cflags) $(mke2fs_cflags_linux)
else
LOCAL_CFLAGS := $(mke2fs_cflags)
endif
LOCAL_SHARED_LIBRARIES := $(addsuffix _host, $(mke2fs_shared_libraries))
LOCAL_MODULE := mke2fs_host
LOCAL_MODULE_STEM := mke2fs
LOCAL_MODULE_TAGS := optional

include $(BUILD_HOST_EXECUTABLE)

###########################################################################
# Build tune2fs
#
tune2fs_src_files := \
	tune2fs.c \
	plausible.c \
	util.c

tune2fs_c_includes := \
	external/e2fsprogs/lib \
	external/e2fsprogs/e2fsck

tune2fs_cflags := -O2 -g -W -Wall \
	-DHAVE_UNISTD_H \
	-DHAVE_ERRNO_H \
	-DHAVE_NETINET_IN_H \
	-DHAVE_SYS_IOCTL_H \
	-DHAVE_SYS_MMAN_H \
	-DHAVE_SYS_MOUNT_H \
	-DHAVE_SYS_PRCTL_H \
	-DHAVE_SYS_RESOURCE_H \
	-DHAVE_SYS_SELECT_H \
	-DHAVE_SYS_STAT_H \
	-DHAVE_SYS_TYPES_H \
	-DHAVE_STDLIB_H \
	-DHAVE_STRCASECMP \
	-DHAVE_STRDUP \
	-DHAVE_MMAP \
	-DHAVE_UTIME_H \
	-DHAVE_GETPAGESIZE \
	-DHAVE_LSEEK64 \
	-DHAVE_LSEEK64_PROTOTYPE \
	-DHAVE_EXT2_IOCTLS \
	-DHAVE_LINUX_FD_H \
	-DHAVE_TYPE_SSIZE_T \
	-DHAVE_GETOPT_H \
	-DHAVE_SYS_TIME_H \
        -DHAVE_SYS_PARAM_H \
	-DHAVE_SYSCONF

tune2fs_cflags += -DNO_CHECK_BB

tune2fs_shared_libraries := \
	libext2fs \
	libext2_com_err \
	libext2_blkid \
	libext2_quota \
	libext2_uuid \
	libext2_e2p

tune2fs_system_shared_libraries := libc

include $(CLEAR_VARS)

LOCAL_SRC_FILES := $(tune2fs_src_files)
LOCAL_C_INCLUDES := $(tune2fs_c_includes)
LOCAL_CFLAGS := $(tune2fs_cflags)
LOCAL_SHARED_LIBRARIES := $(tune2fs_shared_libraries)
LOCAL_SYSTEM_SHARED_LIBRARIES := $(tune2fs_system_shared_libraries)
LOCAL_MODULE := tune2fs
LOCAL_MODULE_TAGS := optional

include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := $(tune2fs_src_files)
LOCAL_C_INCLUDES := $(tune2fs_c_includes)
LOCAL_CFLAGS := $(tune2fs_cflags)
LOCAL_SHARED_LIBRARIES := $(addsuffix _host, $(tune2fs_shared_libraries))
LOCAL_MODULE := tune2fs_host
LOCAL_MODULE_STEM := tune2fs
LOCAL_MODULE_TAGS := optional

include $(BUILD_HOST_EXECUTABLE)

#########################################################################
# Build badblocks
#
include $(CLEAR_VARS)

badblocks_src_files := \
	badblocks.c

badblocks_c_includes := \
	external/e2fsprogs/lib

badblocks_cflags := -O2 -g -W -Wall \
	-DHAVE_UNISTD_H \
	-DHAVE_ERRNO_H \
	-DHAVE_NETINET_IN_H \
	-DHAVE_SYS_IOCTL_H \
	-DHAVE_SYS_MMAN_H \
	-DHAVE_SYS_MOUNT_H \
	-DHAVE_SYS_PRCTL_H \
	-DHAVE_SYS_RESOURCE_H \
	-DHAVE_SYS_SELECT_H \
	-DHAVE_SYS_STAT_H \
	-DHAVE_SYS_TYPES_H \
	-DHAVE_STDLIB_H \
	-DHAVE_STRCASECMP \
	-DHAVE_STRDUP \
	-DHAVE_MMAP \
	-DHAVE_UTIME_H \
	-DHAVE_GETPAGESIZE \
	-DHAVE_LSEEK64 \
	-DHAVE_LSEEK64_PROTOTYPE \
	-DHAVE_EXT2_IOCTLS \
	-DHAVE_LINUX_FD_H \
	-DHAVE_TYPE_SSIZE_T \
	-DHAVE_GETOPT_H \
	-DHAVE_SYS_TIME_H \
        -DHAVE_SYS_PARAM_H \
	-DHAVE_SYSCONF

badblocks_shared_libraries := \
	libext2fs \
	libext2_com_err \
	libext2_uuid \
	libext2_blkid \
	libext2_e2p

badblocks_system_shared_libraries := libc

include $(CLEAR_VARS)

LOCAL_SRC_FILES := $(badblocks_src_files)
LOCAL_C_INCLUDES := $(badblocks_c_includes)
LOCAL_CFLAGS := $(badblocks_cflags)
LOCAL_SHARED_LIBRARIES := $(badblocks_shared_libraries)
LOCAL_SYSTEM_SHARED_LIBRARIES := $(badblocks_system_shared_libraries)
LOCAL_MODULE := badblocks
LOCAL_MODULE_TAGS := optional

include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := $(badblocks_src_files)
LOCAL_C_INCLUDES := $(badblocks_c_includes)
LOCAL_CFLAGS := $(badblocks_cflags)
LOCAL_SHARED_LIBRARIES := $(addsuffix _host, $(badblocks_shared_libraries))
LOCAL_MODULE := badblocks_host
LOCAL_MODULE_STEM := badblocks
LOCAL_MODULE_TAGS := optional

include $(BUILD_HOST_EXECUTABLE)

#########################################################################
# Build chattr
#
include $(CLEAR_VARS)

chattr_src_files := \
	chattr.c

chattr_c_includes := \
	external/e2fsprogs/lib

chattr_cflags := -O2 -g -W -Wall \
	-DHAVE_UNISTD_H \
	-DHAVE_ERRNO_H \
	-DHAVE_NETINET_IN_H \
	-DHAVE_SYS_IOCTL_H \
	-DHAVE_SYS_MMAN_H \
	-DHAVE_SYS_MOUNT_H \
	-DHAVE_SYS_PRCTL_H \
	-DHAVE_SYS_RESOURCE_H \
	-DHAVE_SYS_SELECT_H \
	-DHAVE_SYS_STAT_H \
	-DHAVE_SYS_TYPES_H \
	-DHAVE_STDLIB_H \
	-DHAVE_STRCASECMP \
	-DHAVE_STRDUP \
	-DHAVE_MMAP \
	-DHAVE_UTIME_H \
	-DHAVE_GETPAGESIZE \
	-DHAVE_LSEEK64 \
	-DHAVE_LSEEK64_PROTOTYPE \
	-DHAVE_EXT2_IOCTLS \
	-DHAVE_LINUX_FD_H \
	-DHAVE_TYPE_SSIZE_T \
	-DHAVE_GETOPT_H \
	-DHAVE_SYS_TIME_H \
        -DHAVE_SYS_PARAM_H \
	-DHAVE_SYSCONF

chattr_shared_libraries := \
	libext2_com_err \
	libext2_e2p

chattr_system_shared_libraries := libc

include $(CLEAR_VARS)

LOCAL_SRC_FILES := $(chattr_src_files)
LOCAL_C_INCLUDES := $(chattr_c_includes)
LOCAL_CFLAGS := $(chattr_cflags)
LOCAL_SHARED_LIBRARIES := $(chattr_shared_libraries)
LOCAL_SYSTEM_SHARED_LIBRARIES := $(chattr_system_shared_libraries)
LOCAL_MODULE := chattr
LOCAL_MODULE_TAGS := optional

include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := $(chattr_src_files)
LOCAL_C_INCLUDES := $(chattr_c_includes)
LOCAL_CFLAGS := $(chattr_cflags)
LOCAL_SHARED_LIBRARIES := $(addsuffix _host, $(chattr_shared_libraries))
LOCAL_MODULE := chattr_host
LOCAL_MODULE_STEM := chattr
LOCAL_MODULE_TAGS := optional

include $(BUILD_HOST_EXECUTABLE)

#########################################################################
# Build lsattr
#
include $(CLEAR_VARS)

lsattr_src_files := \
	lsattr.c

lsattr_c_includes := \
	external/e2fsprogs/lib

lsattr_cflags := -O2 -g -W -Wall \
	-DHAVE_UNISTD_H \
	-DHAVE_ERRNO_H \
	-DHAVE_NETINET_IN_H \
	-DHAVE_SYS_IOCTL_H \
	-DHAVE_SYS_MMAN_H \
	-DHAVE_SYS_MOUNT_H \
	-DHAVE_SYS_PRCTL_H \
	-DHAVE_SYS_RESOURCE_H \
	-DHAVE_SYS_SELECT_H \
	-DHAVE_SYS_STAT_H \
	-DHAVE_SYS_TYPES_H \
	-DHAVE_STDLIB_H \
	-DHAVE_STRCASECMP \
	-DHAVE_STRDUP \
	-DHAVE_MMAP \
	-DHAVE_UTIME_H \
	-DHAVE_GETPAGESIZE \
	-DHAVE_LSEEK64 \
	-DHAVE_LSEEK64_PROTOTYPE \
	-DHAVE_EXT2_IOCTLS \
	-DHAVE_LINUX_FD_H \
	-DHAVE_TYPE_SSIZE_T \
	-DHAVE_GETOPT_H \
	-DHAVE_SYS_TIME_H \
        -DHAVE_SYS_PARAM_H \
	-DHAVE_SYSCONF

lsattr_shared_libraries := \
	libext2_com_err \
	libext2_e2p

lsattr_system_shared_libraries := libc

include $(CLEAR_VARS)

LOCAL_SRC_FILES := $(lsattr_src_files)
LOCAL_C_INCLUDES := $(lsattr_c_includes)
LOCAL_CFLAGS := $(lsattr_cflags)
LOCAL_SHARED_LIBRARIES := $(lsattr_shared_libraries)
LOCAL_SYSTEM_SHARED_LIBRARIES := $(lsattr_system_shared_libraries)
LOCAL_MODULE := lsattr
LOCAL_MODULE_TAGS := optional

include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := $(lsattr_src_files)
LOCAL_C_INCLUDES := $(lsattr_c_includes)
LOCAL_CFLAGS := $(lsattr_cflags)
LOCAL_SHARED_LIBRARIES := $(addsuffix _host, $(lsattr_shared_libraries))
LOCAL_MODULE := lsattr_host
LOCAL_MODULE_STEM := lsattr
LOCAL_MODULE_TAGS := optional

include $(BUILD_HOST_EXECUTABLE)

#########################################################################
# Build blkid
#
include $(CLEAR_VARS)

blkid_src_files := \
    blkid.c

blkid_c_includes := \
    external/e2fsprogs/lib

blkid_cflags := -O2 -g -W -Wall \
    -DHAVE_UNISTD_H \
    -DHAVE_ERRNO_H \
    -DHAVE_NETINET_IN_H \
    -DHAVE_SYS_IOCTL_H \
    -DHAVE_SYS_MMAN_H \
    -DHAVE_SYS_MOUNT_H \
    -DHAVE_SYS_PRCTL_H \
    -DHAVE_SYS_RESOURCE_H \
    -DHAVE_SYS_SELECT_H \
    -DHAVE_SYS_STAT_H \
    -DHAVE_SYS_TYPES_H \
    -DHAVE_STDLIB_H \
    -DHAVE_STRCASECMP \
    -DHAVE_STRDUP \
    -DHAVE_MMAP \
    -DHAVE_UTIME_H \
    -DHAVE_GETPAGESIZE \
    -DHAVE_LSEEK64 \
    -DHAVE_LSEEK64_PROTOTYPE \
    -DHAVE_EXT2_IOCTLS \
    -DHAVE_LINUX_FD_H \
    -DHAVE_TYPE_SSIZE_T \
    -DHAVE_GETOPT_H \
    -DHAVE_SYS_TIME_H \
    -DHAVE_SYS_PARAM_H \
    -DHAVE_SYSCONF \
    -DHAVE_TERMIO_H

blkid_shared_libraries := \
    libext2fs \
    libext2_blkid \
    libext2_com_err \
    libext2_e2p

blkid_system_shared_libraries := libc

include $(CLEAR_VARS)

LOCAL_SRC_FILES := $(blkid_src_files)
LOCAL_C_INCLUDES := $(blkid_c_includes)
LOCAL_CFLAGS := $(blkid_cflags)
LOCAL_SHARED_LIBRARIES := $(blkid_shared_libraries)
LOCAL_SYSTEM_SHARED_LIBRARIES := $(blkid_system_shared_libraries)
LOCAL_MODULE := blkid
LOCAL_MODULE_TAGS := optional

include $(BUILD_EXECUTABLE)
