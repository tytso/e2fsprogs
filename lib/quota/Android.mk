LOCAL_PATH := $(call my-dir)

libext2_quota_src_files := \
	mkquota.c \
	quotaio.c \
	quotaio_tree.c \
	quotaio_v2.c \
	../../e2fsck/dict.c

libext2_quota_c_includes := external/e2fsprogs/lib

libext2_quota_cflags := -O2 -g -W -Wall \
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
	-DHAVE_STRDUP \
	-DHAVE_MMAP \
	-DHAVE_UTIME_H \
	-DHAVE_GETPAGESIZE \
	-DHAVE_LSEEK64 \
	-DHAVE_LSEEK64_PROTOTYPE \
	-DHAVE_EXT2_IOCTLS \
	-DHAVE_LINUX_FD_H \
	-DHAVE_TYPE_SSIZE_T \
	-DHAVE_SYS_TIME_H \
        -DHAVE_SYS_PARAM_H \
	-DHAVE_SYSCONF

libext2_quota_shared_libraries := libext2fs libext2_com_err

include $(CLEAR_VARS)

LOCAL_SRC_FILES := $(libext2_quota_src_files)
LOCAL_C_INCLUDES := $(libext2_quota_c_includes)
LOCAL_CFLAGS := $(libext2_quota_cflags)
LOCAL_SYSTEM_SHARED_LIBRARIES := libc $(libext2_quota_shared_libraries)
LOCAL_MODULE := libext2_quota
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := $(libext2_quota_src_files)
LOCAL_C_INCLUDES := $(libext2_quota_c_includes)
LOCAL_CFLAGS := $(libext2_quota_cflags)
LOCAL_MODULE := libext2_quota_host
LOCAL_MODULE_TAGS := optional
LOCAL_SHARED_LIBRARIES := $(addsuffix _host, $(libext2_quota_shared_libraries))

include $(BUILD_HOST_SHARED_LIBRARY)
