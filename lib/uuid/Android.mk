LOCAL_PATH := $(call my-dir)

libext2_uuid_src_files := \
	clear.c \
	compare.c \
	copy.c \
	gen_uuid.c \
	isnull.c \
	pack.c \
	parse.c \
	unpack.c \
	unparse.c \
	uuid_time.c


libext2_uuid_c_includes := external/e2fsprogs/lib

libext2_uuid_cflags := -O2 -g -W -Wall \
	-DHAVE_INTTYPES_H \
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

libext2_uuid_system_shared_libraries := libc

include $(CLEAR_VARS)

LOCAL_SRC_FILES := $(libext2_uuid_src_files)
LOCAL_C_INCLUDES := $(libext2_uuid_c_includes)
LOCAL_CFLAGS := $(libext2_uuid_cflags)
LOCAL_SYSTEM_SHARED_LIBRARIES := $(libext2_uuid_system_shared_libraries)
LOCAL_MODULE := libext2_uuid
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := $(libext2_uuid_src_files)
LOCAL_C_INCLUDES := $(libext2_uuid_c_includes)
LOCAL_CFLAGS := $(libext2_uuid_cflags)
LOCAL_MODULE := libext2_uuid_host
LOCAL_MODULE_TAGS := optional

include $(BUILD_HOST_SHARED_LIBRARY)


include $(CLEAR_VARS)

LOCAL_SRC_FILES := $(libext2_uuid_src_files)
LOCAL_C_INCLUDES := $(libext2_uuid_c_includes)
LOCAL_CFLAGS := $(libext2_uuid_cflags)
LOCAL_STATIC_LIBRARIES := libc
LOCAL_MODULE := libext2_uuid_static
LOCAL_MODULE_TAGS := optional

include $(BUILD_STATIC_LIBRARY)


include $(CLEAR_VARS)

LOCAL_SRC_FILES := $(libext2_uuid_src_files)
LOCAL_C_INCLUDES := $(libext2_uuid_c_includes)
LOCAL_CFLAGS := $(libext2_uuid_cflags)
LOCAL_MODULE := libext2_uuid_host
LOCAL_MODULE_TAGS := optional

include $(BUILD_HOST_STATIC_LIBRARY)
