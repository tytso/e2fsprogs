LOCAL_PATH := $(call my-dir)

libext2_blkid_src_files := \
	cache.c \
	dev.c \
	devname.c \
	devno.c \
	getsize.c \
	llseek.c \
	probe.c \
	read.c \
	resolve.c \
	save.c \
	tag.c \
	version.c \


libext2_blkid_shared_libraries := libext2_uuid

libext2_blkid_system_shared_libraries := libc

libext2_blkid_c_includes := external/e2fsprogs/lib

libext2_blkid_cflags := -O2 -g -W -Wall -fno-strict-aliasing \
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
	-DHAVE_STRDUP \
	-DHAVE_MMAP \
	-DHAVE_UTIME_H \
	-DHAVE_GETPAGESIZE \
	-DHAVE_EXT2_IOCTLS \
	-DHAVE_TYPE_SSIZE_T \
	-DHAVE_SYS_TIME_H \
        -DHAVE_SYS_PARAM_H \
	-DHAVE_SYSCONF

libext2_blkid_cflags_linux := \
	-DHAVE_LINUX_FD_H \
	-DHAVE_SYS_PRCTL_H \
	-DHAVE_LSEEK64 \
	-DHAVE_LSEEK64_PROTOTYPE

include $(CLEAR_VARS)

LOCAL_SRC_FILES := $(libext2_blkid_src_files)
LOCAL_SYSTEM_SHARED_LIBRARIES := $(libext2_blkid_system_shared_libraries)
LOCAL_SHARED_LIBRARIES := $(libext2_blkid_shared_libraries)
LOCAL_C_INCLUDES := $(libext2_blkid_c_includes)
LOCAL_CFLAGS := $(libext2_blkid_cflags) $(libext2_blkid_cflags_linux) -fno-strict-aliasing
LOCAL_MODULE := libext2_blkid
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := $(libext2_blkid_src_files)
LOCAL_SHARED_LIBRARIES := $(addsuffix _host, $(libext2_blkid_shared_libraries))
LOCAL_C_INCLUDES := $(libext2_blkid_c_includes)
ifeq ($(HOST_OS),linux)
LOCAL_CFLAGS := $(libext2_blkid_cflags) $(libext2_blkid_cflags_linux)
else
LOCAL_CFLAGS := $(libext2_blkid_cflags)
endif
LOCAL_MODULE := libext2_blkid_host
LOCAL_MODULE_TAGS := optional

include $(BUILD_HOST_SHARED_LIBRARY)
