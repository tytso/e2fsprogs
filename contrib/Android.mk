LOCAL_PATH := $(call my-dir)

###########################################################################
# Build fsstress
#
fsstress_src_files := \
	fsstress.c

fsstress_c_includes := 

fsstress_cflags := -O2 -g -W -Wall

fsstress_shared_libraries := 

fsstress_system_shared_libraries := libc

include $(CLEAR_VARS)

LOCAL_SRC_FILES := $(fsstress_src_files)
mke2fs_c_includesLOCAL_CFLAGS := $(fsstress_cflags)
LOCAL_SYSTEM_SHARED_LIBRARIES := $(fsstress_system_shared_libraries)
LOCAL_MODULE := fsstress
LOCAL_MODULE_TAGS := optional

include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := $(fsstress_src_files)
LOCAL_CFLAGS := $(fsstress_cflags)
LOCAL_MODULE := fsstress_host
LOCAL_MODULE_STEM := fsstress
LOCAL_MODULE_TAGS := optional

include $(BUILD_HOST_EXECUTABLE)

