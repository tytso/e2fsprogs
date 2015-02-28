LOCAL_PATH := $(call my-dir)

libext2_com_err_src_files := \
	error_message.c \
	et_name.c \
	init_et.c \
	com_err.c \
	com_right.c

libext2_com_err_c_includes := external/e2fsprogs/lib

libext2_com_err_cflags := -O2 -g -W -Wall \
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

libext2_com_err_cflags_linux := \
	-DHAVE_LINUX_FD_H \
	-DHAVE_SYS_PRCTL_H \
	-DHAVE_LSEEK64 \
	-DHAVE_LSEEK64_PROTOTYPE

libext2_com_err_system_shared_libraries := libc

include $(CLEAR_VARS)

LOCAL_SRC_FILES := $(libext2_com_err_src_files)
LOCAL_C_INCLUDES := $(libext2_com_err_c_includes)
LOCAL_CFLAGS := $(libext2_com_err_cflags) $(libext2_com_err_cflags_linux)
LOCAL_SYSTEM_SHARED_LIBRARIES := libc
LOCAL_MODULE := libext2_com_err
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := $(libext2_com_err_src_files)
LOCAL_C_INCLUDES := $(libext2_com_err_c_includes)
ifeq ($(HOST_OS),linux)
LOCAL_CFLAGS := $(libext2_com_err_cflags) $(libext2_com_err_cflags_linux)
else
LOCAL_CFLAGS := $(libext2_com_err_cflags)
endif
LOCAL_MODULE := libext2_com_err_host
LOCAL_MODULE_TAGS := optional

include $(BUILD_HOST_SHARED_LIBRARY)
