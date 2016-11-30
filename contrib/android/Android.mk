LOCAL_PATH:= $(call my-dir)

e2fsdroid_src := e2fsdroid.c \
    block_range.c \
    fsmap.c \
    block_list.c \
    base_fs.c \
    perms.c \
    basefs_allocator.c \
    hashmap.c \
    ../../misc/create_inode.c

include $(CLEAR_VARS)
LOCAL_SRC_FILES := $(e2fsdroid_src)
LOCAL_MODULE := e2fsdroid
LOCAL_C_INCLUDES := $(LOCAL_PATH)/../../misc/
LOCAL_SHARED_LIBRARIES := libext2fs-host \
    libext2_com_err-host \
    libcutils \
    libbase \
    libselinux \
    libcrypto
include $(BUILD_HOST_EXECUTABLE)


include $(CLEAR_VARS)
LOCAL_SRC_FILES := $(e2fsdroid_src)
LOCAL_MODULE := e2fsdroid
LOCAL_C_INCLUDES := $(LOCAL_PATH)/../../misc/
LOCAL_SHARED_LIBRARIES := libext2fs \
    libext2_com_err \
    libcutils \
    libbase \
    libselinux \
    libcrypto
include $(BUILD_EXECUTABLE)
