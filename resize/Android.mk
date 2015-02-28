LOCAL_PATH := $(call my-dir)

resize2fs_src_files := \
	extent.c \
	resize2fs.c \
	main.c \
	online.c \
	sim_progress.c \
	resource_track.c

resize2fs_c_includes := external/e2fsprogs/lib

resize2fs_cflags := -O2 -g -W -Wall \
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

resize2fs_shared_libraries := \
	libext2fs \
	libext2_com_err \
	libext2_e2p \
	libext2_uuid \
	libext2_blkid

resize2fs_system_shared_libraries := libc

include $(CLEAR_VARS)

LOCAL_SRC_FILES := $(resize2fs_src_files)
LOCAL_C_INCLUDES := $(resize2fs_c_includes)
LOCAL_CFLAGS := $(resize2fs_cflags)
LOCAL_SHARED_LIBRARIES := $(resize2fs_shared_libraries)
LOCAL_SYSTEM_SHARED_LIBRARIES := $(resize2fs_system_shared_libraries)
LOCAL_MODULE := resize2fs
LOCAL_MODULE_TAGS := optional
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := $(resize2fs_src_files)
LOCAL_C_INCLUDES := $(resize2fs_c_includes)
LOCAL_CFLAGS := $(resize2fs_cflags)
LOCAL_SHARED_LIBRARIES := $(addsuffix _host, $(resize2fs_shared_libraries))
LOCAL_MODULE := resize2fs_host
LOCAL_MODULE_STEM := resize2fs
LOCAL_MODULE_TAGS := optional

include $(BUILD_HOST_EXECUTABLE)
