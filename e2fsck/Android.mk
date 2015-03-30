LOCAL_PATH := $(call my-dir)

#########################
# Build the libext2 profile library

libext2_profile_src_files :=  \
	prof_err.c \
	profile.c

libext2_profile_shared_libraries := \
	libext2_com_err

libext2_profile_system_shared_libraries := libc

libext2_profile_c_includes := external/e2fsprogs/lib

libext2_profile_cflags := -O2 -g -W -Wall

include $(CLEAR_VARS)

LOCAL_SRC_FILES := $(libext2_profile_src_files)
LOCAL_SYSTEM_SHARED_LIBRARIES := $(libext2_profile_system_shared_libraries)
LOCAL_SHARED_LIBRARIES := $(libext2_profile_shared_libraries)
LOCAL_C_INCLUDES := $(libext2_profile_c_includes)
LOCAL_CFLAGS := $(libext2_profile_cflags)
LOCAL_MODULE := libext2_profile
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := $(libext2_profile_src_files)
LOCAL_SHARED_LIBRARIES := $(addsuffix _host, $(libext2_profile_shared_libraries))
LOCAL_C_INCLUDES := $(libext2_profile_c_includes)
LOCAL_CFLAGS := $(libext2_profile_cflags)
LOCAL_MODULE := libext2_profile_host
LOCAL_MODULE_TAGS := optional

include $(BUILD_HOST_SHARED_LIBRARY)

#########################
# Build the e2fsck binary

e2fsck_src_files :=  \
	e2fsck.c \
	dict.c \
	super.c \
	pass1.c \
	pass1b.c \
	pass2.c \
	pass3.c \
	pass4.c \
	pass5.c \
	logfile.c \
	journal.c \
	recovery.c \
	revoke.c \
	badblocks.c \
	util.c \
	unix.c \
	dirinfo.c \
	dx_dirinfo.c \
	ehandler.c \
	problem.c \
	message.c \
	ea_refcount.c \
	quota.c \
	rehash.c \
	region.c \
	sigcatcher.c \
	plausible.c

e2fsck_shared_libraries := \
	libext2fs \
	libext2_blkid \
	libext2_uuid \
	libext2_profile \
	libext2_quota \
	libext2_com_err \
	libext2_e2p
e2fsck_system_shared_libraries := libc

e2fsck_c_includes := external/e2fsprogs/lib

e2fsck_cflags := -O2 -g -W -Wall -fno-strict-aliasing

include $(CLEAR_VARS)

LOCAL_SRC_FILES := $(e2fsck_src_files)
LOCAL_C_INCLUDES := $(e2fsck_c_includes)
LOCAL_CFLAGS := $(e2fsck_cflags)
LOCAL_SYSTEM_SHARED_LIBRARIES := $(e2fsck_system_shared_libraries)
LOCAL_SHARED_LIBRARIES := $(e2fsck_shared_libraries)
LOCAL_MODULE := e2fsck
LOCAL_MODULE_TAGS := optional
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := $(e2fsck_src_files)
LOCAL_C_INCLUDES := $(e2fsck_c_includes)
LOCAL_CFLAGS := $(e2fsck_cflags)
LOCAL_SHARED_LIBRARIES := $(addsuffix _host, $(e2fsck_shared_libraries))
LOCAL_MODULE := e2fsck_host
LOCAL_MODULE_STEM := e2fsck
LOCAL_MODULE_TAGS := optional

include $(BUILD_HOST_EXECUTABLE)
